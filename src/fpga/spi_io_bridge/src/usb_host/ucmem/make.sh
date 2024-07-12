#!/bin/sh

# cd ../../
# git clone https://gitlab.com/pnru/riscv-kencc
# cd riscv-kencc
# ./CONFIG.sh
# ./BUILD.sh
PATH=$PATH:../../riscv-kencc/host/bin/

ia entry.s
ic task.c
ic req.c
ic enum.c
ic hub.c
ic hid.c
ic prn.c
ic lib.c
il -H1 -l -T0x10000000 -c -t -a *.i >test.txt
il -H1 -l -T0x10000000 -c -t    *.i
rm *.i

#cp mem.hex old.hex
hexdump -e '1/4 "%08x\n"' -v i.out >mem.tmp
cat mem.tmp zero.hex | head -n 4096 >mem.hex
rm mem.tmp

#ecpbram -i ../sys.cfg -o sys.cfg -f old.hex -t mem.hex
#ecppack sys.cfg ../sys.bit

