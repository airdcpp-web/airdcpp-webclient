#!/bin/sh

#check if we have a repository
git ls-remote > /dev/null 2>&1
if [ $? -ne 0 ];then
   echo 'Not using a Git version'
   exit 0
fi

file=$1
tmpFile="$file.tmp"

echo "#define GIT_TAG \"`git describe --abbrev=4 --dirty=-d`\"" >> $tmpFile
echo "#define GIT_COMMIT_COUNT `git rev-list HEAD --count`" >> $tmpFile
echo "#define VERSION_DATE `git show --format=\"%at\" | head -n1`" >> $tmpFile
echo "#define APPNAME_INC \"AirDC++w\"" >> $tmpFile

if diff -q "$file" "$tmpFile" > /dev/null; then
    : # files are the same
    rm "$tmpFile"
    echo 'No commit changes detected, using the old version file'
else
    : # files are different
    mv "$tmpFile" "$file"
    echo 'Version file generated'
fi
