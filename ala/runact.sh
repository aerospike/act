#!/bin/bash
upto=$1
downto=$2
if [ -z $upto ]
then
    upto=120
fi

if [ -z $downto ]
then
    downto=10
fi

echo "testing ${upto}X - ${downto}X configs"

while `pidof actprep>/dev/null`; do echo "waiting for actprep: $(ps aux|grep -v grep|grep actprep)";sleep 60;done;sleep 60
for n in {12..1}; do
  let x=n*10;
  /opt/act/act /opt/actconfig_${x}x.txt > actconfig_${x}x.txt.out;
  sleep 120;
done
