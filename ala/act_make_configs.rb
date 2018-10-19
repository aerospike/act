#!/usr/bin/env ruby
require 'optimist' #for options parsing
require 'fileutils' #for recursive dir creation when specifying directory

opts = Optimist::options do
    opt :devices, "REQUIRED: List of devices to test. Ex: /dev/nvme3n1p1,/dev/nvme1n1p4", :type => :string
    opt :directory, "Directory to save config files to. (No trailing /)", :type => string, :default => "/opt"
    opt :x, "How many 'X' configs you want to generate. At the default of 120, it will generate 120 actconfigs.", :default => 120
    opt :duration, "How long to run the act test for (hours).", :default => 24
    opt :threads_per_queue, "How many threads per queue? Defaults to 4.", :default => 4
    opt :num_queues, "How many queues? Default is number of CPUs.", :default => false
    opt :printlast, "Print the last config file generated. (flag)", :default => false
    opt :microsecond_histograms, "Log microseconds in histograms. (yes|no)", :default => "no"
    opt :record_bytes, "Record size in bytes.", :default => 1536
    opt :record_bytes_range_max, "If set, simulate a range of record sizes from record-bytes up to record-bytes-range-max.", :default => 0
    opt :large_block_op_kbytes, "Size written and read in each large-block write and large-block read operation respectively, in Kbytes.", :default => 128
    opt :replication_factor, "Simulate the device load you would see if this node was in a cluster with the specified replication-factor.", :default => 1
    opt :update_pct, "Update percent.", :default => 0
    opt :defrag_lwm_pct, "Defrag lwm pct.", :default => 50
    opt :commit_to_device, "Commit to device. (yes|no)", :default => "no"
    opt :commit_min_bytes, "Commit min bytes. Default: Detect minimum device IO size.", :default => false
    opt :tomb_raider, "Tomb raider. (yes|no)", :default => "no"
    opt :tomb_raider_sleep_usec, "Tomb raider sleep usec.", :default => 0
    opt :max_reqs_queued, "Max requests queued.", :default => 100000
    opt :max_lag_sec, "Max lag sec.", :default => 10
    opt :scheduler_mode, "IO Scheduler mode.", :default => "noop"
    opt :report_interval_sec, "How often to report histograms back for logging (seconds).", :default => 1
    banner <<-EOS

    Reference https://github.com/aerospike/act for more detailed explanation of ACT parameters.

    EOS
end

Optimist::educate if(opts[:devices]==nil)

test_duration_sec=opts[:duration].to_i * 60 * 60

FileUtils.mkdir_p(opts[:directory])

(10..opts[:x]).each do |x|
  reads=2000*x
  writes=1000*x

  config="device-names: #{opts[:devices]}
test-duration-sec: #{test_duration_sec}
read-reqs-per-sec: #{reads}
write-reqs-per-sec: #{writes}
threads-per-queue: #{opts[:threads_per_queue].to_i}
report-interval-sec: #{opts[:report_interval_sec].to_i}
record-bytes: #{opts[:record_bytes].to_i}
record-bytes-range-max: #{opts[:record_bytes_range_max].to_i}
large-block-op-kbytes: #{opts[:large_block_op_kbytes].to_i}
replication-factor: #{opts[:replication_factor].to_i}
update-pct: #{opts[:update_pct].to_i}
defrag-lwm-pct: #{opts[:defrag_lwm_pct].to_i}
commit-to-device: #{opts[:commit_to_device]}
tomb-raider: #{opts[:tomb_raider]}
tomb-raider-sleep-usec: #{opts[:tomb_raider_sleep_usec].to_i}
max-reqs-queued: #{opts[:max_reqs_queued].to_i}
max-lag-sec: #{opts[:max_lag_sec].to_i}
scheduler-mode: #{opts[:scheduler_mode]}
"

    if opts[:num_queues] 
        config = config + "num-queues: #{opts[:num_queues].to_i}"
    end

    if opts[:commit_min_bytes]
        config = config + "commit-min-bytes: #{opts[:commit_min_bytes].to_i}"
    end

    #write file
    open("#{opts[:directory]}/actconfig_#{x}x.txt", 'w') do |file|
        file.puts config
    end

    #if this is the last config file to be written, print it.
    if(opts[:printlast] && x==opts[:x])
        puts "Printing last config written (#{x}X):\n#{config}"
    end

end

puts "Done."
