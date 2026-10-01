
#!/bin/bash

if [[ "`id -u`" != "0" ]]; then
	echo "REQUIRED ROOT; attempting to resolve into sudo:"
	exec sudo $0 "$@"
fi

zypper update -y
zypper install -y tcl cmake libopenssl-devel gdb

