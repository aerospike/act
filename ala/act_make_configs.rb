#!/usr/bin/env ruby
require 'optimist'

opts = Optimist::options do
  opt :devices, "List of devices to test. Ex: /dev/nvme3n1p1,/dev/nvme1n1p4", :type => :string
  opt :x, "How many 'X' configs you want to generate. At the default of 120, it will generate 120 actconfigs.", :default => 120
  opt :duration, "How long to run the act test for (hours).", :default => 24
  opt :qthreads, "How many threads per queue?", :default => 4
  banner <<-EOS
  EOS
end

Optimist::educate if(opts[:devices]==nil)

num_queues = opts[:devices].split(',').length.to_i * 2 #act recommendation is 2 per device.
test_duration_sec=opts[:duration].to_i * 60 * 60

(10..opts[:x]).each do |x|
  reads=2000*x
  writes=1000*x

  config="device-names: #{opts[:devices]}
num-queues: #{num_queues}
threads-per-queue: #{opts[:qthreads].to_i}
test-duration-sec: #{test_duration_sec}
report-interval-sec: 1
large-block-op-kbytes: 128
record-bytes: 1536
read-reqs-per-sec: #{reads}
write-reqs-per-sec: #{writes}"

  #write file
  open("actconfig_#{x}x.txt", 'w') do |file|
    file.puts config
  end

  if(x==opts[:x])
    puts "Printing last config written (#{x}X):\n#{config}"
  end

end
