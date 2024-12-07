#!/bin/bash

# Debug mode
# set -x

if [ -z "$2" ]
  then
    echo "Usage: install-buildroot <buildroot version> <install directory> <arch>"
    echo ""
    echo "Additional options:"
    echo ""
    echo "BUILD_THREADS: number of compiler threads to use (default: auto)"
    echo ""
    exit 1
fi

BR_VERSION=$1
REPOSITORY_BR_PATH=$(cd "$(dirname "$0")" && pwd)
BUILDROOT_ROOT=$2
ARCH=$3

if [ ! $BUILD_THREADS ]; then
  BUILD_THREADS=`getconf _NPROCESSORS_ONLN`
fi

SetupEnv()
{
  echo ""
  echo "${bold}Setting up environment $1...${normal}"

  cd $BUILDROOT_ROOT

  # Remove old buildroot instances
  if [ -d $1 ]; then
    echo "${bold}NOTE: an existing installation of buildroot was found, the directory will be removed${normal}"
    read -p "Press any key to continue... " -n1 -s

    #sleep 5s
    #read -r -p "Wait 5 seconds or press any key to continue immediately" -t 5 -n 1 -s

    echo "Removing previous installation"
    rm -rf $1
  fi

  # Download buildroot
  if [ ! -f buildroot-$BR_VERSION.tar.gz ]; then
    echo "Downloading buildroot archive"
    wget https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.gz
  else
    echo "Buildroot archive exists"
  fi

  # Unpack
  echo "Unpacking buildroot"
  tar zxvf buildroot-$BR_VERSION.tar.gz > /dev/null 2>&1
  mv buildroot-$BR_VERSION $1

  # Install initial config
  cd $1
  sh $REPOSITORY_BR_PATH/setup-buildroot-config.sh $BUILDROOT_ROOT/$1/

  # Let the user run the setup
  make nconfig

  # Build everything
  echo "Building environment ${ARCH} with ${BUILD_THREADS} threads"
  echo ""

  make -j${BUILD_THREADS}

  cd ..
}

SetupEnv $ARCH

