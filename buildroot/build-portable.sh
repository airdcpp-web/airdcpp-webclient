#!/bin/bash

# Exit on errors
set -e

# Debug mode
# set -x

if [ -z "$2" ]
  then
    echo "Usage: build-portable <buildroot path> <output root path> [ <arch> ] [ <branch/tag> ]"
    exit 1
fi

# Source
BR_ROOT=$1
ARCH=`echo "$3" | xargs`
BRANCH=$4
#AIR_ROOT="$(dirname " $( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd ) ")"

# Output
OUTPUT_DIR=$2
PKG_DIR=${OUTPUT_DIR}/packages
TMP_DIR=${OUTPUT_DIR}/tmp
TMP_PKG_DIR=${TMP_DIR}/airdcpp-webclient
BUILD_CACHE_DIR=${OUTPUT_DIR}/cache

if [ ! -d $OUTPUT_DIR ]; then
  mkdir -p $OUTPUT_DIR;
fi

if [ ! -d $BUILD_CACHE_DIR ]; then
  mkdir -p $BUILD_CACHE_DIR;
fi

if [ ! -d $PKG_DIR ]; then
  mkdir -p $PKG_DIR;
fi

if [ ! -d $TMP_DIR ]; then
  mkdir -p $TMP_DIR;
fi



# echo "Creating portable archives for version ${VERSION}..."
echo ""
echo "Buildroot root: ${BR_ROOT}"
echo "Output directory: ${OUTPUT_DIR}"
echo ""


FetchGit()
{
  if [[ ! $BRANCH ]]; then
    BRANCH="develop"
  fi

  echo "Using git version ${BRANCH}"

  if [ ! -d $AIR_ARCH_ROOT ]; then
    mkdir -p $AIR_ARCH_ROOT;
    cd ${AIR_ARCH_ROOT}
    git clone https://github.com/airdcpp-web/airdcpp-webclient.git ${AIR_ARCH_ROOT}
  else
    cd ${AIR_ARCH_ROOT}
    git pull
  fi

  git checkout ${BRANCH}
}


# Call with the current arch
SetArch()
{
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

  BR_ARCH_PATH=${BR_ROOT}/$1
  AIR_ARCH_ROOT=${BUILD_CACHE_DIR}/$1

  if [ ! -d $BR_ARCH_PATH ]; then
    echo "Buildroot architecture ${BR_ARCH_PATH} doesn't exist"
    exit 1
  fi


  FetchGit

  ARCH_PKG_BASE_EXTRA=""
  if [ $CMAKE_BUILD_TYPE = "Debug" ]; then
    ARCH_PKG_BASE_EXTRA="-dbg"
  fi

  ARCH_VERSION=`git describe --tags --abbrev=4 --dirty=-d`
  ARCH_PKG_BASE=airdcpp-${ARCH_VERSION}-${ARCHSTR}-portable${ARCH_PKG_BASE_EXTRA}

  if [ $BRANCH = "master" ]; then
    PKG_TYPE_DIR=${PKG_DIR}/stable
  else
    PKG_TYPE_DIR=${PKG_DIR}/$BRANCH
  fi

  if [ ! -d $PKG_TYPE_DIR ]; then
    mkdir -p $PKG_TYPE_DIR;
  fi
}

DeleteTmpDir()
{
  rm -rf $TMP_PKG_DIR
  #rm $TMP_PKG_DIR/*
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

  mkdir -p $TMP_PKG_DIR;
  mkdir -p $TMP_PKG_DIR;
  mkdir -p $TMP_PKG_DIR/web-resources;

  echo "Packaging..."

  cp -r ${AIR_ARCH_ROOT}/node_modules/airdcpp-webui/dist/* ${TMP_PKG_DIR}/web-resources
  cp ${AIR_ARCH_ROOT}/buildroot/resources/dcppboot.xml ${TMP_PKG_DIR}
  cp ${AIR_ARCH_ROOT}/airdcppd/airdcppd ${TMP_PKG_DIR}

  OUTPUT_PKG_PATH=$PKG_TYPE_DIR/$ARCH_PKG_BASE.tar.gz
  tar czvf $OUTPUT_PKG_PATH -C ${TMP_DIR} airdcpp-webclient

  DeleteTmpDir

  echo "Package was saved to ${OUTPUT_PKG_PATH}"
}

# Call with the current arch
BuildArch()
{
  SetArch $1

  FetchGit

  if [[ ! $BUILD_THREADS ]]; then
    BUILD_THREADS=`getconf _NPROCESSORS_ONLN`
  fi

  echo "Building package ${ARCH_PKG_BASE} with ${BUILD_THREADS} threads"
  echo ""

  if [ -f ${AIR_ARCH_ROOT}/CMakeCache.txt ]; then
    rm ${AIR_ARCH_ROOT}/CMakeCache.txt
  fi

  cmake -DCMAKE_TOOLCHAIN_FILE="${BR_ARCH_PATH}/output/host/usr/share/buildroot/toolchainfile.cmake" -DBUILD_SHARED_LIBS=OFF ${AIR_ARCH_ROOT}

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
