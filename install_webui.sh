mkdir -p ./node_modules

wantedVersion=$1
if [ "$wantedVersion" = "latest" ]; then
  wantedVersion=`npm show airdcpp-webui version`
else
  # Convert to ^0.1.x
  wantedVersion="^${wantedVersion%?}x"
fi

echo "Installing Web UI ($wantedVersion)"
npm install --prefix . airdcpp-webui@$wantedVersion
