#!/bin/bash

bold=$(tput bold)
normal=$(tput sgr0)

# Exit on errors
set -e

# Debug mode
# set -x

if [ -z "$2" ]
  then
    echo "Usage: build-portable <buildroot path> <output root path> [ <arch> ]"
    echo ""
    echo "Additional options:"
    echo ""
    echo "BRANCH: branch/commit id for checkout (default: develop)"
    echo "BUILD_THREADS: number of compiler threads to use (default: auto)"
    echo "BUILD_LATEST: create a latest portable package without version numbers (default: false)"
    echo "SKIP_EXISTING: don't build/overwrite existing target packages (default: disabled)"
    echo ""
    exit 1
fi

# Source
BR_ROOT=$1
ARCH=`echo "$3" | xargs`
BUILD_LATEST=$4

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
  fi

  git checkout ${BRANCH}
  git pull
  git fetch --prune --tags
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
  if [[ $CMAKE_BUILD_TYPE = "Debug" ]]; then
    ARCH_PKG_BASE_EXTRA="-dbg"
  fi

  if [[ $BRANCH = "master" ]]; then
    PKG_TYPE_DIR=${PKG_DIR}/stable
    ARCH_GIT_VERSION=$(git describe --abbrev=0 --tags)
    ARCH_VERSION=$(cat ${AIR_ARCH_ROOT}/CMakeLists.txt | pcregrep -o1 'set \(VERSION \"([0-9]+\.[0-9]+\.[0-9]+)\"\)')

    # Additional check so that incorrect stable versions aren't being built...
    if [[ $ARCH_GIT_VERSION != $ARCH_VERSION ]]; then
      echo "${bold}Git tag/CMakeLists version mismatch (git $ARCH_GIT_VERSION, CMakeLists $ARCH_VERSION)${normal}"
      exit 1
    fi
  else
    PKG_TYPE_DIR=${PKG_DIR}/$BRANCH
    ARCH_VERSION=$(git describe --tags --abbrev=4 --dirty=-d)
  fi

  ARCH_PKG_UI_VERSION=$(sh ./scripts/parse_webui_version.sh ${ARCH_VERSION})
  ARCH_PKG_BASE="airdcpp_${ARCH_VERSION}_webui-${ARCH_PKG_UI_VERSION}_${ARCHSTR}_portable${ARCH_PKG_BASE_EXTRA}"
  ARCH_PKG_PATH="$PKG_TYPE_DIR/$ARCH_PKG_BASE.tar.gz"

  if [ ! -d $PKG_TYPE_DIR ]; then
    mkdir -p $PKG_TYPE_DIR;
  fi

  if [[ $SKIP_EXISTING ]] && [[ -f $ARCH_PKG_PATH ]]; then
    echo "${bold}Skipping architecture $1, target file ${ARCH_PKG_PATH} exist${normal}"
    SKIP_ARCH=1
  else
    SKIP_ARCH=0
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

  ARCH_PKG_PATH=$PKG_TYPE_DIR/$ARCH_PKG_BASE.tar.gz
  tar czvf $ARCH_PKG_PATH -C ${TMP_DIR} airdcpp-webclient

  if [[ $BUILD_LATEST == true ]]; then
  cp $ARCH_PKG_PATH $PKG_TYPE_DIR/airdcpp_latest_${BRANCH}_${ARCHSTR}_portable.tar.gz
  echo "${bold}Package was saved to $PKG_TYPE_DIR/airdcpp_latest_${BRANCH}_${ARCHSTR}_portable.tar.gz"
  fi
  
  DeleteTmpDir

  echo "${bold}Package was saved to ${ARCH_PKG_PATH}${normal}"
  BUILD_SUMMARY="${BUILD_SUMMARY}${ARCH_PKG_PATH}\n"
}

# Call with the current arch
BuildArch()
{
  echo ""
  echo "${bold}Configuring architecture $1...${normal}"
  SetArch $1
  if [[ $SKIP_ARCH -eq 1 ]]; then
    return 0
  fi


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
  echo "${bold}Architecture ${ARCH} was specified${normal}"
  BuildArch $ARCH
else
  echo "${bold}No architecture was specified, building all${normal}"
  for d in ${BR_ROOT}/*/ ; do
    BuildArch $(basename $d)
  done
fi

echo ""
echo "${bold}Created packages:${normal}"
echo ""
echo -e "${BUILD_SUMMARY}"
echo ""
