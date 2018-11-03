#!/bin/sh

# Move symbols from the binary to a separate file
# Based on https://stackoverflow.com/a/866731

scriptdir=`dirname ${0}`
scriptdir=`(cd ${scriptdir}; pwd)`
scriptname=`basename ${0}`

objcopy=$2
if [ -z "$objcopy" ]; then
  objcopy="objcopy"
fi

set -e

tostripdir=`dirname "$1"`
tostripfile=`basename "$1"`

if [ -z ${tostripfile} ] ; then
  echo "USAGE ${scriptname} <tostrip> [<objcopy cmd>]"
  exit 1
fi

cd "${tostripdir}"

debugdir=.debug
debugfile="${tostripfile}.debug"
debugpath="${debugdir}/${debugfile}"
debugpathtmp="${debugpath}.tmp"


if [ ! -d "${debugdir}" ] ; then
  echo "creating dir ${tostripdir}/${debugdir}"
  mkdir -p "${debugdir}"
fi

echo "stripping ${tostripfile}, putting debug info into ${debugfile}"
"${objcopy}" --only-keep-debug "${tostripfile}" "${debugpathtmp}"

# Check the size of the output file to avoid packing invalid debug information
FILESIZE=$(stat -c%s "${debugpathtmp}")
if [ "$FILESIZE" -le 200000 ] ;then
  echo "No debug information was found from the executable, not overwriting existing symbols"
  rm "${debugpathtmp}"
  exit 0
else
  mv "${debugpathtmp}" "${debugpath}"
fi

"${objcopy}" --strip-debug --strip-unneeded "${tostripfile}"
"${objcopy}" --add-gnu-debuglink="${debugpath}" "${tostripfile}"
chmod -x "${debugdir}/${debugfile}"

