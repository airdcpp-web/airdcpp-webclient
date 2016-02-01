#!/bin/sh

if [ -z "$3" ]
  then
    echo "Usage: generate_version.sh <output file> <version number> <application name>"
fi

version=$2
appName=$3
versionDate=`date +%s`
commitCount=0

#check if we have a repository
git ls-remote > /dev/null 2>&1
if [ $? -ne 0 ];then
  echo 'Not using a Git version'
elif [ `git rev-parse --abbrev-ref HEAD` != "master" ];then
	echo 'Git version detected'
	version=`git describe --abbrev=4 --dirty=-d`
	commitCount=`git rev-list HEAD --count`
	versionDate=`git show --format=%at | head -n1`
fi

file=$1
tmpFile="$file.tmp"

echo "#define GIT_TAG \"$version\"" >> $tmpFile
echo "#define GIT_COMMIT_COUNT $commitCount" >> $tmpFile
echo "#define VERSION_DATE $versionDate" >> $tmpFile
echo "#define APPNAME_INC \"$appName\"" >> $tmpFile

if diff -q "$file" "$tmpFile" > /dev/null 2>&1; then
    : # files are the same
    rm "$tmpFile"
    echo '-- No commit changes detected, using the old version file'
else
    : # files are different
    mv "$tmpFile" "$file"
    echo '-- Version file generated'
fi
