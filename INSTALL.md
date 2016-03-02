## Table of contents

 * [Installation](#installation)
 * [Updating](#updating)
 * [Uninstalling](#uninstalling)

## Installation

### Installing packages on Ubuntu

Ubuntu 14.04 or newer is required for installing the client.

#### Install tools

`sudo apt-get install gcc g++ git cmake npm python`

#### Install libraries

##### Ubuntu 14.04 LTS

`sudo apt-get install pkg-config libbz2-dev zlib1g-dev libssl-dev libstdc++6 libminiupnpc-dev libnatpmp-dev libtbb-dev libgeoip-dev libboost1.55-dev libboost-regex1.55 libboost-thread1.55 libboost-system1.55 libleveldb-dev`

##### Ubuntu 15.10 or newer

`sudo apt-get install pkg-config libbz2-dev zlib1g-dev libssl-dev libstdc++6 libminiupnpc-dev libnatpmp-dev libtbb-dev libgeoip-dev libboost1.5*-dev libboost-regex1.5* libboost-thread1.5* libboost-system1.5* libleveldb-dev`

#### Install WebSocket++

**If the client is accessed via HTTPS, it's heavily recommended that you use WebSocket++ 0.7.0 or newer due to stability issues in older versions.** Ubuntu doesn't ship with version 0.7.0 yet but you may follow the manual installation steps to get it.

If you are running Ubuntu 15.10 or newer, you may use the following command to install the package:

`sudo apt-get install libwebsocketpp-dev`

If you are running an older version of Ubuntu, run the following commands to install the package manually:

```
git clone git://github.com/zaphoyd/websocketpp.git
mkdir websocketpp/build && cd websocketpp/build
cmake ..
make
sudo make install
cd ..
cd ..
```

### Download the client

```
git clone https://github.com/airdcpp-web/airdcpp-webclient.git
cd airdcpp-webclient
```

### Compile and install

```
cmake .
make -j2
sudo make install
```
`-j2` after the `make` command means that the client is compiled by using 2 threads. It's a good idea to replace the value with the number of available CPU cores. 

Note that each compiler thread requires about 1 GB of free RAM. If the compiler crashes with "internal compiler error", you have run out of memory.


### Configure and run

When starting the client for the first time, you need to run the initial configuration script. This will set up the server ports and administrative user account for accessing web user interface.

```
airdcppd --configure
```

You may now start the client normally

```
airdcppd
```

Access the user interface with your web browser and log in with the user account that was created. If you accepted the default ports and the client is running on the same computer, the following address can be used:

[http://localhost:5600](http://localhost:5600)


## Updating

Fetch the latest files

```
git pull
```

Remove the old installation. Note that you won't be able to access the Web UI after this command. If you want to keep using the client while the new version is being compiled, You may also choose to perform this step just before running the 'make install' command in the installation section. 

```
sudo make uninstall
```

Follow the instructions in the [Compile and install](#compile-and-install) section to install the new version.

**IMPORTANT**: if you had the Web UI open in browser during the upgrade, you should force the latest UI files to be fetched by the browser by reloading the page. Otherwise the UI won't function properly.

Note that if you check the version numbers from the About page (Settings -> About), the last numbers in UI and client versions may differ because minor updates can be released separately for both projects. However, the major version numbers (0.**xx**.x) should always match. The latest available Web UI version can be checked from [the NPM package page](https://www.npmjs.com/package/airdcpp-webui).

## Uninstalling

```
make uninstall
```

You may also remove the source and settings directories as well if you are not going to need them later.
