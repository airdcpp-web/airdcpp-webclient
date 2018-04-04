### Download Buildroot

You should first create a new directory (e.g. `buildroot`) where you will put the buildroot package and all the wanted buildroot environment directories.

Download buildroot in the newly created directory:

```
wget https://buildroot.org/downloads/buildroot-2018.02.tar.bz2
```

### Setup environments

Repeat the following steps for all wanted architectures.

#### Extract files

Extract the package and rename the output directory based on the current architecture: 

`tar jxvf buildroot-2018.02.tar.bz2`

`mv -f buildroot-2018.02 armhf` (replace `armhf` with the wanted arch name)

The architecture name can be freely chosen. The following architecture names are used for the shipped binaries: `armhf`, `x86_64`, `i786`

#### Install defaults

Install the default configuration and patches by running the following command:  

`/AIRDCPP_SOURCE_PATH/buildroot/setup-buildroot.sh /BUILDROOT_ENV_PATH/` 

Example command: `/home/airdcpp/airdcpp-webclient/buildroot/setup-buildroot.sh /home/airdcpp/buildroot/armhf/`

#### Configure

Run `make nconfig` and edit the environment based on you needs (mainly the `Target options` sections).

#### Build environtment

Run `make -j4` (this will compile the environment with 4 threads)

### Build application

`/AIRDCPP_SOURCE_PATH/buildroot/build-portable.sh /BUILDROOT_ROOT_PATH/ /OUTPUT_DIRECTORY/ [ARCH_NAME]`

- `BUILDROOT_ROOT_PATH` is the manually created directory containing all your configured buildroot environments (it shouldn't contain the actual architecture directory)
- `ARCH_NAME` is the architecture that you want to compile. If no architecture is being provided, all architectures found from the directory will be compiled.
- `OUTPUT_DIRECTORY` is the output directory that will be used for cached build files and the final packages. It's recommended to create a new directory for that.

Run the `build-portable.sh` script without arguments to see the possible additional env variables to use.

Example command: `SKIP_EXISTING=1 BUILD_THREADS=4 BRANCH=master /home/airdcpp/airdcpp-webclient/buildroot/build-portable.sh /home/airdcpp/buildroot/ /home/airdcpp/build/` (this will compile the latest stable version for all available architectures with 4 threads without replacing existing packages)
