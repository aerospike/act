## Aerospike Certification Tool (ACT)

This project is maintained by [Aerospike](http://www.aerospike.com)

### Overview
------------

ACT is a program for testing and certifying flash/SSD devices' performance for
Aerospike Database (with SATA, SAS and PCIe connectors).  ACT shows latency responses when you are reading from and writing to
the database concurrently while modeling the Aerospike Database server's I/O
pattern as closely as practical.

ACT allows you to test a single drive or multiple drives, using your actual connector/controller hardware.

The purpose of this certification is:

1. Determine if an SSD device(s) will stand up to the demands of a high-speed real-time database
2. Evaluate the upper limits of the throughput you can expect from a drive(s)

Not all SSDs can handle the high volume of transactions required by high
performance real-time databases like Aerospike Database.  Many SSDs are rated
for 100K+ reads/writes per second, but in production the actual load they
can withstand for sustained periods of time is generally much lower.  In the process
of testing many common SSDs in high-throughput tests, Aerospike developed this certification tool, ACT, that you can use to test/certify an SSD for yourself.

We have found performance – especially latency – of SSDs to be highly
dependent on the write load the SSD is subjected to. Over the first few hours of a test,
performance can be excellent, but past the 4- to 10-hour mark (depending
on the drive), performance can suffer.

The ACT tool allows you to test an SSD device(s) for yourself.
In addition, Aerospike has tested a variety of SSDs and has specific recommendations.
For more information, visit the Aerospike Database documentation at:  http://www.aerospike.com/docs.

#### What the ACT Tool Does
---------------------------

ACT performs a combination of large (128K) block reads and writes and small (1.5K) block reads, simulating
standard real-time database read/write loads.

Reads and write latency is measured for a long enough period of time (typically 24 hours) to evaluate drive stability and
overall performance.

**Traffic/Loading**

You can simulate:

* 1x - normal load (2000 reads/sec and 1000 writes/sec per drive)
* 3x - high load (6000 reads/sec and 3000 writes/sec per drive)
* any other stress load or high-performance load (custom configurable)

**Latency Rate Analysis**

ACT's output shows latency rates broken down by intervals of 1, 8 and 64 ms (configurable).

For example, the test might indicate that 0.25% of requests
failed to complete in 1 ms or less and 0.01% of requests failed to complete in 8 ms or less.

**Methodology**

The small read operations model client transaction requests.  The operations occur at a
specified rate.  Requests are added at this rate to a specified number of
read transaction queues, each of which is serviced by a specified number of
threads.

The large-block read and write operations model the Aerospike server's
defragmentation process.  The operations occur at a specified rate, executed from one
dedicated large-block read thread and one dedicated large-block write thread per
device.

#### Process for Certifying a Drive(s) for 3x Performance
----------------------------------

**In general, we recommend that you certify a drive for 3x performance.  Many drives do not pass the 3x
certification.  If you do not have a high-volume application, you may find that a 2x or 2.5x certification
will be sufficient.  The instructions below describe the 3x certification process, but you may need to adjust
the test based on your requirements.**

To certify a drive(s) for 3x performance with Aerospike Database requires two stages:

1. Test a single drive to determine performance using the hardware configuration and connectors. The single-device certification will help you determine individual drive performance.
2. If you will be using multiple drives, you can then run ACT to test multiple drives to see how
the results will be affected by the capacity of the bus or the throughput of the RAID controller that is managing your drives.

The test process with ACT is the same for both stages, but in the first stage you are testing a drive and
in the second stage, you are testing the linearity/scalability of your connector with multiple drives installed.

The single-drive stage takes 48 hours.  The multi-drive stage takes an additional 48 hours.

##### The first stage is to certify a single drive, to test the drive itself and the connection.

Begin by installing your SSD device.  Our website has more details about installing SSDs in different environments
and configurations at http://www.aerospike.com/docs.

**Test 1: Test under high loads**

Run ACT for 24 hrs using the 3x test (6000 reads/sec and 3000 writes/sec).
The drive passes this test if less than 5% of operations fail to complete in 1 ms or less.

Many drives fail the 3x test and are unsuitable for use with Aerospike Database.

**Test 2: Stress test to ensure the drive does not fail under excessive loads**

Run a 6x test for 24 hrs (12000 reads/sec and 6000 writes/sec).
The drive passes this test if ACT runs to completion, regardless of the error rate.

**If you are testing a single drive, then the drive is certified when it passes Test 1 and Test 2.**

##### The second stage is to certify multiple drives, to make sure that performance scales linearly when you add drives.

Install the additional SSDs to be tested.  Our website has more details about installing SSDs in different environments
and configurations at http://www.aerospike.com/docs.

**Test 3: Repeat Test 1, with all drives installed: Test under high loads**

Run ACT for 24 hrs using the 3x test (6000 reads/sec and 3000 writes/sec).
The drives pass this test if less than 5% of operations fail to complete in 1 ms or less.

**Test 4: Repeat Test 2, with all drives installed: Stress test to ensure the drives do not fail under excessive loads**

Run a 6x test for 24 hrs (12000 reads/sec and 6000 writes/sec).  The drives pass this test if ACT runs to completion, regardless of the error rate.

**The drives are certified if they pass Test 3 and Test 4.**  Once the drive(s) has been certified, the drive can be used with Aerospike Database.

&nbsp;

#### Determining Expected Performance at Higher Throughput
-------------------------------------------------------

If your application is going to have high volumes of transactions and your drive(s) passes the 3x certification,
we recommend that you test your drive to determine its upper limit on transaction processing latency.  This will help
you determine how many SSDs you will need to run your application when you are fully scaled up.

To certify a drive(s) at higher levels of performance, do the certification process as described above, but use higher loads (12x, 24x, etc.).
Test the drive(s) at progressively higher rates until more than 5% of operations fail in 1 ms.

For example, if you test at 24x and less than 5% of operations fail to complete in 1 ms, re-run the test at 48x, etc.  When the drive completes
the test at a particular speed with *more* than 5% of operations failing to complete in 1 ms (i.e., fails the test), then the drive is certified at the
next lower level where the drive DOES have fewer than 5% of errors in under 1 ms.

If your drive is testing well at higher loads, you may want to shorten the test time.  Running ACT for six hours
will give you a good idea whether your drive can pass ACT testing at a given traffic volume.  Before certifying your
drive at a given traffic level, we recommend a full 24-hour test.

As before, test a single drive first, and then test with multiple drives to make sure that the
performance scales linearly with your connector/controller.

### Getting Started
--------------------

**Download the ACT package through git:**

```
$ git clone https://github.com/aerospike/act.git
```
This creates an /act directory.

Alternately you can download the ZIP or TAR file from the links at the left.
When you unpack/untar the file, it acreates an /aerospike-act-<version> directory.

**Install the Required Libraries**
Before you can build ACT, you need to install some libraries.

For CentOS:
```
$ sudo yum install make gcc openssl-devel
```

For Debian or Ubuntu:
```
$ sudo apt-get install make gcc libc6-dev libssl-dev
```

**Build the package.**

```
$ cd act    OR    cd /aerospike-act-<version>
$ make
$ make -f Makesalt
```

This will create 2 binaries:

* ***actprep***: This executable prepares a drive for ACT by writing zeroes on every sector of the disk and then filling it up with random data (salting). This simulates a normal production state.
* ***act***: The ACT tool executable.

The root also contains a bash script called **runact** that runs actprep and act in a single process.

### Running the ACT Certification Process
---------------------

To certify your drive(s), first determine what certification test you will run,
as described above in **Process for Certifying a Drive(s) for 3x Performance** or
**Determining Expected Performance at Higher Throughput**.

For each certification test with ACT, you must perform the following steps:

1. Prepare the drive(s) with actprep -- only the first time you test a drive(s)
2. Create the config file for your test.
3. Run the test, sending the results to a log file.
4. Analyze log file output using the /latency_calc/act_latency.py script.
5. Determine pass/fail for the test.

The details of these steps are described in detail below.

**The tests destroy all data on the devices being tested!**

When preparing devices and running tests, make sure the devices are
specified by name correctly.

Make sure the test device is not mounted.


#### 1. Prepare the Drives with actprep - First Time Only
-------------

The first time you test a drive(s), you must
prepare the drive(s) by first cleaning them (writing zeros everywhere) and
then "salting" them (writing random data everywhere) with actprep.

actprep takes a device name as its only command-line parameter.  For
a typical 240GB SSD, actprep takes 30-60+ minutes to run. The time varies depending on the
drive and the capacity.

If you are testing multiple drives, you can run actprep on all of the drives in parallel. Preparing multiple drives
in parallel does not take a lot more time than preparing a single drive, so this step should only take an hour or two.

For example, to clean and salt the device /dev/sdc: (over-provisioned using hdparm)
```
$ sudo ./actprep /dev/sdc &
```
If you are using a RAID controller / over-provisioned using fdisk, make sure you specify the partition and not the raw
device. If the raw device is used then ACT will wipe out the partition table and this will
invalidate the test.
```
$ sudo ./actprep /dev/sdc1 &
```

#### 2. Create a Configuration File
-------------------------

The ACT package includes a Python script act_config_helper.py which helps you create a configuration file you can
use to run ACT. When you run this program it will:

1. Ask you basic questions about the test you want to run
2. Generate the correct config file, based on your answers

To run act_config_helper.py:
```
$ python act_config_helper.py
```
If you are testing multiple drives, specify the drives and the desired traffic per drive per second, and the config
file will be created appropriately.

Alternately you can create the config file manually by copying one of the sample config
files in the /examples directory and modifying it, as described in the **ACT Configuration Reference** below.



#### 3. Run the test
---------

From the ACT installation directory, run:
```
$ sudo ./act actconfig.txt > ouput.txt &
```
where:
```
* actconfig.txt - path/name for your config file name
* output.txt    - path/name of your log file
```
If running ACT from a remote terminal, it is best to run it as a background
process, or within a "screen".  To verify that ACT is running, tail the output
text file with the -f option.

Note that if the drive(s) being tested performs so badly that ACT's internal
transaction queues become extremely backed-up, ACT will halt before the
configured test duration has elapsed.  ACT may also halt prematurely if it
encounters unexpected drive I/O or system errors.


#### 4. Analyze ACT Output
--------------------

Run /latency_calc/act_latency.py to process the ACT log file and tabulate data.  Note that you can run
the script when the test is not yet complete, and you will see the partial results.

For example:
```
$ ./latency_calc/act_latency.py -l output.txt
```

where:

```
 -l <act output file name>   - required parameter that specifies the path/name of the log file generated by ACT
 -t <slice duration in seconds>  - optional parameter specifying slice length; default is 3600 sec (1 hour)
```

The Python script analyzes the ACT output in time slices as specified, and displays
latency data and various verification intervals for each slice.  The script output will
show latencies both for end-to-end transactions (which include time spent on the
transaction queues) and for the device IO portion of transactions.

The example output below shows a 12-hour test (each slice is an hour).  The **trans** table
shows transaction latency (end to end) and the **device** table at the right shows device latency.  So for
example, in the 5th hour, 1.68% of transactions failed to complete in under 1ms.

```
         trans                  device
         %>(ms)                 %>(ms)
 slice        1      8     64        1      8     64
 -----   ------ ------ ------   ------ ------ ------
     1     1.67   0.00   0.00     1.63   0.00   0.00
     2     1.38   0.00   0.00     1.32   0.00   0.00
     3     1.80   0.14   0.00     1.56   0.08   0.00
     4     1.43   0.00   0.00     1.39   0.00   0.00
     5     1.68   0.00   0.00     1.65   0.00   0.00
     6     1.37   0.00   0.00     1.33   0.00   0.00
     7     1.44   0.00   0.00     1.41   0.00   0.00
     8     1.41   0.00   0.00     1.35   0.00   0.00
     9     2.70   0.73   0.00     1.91   0.08   0.00
    10     1.54   0.00   0.00     1.51   0.00   0.00
    11     1.53   0.00   0.00     1.48   0.00   0.00
    12     1.47   0.00   0.00     1.43   0.00   0.00
 -----   ------ ------ ------   ------ ------ ------
   avg     1.62   0.07   0.00     1.50   0.01   0.00
   max     2.70   0.73   0.00     1.91   0.08   0.00
```

#### 5. Evaluate Device(s) by the Standard Pass/Fail Criteria
-------------------------

##### Passing a Performance Test
In any one-hour period of an ACT performance test, we expect that:

 - fewer than 5% of transactions fail to complete in 1 ms
 - fewer than 1% of transactions fail to complete in 8 ms
 - fewer than 0.1% of transactions fail to complete in 64 ms

The **max** line of the output shows the highest values observed in any single slice (hour) of time
and the values on the max line should not
exceed the allowable error values specified above.

In the example output above, we show only 12 hours of results, and the drive passes because the worst performance in any slice
was 2.7% of transactions failing to complete within 1 ms, 0.73% of transactions failing to complete in less
than 8 ms and no transactions failing to complete within 64 ms.

A device(s) which does not exceed these error thresholds in 24 hours passes the load test.

##### Passing a Stress Test
When doing stress testing at a level ABOVE where the drive is certified, a device passes the test
if ACT runs to completion, regardless of the number of errors.

## Tips and Tricks
-----------------
If a drive is failing or there is a large discrepancy between the device and transaction
latencies, try increasing the number of threads in the config file by one or two (as described below).

If a drive has been used for some other purpose for a period of time before testing, then the
speed may have degraded and performance may be much poorer than a new drive of the same model.

## ACT Configuration Reference
----------------------

#### Modifying the Config File Manually
-------------

For ease of use, this package includes act_config_helper.py for creating config
files. **Using act_config_helper.py is the recommended method for creating config files.**

If you are going to modify the config file manually, the package includes five example configuration files:

* actconfig_1x.txt    - run a normal load test on one device
* actconfig_3x.txt    - run a 3 times normal load test on one device
* actconfig_6x.txt    - run a 6 times normal load test on one device
* actconfig_12x.txt   - run a 12 times normal load test on one device
* actconfig_24x.txt   - run a 24 times normal load test on one device
* actconfig_1x_2d.txt - run a normal load test on two devices at a time
* actconfig_1x_4d.txt - run a normal load test test on four devices at a time

When modifying config files, you must be sure to set:

1. the device name(s)
2. the number of reads/writes to perform

For example, to run a 48x test, you would modify
the actconfig_24x.txt file to specify the correct drive and the correct number of reads/writes per drive.  For a
test of 8 drives at 6x, you would modify the actconfig_1x_4d.txt file to specify all of your drives AND to specify the
number of reads/writes to perform (6x rather than 1x).

The other fields in the configuration files should generally not be changed.

#### Format of Lines in the Config File
-------------------

All fields use a
```
name-token: value
```
format, and must be on a single line.
Field order in the file is unimportant.  To
add comments, add a line(s) that begin with '#'.

### Fields that you Must Change:

**device-names**
Comma-separated list of device names (full path) to test.  For example:
```
device-names: /dev/sdb,/dev/sdc
```
Make sure the devices named are entered correctly.

### Fields that you will Sometimes Change:

**num-queues**
Total number of queues.  If queue-per-device is set to yes, the num-queues field is ignored,
since in this case the number of queues is determined by the number of devices. if queue-per-device
is set to no, you must specify the number of queues based on how many devices you are testing.
We recommend two queues per device.  Formula: 2 x number of devices.

**threads-per-queue**
Number of threads per read
transaction queue. If a drive is failing and
there is a large discrepancy between transaction and device speeds from the ACT test
you can try increasing the number of threads.  Default is 8 threads/queue.

**record-bytes**
Size of a record in bytes.  This determines the size of a read operation -- just
record-bytes rounded up to a multiple of 512 bytes (or whatever the device's
minimum direct op size).  Also, along with write-reqs-per-sec and
large-block-op-kbytes, this item determines the rate of large block operations.
record-bytes is rounded up to a multiple of 128 bytes for the purposes of this
calculation.  For example, if record-bytes is 1500, write-reqs-per-sec is 1000,
and large-block-op-kbytes is 128, we write (1536 x 1000) bytes per second, or
(1536 x 1000) / (128 x 1024) = 11.71875 large blocks per second.  We double this
to simulate defragmentation where blocks depleted to 50%-used are re-packed,
yielding a large block write (and read) rate of 23.4375 blocks per second.

**read-reqs-per-sec**
Read transactions/second to generate.  Note
that this is not per device, or per read transaction queue. For 3 times (3x)
the normal load for four drives, this value would be 3*4*2000 = 24000. Formula: n x number of drives x 2000

**write-reqs-per-sec**
Write transactions/second to simulate.  Note that we do not do separate device
write operations per transaction -- written records are pooled up in big buffers
which are then written to device.  Therefore along with record-bytes and
large-block-op-kbytes, this item determines the rate of large block operations.
Note also, if write-reqs-per-sec is zero (read-only simulation) then no large
block operations are done.

### Fields that you will Rarely or Never Change:

**test-duration-sec**
Duration of the entire test, in seconds.
Note that it has to be a single number, e.g. use 86400, not 60*60*24.
The default is one day (24 hours).

**report-interval-sec**
Interval between generating observations,
in seconds. This is the smallest granularity that you can analyze.  Default is 1 sec.  The
/latency_calc/act_latency.py script aggregates these observations into slices, typically hour-long groups.

**microsecond-histograms**
Flag that specifies what time units the histogram buckets will use -- yes means
use microseconds, no means use milliseconds.  If this field is left out, the
default is no.

**record-bytes-range-max**
If set, simulate a range of record sizes from record-bytes up to
record-bytes-range-max.  Therefore if set, it must be larger than record-bytes
and smaller than or equal to large-block-op-kbytes.  The simulation models a
linear distribution of sizes within the range.
The default record-bytes-range-max is 0, meaning no range -- model all records
with size record-bytes.

**large-block-op-kbytes**
Size written and read in each
large-block write and large-block read operation respectively, in Kbytes.

**replication-factor**
Simulate the device load you would see if this node was in a cluster with the
specified replication-factor.  Increasing replication-factor increases the write
load, e.g. replication-factor 2 doubles the write load, and therefore doubles
the large-block read and write rates.  It does not ever affect the record-sized
read rate, however, since replica writes are always replaces (not updates).
The default replication-factor is 1.

**update-pct**
Simulate the device load you would see if this percentage of write requests were
updates, as opposed to replaces.  Updates cause the current version of a record
to be read before the modified version is written, while replaces do not need to
read the current version.  Therefore a non-zero update-pct will generate a
bigger internal record-sized read rate.  E.g. if read-reqs-per-sec is 2000 and
write-reqs-per-sec is 1000, the internal read-req rate will be somewhere between
2000 (update-pct 0), and 2000 + 1000 = 3000 (update-pct 100).
The default update-pct is 0.

**defrag-lwm-pct**
Simulate the device load you would see if this was the defrag threshold. The
lower the threshold, the emptier large blocks are when we defragment them (pack
the remaining records into new blocks), and the lower the "write amplification"
caused by defragmentation.  E.g. if defrag-lwm-pct is 50, the write
amplification will be 2x, meaning defragmentation doubles the internal effective
write rate, which for ACT is manifest as the large-block read and write rates.
The default defrag-lwm-pct is 50.

**commit-to-device**
Flag to model the mode where Aerospike commits each record to device
synchronously, instead of flushing large blocks full of records.  This causes a
device IO load with many small, variable-sized writes.  Large block writes (and
reads) still occur to model defragmentation, but the rate of these is reduced.
The default commit-to-device is no.

**commit-min-bytes**
Minimum size of a write in commit-to-device mode. Must be a power of 2. Each
write rounds the record size up to a multiple of commit-min-bytes. If
commit-min-bytes is configured smaller than the minimum IO size allowed on the
device, the record size will be rounded up to a multiple of the minimum IO size.
The default commit-min-bytes is 0, meaning writes will round up to a multiple of
the minimum IO size.

**tomb-raider**
Flag to model the Aerospike tomb raider.  This simply spawns a thread per device
in which the device is read from beginning to end, one large block at a time.
The thread sleeps for tomb-raider-sleep-usec microseconds between each block.
When the end of the device is reached, we repeat, reading from the beginning.
(In other words, we don't model Aerospike's tomb-raider-period.)
The default tomb-raider is no.

**tomb-raider-sleep-usec**
How long to sleep in each device's tomb raider thread between large block reads.
The default tomb-raider-sleep-usec is 1000, or 1 millisecond.

**scheduler-mode**
Mode in /sys/block/<device>/queue/scheduler for all the devices in
the test run -- noop means no special scheduling is done for device I/O
operations, cfq means operations may be reordered to optimize for physical
constraints imposed by rotating disc drives (which likely means it hurts
performance for ssds).  If the field is left out, the default is noop.
