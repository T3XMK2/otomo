#!/bin/bash
# Build a PSP plugin folder with pspdev (in WSL): ./build.sh felyne_net
export PSPDEV=/root/pspdev
export PATH=/usr/bin:/bin:/usr/local/bin:$PSPDEV/bin
cd "$(dirname "$0")/$1" && make
