This guide will instruct how to use [Buildroot](https://buildroot.org) to cross-compile binaries for different system architectures. Note that [binaries for the most common architectures are provided](https://airdcpp-web.github.io/docs/installation/linux-binaries.html) and you may post an issue on Github if you want new architectures to be added.

## Download Buildroot

You should first create a new directory (e.g. `buildroot`) where you will put the buildroot package and all the wanted buildroot environment directories.

Download buildroot in the newly created directory:

```
wget https://buildroot.org/downloads/buildroot-2021.02.3.tar.bz2
```

Note: check https://buildroot.org/download.html for the latest patch release to get the latest fixes and security updates.

## Setup environments

Repeat the following steps for all wanted architectures.

### Extract files

Extract the package and rename the output directory based on the current architecture: 

`tar jxvf buildroot-2021.02.3.tar.bz2`

`mv -f buildroot-2021.02.3 armhf` (replace `armhf` with the wanted arch name)

The architecture name can be freely chosen. The following architecture names are used for the shipped binaries: `armhf`, `x86_64`, `i786`

### Install defaults

Install the default configuration and patches by running the following command:  

`/AIRDCPP_SOURCE_PATH/buildroot/setup-buildroot.sh /BUILDROOT_ENV_PATH/` 

Example command: `/home/airdcpp/airdcpp-webclient/buildroot/setup-buildroot.sh /home/airdcpp/buildroot/armhf/`

### Configure and build

Run `make nconfig` in the environment directory and edit the configuration based on you needs (mainly the `Target options` sections).

When you are satisfied with the configuration, run `make -j4` to compile the environment (the example command will compile the environment with 4 threads).

## Build AirDC++

Use the following command to compile the AirDC++ Web Client binaries for your configured target environments:

`/AIRDCPP_SOURCE_PATH/buildroot/build-portable.sh /BUILDROOT_ROOT_PATH/ /OUTPUT_DIRECTORY/ [ARCH_NAME]`

- `BUILDROOT_ROOT_PATH` is the manually created directory containing all your configured buildroot environments (it shouldn't contain the actual architecture directory)
- `ARCH_NAME` is the architecture that you want to compile. If no architecture is being provided, all architectures found from the directory will be compiled.
- `OUTPUT_DIRECTORY` is the output directory that will be used for cached build files and the final packages. It's recommended to create a new directory for that.

Run the `build-portable.sh` script without arguments to see the possible additional env variables to use.

Example command: `SKIP_EXISTING=1 BUILD_THREADS=4 BRANCH=master /home/airdcpp/airdcpp-webclient/buildroot/build-portable.sh /home/airdcpp/buildroot/ /home/airdcpp/build/` (this will compile the latest stable version for all available architectures with 4 threads without replacing existing packages)
