# Cleanup from previous tests
if [ -f "~/.airdc++/WebServer.xml" ]; then
        rm "~/.airdc++/WebServer.xml"
fi

# Create config
( sleep .1 ; echo 5600 ; sleep .1 ; echo 5601; sleep .1 ; echo user ; sleep .1 ; echo pass ; sleep .1 ; echo pass) | airdcppd --configure

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
curl -s -o /dev/null -I -v -w "%{http_code}" -H "Content-Type: application/json" -X "POST" -u "user:pass" "http://localhost:5600/api/v1/system/shutdown"
echo $http_code

if ! [ $? -eq 0 ]; then
        echo "Failed to execute the shutdown command"
        exit 1
else
        echo "Shutdown initiated"
fi

sleep 20

# Ensure that it's not running
ps cax | grep airdcppd > /dev/null
if [ $? -eq 0 ]; then
        echo "Process did not exit"
        exit 1
else
        echo "Process has exited"
fi
