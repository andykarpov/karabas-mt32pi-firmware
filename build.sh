#!/bin/bash

export MT32PI_GCC64=/opt/arm-gnu-toolchain-14.3.rel1-x86_64-aarch64-none-elf
export PATH="$MT32PI_GCC64/bin:$PATH"

aarch64-none-elf-gcc --version

rm -rf build-munt build-fluidsynth build-inih external/circle-stdlib/build
find . -name ".done" -delete

git submodule foreach --recursive 'git reset --hard HEAD && git clean -fdx'

# !!! не запускаем вручную!
# make submodules

make -j4 BOARD=pi3-64
cp kernel8.img sdcard/kernel8.img
make clean BOARD=pi3-64

