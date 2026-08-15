#!/bin/bash

export MT32PI_GCC64=/opt/arm-gnu-toolchain-15.2.rel1-x86_64-aarch64-none-elf
export MT32PI_GCC32=/opt/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi
export PATH="$MT32PI_GCC32/bin:$MT32PI_GCC64/bin:$PATH"

arm-none-eabi-gcc --version
aarch64-none-elf-gcc --version

make submodules

make -j4 BOARD=pi3-64
cp kernel8.img sdcard/kernel8.img
make clean BOARD=pi3-64

make mrproper BOARD=pi3
make -j4 BOARD=pi3
cp kernel8-32.img sdcard/kernel8-32.img
make clean BOARD=pi3

make mrproper BOARD=pi2
make -j4 BOARD=pi2
cp kernel7.img sdcard/kernel7.img
make clean BOARD=pi2

make mrproper BOARD=pi4-64
make -j4 BOARD=pi4-64
cp kernel8-rpi4.img sdcard/kernel8-rpi4.img
make clean BOARD=pi4-64

make mrproper BOARD=pi4
make -j4 BOARD=pi4
cp kernel7l.img sdcard/kernel7l.img
make clean BOARD=pi4
