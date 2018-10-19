#!/bin/bash
folder=$1
upto=$2
downto=$3

if [ -z $upto ]
then
    upto=120
fi

if [ -z $downto ]
then
    downto=10
fi

if [ -z $configfolder]
then
    folder="/opt"
fi

if [ ! -d $configfolder ]
then
    echo "ACT Config Folder: ${configfolder} does not exist. Exiting."
fi

echo "testing ${upto}X - ${downto}X configs in $configfolder"

while `pidof actprep>/dev/null`; do echo "waiting for actprep: $(ps aux|grep -v grep|grep actprep)";sleep 60;done;sleep 60
for n in {12..1}; do
  let x=n*10;
  /opt/act/target/bin/act_storage ${configfolder}/actconfig_${x}x.txt > actconfig_${x}x.txt.out;
  sleep 120;
done
