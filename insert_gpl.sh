#!/bin/bash

lic=$(<license.txt)
echo "$lic"

files=()
files=`find . -name "*.c"`
echo "${files[@]}"

for file in $files; do
    echo "$file"
    echo "$(echo "$lic" | cat - $file)" > $file
done

