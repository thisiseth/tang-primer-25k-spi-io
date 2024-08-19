this folder contains source code for miniature RISC-V CPU handling USB HID routines

to change the firmware

1. (once) run setup.sh to pull riscv toolchain
2. run make.sh to compile and build mem.hex
3. resynthesize to embed mem.hex into the bitstream