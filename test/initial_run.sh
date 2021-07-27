# Cleanup from previous tests
if [ -f ~/.airdc++/web-server.json ]; then
        echo "Deleting existing web server config"
        rm ~/.airdc++/web-server.json

fi

if [ -f ~/.airdc++/web-users.json ]; then
        echo "Deleting existing web user config"
        rm ~/.airdc++/web-users.json
fi


HTTP_PORT=5700
USERNAME=user
PASSWORD=pass

# Create config
( sleep .1 ; echo $HTTP_PORT ; sleep .1 ; echo 5601; sleep .1 ; echo ${USERNAME} ; sleep .1 ; echo ${PASSWORD} ; sleep .1 ; echo ${PASSWORD}) | airdcppd --configure

# Run the app
airdcppd -d
sleep 3

# Check that it's running
ps cax | grep airdcppd > /dev/null
if ! [ $? -eq 0 ]; then
        echo "Process did not start"
        exit 1
else
        echo "Process is running"
fi

# Shut it down
curl -s -o /dev/null -I -v -w "%{http_code}" -H "Content-Type: application/json" -X "POST" -u "${USERNAME}:${PASSWORD}" "http://localhost:${HTTP_PORT}/api/v1/system/shutdown"
echo $http_code

if ! [ $? -eq 0 ]; then
        echo "Failed to execute the shutdown command"
        exit 1
else
        echo "Shutdown initiated"
fi

sleep 40

# Ensure that it's not running
ps cax | grep airdcppd > /dev/null
if [ $? -eq 0 ]; then
        echo "Process did not exit"
        exit 1
else
        echo "Process has exited"
fi
