### ACT ( Aerospike Certification Tool )

### Overview
------------

ACT is a program for certifying flash/SSD devices' performance for
Aerospike Database (with SATA, SAS and PCIe connectors).

Not all SSDs can handle the high volume of transactions required by high 
performance real-time databases like Aerospike Database.  Many SSDs are rated 
for 100K+ reads/writes per second, but in production the actual load they 
can withstand for sustained periods of time is generally much lower.  
Aerospike has tested many common SSDs in high-throughput tests and we have 
also developed this certification tool that you can use to test/certify an 
SSD for yourself.

We have found performance – especially latency – of SSDs to be highly 
dependent on the write load the SSD is subjected to. Over the first few hours, 
performance can still be excellent, but past the 4 to 10 hour mark (depending 
on the drive), performance can suffer.

This tool shows latency responses when you are reading from and writing to
the database concurrently while modeling the Aerospike server's device IO 
pattern as closely as practical.

#### How to Certify a Drive with ACT
------------------------------------

##### The first stage is to certify a single drive, to test the drive itself and the
connection.

Step 1: Test under normal/high loads

Run ACT for 24 hrs using the 3x test (6000 reads/sec and 3000 writes/sec)
The drive passes this step if the results have less than 5% of operations fail to complete in 1 ms or less

Step 2: Test under peak loads to ensure the server does not crash under high traffic loads

Run a 6x test for 24 hrs (12000 reads/sec and 6000 writes/sec)
THe drive passes this step if it finishes the test in any way

##### The second stage is to certify multiple drives, to make sure that the drives
perform correctly in multi-drive configurations.

Step 3: Repeat step 1, with all drives: Test under normal/high loads

Run ACT for 24 hrs using the 3x test (6000 reads/sec and 3000 writes/sec)
The drives pass this step if the results have less than 5% of operations fail to complete in 1 ms or less 

Step 4: Repeat step 2, with all drives: Test under peak loads to ensure the server does not crash under high traffic loads

Run a 6x test for 24 hrs (12000 reads/sec and 6000 writes/sec)
THe drives pass this step if it finishes the test in any way

#### What the ACT Tool Does
---------------------------

Three types of IO operations occur during a test run:

1. Small (~2 Kbyte) read operations, typically several thousand per second.
2. Large-block (~128 Kbyte) read operations, typically a few tens per second.
3. Large-block write operations, same size and rate as large-block reads.

The small read operations model client transaction requests.  They occur at a
specified rate.  Requests are added at this rate to a specified number of
read transaction queues, each of which is serviced by a specified number of
threads.

The large-block read and write operations model the Aerospike server's
defragmentation process.  They occur at a specified rate, executed from one
dedicated large-block read thread and one dedicated large-block write thread per
device.

### Getting started
--------------------

```
$ git clone git@github.com:aerospike/act.git
$ cd act
$ make
$ make -f Makesalt
```

This will create 2 binaries, act and actprep

* ***actprep***:This executable will basically zero’s out the drives and fills it up with random data(Salting). Basically to reproduce a normal production state.
* ***act***: The primary executable.

### Test Process Overview
---------------------


1. Clean and initialize the storage device(s).
2. Run the act executable.
3. Analyze act's output using the act_latency.py script.


### Caution
-------

THE TESTS DESTROY ALL DATA ON THE TEST DEVICES!

When cleaning, initializing, and running tests, make sure the devices are
specified by name correctly.

Also make sure that the test devices are not mounted.

Run the command:
```
	$ mount
```
and examine the result.  e.g. the result:
	/dev/sda1 on /boot type ext3 (rw)
implies device /dev/sda1 is mounted.

Also run the command:
```
	$ sudo /sbin/pvscan
```
and examine the result.  e.g. the result:
	  PV /dev/sda2   VG VolGroup00   lvm2 [19.88 GB / 0    free]
implies device /dev/sda2 is mounted.

Unmount any intended test devices that are mounted.


### Cleaning and Initializing Devices
---------------------------------

For consistency, and to obtain test results that model the long-time
equilibrium condition expected in Aerospike production servers, it is best to
prepare storage devices by first cleaning them (writing zeros everywhere) and
then "salting" them (writing random data everywhere).

This package contains actprep, an executable that may be used to clean and salt
a device.  actprep takes a device name as its only command-line parameter.  For
a typical 240GB SSD, actprep takes a little over an hour to run.

Example - to clean and salt device /dev/sdc: (If Over-Provisioned using hdparm)
```
        $ sudo ./actprep /dev/sdc
```
If Over-Provisioned using fdisk, make sure you specify the partition and not raw
device, if raw device(sdc) is used then it will wipe out the partition table.
```
        $ sudo ./actprep /dev/sdc1
```



### Create Configuration file
-------------------------

The repo contains act_config_helper.py which can create configuration file you can
use to run act, When yoy run this program it will ask you basic questions on test
you want to run and generate config file at the end of the questions
```
        $ python act_config_helper.py
        ### Answer the questions asked in command line.
```
Alternately you can create config file manually based on instructions below.

### Using ACT
---------

Necessary files: act (the executable), plus a configuration text file.

For ease of use, this package includes act_config_helper.py for creating config 
files and also has five example configuration files:

* actconfig_1x.txt    - run a normal load test on one device
* actconfig_3x.txt    - run a 3 times normal load test on one device
* actconfig_6x.txt    - run a 6 times normal load test on one device
* actconfig_12x.txt   - run a 12 times normal load test on one device
* actconfig_24x.txt   - run a 24 times normal load test on one device
* actconfig_1x_2d.txt - run a normal load test on two devices at a time
*  actconfig_1x_4d.txt - run a normal load test test on four devices at a time

These configuration files must be modified to make sure the device-names field
(see below) specifies exactly the device(s) to be tested.

The other fields in the configuration files should not be changed without good
reasons.  As they are, the files specify 24-hour tests with IO patterns and
loads very similar to Aerospike production servers.

Usage example:
```
	$ sudo ./act actconfig.txt > ouput.txt
```
act outputs to stdout, so for normal (long-duration) tests, pipe to an output
file as above.  This will be necessary to run the act_latency.py script to
analyze the output.

If running act from a remote terminal, it is best to run it as a background
process, or within a "screen".  To verify that act is running, tail the output
text file with the -f option.

Note that if the drive(s) being tested perform so badly that act's internal
transaction queues become extremely backed-up, act will halt before the
configured test duration has elapsed.  act may also halt prematurely if it
encounters unexpected drive I/O or system errors.


### ACT Configuration File
----------------------

All fields use a "name-token: value" format, and must be on a single line.
Field order in the file is unimportant.  Integer values must be in decimal.  To
add comments, use '#' at the beginning of a line.  The fields are:

**device-names**
The value is a comma-separated list of device names (full path), such as
/dev/sdb.  Make absolutely sure the devices named are exactly the devices to be
used in the test.

**queue-per-device**
The value is either yes or no.  If the field is left out, the default is no.
This flag determines act's internal read transaction queue setup -- yes means
each device is read by a single dedicated read transaction queue, no means each
device is read by all read transaction queues.

**num-queues**
The value is a non-zero integer.  This is the total number of read transaction
queues.  However if queue-per-device is set to yes, this field is ignored,
since in this case the number of queues is determined by the number of devices.

**threads-per-queue**
The value is a non-zero integer.  This is the number of threads per read
transaction queue that execute the read transactions.

**test-duration-sec**
The value is a non-zero integer.  This is the duration of the test, in seconds.
Note that it has to be a single number, e.g. use 86400, not 60*60*24.

**report-interval-sec**
The value is a non-zero integer.  This is the interval between metric reports,
in seconds.

**read-reqs-per-sec**
The value is a non-zero integer.  This is the total read transaction rate.  Note
that it is not per device, or per read transaction queue. e.g. For 2 times (2x)
the normal load, value would be 2*2000 = 4000. Formula: n x 2000

**large-block-ops-per-sec**
The value is a non-zero integer.  This is the total rate used for both
large-block write and large-block read operations.  Note that it is not per
device. e.g. For 2 times (2x) the normal load, value would be 2*23.5 = 47
(rounded up) Formula: n x 23.5

**read-req-num-512-blocks**
The value is a non-zero integer.  This is the size read in each read
transaction, in 512-byte blocks, e.g. for 1.5-Kbyte reads, use 3.

**large-block-op-kbytes**
The value is a non-zero integer.  This is the size written and read in each
large-block write and large-block read operation respectively, in Kbytes.

**use-valloc**
The value is either yes or no.  If the field is left out, the default is no.
This flag determines act's memory allocation mechanism for read transaction
buffers -- yes means a system memory allocation call is used, no means dynamic
stack allocation is used.

**num-write-buffers**
The value is an integer.  If the field is left out, the default is 0.  This is
the number of different large blocks of random data we choose from when doing a
large-block write operation -- 0 will cause all zeros to be written every time.

**scheduler-mode**
The value is either noop or cfq.  If the field is left out, the default is noop.
This sets the mode in /sys/block/<device>/queue/scheduler for all the devices in
the test run -- noop means no special scheduling is done for device IO
operations, cfq means operations may be reordered to optimize for physical
constraints imposed by rotating disc drives (which likely means it hurts
performance for ssds).


### Analyzing act Output
--------------------

Run act_latency.py to process a act output file and tabulate data about
"latencies" (small read transactions that took longer than usual).

Example usage:
```
	$ ./act_latency.py -l output.txt
```
**act_latency.py** command-line parameters:
```
 -l <act output file name>

 -t <analysis slice interval in seconds> (default is 3600)
```
(There are two other optional parameters for more advanced use, to control which
latency thresholds are displayed.)

The script will analyze the act output in time slices as specified, and display
latency data above various thresholds for each slice.  The script output will
show latencies both for end-to-end transactions (which include time spent on the
transaction queues) and for the device IO portion of transactions.

Example **act_latency.py** output (for a act output file yielding 12 slices):
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

### Device Pass/Fail Criteria
-------------------------

To deploy a device in production, Aerospike expects it to be able to perform
consistently as follows:

In any one-hour period for normal load , we must find that:

 - fewer than 5% of transactions exceed 1 ms
 - fewer than 1% of transactions exceed 8 ms
 - fewer than 0.1% of transactions exceed 64 ms

A device which does not violate these thresholds for 48 hours is considered
production-worthy.
