#!/bin/sh

cd build

./riscv-kencc/host/bin/ia ../_entry.s
./riscv-kencc/host/bin/ic ../task.c
./riscv-kencc/host/bin/ic ../req.c
./riscv-kencc/host/bin/ic ../enum.c
./riscv-kencc/host/bin/ic ../hub.c
./riscv-kencc/host/bin/ic ../hid.c
./riscv-kencc/host/bin/ic ../prnt.c
./riscv-kencc/host/bin/ic ../lib.c
./riscv-kencc/host/bin/il -H1 -l -T0x10000000 -c -t -a *.i >test.txt
./riscv-kencc/host/bin/il -H1 -l -T0x10000000 -c -t    *.i
rm *.i

#cp mem.hex old.hex
hexdump -e '1/4 "%08x\n"' -v i.out >mem.tmp
cat mem.tmp ../zero.hex | head -n 4096 >../mem.hex
rm mem.tmp

#ecpbram -i ../sys.cfg -o sys.cfg -f old.hex -t mem.hex
#ecppack sys.cfg ../sys.bit

