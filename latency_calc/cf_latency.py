#!/usr/bin/python

# Get Latency of SSD from the act output
 
import sys
import types
import getopt
import re

# Index for the 16 buckets
bucketlist = ("00","01","02","03","04","05","06","07","08","09","10","11","12","13","14","15","16")

# Function to get the values in the bucket.
# Pass the operation (or the string in the act output)
def get_buckets(operation, line, fileid):
	value = {}
	total=long(line[line.find("(")+1:line.find(" total)")])
	line = fileid.readline()
	found = 1
	for b in bucketlist:
		value[b] = 0.0
	while (found == 1):
		found = 0
		for b in bucketlist:
			pattern = '.*?\('+b+': (.*?)\).*?'
			r = re.compile(pattern)
			m = r.search(line)
			if m:
				found = 1
				value[b] = long(r.search(line).group(1))
		if (found == 1):
			line = fileid.readline()

	return total, value, line

# Function to substract two buckets. Return the bucket of substracted values
def substract_buckets(value1, value2):
	value = {}
	for b in bucketlist:
		value[b] = value1[b] - value2[b]
	return value

# Function to get the percentage of operations within a bucket,
# pass the total operation and the bucket containing values.
# Returns the bucket containing percentage in each bucket
def percentage_buckets(num_operations, operation_values):
	percentage_value = {}
	if (num_operations > 0):
		for b in bucketlist:
			percentage_value[b] = (float(operation_values[b])/num_operations)*100
	return percentage_value

# Function to get the percentage of operations in bucket numbers > num
def get_percentage(percentage_value, num):
	if (num > 0):
		i = 1;
		perc = 0.0
		for b in bucketlist:
			if (i>num):
				perc = perc + percentage_value[b]
			i=i+1
	return perc
		
# Function to get the chunk of values for each interval of time.
# Skip as many time intervals given in skip parameter. Pass any line from output
def get_chunk(line, fileid, skip):
	time = 0
	for i in range(skip):
		while (line and not line.startswith("After ")):
			line = fileid.readline()
		if (i < skip - 1):
			line = fileid.readline()
		if not line:
			return 0, 0, 0, 0, 0, line
	time = long(line[line.find("After ")+6:line.find(" sec:")])
	line = fileid.readline()
	while(line and line.strip()):
		if (line.startswith("RAW READS ")):
			raw_total, raw_value, line = get_buckets("RAW READS", line, fileIN)
		elif (line.startswith("READS ")):
			read_total, read_value, line = get_buckets("READS", line, fileIN)
		else: 
			line = fileid.readline()

	try:
		time, raw_total, raw_value, read_total, read_value
	except NameError:
		return 0, 0, 0, 0, 0, line
	else:	
		return time, raw_total, raw_value, read_total, read_value, line

# Print output line.
def print_line(read, raw_read, num_buckets, skip_buckets, num):
	output="%3s" % (num)+"       "
	
	for i in range((num_buckets -1) / skip_buckets + 1):
		space="  "
		if (i > 4):
			space=space+" "
			if(i>7):
				space=space+" "
		output=output+space+"%.2f" % (read[i*skip_buckets])
	output=output+"          "
	if(num_buckets > 4):
		output=output+" "
		if(num_buckets > 7):
			output=output+" "
	for i in range((num_buckets - 1) / skip_buckets + 1):
		space="  "
		if (i > 4):
			space=space+" "
			if(i>7):
				space=space+" "
		output=output+space+"%.2f" % (raw_read[i*skip_buckets])
	print output

# Function to print usage
def usage():
	print "Usage:"
	print " -l act outfile (Eg. actout.txt)"
#	print " -b Number of buckets (Eg. 7)"
#	print " -s Skip buckets while printing (Eg. 3)"
	print " -t Time Interval in seconds (Eg. 3600)"
	return

#arg processing
try:
	opts, args = getopt.getopt(sys.argv[1:], "l:b:s:t:", ["log=","buckets=","skip_buckets=","time="])
except getopt.GetoptError, err:
	print str(err)
	usage()
	sys.exit(-1)

# Main

# Default values for arguments
arg_log = None
arg_buckets = 7
arg_skip_buckets = 3
arg_time = 3600
for o, a in opts:
	if ((o == "-l") or (o == "--log")):
		arg_log = a
	if ((o == "-b") or (o == "--buckets")):
		arg_buckets = int(a)
	if ((o == "-s") or (o == "--skip_buckets")):
		arg_skip_buckets = int(a)
	if ((o == "-t") or (o == "--time")):
		arg_time = long(a)

if ((arg_log == None) or (arg_buckets == -1) or (arg_skip_buckets == -1) or (arg_time == -1)):
	usage()
	sys.exit(-1)

if (arg_buckets > 16 or arg_buckets < 1):
	print "Buckets should be in between 1 and 16. Given", arg_buckets
	sys.exit(-1)

if (arg_skip_buckets > arg_buckets):
        print "Skip buckets",arg_skip_buckets, "should be less than buckets",arg_buckets
        sys.exit(-1)

# Open the log file
try:
	fileIN = open(arg_log, "r")
except:
	print "log file "+arg_log+" not found."
        sys.exit(-1)

# Get the first chunk of values
line = fileIN.readline()
old_time, old_raw_read, old_raw_value, old_read, old_read_value, line = get_chunk(line, fileIN, 1)
if (not old_time):
	print "get_chunk failed"
	exit (-1)

# Get the second chunk of values
new_time, new_raw_read, new_raw_value, new_read, new_read_value, line = get_chunk(line, fileIN, 1)
if (not new_time):
	print "get_chunk failed"
	exit (-1)

# Find the time interval and skip number according to arg_time
time_interval = new_time-old_time
if ((arg_time % time_interval) != 0):
	print "Time", arg_time, "is not multiple of",time_interval,". Cannot proceed"
	sys.exit(-1)
num_interval = (arg_time / time_interval)
if (num_interval < 3):
	print "Time", arg_time, "should be atleast more than twice",time_interval,". Cannot proceed"
	sys.exit(-1)

# Initialize the array variable to store data
num = 1
read = [0.0] * arg_buckets
raw_read = [0.0] * arg_buckets
avg_read = [0.0] * arg_buckets
avg_raw_read = [0.0] * arg_buckets
max_read = [0.0] * arg_buckets
max_raw_read = [0.0] * arg_buckets

# Get the first chunk of data to process after the arg_time interval (already 2 lines read)
new_time, new_raw_read, new_raw_value, new_read, new_read_value, line = get_chunk(line, fileIN, num_interval - 2)

if (not new_time):
	print "Not enough line to print for "+str(arg_time)+" secs"
	sys.exit()
else:
	final_time = new_time
num_reads = new_read - old_read
num_raw_reads = new_raw_read - old_raw_read
num_read_value = substract_buckets(new_read_value, old_read_value)
num_raw_value = substract_buckets(new_raw_value, old_raw_value)

percentage_reads = percentage_buckets(num_reads, num_read_value)
percentage_raw_reads = percentage_buckets(num_raw_reads, num_raw_value)
for i in range(arg_buckets):
	read[i] = round(get_percentage(percentage_reads, i+1),2)
	raw_read[i] = round(get_percentage(percentage_raw_reads, i+1),2)
	avg_read[i] = read[i]
	avg_raw_read[i] = raw_read[i]
	max_read[i] = read[i]
	max_raw_read[i] = raw_read[i]

# Print the heading
output="slice whole"
for i in range((arg_buckets - 1) / arg_skip_buckets + 1):
	timeout=pow(2, (i*arg_skip_buckets))
	output=output+" %>"+str(timeout)+"ms"
output=output+"  SSD-only"
for i in range((arg_buckets - 1)/ arg_skip_buckets + 1):
	timeout=pow(2, (i*arg_skip_buckets))
	output=output+" %>"+str(timeout)+"ms"
print output

output_line="-------------------"
for i in range((arg_buckets - 1) / arg_skip_buckets + 1):
	output_line=output_line+"--------------"
print output_line

# Print the first latency value
print_line(read, raw_read, arg_buckets, arg_skip_buckets, num)

# Process the rest of the data print latency values for each arg_time (skip num_interval)
while line:
	num = num + 1
	new_time, new_raw_read, new_raw_value, new_read, new_read_value, line = get_chunk(line, fileIN, num_interval)

	if (not new_time):
		break
	else:
		final_time = new_time
	num_reads = new_read - old_read
	num_raw_reads = new_raw_read - old_raw_read
	num_read_value = substract_buckets(new_read_value, old_read_value)
	num_raw_value = substract_buckets(new_raw_value, old_raw_value)

	percentage_reads = percentage_buckets(num_reads, num_read_value)
	percentage_raw_reads = percentage_buckets(num_raw_reads, num_raw_value)

	for i in range(arg_buckets):
		read[i] = round(get_percentage(percentage_reads, i+1),2)
		raw_read[i] = round(get_percentage(percentage_raw_reads, i+1),2)
		avg_read[i] = round((avg_read[i] + read[i])/2,2)
		avg_raw_read[i] = round((avg_raw_read[i] + raw_read[i])/2,2)
		if (read[i] > max_read[i]):
			max_read[i] = read[i]
		if (raw_read[i] > max_raw_read[i]):
			max_raw_read[i] = raw_read[i]
	print_line(read, raw_read, arg_buckets, arg_skip_buckets, num)

	old_time, old_raw_read, old_raw_value, old_read, old_read_value = new_time, new_raw_read, new_raw_value, new_read, new_read_value
	line = fileIN.readline()

# Print average and maximum
print output_line
print_line(avg_read, avg_raw_read, arg_buckets, arg_skip_buckets, "avg")
print_line(max_read, max_raw_read, arg_buckets, arg_skip_buckets, "max")

print "\nAnalyzed Test Duration:", final_time,"seconds\n"
