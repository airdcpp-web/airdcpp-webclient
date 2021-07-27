#!/bin/bash

if [ -z "$1" ]
  then
    echo "Usage: setup-buildroot <full buildroot path>"
    exit 1
fi


AIR_BR_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BR_PATH=$1

echo "Setup file location: ${AIR_BR_PATH}"
echo "Target path: ${BR_PATH}"
echo ""

# Enable static build with libminiupnpc

patch -p0 --dry-run --silent -d${BR_PATH} -p1 < $AIR_BR_PATH/patches/0002-enable-libminiupnpc-static.patch >/dev/null 2>&1

if [ $? -eq 0 ];
then
  echo "Applying static build patch for libminiupnpc"
  patch -d${BR_PATH} -p1 < $AIR_BR_PATH/patches/0002-enable-libminiupnpc-static.patch
else
  echo "libminiupnpc build patch is applied already"
fi




# Install packages

cat ${BR_PATH}package/Config.in | grep "package/airdcpp/Config.in" > /dev/null
if [ $? -ne 0 ];
then
  echo "Installing package airdcpp"
  ln -s ${AIR_BR_PATH}/package/airdcpp/ ${BR_PATH}/package/
  echo 'source "package/airdcpp/Config.in"' >> "${BR_PATH}/package/Config.in"
else
  echo "package airdcpp is installed already"
fi

cat ${BR_PATH}package/Config.in | grep "package/websocketpp/Config.in" > /dev/null
if [ $? -ne 0 ];
then
  echo "Installing package websocketpp"
  ln -s ${AIR_BR_PATH}/package/websocketpp/ ${BR_PATH}/package/
  echo 'source "package/websocketpp/Config.in"' >> "${BR_PATH}/package/Config.in"
else
  echo "package websocketpp is installed already"
fi


# Install build config
if [ ! -f ${BR_PATH}/.config ]; then
  echo "Installing build config"
  cp ${AIR_BR_PATH}/config/.config ${BR_PATH}/
  echo "BR2_GLOBAL_PATCH_DIR=\"${AIR_BR_PATH}/patches\"" >> "${BR_PATH}/.config"
else
  echo "build config is installed already"
fi
