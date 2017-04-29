#!/bin/bash

if [ -z "$1" ]
  then
    echo "Usage: build-portable <buildroot path> [ <arch> ]"
    exit 1
fi

BR_ROOT=`echo "$1" | xargs`
ARCH=`echo "$2" | xargs`
AIR_ROOT="$(dirname " $( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd ) ")"

# Put under the buildroot directory
OUTPUT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/release"
TMP_DIR=${OUTPUT_DIR}/tmp
TMP_PKG_DIR=${TMP_DIR}/airdcpp-webclient

VERSION=`git describe --tags --abbrev=4 --dirty=-d`

echo "Creating portable archives for version ${VERSION}..."
echo ""
echo "Application root: ${AIR_ROOT}"
echo "Buildroot root: ${BR_ROOT}"
echo "Output directory: ${OUTPUT_DIR}"
echo ""


# Call with the current arch
SetArch()
{
  # echo "SET ARCH $1 ii"
  case $1 in
    i786)
      ARCHSTR=32-bit
      ;;
    x86_64)
      ARCHSTR=64-bit
      ;;
    *)
      ARCHSTR=$1
      ;;
  esac

  PKG_BASE=airdcpp-$VERSION-$ARCHSTR-portable
  BR_ARCH_PATH=${BR_ROOT}/$1

  if [ ! -d $BR_ARCH_PATH ]; then
    echo "Buildroot architecture ${BR_ARCH_PATH} doesn't exist"
    exit 1
  fi
}

DeleteTmpDir()
{
  rm -rf $TMP_PKG_DIR
  rm -d $TMP_DIR
}

CreatePackage()
{
  if [ ! -d $OUTPUT_DIR ]; then
    mkdir -p $OUTPUT_DIR;
  fi

  if [ -d $TMP_DIR ]; then
    DeleteTmpDir
  fi

  mkdir -p $TMP_DIR;
  mkdir -p $TMP_PKG_DIR;
  mkdir -p $TMP_PKG_DIR/web-resources;

  echo "Packaging..."

  cp -r ${AIR_ROOT}/node_modules/airdcpp-webui/dist/* ${TMP_PKG_DIR}/web-resources
  cp ${AIR_ROOT}/buildroot/resources/dcppboot.xml ${TMP_PKG_DIR}
  cp ${AIR_ROOT}/airdcppd/airdcppd ${TMP_PKG_DIR}

  tar czvf $OUTPUT_DIR/$PKG_BASE.tar.gz -C ${TMP_DIR} airdcpp-webclient

  DeleteTmpDir
}

# Call with the current arch
BuildArch()
{
  SetArch $1

  if [[ ! $BUILD_THREADS ]]; then
    BUILD_THREADS=`getconf _NPROCESSORS_ONLN`
  fi

  echo "Building package ${PKG_BASE} with ${BUILD_THREADS} threads"
  echo ""

  if [ -f ${AIR_ROOT}/CMakeCache.txt ]; then
    rm ${AIR_ROOT}/CMakeCache.txt
  fi

  cmake -DCMAKE_TOOLCHAIN_FILE="${BR_ARCH_PATH}/output/host/usr/share/buildroot/toolchainfile.cmake" -DBUILD_SHARED_LIBS=OFF ${AIR_ROOT}

  make -j${BUILD_THREADS}

  CreatePackage
}



if [[ $ARCH ]]; then
  echo "Architecture ${ARCH} was specified"
  BuildArch $ARCH
else
  echo "No architecture was specified, building all ${ARCH}"
  for d in ${BR_ROOT}/*/ ; do
    BuildArch $(basename $d)
  done
fi
