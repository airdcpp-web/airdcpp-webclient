This guide will instruct how to use [Buildroot](https://buildroot.org) to cross-compile binaries for different system architectures. Note that [binaries for the most common architectures are provided](https://airdcpp-web.github.io/docs/installation/linux-binaries.html) and you may post an issue on Github if you want new architectures to be added.

## Install Buildroot

You should first create a new directory (e.g. `buildroot`) where you will put the buildroot package and all the wanted buildroot environment directories.

There's a script that you can use for setting up the buildroot environment:

```
/AIRDCPP_SOURCE_PATH/buildroot/install-buildroot.sh 2024.11.1 /BUILDROOT_ROOT_PATH/ ARCH_NAME
```

- `BUILDROOT_ROOT_PATH` is the manually created buildroot directory where all the installed buildroot environments for possible different architectures will be put (e.g. `/home/myusername/buildroot/`)
- `ARCH_NAME` is the architecture that you wish to compile. The architecture name can be freely chosen. The following architecture names are used for the shipped portable binaries: `armhf`, `x86_64`, `i786`

After entering the command, you'll soon see the buildroot config menu. Edit the target configuration based on you needs (mainly the `Target options` sections). Save the config by pressing F9 and the script will proceed with building all the necessary packages (it will take a while).

## Build AirDC++

Use the following command to compile the AirDC++ Web Client binaries for your configured target environments:

`/AIRDCPP_SOURCE_PATH/buildroot/build-portable.sh /BUILDROOT_ROOT_PATH/ /OUTPUT_DIRECTORY/ [ARCH_NAME]`

- `BUILDROOT_ROOT_PATH` is the manually created directory containing all your configured buildroot environments (it shouldn't contain the actual architecture directory)
- `ARCH_NAME` is the architecture that you want to compile. If no architecture is being provided, all architectures found from the directory will be compiled.
- `OUTPUT_DIRECTORY` is the output directory that will be used for cached build files and the final packages. It's recommended to create a new directory for that.

Run the `build-portable.sh` script without arguments to see the possible additional env variables to use.

Example command: `SKIP_EXISTING=1 BUILD_THREADS=4 BRANCH=master /home/airdcpp/airdcpp-webclient/buildroot/build-portable.sh /home/airdcpp/buildroot/ /home/airdcpp/build/` (this will compile the latest stable version for all available architectures with 4 threads without replacing existing packages)
