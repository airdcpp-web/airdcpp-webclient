#!/bin/bash

# Exit on errors
set -e

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
RELEASE_DIR=${OUTPUT_DIR}/release
TMP_DIR=${OUTPUT_DIR}/tmp
TMP_PKG_DIR=${TMP_DIR}/airdcpp-webclient


if [ ! -d $OUTPUT_DIR ]; then
  mkdir -p $OUTPUT_DIR;
fi

if [ ! -d $RELEASE_DIR ]; then
  mkdir -p $RELEASE_DIR;
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
  AIR_ARCH_ROOT=${OUTPUT_DIR}/$1

  if [ ! -d $BR_ARCH_PATH ]; then
    echo "Buildroot architecture ${BR_ARCH_PATH} doesn't exist"
    exit 1
  fi


  FetchGit

  ARCH_VERSION=`git describe --tags --abbrev=4 --dirty=-d`
  ARCH_PKG_BASE=airdcpp-$ARCH_VERSION-$ARCHSTR-portable
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

  tar czvf $RELEASE_DIR/$ARCH_PKG_BASE.tar.gz -C ${TMP_DIR} airdcpp-webclient

  DeleteTmpDir
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
