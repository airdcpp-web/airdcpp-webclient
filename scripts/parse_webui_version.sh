#!/bin/sh

if [ -z "$1" ]
  then
    echo "Usage: parse_ui_version.sh <application version>"
    exit 1
fi

wantedVersion=$1
if [ "$wantedVersion" = "latest" ]; then
  wantedVersion=$(npm show airdcpp-webui version)
else
  git ls-remote > /dev/null 2>&1

  # Always install the latest Web UI version when using the develop branch
  if [ $? -eq 0 ] && [ `git rev-parse --abbrev-ref HEAD` = "develop" ];then
    # There is no separate beta tag, everything is released under "latest"
    wantedVersion=$(npm show airdcpp-webui version)
  else
    if [ $(echo $wantedVersion | grep 'a\|b') ]; then
      # Convert to ^1.0.0-beta
      wantedVersion="~${wantedVersion%??}0-beta"
    else
      # Convert to ^1.0.0
      wantedVersion="~${wantedVersion%?}0"
    fi

    wantedVersion=$(npm show airdcpp-webui@${wantedVersion} version | tail -n1 | cut -d"'" -f2)
  fi
fi

echo $wantedVersion
