#!/bin/sh

if [ -z "$2" ]
  then
    echo "Usage: install_webui.sh <application version> <version script path>"
    exit 1
fi

mkdir -p ./node_modules

wantedVersion=$($2 $1)

if [ -f ./node_modules/airdcpp-webui/package.json ]; then
  currentVersion=$(cat ./node_modules/airdcpp-webui/package.json | grep -P '\"version\": \"([0-9]+\.[0-9]+\.[0-9]+(-.*)?)\"' | cut -d \" -f4)
  if [ $currentVersion = $wantedVersion ]; then
    echo "\033[1mWanted UI version $wantedVersion is installed already\033[0m"
    exit 0
  fi
fi

echo "\033[1mInstalling Web UI ($wantedVersion)\033[0m"
npm install --loglevel=error --ignore-scripts --prefix . airdcpp-webui@$wantedVersion

if [ $? -eq 0 ]; then
  echo "\033[1mWeb UI was installed successfully\033[0m"
  exit 0
fi

exit 1

