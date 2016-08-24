mkdir -p ./node_modules

wantedVersion=$1
if [ "$wantedVersion" = "latest" ]; then
  wantedVersion=$(npm show airdcpp-webui version)
else
	git ls-remote > /dev/null 2>&1
	if [ $? -eq 0 ] && [ `git rev-parse --abbrev-ref HEAD` != "master" ];then
		# There is no separate beta tag, everything is released under "latest"
		wantedVersion=$(npm show airdcpp-webui version)
  elif [ $(echo $wantedVersion | grep b) ]; then
    # Convert to ^1.0.0-beta
    wantedVersion="~${wantedVersion%??}0-beta"
  else
    # Convert to ^1.0.0
    wantedVersion="~${wantedVersion%?}0"
  fi
fi

echo "\033[1mInstalling Web UI ($wantedVersion)\033[0m"
npm install --loglevel=error --ignore-scripts --prefix . airdcpp-webui@$wantedVersion

if [ $? -eq 0 ]; then
  echo "\033[1mWeb UI was installed successfully\033[0m"
  exit 0
fi

exit 1

