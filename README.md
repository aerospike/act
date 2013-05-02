## Aerospike Certification Tool (ACT)

### Overview
------------

ACT is a program for testing and certifying flash/SSD devices' performance for
Aerospike Database (with SATA, SAS and PCIe connectors).  ACT shows latency responses when you are reading from and writing to
the database concurrently while modeling the Aerospike Database server's I/O 
pattern as closely as practical.

The purpose of this certification is:

1. Determine if an SSD will stand up to the demands of a high-speed real-time database
2. Evaluate the upper limits of an SSD's capabilities to understand how much throughput you can expect from a drive, to help you determine how many SSDs will be required to handle your expected load.

Not all SSDs can handle the high volume of transactions required by high 
performance real-time databases like Aerospike Database.  Many SSDs are rated 
for 100K+ reads/writes per second, but in production the actual load they 
can withstand for sustained periods of time is generally much lower.  In the process
of testing many common SSDs in high-throughput tests, Aerospike developed this certification tool, ACT, that you can use to test/certify an 
SSD for yourself.

We have found performance – especially latency – of SSDs to be highly 
dependent on the write load the SSD is subjected to. Over the first few hours of a test, 
performance can be excellent, but past the 4- to 10-hour mark (depending 
on the drive), performance can suffer.

The ACT tool allows you to test an SSD device(s) for yourself.
However Aerospike has tested a variety of SSDs and has specific recommendations.
For more information, visit the Aerospike Database documentation at:  https://docs.aerospike.com/.

#### What the ACT Tool Does
---------------------------

ACT performs a combination of large (128K) block reads and writes and small (1.5K) block reads, simulating
standard real-time database read/write loads.

Reads and write latency is measured for a long enough period of time (typically 24 hours) to evaluate drive stability and 
overall performance.

**Traffic/Loading** You can simulate:

* 1x - normal load (2000 reads/sec and 1000 writes/sec)
* 3x - high load (6000 reads/sec and 3000 writes/sec)
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
the test based on actual performance.**

To certify a drive(s) for 3x performance with Aerospike Database requires two stages:

1. Test a single drive to determine performance using the hardware configuration and connectors. The single-device certification will help you determine individual drive performance. 
2. If you will be using multiple drives, you can then run ACT to test multiple drives to see how 
the results will be affected by the capacity of the bus or the throughput of the RAID controller that is managing your drives.

The test process with ACT is the same for both stages, but in the first stage you are testing a drive and
in the second stage, you are testing the linearity/scalability of your connector with multiple drives installed.

The single-drive stage takes 48 hours.  The multi-drive stage takes an additional 48 hours.

##### The first stage is to certify a single drive, to test the drive itself and the connection.

Begin by installing your SSD device.  Our website has more details about installing SSDs in different environments
and configurations at https://docs.aerospike.com/.

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
and configurations at https://docs.aerospike.com/.

**Test 3: Repeat Test 1, with all drives installed: Test under high loads**

Run ACT for 24 hrs using the 3x test (6000 reads/sec and 3000 writes/sec).
The drives pass this test if less than 5% of operations fail to complete in 1 ms or less.

**Test 4: Repeat Test 2, with all drives installed: Stress test to ensure the drives do not fail under excessive loads**

Run a 6x test for 24 hrs (12000 reads/sec and 6000 writes/sec).  The drives pass this test if ACT runs to completion, regardless of the error rate.

**The drives are certified if they pass Test 3 and Test 4.**  Once the drive(s) has been certified, the drive can be used with Aerospike Database.

&nbsp;

#### How to Test a Drive(s) to Determine Expected Performance at Higher Throughput
-------------------------------------------------------

If your application is going to have high volumes of transactions and your drive(s) passes the 3x certification, 
we recommend that you test your drive to determine its upper limit on transaction processing latency.  This will help
you determine how many SSDs you will need to run your application when you are fully scaled up.

To certify a drive(s) at higher levels of performance, do the certification process as described above, but use higher loads (12x, 24x, etc.).
Test the drive(s) at progressively higher rates until more than 5% of operations fail in 1 ms.  

For example, if you test at 24x and less than 5% of operations fail to complete in 1 ms, re-run the test at 48x, etc.  When the drive completes
the test at a particular speed with *more* than 5% of operations failing to complete in 1 ms (i.e., fails the test), then the drive is certified at the
next lower level where the drive DOES have fewer than 5% of errors in under 1 ms.

As before, test a single drive first, and then test with multiple drives to make sure that the
performance scales linearly with your connector/controller.

### Getting Started
--------------------

**Download the ACT package through git:**

```
$ git clone git@github.com:aerospike/act.git
```
This creates an /act directory.  

Alternately you can download the ZIP or TAR file from the links at the left.
When you unpack/untar the file, it acreates an /aerospike-act-<version> directory.

**Build the package.**

```
$ cd act    OR    cd /aerospike-act-<version>
$ make
$ make -f Makesalt
```

This will create 2 binaries:

* ***actprep***: This executable prepares a drive for ACT by writing zeroes on every sector of the disk and then filling it up with random data (salting). This simulates a normal production state.
* ***act***: The ACT tool executable.

### Running the ACT Certification Process 
---------------------

To certify your drive(s), first determine what certification test you will run, 
as described above in **Recommended Process for Certifying a Drive with ACT** or 
**How to Certify a Drive(s) with Higher Loads**.

For each certification test with ACT, you must perform the following steps:

1. Prepare the storage device(s) using actprep.
2. Create the config file for your test.
3. Run ACT, sending the results to a log file.
3. Analyze log file output using the /latency_calc/act_latency.py script.
4. Determine pass/fail for the test.

The details of these steps are described in detail below.

**The tests destroy all data on the devices being tested!**

When preparing devices and running tests, make sure the devices are
specified by name correctly.

Make sure the test device is not mounted.

#### 1. Prepare Devices Using actprep
---------------------------------

The first step of the test is to 
prepare storage devices by first cleaning them (writing zeros everywhere) and
then "salting" them (writing random data everywhere) with actprep.

actprep takes a device name as its only command-line parameter.  For
a typical 240GB SSD, actprep takes a little over an hour to run.

For example, to clean and salt the device /dev/sdc: (over-provisioned using hdparm)
```
$ sudo ./actprep /dev/sdc
```
If you are using a RAID controller / over-provisioned using fdisk, make sure you specify the partition and not the raw
device. If the raw device is used then ACT will wipe out the partition table and this will
invalidate the test.
```
$ sudo ./actprep /dev/sdc1
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
### Answer the questions asked in command line.
```
Alternately you can create the config file manually by copying one of the sample config
files in the /examples directory and modifying it, as described in the **ACT Configuration Reference** below.

#### 3. Run your Test with ACT
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
$ ./act_latency.py -l output.txt
```

where:

```
 -l <act output file name>   - required parameter that specifies the path/name of the log file generated by ACT
 -t <slice duration in seconds>  - optional parameter specifying slice length; default is 3600 sec
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

##### Passing the Load Test
To deploy a device(s) in production, Aerospike expects it to be able to perform
consistently for the 3x test:

In any one-hour period of the 3x test, we expect that:

 - fewer than 5% of transactions fail to complete in 1 ms
 - fewer than 1% of transactions fail to complete in 8 ms
 - fewer than 0.1% of transactions fail to complete in 64 ms

The **max** line of the output shows the highest values observed in any single slice (hour) of time
and the values on the max line should not
exceed the allowable error values specified above.  

In the example output above, we show only 12 hours of results, and the drive passes because the worst performance in any slice
was 2.7% of transactions failing to complete within 1 ms, 0.73% of transactions failed to complete in less
than 8 ms and no transactions failed to complete within 64 ms.

A device(s) which does not exceed these error thresholds in 24 hours passes the load test.

##### Passing the Stress Test
When doing stress testing at 6x, a device passes the test if ACT runs to completion, regardless of the number of errors.

## Tips and Tricks
-----------------
If a drive is failing or there is a large discrepancy between the device and transaction
speeds, try increasing the number of threads in the config file (described below).

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
3. the number of large block operations to perform (large-block-ops-per-sec)

For example, to run a 48x test, you would modify
the actconfig_24x.txt file to specify the correct drive and the correct number of reads/writes.  For a
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

**read-reqs-per-sec**
Read transactions/second.  Note
that this is not per device, or per read transaction queue. For 3 times (3x)
the normal load, this value would be 3*2000 = 6000. Formula: n x 2000

**large-block-ops-per-sec**
Large-block write and large-block read operations per second.  Note that this is not per
device. e.g. For 3 times (3x) the normal load, this value would be 3*23.5 = 71
(rounded up). Formula: n x 23.5

### Fields that you will Sometimes Change:

**threads-per-queue**
Number of threads per read
transaction queue. If a drive is failing and
there is a large discrepancy between transaction and device speeds from the ACT test
you can try increasing the number of threads.

**read-req-num-512-blocks**
Size for each read
transaction, in 512-byte blocks, e.g. for 1.5-Kbyte reads (the default), this value would be 3.

### Fields that you will Rarely or Never Change:

**queue-per-device**
Flag that determines ACT's internal read transaction queue setup -- yes means
each device is read by a single dedicated read transaction queue, no means each
device is read by all read transaction queues. If this field is left out, the default is no.

**num-queues**
Total number of read transaction
queued.  If queue-per-device is set to yes, the num-queues field is ignored,
since in this case the number of queues is determined by the number of devices.

**test-duration-sec**
Duration of each analysis slice, in seconds.
Note that it has to be a single number, e.g. use 86400, not 60*60*24.
The default is one day (24 hours).

**report-interval-sec**
Interval between metric reports,
in seconds.

**large-block-op-kbytes**
Size written and read in each
large-block write and large-block read operation respectively, in Kbytes.

**use-valloc**
Flag that determines ACT's memory allocation mechanism for read transaction
buffers -- yes means a system memory allocation call is used, no means dynamic
stack allocation is used.  If this field is left out, the default is no.

**num-write-buffers**
Number of different large blocks of random data we choose from when doing a
large-block write operation -- 0 will cause all zeros to be written every time. 
If this field is left out, the default is 0.  

**scheduler-mode**
Mode in /sys/block/<device>/queue/scheduler for all the devices in
the test run -- noop means no special scheduling is done for device I/O
operations, cfq means operations may be reordered to optimize for physical
constraints imposed by rotating disc drives (which likely means it hurts
performance for ssds).  If the field is left out, the default is noop.
