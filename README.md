## Aerospike Certification Tool (ACT)

This project is maintained by [Aerospike](http://www.aerospike.com)

### Overview
------------

ACT provides a pair of programs for testing and certifying flash/SSD devices'
performance for Aerospike Database data and index storage.  ACT measures latency
during a mixed load of read and write operations while modeling the Aerospike
Database server's I/O pattern as closely as practical.

ACT allows you to test a single device or multiple devices, using your actual
connector/controller hardware.

There are two programs: act_storage models Aeropike Database data storage I/O
patterns, and act_index models Aeropike Database index storage I/O patterns for
Aerospike Database's "All Flash" mode.

The purpose of this certification is:

1. Determine if an SSD device(s) will stand up to the demands of a high-speed
   real-time database.
2. Evaluate the upper limits of the throughput you can expect from a device(s).

Not all SSDs can handle the high volume of transactions required by high
performance real-time databases like Aerospike Database.  Many SSDs are rated
for 100K+ reads/writes per second, but in production the actual load they can
withstand for sustained periods of time is generally much lower.  In the process
of testing many common SSDs in high-throughput tests, Aerospike developed this
certification tool, ACT, that you can use to test/certify an SSD for yourself.

We have found performance – especially latency – of SSDs to be highly dependent
on the write load the SSD is subjected to. Over the first few hours of a test,
performance can be excellent, but past the 4- to 12-hour mark (depending on the
device), performance can suffer.

The ACT tool allows you to test an SSD device(s) for yourself. In addition,
Aerospike has tested a variety of SSDs and has specific recommendations.  For
more information, visit the Aerospike Database documentation at:
http://www.aerospike.com/docs/operations/plan/ssd/ssd_certification.html.

#### What ACT Does
------------------

By default, act_storage performs a combination of large (128K) block reads and
writes and small (1.5K) block reads, simulating standard real-time Aerospike
Database data read/write and defragmentation loads.

By default, act_index performs a mixture of 4K reads and writes, simulating
standard real-time Aerospike Database "All Flash" index device loads.

Latencies are measured for a long enough period of time (typically 24 hours) to
evaluate device stability and overall performance.

**Traffic/Loading**

You can simulate:

* "Nx" load - 1x load (2000 reads/sec and 1000 writes/sec per device) times N
* any other stress load or high-performance load (custom configurable)

**Latency Rate Analysis**

ACT's output shows latency broken into intervals of 2^n ms: 1, 2, 4, 8 ... ms
(analysis program's display intervals are configurable).

For example, a test might indicate that 0.25% of requests failed to complete in
1 ms or less and 0.01% of requests failed to complete in 8 ms or less.

**Methodology for act_storage**

The small read operations model client read requests.  Requests are done at the
specified rate by a number of service threads.

The large-block read and write operations model the Aerospike server's write
requests and defragmentation process.  The operations occur at a rate determined
by the specified write request rate, and are executed from one dedicated
large-block read thread and one dedicated large-block write thread per device.

**Methodology for act_index**

The 4K device reads model index element access that occurs during client read
and write requests, and defragmentation.  One device read is executed on service
threads for each client read, and for each client write.  In addition, more
reads are executed in "cache threads" to model index element access during
defragmentation.

The "cache threads" also execute all the 4k device writes, which model index
element changes due to client write requests and defragmentation.

Unlike the Aerospike Database "All Flash" mode, act_index does not mmap files in
mounted directories on the devices - it models the raw device I/O pattern,
assuming no caching benefit from mmap. Therefore to configure act_index we
simply specify the devices.

#### Process for Certifying Device(s) for 30x Performance
---------------------------------------------------------

In general, we recommend that you certify a device for 30x performance.  Many
devices do not pass the 30x certification.  If you do not have a high-volume
application, you may find that a 10x or 20x certification will be sufficient.
The instructions below describe the 30x certification process, but you may need
to adjust the test based on your requirements.

To certify a device(s) for 30x performance with Aerospike Database requires two
stages:

1. Test a single device to determine performance using the hardware
   configuration and connectors.  The single-device certification will help you
   determine individual device performance.
2. If you will be using multiple devices, you can then run ACT to test multiple
   devices to see how the results will be affected by the capacity of the bus or
   the throughput of the RAID controller that is managing your devices.

The test process with ACT is the same for both stages, but in the first stage
you are testing a device and in the second stage, you are testing the
linearity/scalability of your connector with multiple devices installed.

The single-device stage takes 48 hours.  The multi-device stage takes an
additional 48 hours.

##### The first stage is to certify a single device, to test the device itself and the connection.

Begin by installing your SSD device.  Our website has more details about
installing SSDs in different environments and configurations at
http://www.aerospike.com/docs/operations/plan/ssd/ssd_setup.html.

**Test 1: Test under high loads**

Run ACT for 24 hrs using the 30x test (60000 reads/sec and 30000 writes/sec).
The device passes this test if less than 5% of operations fail to complete in
1 ms or less.

Many devices fail this test and are unsuitable for use with Aerospike Database.

**Test 2: Stress test to ensure the device does not fail under excessive loads**

Run a 60x test for 24 hrs (120000 reads/sec and 60000 writes/sec). The device
passes this test if ACT runs to completion, regardless of the error rate.

**If you are testing a single device, then the device is certified when it passes Test 1 and Test 2.**

##### The second stage is to certify multiple devices, to make sure that performance scales linearly when you add devices.

Install the additional SSDs to be tested.  Our website has more details about
installing SSDs in different environments and configurations at
http://www.aerospike.com/docs/operations/plan/ssd/ssd_setup.html.

**Test 3: Repeat Test 1, with all devices installed: Test under high loads**

Run ACT for 24 hrs using the 30x test (60000 reads/sec and 30000 writes/sec per
device).  The devices pass this test if less than 5% of operations fail to
complete in 1 ms or less.

**Test 4: Repeat Test 2, with all devices installed: Stress test to ensure the devices do not fail under excessive loads**

Run a 60x test for 24 hrs (120000 reads/sec and 60000 writes/sec per device).
The devices pass this test if ACT runs to completion, regardless of the error
rate.

**The devices are certified if they pass Test 3 and Test 4.**

Once the device(s) has been certified, the device can be used with Aerospike
Database.

&nbsp;

#### Determining Expected Performance at Higher Throughput
----------------------------------------------------------

If your application is going to have high volumes of transactions and your
device(s) passes the 30x certification, we recommend that you test your device
to determine its upper limit on transaction processing latency.  This will help
you determine how many SSDs you will need to run your application when you are
fully scaled up.

To certify a device(s) at higher levels of performance, do the certification
process as described above, but use higher loads (80x, 100x, etc.).  Test the
device(s) at progressively higher rates until more than 5% of operations fail in
1 ms.

For example, if you test at 60x and less than 5% of operations fail to complete
in 1 ms, re-run the test at 80x, etc.  When the device completes the test at a
particular speed with *more* than 5% of operations failing to complete in 1 ms
(i.e., fails the test), then the device is certified at the next lower level
where the device DOES have fewer than 5% of errors in under 1 ms.

If your device is testing well at higher loads, you may want to shorten the test
time.  Running ACT for six hours will give you a good idea whether your device
can pass ACT testing at a given traffic volume.  Before certifying your device
at a given traffic level, we recommend a full 24-hour test.

As before, test a single device first, and then test with multiple devices to
make sure that the performance scales linearly with your connector/controller.

### Getting Started
--------------------

**Download the ACT package through git:**

```
$ git clone https://github.com/aerospike/act.git
```
This creates an /act directory.

Alternately you can download the ZIP or TAR file from the links at the left.
When you unpack/untar the file, it creates an /aerospike-act-<version>
directory.

**Install the Required Libraries**

Before you can build ACT, you need to install some libraries.

For CentOS:
```
$ sudo yum install make gcc
```

For Debian or Ubuntu:
```
$ sudo apt-get install make gcc libc6-dev
```

**Build the package.**

```
$ cd act    OR    cd /aerospike-act-<version>
$ make
```

This will create 3 binaries in a target/bin directory:

* ***act_prep***:  This executable prepares a device for ACT by writing zeroes
on every sector of the disk and then filling it up with random data (salting).
This simulates a normal production state.

* ***act_storage***:  The executable for modeling Aerospike Database data
storage device I/O patterns.

* ***act_index***:  The executable for modeling Aerospike Database "All Flash"
mode index device I/O patterns.

### Running the ACT Certification Process
-----------------------------------------

To certify your device(s), first determine what certification test you will run,
as described above in **Process for Certifying a Drive(s) for 3x Performance**
or **Determining Expected Performance at Higher Throughput**.

For each certification test with ACT, you must perform the following steps:

1. Prepare the device(s) with act_prep -- only the first time you test.
2. Create the config file for your test.
3. Run the test, sending the results to a log file.
4. Analyze log file output using the /analysis/act_latency.py script.
5. Determine pass/fail for the test.

The details of these steps are described in detail below.

**The tests destroy all data on the devices being tested!**

When preparing devices and running tests, make sure the devices are specified by
name correctly.

Make sure the test device is not mounted.

#### 1. Prepare the Drives with act_prep - First Time Only
----------------------------------------------------------

The first time you test a device(s), you must prepare the device(s) by first
cleaning them (writing zeros everywhere) and then "salting" them (writing random
data everywhere) with act_prep.

act_prep takes a device name as its only command-line parameter.  For a typical
240GB SSD, act_prep takes 30-60+ minutes to run.  The time varies depending on
the device and the capacity.

If you are testing multiple devices, you can run act_prep on all of the devices
in parallel.  Preparing multiple devices in parallel does not take a lot more
time than preparing a single device, so this step should only take an hour or
two.

For example, to clean and salt the device /dev/sdc:
(over-provisioned using hdparm)
```
$ sudo ./act_prep /dev/sdc &
```
If you are using a RAID controller / over-provisioned using fdisk, make sure you
specify the partition and not the raw device. If the raw device is used then ACT
will wipe out the partition table and this will invalidate the test.
```
$ sudo ./act_prep /dev/sdc1 &
```

#### 2. Create a Configuration File
-----------------------------------

Create your config file by copying the appropriate example config file in the
/config directory and modifying it, as described in the
**ACT Configuration Reference** below.  The example files are for the standard
1x load (2000 reads/sec and 1000 writes/sec per device).

Copy act_storage.conf to run the normal data storage modeling tests, or copy
act_index.conf to run "All Flash" mode tests for index devices.

#### 3. Run the test
--------------------

From the ACT installation directory, run:
```
$ sudo ./target/bin/act_storage actconfig.txt > output.txt &
```
where:
```
* actconfig.txt - path/name of your config file
* output.txt    - path/name of your log file
```
If running ACT from a remote terminal, it is best to run it as a background
process, or within a "screen".  To verify that ACT is running, tail the output
text file with the -f option.

Note that if the device(s) being tested performs so badly that ACT cannot keep
up with the specified load, ACT will halt before the configured test duration
has elapsed.  ACT may also halt prematurely if it encounters unexpected device
I/O or system errors.

#### 4. Analyze ACT Output
--------------------------

Run /analysis/act_latency.py to process the ACT log file and tabulate data.
Note that you can run the script when the test is not yet complete, and you will
see the partial results.

For example:
```
$ ./analysis/act_latency.py -l output.txt
```

where:
```
 -l <act output file name> - required parameter that specifies the path/name of the log file generated by ACT
```

and optionally:
```
 -h <histogram name(s)> - optional parameter specifying histogram name(s): defaults are small read latency histograms
 -t <slice duration in seconds> - optional parameter specifying slice length; default is 3600 sec (1 hour)
 -s <start threshold> - optional parameter specifying start threshold for display; default is 0 (1 ms/us)
 -n <number of thresholds> - optional parameter specifying number of thresholds to display; default is 7
 -e <display every 'e'th threshold> - optional parameter specifying display threshold frequency; default is (every) 1
 -x <display throughputs> - optional parameter indicating that throughputs should also be displayed: default is no
```

The Python script analyzes the ACT output in time slices as specified, and
displays latency data at various verification intervals for each slice.

The example output below is for an **act_storage** 12-hour test (each slice is
an hour), run with options -n 3 (display 3 thresholds) and -e 3 (display every
3rd threshold).  The **reads** table shows read latencies accumulated over all
devices.  So for example, in the 5th hour, 1.68% of reads failed to complete in
under 1 ms.

```
         reads
         %>(ms)
 slice        1      8     64
 -----   ------ ------ ------
     1     1.67   0.00   0.00
     2     1.38   0.00   0.00
     3     1.80   0.14   0.00
     4     1.43   0.00   0.00
     5     1.68   0.00   0.00
     6     1.37   0.00   0.00
     7     1.44   0.00   0.00
     8     1.41   0.00   0.00
     9     2.70   0.73   0.00
    10     1.54   0.00   0.00
    11     1.53   0.00   0.00
    12     1.47   0.00   0.00
 -----   ------ ------ ------
   avg     1.62   0.07   0.00
   max     2.70   0.73   0.00
```

The script will also echo the configuration used to generate the log file, along
with other basic information, above the latency tables.  (We do not show his
output in the example above.)

#### 5. Evaluate Device(s) by the Standard Pass/Fail Criteria
-------------------------------------------------------------

##### Passing a Performance Test
In any one-hour period of an ACT performance test, we expect that:

 - fewer than 5% of transactions fail to complete in 1 ms
 - fewer than 1% of transactions fail to complete in 8 ms
 - fewer than 0.1% of transactions fail to complete in 64 ms

The **max** line of the output shows the highest values observed in any single
slice (hour) of time, and the values on the max line should not exceed the
allowable error values specified above.

In the example output above, we show only 12 hours of results, and the device
passes because the worst performance in any slice was 2.7% of transactions
failing to complete within 1 ms, 0.73% of transactions failing to complete in
less than 8 ms and no transactions failing to complete within 64 ms.

A device(s) which does not exceed these error thresholds in 24 hours passes the
load test.

##### Passing a Stress Test
When doing stress testing at a level ABOVE where the device is certified, a
device passes the test if ACT runs to completion, regardless of the number of
errors.

## ACT Configuration Reference
------------------------------

#### Modifying the Config File
------------------------------

This package includes two example config files, one for act_storage
(/config/act_storage.conf) and one for act_index (/config/act_index.conf).

Chose the one appropriate for the test you wish to run.  (This is usually
act_storage.  Run act_index only if you are testing devices for storing indexes
when running Aerospike Database in "All Flash" mode.)

First, you must be sure to set the correct device name(s).  Then you should
adjust the transaction request rates.

Each example config file has the transaction request rates for a 1x load with a
single device.  To generate a config file for an Nx load, simply multiply those
rates by N, and by the number of devices you are testing with, if using multiple
devices.

For example, to generate a config file for a single-device 60x load, change
read-reqs-per-sec to 120000, and write-reqs-per-sec to 60000.

Or, to generate a config file for a four-device 60x load, change
read-reqs-per-sec to 480000, and write-reqs-per-sec to 240000.

You may of course run customized loads, including read-only loads (set
write-reqs-per-sec to 0) or write-only loads (set read-reqs-per-sec to 0).

The other fields in the configuration files should generally not be changed, but
you may do so to run highly customized tests.

#### Format of Lines in the Config File
---------------------------------------

All fields use a
```
name-token: value
```
format, and must be on a single line.  Field order in the file is unimportant.
To add comments, add a line(s) that begin with '#'.

### Fields that you Must Change:

**device-names**
Comma-separated list of device names (full path) to test.  For example:
```
device-names: /dev/sdb,/dev/sdc
```
Make sure the devices named are entered correctly.

### Fields that you will Almost Always Change:

**read-reqs-per-sec**
Read transactions/second to simulate.  Note that this is not per device.
For 30 times (30x) the normal load for four devices, this value would be
30 x 4 x 2000 = 240000.  Formula: n x number of devices x 2000.

**write-reqs-per-sec**
Write transactions/second to simulate.  For act_storage, this value along with
record-bytes, large-block-op-kbytes, defrag-lwm-pct, and others, determines the
rate of large-block operations.  Note that this is not per device.
For 30 times (30x) the normal load for four devices, this value would be
30 x 4 x 1000 = 120000.  Formula: n x number of devices x 1000.

### Fields that you may Sometimes Change:

**test-duration-sec**
Duration of the entire test, in seconds.  Note that it has to be a single
number, e.g. use 86400, not 60 x 60 x 24.  The default is one day (24 hours).

### Fields that you will Rarely or Never Change:

**service-threads**
Total number of service threads on which requests are generated and done.  If a
test stops with a message like "... ACT can't do requested load ...", it doesn't
mean the devices failed, it just means the transaction rates specified are too
high to achieve with the configured number of service threads.  Try testing
again with more service threads.  The default service-threads is 5x the number
of CPUs, detected by ACT at runtime.

**cache-threads (act_index ONLY)**
Number of threads from which to execute all 4K writes, and 4K reads due to
index access during defragmentation.  These threads model the system threads
that would do these device I/O operations behind mmap.  The default
cache-threads is 8.

**report-interval-sec**
Interval between generating observations, in seconds. This is the smallest
granularity that you can analyze.  Default is 1 sec.  The
/analysis/act_latency.py script aggregates these observations into slices,
typically hour-long groups.

**microsecond-histograms**
Flag that specifies what time units the histogram buckets will use -- yes means
use microseconds, no means use milliseconds.  If this field is left out, the
default is no.

**record-bytes (act_storage ONLY)**
Size of a record in bytes.  This determines the size of a read operation -- just
record-bytes rounded up to a multiple of 512 bytes (or whatever the device's
minimum direct op size).  Along with write-reqs-per-sec, large-block-op-kbytes,
and others, this item determines the rate of large-block operations.
record-bytes is rounded up to a multiple of 16 bytes to model Aerospike Database
storage granularity.  For example, if record-bytes is 1500, write-reqs-per-sec
is 1000, and large-block-op-kbytes is 128, we write (1504 x 1000) bytes per
second, or (1504 x 1000) / (128 x 1024) = 11.4746 large blocks per second.  With
defrag-lwm-pct 50, we double this to simulate defragmentation where blocks
depleted to 50%-used are re-packed, yielding a large-block write (and read) rate
of 22.949 blocks per second.

**record-bytes-range-max (act_storage ONLY)**
If set, simulate a range of record sizes from record-bytes up to
record-bytes-range-max.  Therefore if set, it must be larger than record-bytes
and smaller than or equal to large-block-op-kbytes.  The simulation models a
linear distribution of sizes within the range.  The default
record-bytes-range-max is 0, meaning no range -- model all records with size
record-bytes.

**large-block-op-kbytes (act_storage ONLY)**
Size written and read in each large-block write and large-block read operation
respectively, in Kbytes.

**replication-factor**
Simulate the device load you would see if this node was in a cluster with the
specified replication-factor.  Increasing replication-factor increases the write
load, e.g. replication-factor 2 doubles the write load.  For act_storage, this
doubles the large-block read and write rates.  It can also affect the
record-sized internal read rate if update-pct is non-zero.  The default
replication-factor is 1.

**update-pct (act_storage ONLY)**
Simulate the device load you would see if this percentage of write requests were
updates, as opposed to replaces.  Updates cause the current version of a record
to be read before the modified version is written, while replaces do not need to
read the current version.  Therefore a non-zero update-pct will generate a
bigger internal record-sized read rate.  E.g. if read-reqs-per-sec is 2000 and
write-reqs-per-sec is 1000, the internal read-req rate will be somewhere between
2000 (update-pct 0), and 2000 + 1000 = 3000 (update-pct 100).  The default
update-pct is 0.

**defrag-lwm-pct**
Simulate the device load you would see if this was the defrag threshold. The
lower the threshold, the emptier large blocks are when we defragment them (pack
the remaining records into new blocks), and the lower the "write amplification"
caused by defragmentation.  E.g. if defrag-lwm-pct is 50, the write
amplification will be 2x, meaning defragmentation doubles the internal effective
storage write rate, which (for act_storage) is manifest as the large-block read
and write rates.  For act_index, defragmentation generates an extra internal
index device read and write load. The default defrag-lwm-pct is 50.

**compress-pct (act_storage ONLY)**
Generate compressible data when writing to devices.  With compress-pct 100, the
data is fully random (not compressible).  Lower values cause runs of zeros to
be interleaved with random data such that the data should be compressible to the
specified percentage of original size.  The compressibility of data may affect
performance on some devices, especially those supporting in-line compression.
The default compress-pct is 100.

**disable-odsync**
Option to not set O_DSYNC when opening file descriptors. Don't configure this
true if configuring commit-to-device.  The default disable-odsync is no (i.e.
O_DSYNC is set by default).

**commit-to-device (act_storage ONLY)**
Flag to model the mode where Aerospike commits each record to device
synchronously, instead of flushing large blocks full of records.  This causes a
device I/O load with many small, variable-sized writes.  Large block writes (and
reads) still occur to model defragmentation, but the rate of these is reduced.
The default commit-to-device is no.

**commit-min-bytes (act_storage ONLY)**
Minimum size of a write in commit-to-device mode. Must be a power of 2. Each
write rounds the record size up to a multiple of commit-min-bytes. If
commit-min-bytes is configured smaller than the minimum I/O size allowed on the
device, the record size will be rounded up to a multiple of the minimum I/O
size.  The default commit-min-bytes is 0, meaning writes will round up to a
multiple of the minimum I/O size.

**tomb-raider (act_storage ONLY)**
Flag to model the Aerospike tomb raider.  This simply spawns a thread per device
in which the device is read from beginning to end, one large block at a time.
The thread sleeps for tomb-raider-sleep-usec microseconds between each block.
When the end of the device is reached, we repeat, reading from the beginning.
(In other words, we don't model Aerospike's tomb-raider-period.)  The default
tomb-raider is no.

**tomb-raider-sleep-usec (act_storage ONLY)**
How long to sleep in each device's tomb raider thread between large-block reads.
The default tomb-raider-sleep-usec is 1000, or 1 millisecond.

**max-lag-sec**
How much the large-block operations (act_storage) or cache-thread operations
(act_index) are allowed to lag behind their target rates before the ACT test
fails.  Also, how much the service threads that generate and do requests are
allowed to lag behind their target rates before the ACT test is stopped. Note
that this doesn't necessarily mean the devices failed the test - it means the
transaction rates specified are too high to achieve with the configured number
of service threads.  Note - max-lag-sec 0 is a special value for which the test
will not be stopped due to lag.  The default max-lag-sec is 10.

**scheduler-mode**
Mode in /sys/block/<device>/queue/scheduler for all the devices in the test run.
noop means no special scheduling is done for device I/O operations, cfq means
operations may be reordered to optimize for physical constraints imposed by
rotating disc devices (which likely means it hurts performance for ssds).  If
the field is left out, the default is noop.
