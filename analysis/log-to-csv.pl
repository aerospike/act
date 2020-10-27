#!/usr/bin/perl
#
# pjensen@aerospike.com
#
# Script to convert an ACT log file to .csv format, enabling it to
#   be read by Excel (e.g. for charts, further analysis, etc.)
#
# Usage: log-to-csv.pl [-l <act-log-file> [-o]]

use Getopt::Std;
my %options = ();
getopts("l:o", \%options);


my $fh, $ofh;
if (defined $options{l}) {
    open($fh, "<", $options{l}) || die "can't open $options{l}\n";
    if (defined $options{o}) {
        $options{l} =~ s/\.log$//;
        $options{l} .= ".csv";
        open($ofh, ">", "$options{l}");
    }
} else {
    die "-o requires -l\n" if (defined $options{o});
    $fh = *STDIN;
    $ofh = *STDOUT;
}

#exit(0);

while (<$fh>) {
    last if (/^HISTOGRAM NAMES/);
}

my @devs = ();
my @cur = ();

while (<$fh>) {
    chomp;
    next if (/^device-reads$/);
    last if (/^\s*$/);
    s/\/dev\///;
    push(@devs, $_);
    print $ofh  "$_,1 ms,2 ms,4 ms,8 ms,16 ms,32 ms,64 ms,";
}
print $ofh "\n";

my @prev = (0) x (8 * ($#devs + 1));
my $n;

while (<$fh>) {
    chomp;
    if (/^after/) {
        print $ofh "\n";
        $n = -8;
        @cur = ();
    } elsif (/^.* \((\d+) total/) {
        if ($#cur >= 0) {
            for (my $i=0; $i<7; $i++) {
                print $ofh "$cur[$i],";
                $cur[$i] = 0;
            }
        }
        $n += 8;
        my $delta = $1 - $prev[$n];
        print $ofh "$delta,";
        $prev[$n] = $1;
    } elsif (/^ \(/) {
        while (s/ \((\d\d): (\d+)\)//) {
            my $delta = $2 - $prev[$n + $1 + 1];
            $cur[$1] = $delta;
            $prev[$n + $1 + 1] = $2;
        }
    } elsif (/^$/) {
        if ($#cur >= 0) {
            for (my $i=0; $i<7; $i++) {
                print $ofh "$cur[$i],";
                $cur[$i] = 0;
            }
        }
    }
}
print $ofh "\n";
close $fh if (defined $options{l});
