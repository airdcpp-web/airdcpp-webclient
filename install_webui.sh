mkdir -p ./node_modules

wantedVersion=$1
if [ "$wantedVersion" = "latest" ]; then
  wantedVersion=$(npm show airdcpp-webui version)
else
  if [ $(echo -n $wantedVersion | tail -c 1) = "b" ]; then
    # Convert to ^1.0.0-beta
    wantedVersion="^${wantedVersion%??}0-beta"
  else
    # Convert to ^1.0.0
    wantedVersion="^${wantedVersion%?}0"
  fi
fi

echo "Installing Web UI ($wantedVersion)"
npm install --prefix . airdcpp-webui@$wantedVersion
