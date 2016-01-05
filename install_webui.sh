mkdir -p ./node_modules

wantedVersion=$1
if [ "$wantedVersion" = "latest" ]; then
  wantedVersion=`npm show airdcpp-webui version`
fi

npm list | grep airdcpp-webui@"$wantedVersion" > /dev/null 2>&1
if [ $? = 0 ]; then
  echo "Latest version of Web UI exists on the disk already"
  exit 0
fi

echo "Installing version $wantedVersion of Web UI" 
npm install --prefix . airdcpp-webui@$1
