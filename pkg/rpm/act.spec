Name: act
Version: @VERSION@
Release: 1%{?dist}
Summary: The Aerospike Certification Tool
License: Apache 2.0 license
Group: Application
BuildArch: @ARCH@
%description
ACT provides a pair of programs for testing and certifying flash/SSD devices' performance for Aerospike Database data and index storage.
%define _topdir dist
%define __spec_install_post /usr/lib/rpm/brp-compress

%package tools
Summary: The Aerospike Certification Tool
Group: Applications
%description tools
Tools for use with the Aerospike database
%files
%defattr(-,aerospike,aerospike)
/opt/aerospike/bin/act_index
/opt/aerospike/bin/act_prep
/opt/aerospike/bin/act_storage
/opt/aerospike/bin/act_latency.py
%defattr(-,root,root)
/usr/bin/act_index
/usr/bin/act_prep
/usr/bin/act_storage
/usr/bin/act_latency.py

%config(noreplace) 
/etc/aerospike/act_storage.conf
/etc/aerospike/act_index.conf

%prep
ln -sf /opt/aerospike/bin/act_index %{buildroot}/usr/bin/act_index
ln -sf /opt/aerospike/bin/act_prep %{buildroot}/usr/bin/act_prep
ln -sf /opt/aerospike/bin/act_storage %{buildroot}/usr/bin/act_storage
ln -sf /opt/aerospike/bin/act_latency.py %{buildroot}/usr/bin/act_latency.py

%pre tools
echo Installing /opt/aerospike/act
if ! id -g aerospike >/dev/null 2>&1; then
        echo "Adding group aerospike"
        /usr/sbin/groupadd -r aerospike
fi
if ! id -u aerospike >/dev/null 2>&1; then
        echo "Adding user aerospike"
        /usr/sbin/useradd -r -d /opt/aerospike -c 'Aerospike server' -g aerospike aerospike
fi

%preun tools
if [ $1 -eq 0 ]
then
        echo Removing /opt/aerospike/act
fi
