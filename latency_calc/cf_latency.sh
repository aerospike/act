#!/bin/sh
#set -x
if [ $# != 3 ]
then
        echo "Wrong Usage. Provide the log file name. Usage as follows:"
        echo "./cf_latency.sh <log file name> <preferred number of buckets> <need graph>"
        echo "Eg: ./cf_latency.sh cflog.log 3 0"
        exit 1
fi

if [ ! -r $1 ]
then
        echo "File $1 is not found/readable"
        exit 1
fi

if [ $2 -le 0 ]
then
        echo "Preferred number of buckets should be greater than 0"
        exit 1
fi

graph=0
if [ $3 -eq 1 ]
then
        graph=1
fi

logfile=$1
pref_buckets=$2

# Extract the lines which has information about read/writes and proxy and save in a temp log.
awk ' /^READS/  { {printf("%s %s",$1, $2)} ; x=1; while( x++ <= 4 ) { getline; sub(//,"",$0); printf("%s", $0) } printf ("\n") }' $1  | sed 's/([0-9][0-9]://g ; s/)//g; s/(//g' > tmp-reads.log

awk ' /^LARGE BLOCK WRITES/  { {printf("%s %s",$1, $4)}; x=1; while( x++ <= 4 ) { getline; sub(//,"",$0); printf("%s", $0) } printf ("\n") }' $1  | sed 's/([0-9][0-9]://g ; s/)//g; s/(//g' > tmp-writes.log

awk ' /^LARGE BLOCK READS/  { {printf("%s %s",$1, $4)}; x=1; while( x++ <= 4 ) { getline; sub(//,"",$0); printf("%s", $0) } printf ("\n") }' $1  | sed 's/([0-9][0-9]://g ; s/)//g; s/(//g' > tmp-defrag.log

awk ' /^RAW READS/  { {printf("%s %s",$1, $3)}; x=1; while( x++ <= 4 ) { getline; sub(//,"",$0); printf("%s", $0) } printf ("\n") }' $1  | sed 's/([0-9][0-9]://g ; s/)//g; s/(//g' > tmp-ssd-reads.log

# Create graph.out from read.log with the read percentage for each bucket. Format for the csv file is "day of month" "time" "read % for bucket1" "read % for bucket2" etc.
if [ $graph -eq 1 ]
then
        awk '{ {if (NR == 1) { date1=$2; month1=$1; time1=$4; reads1=$5; i=0; while (i++ <= 12) { bucket1[i]=$(i+5) }} else { date1=data2; month1=month2; time1=time2; reads1=reads2; i=0; while (i++ <= 12) { bucket1[i]=bucket2[i]}}} { if (NR == 1) getline; date2=$2; month2=$1; time2=$4; reads2=$5; i=0; while (i++ <= 12) { bucket2[i]=$(i+5)}} {i=0; reads=reads2-reads1 ; printf("%s/%s-%s ",date2,month2, time2) ; if(reads<=0) reads=1 ;while (i++<=12) { printf("%.2f ", ((bucket2[i]-bucket1[i])/reads)*100) } } printf("\n")}' tmp-reads.log > graph-${logfile}.out
	awk '{ {if (NR == 1) { date1=$2; month1=$1; time1=$4; reads1=$5; i=0; while (i++ <= 12) { bucket1[i]=$(i+5) }} else { date1=data2; month1=month2; time1=time2; reads1=reads2; i=0; while (i++ <= 12) { bucket1[i]=bucket2[i]}}} { if (NR == 1) getline; date2=$2; month2=$1; time2=$4; reads2=$5; i=0; while (i++ <= 12) { bucket2[i]=$(i+5)}} {i=0; reads=reads2-reads1 ; printf("%s/%s-%s ",date2,month2, time2) ; if(reads<=0) reads=1 ;while (i++<=12) { printf("%.2f ", ((bucket2[i]-bucket1[i])/reads)*100) } } printf("\n")}' tmp-ssd-reads.log > graph-ssd-${logfile}.out

fi

echo "Reads " > tmp-graph-tp.log
head -1 tmp-reads.log >> tmp-graph-tp.log
tail -2 tmp-reads.log > tmp-reads-t.log
head -1 tmp-reads-t.log >> tmp-graph-tp.log
wc -l tmp-reads.log >> tmp-graph-tp.log
echo "Writes " >> tmp-graph-tp.log
head -1 tmp-writes.log >> tmp-graph-tp.log
tail -2 tmp-writes.log > tmp-writes-t.log
head -1 tmp-writes-t.log >> tmp-graph-tp.log
wc -l tmp-writes.log >> tmp-graph-tp.log
echo "Defrag Reads " >> tmp-graph-tp.log
head -1 tmp-defrag.log >> tmp-graph-tp.log
tail -2 tmp-defrag.log > tmp-defrag-t.log
head -1 tmp-defrag-t.log >> tmp-graph-tp.log
wc -l tmp-defrag.log >> tmp-graph-tp.log
echo "SSD Reads " >> tmp-graph-tp.log
head -1 tmp-ssd-reads.log >> tmp-graph-tp.log
tail -2 tmp-ssd-reads.log > tmp-ssd-reads-t.log
head -1 tmp-ssd-reads-t.log >> tmp-graph-tp.log
wc -l tmp-ssd-reads.log >> tmp-graph-tp.log

echo "Latency Threshold = 2^$pref_buckets ms"

((pref_buckets=pref_buckets+1))

head -4 tmp-graph-tp.log | awk -v pb=$pref_buckets '{getline; i=1; reads1=0; in_reads1=0; while (i++ <= 13) { val=$(1+i); if(substr(val, 0, 1) == "A") { break; } if(i <= pb) { in_reads1=in_reads1+val; } in_reads1_arr[i]=val; reads1=reads1+val; } getline; i=1; reads2=0; in_reads2=0; while (i++ <= 13) { val=$(1+i); if(substr(val, 0, 1) == "A") { break; } if(i <= pb) { in_reads2=in_reads2+$(1+i); } in_reads2_arr[i]=val; reads2=reads2+val; } total_in_bucket=in_reads2-in_reads1; total_reads=reads2-reads1; total_timeout=total_reads-total_in_bucket; in_bucket = (total_in_bucket/total_reads)*100 ; in_timeout = (total_timeout / total_reads)*100; read_buckets=""; getline; time=$0-2; i=1; bvsum=0; while(i++ <= length(in_reads2_arr)) {bucket_val=sprintf("%.1f", (in_reads2_arr[i]-in_reads1_arr[i])/time); read_buckets = read_buckets " | " bucket_val; bvsum=bvsum+bucket_val;}; printf ("Percent Above Latency Threshold     - %s\nRead Buckets     | Total = %s%s\n", in_timeout, bvsum, read_buckets); }'

head -16 tmp-graph-tp.log | awk -v pb=$pref_buckets '{getline; getline; getline; getline; getline; getline; getline; getline; getline; getline; getline; getline; getline; i=1; reads1=0; in_reads1=0; while (i++ <= 13) { val=$(1+i); if(substr(val, 0, 1) == "/" || substr(val, 0, 1) == "R") { break; } if(i <= pb) { in_reads1=in_reads1+val; } in_reads1_arr[i]=val; reads1=reads1+val; } getline; i=1; reads2=0; in_reads2=0; while (i++ <= 13) { val=$(1+i); if(substr(val, 0, 1) == "/" || substr(val, 0, 1) == "R") { break; } if(i <= pb) { in_reads2=in_reads2+val; } in_reads2_arr[i]=val; reads2=reads2+val; } total_in_bucket=in_reads2-in_reads1; total_reads=reads2-reads1; total_timeout=total_reads-total_in_bucket; in_bucket = (total_in_bucket/total_reads)*100 ; in_timeout = (total_timeout / total_reads)*100; read_buckets=""; getline; time=$0-2; i=1; bvsum=0; while(i++ <= length(in_reads2_arr)) {bucket_val=sprintf("%.1f", (in_reads2_arr[i]-in_reads1_arr[i])/time); read_buckets = read_buckets " | " bucket_val; bvsum=bvsum+bucket_val;}; printf ("Percent Above Latency Threshold SSD - %s\nRead Buckets SSD | Total = %s%s\n", in_timeout, bvsum, read_buckets); }'

awk -F"[: ]" '{ printf("%s", $0); getline; if ($2 != "") {reads1=$2} else {reads1=$3} ; getline; if ($2 != "") {reads2=$2} else {reads2=$3}; reads=reads2-reads1; getline; time=$0-2; throughput=reads/time; printf ("Throughput = %s per second\n", throughput)}' tmp-graph-tp.log

echo "----------"

# Plot the graphs from the graph,out
#./graph.pg > graph.png
#./graph_one.pg > graph.png

