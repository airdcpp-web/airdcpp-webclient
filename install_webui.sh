mkdir -p ./node_modules

wantedVersion=$1
if [ "$wantedVersion" = "latest" ]; then
  wantedVersion=$(npm show airdcpp-webui version)
else
  if [ $(echo $wantedVersion | grep b) ]; then
    # Convert to ^1.0.0-beta
    wantedVersion="~${wantedVersion%??}0-beta"
  else
    # Convert to ^1.0.0
    wantedVersion="~${wantedVersion%?}0"
  fi
fi

echo "Installing Web UI ($wantedVersion)"
npm install --prefix . airdcpp-webui@$wantedVersion

if [ $? -eq 0 ]; then
  echo "Web UI was installed successfully"
  exit 0
fi

exit 1

