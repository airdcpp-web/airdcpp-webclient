#!/bin/sh

if [ -z "$4" ]
  then
    echo "Usage: generate_version.sh <output file> <version number> <application name> <application id>"
fi

version=$2
appName=$3
appId=$4
versionDate=`date +%s`
commitCount=0
commit=""

#check if we have a repository
git ls-remote > /dev/null 2>&1
if [ $? -ne 0 ];then
  echo '-- Not using a Git version'
elif [ `git rev-parse --abbrev-ref HEAD` != "master" ];then
	version=`git describe --tags --abbrev=4 --dirty=-d`
	commitCount=`git rev-list HEAD --count`
	versionDate=`git show --format=%at | head -n1`
	commit=`git rev-parse HEAD`

	echo "-- Git version detected ($version)"
fi

file=$1
tmpFile="$file.tmp"

echo "#define GIT_TAG \"$version\"" >> $tmpFile
echo "#define GIT_COMMIT_COUNT $commitCount" >> $tmpFile
echo "#define VERSION_DATE $versionDate" >> $tmpFile
echo "#define APPNAME_INC \"$appName\"" >> $tmpFile
echo "#define APPID_INC \"$appId\"" >> $tmpFile
echo "#define GIT_COMMIT \"$commit\"" >> $tmpFile

if diff -q "$file" "$tmpFile" > /dev/null 2>&1; then
    : # files are the same
    rm "$tmpFile"
    echo '-- No commit changes detected, using the old version file'
else
    : # files are different
    mv "$tmpFile" "$file"
    echo '-- Version file generated'
fi
