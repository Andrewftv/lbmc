#!/bin/bash

config_mak="config.mak"
cflags="CFLAGS:="

# Remove prevision config.mak
rm -f $config_mak

# Parse configuration file
while read line
do
    name=$line
	if [[ ${name:0:1} != "#" ]]; then # Ignore comments
		if [ -n "$name" ]; then       # Ignore empty strings
    		param=${name%=*}
    		value=${name#*\=}
    		echo "$param=$value" >> $config_mak
			cflags+="-D"
			cflags+="$param=$value"
			cflags+=" "
		fi
	fi
done < $1

echo $cflags >> $config_mak

