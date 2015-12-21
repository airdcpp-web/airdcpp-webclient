# AirDC++ Web Client

AirDC++ Web Client is a cross-platform file sharing client for Advanced Direct Connect network. It has a web-based user interface that can be accessed with a web browser.

## Installation

### Installing packages on Ubuntu

Ubuntu 14.04 or newer is required for installing the client.

#### Install tools

`sudo apt-get install gcc g++ git cmake npm nodejs`

#### Install libraries

`sudo apt-get install libbz2-dev zlib1g-dev libssl-dev libstdc++6 libminiupnpc-dev libnatpmp-dev libtbb-dev libgeoip-dev libboost1.5*-dev libboost-regex1.5*-dev libboost-thread1.5*-dev libboost-system1.5*-dev libleveldb-dev`

#### Install WebSocket++

If you are running Ubuntu 15.10 or newer, you may use the following command to install the package:

`sudo apt-get libwebsocketpp-dev`

If you are running an older version of Ubuntu, run the following commands to install the package manually:

```
git clone git://github.com/zaphoyd/websocketpp.git
mkdir websocketpp/build && cd websocketpp/build
cmake ..
make
sudo make install
cd..
cd..
```

### Download the client

`git clone https://github.com/maksis/airdcpp-webclient.git`

### Compile and install

```
cd airdcpp-webclient
cmake .
make -j4
sudo make install
```
This will compile the client with four simultaneous threads.

### Configure and run

When starting the client for the first time, you need to run the initial configuration script. This will set up the server ports and administrative user account for accessing web user interface.

```
airdcppd --configure
```

After this, you may start the client normally

```
airdcppd
```

Access the user interface with your web browser and log in with the user account that was created.
