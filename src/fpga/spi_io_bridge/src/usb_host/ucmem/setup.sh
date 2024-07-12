#!/bin/sh

mkdir build
cd build
git clone https://gitlab.com/pnru/riscv-kencc
cd riscv-kencc
./CONFIG.sh
./BUILD.sh

