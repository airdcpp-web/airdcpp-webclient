# AirDC++ Web Client

AirDC++ Web Client is a cross-platform file sharing client for Advanced Direct Connect network. It has a web-based user interface that can be accessed with a web browser.

## Table of contents

 * [Installation](#installation)
 * [Updating](#updating)
 * [Uninstalling](#uninstalling)
 * [Reporting issues](#reporting-issues)
 * [Feature requests](#feature-requests)

## Installation

### Installing packages on Ubuntu

Ubuntu 14.04 or newer is required for installing the client.

#### Install tools

`sudo apt-get install gcc g++ git cmake npm nodejs`

#### Install libraries

##### Ubuntu 14.04 LTS

`sudo apt-get install libbz2-dev zlib1g-dev libssl-dev libstdc++6 libminiupnpc-dev libnatpmp-dev libtbb-dev libgeoip-dev libboost1.55-dev libboost-regex1.55 libboost-thread1.55 libboost-system1.55 libleveldb-dev`

##### Ubuntu 15.10 or newer

`sudo apt-get install libbz2-dev zlib1g-dev libssl-dev libstdc++6 libminiupnpc-dev libnatpmp-dev libtbb-dev libgeoip-dev libboost1.5*-dev libboost-regex1.5* libboost-thread1.5* libboost-system1.5* libleveldb-dev`

#### Install WebSocket++

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


## Uninstalling

```
make uninstall
```

You may also remove the source and settings directories as well if you are not going to need them later.

## Reporting issues

Use the bug tracker of this project for all bug reports. 

The following information should be included for all reports:

* Instructions for reproducing the issue (if possible)
* Client and UI versions (copy from Settings -> About)

### UI-related issues

If the UI behaves incorrectly, you should open the console of your browser and check if there are any errors. It's even better if you manage to reproduce the issue while the console is open, as it will give more specific error messages. Include the errors in your bug report.

Other useful information:

* Browser version and information whether the issue happens with other browser as well
* Screenshots or video about the issue

### Client crash

Include all text from the generated crash log to your bug report. The log is located at ``/home/<username>/.airdc++/exceptioninfo.txt``.

### Client freeze/deadlock

Note that you should first confirm whether the client has freezed and the issue isn't in the UI (try opening the UI in a new tab).

Install the ``gdb`` package before running the following commands.

```
$ cat ~/.airdc++/airdcppd.pid
[number]
gdb
attach [number]
thread apply all bt full
```

Save the full output to a file and attach it to your bug report.

## Feature requests

The issue tracker can be used for posting feature requests as well.
