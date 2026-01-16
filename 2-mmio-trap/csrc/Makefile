#CROSS_COMPILE ?= $(HOME)/riscv/toolchain/bin/riscv-none-elf-
CROSS_COMPILE ?= $(HOME)/riscv-none-elf-gcc/bin/riscv-none-elf-

ASFLAGS = -march=rv32i_zicsr -mabi=ilp32
CFLAGS = -O0 -Wall -march=rv32i_zicsr -mabi=ilp32
LDFLAGS = --oformat=elf32-littleriscv

AS := $(CROSS_COMPILE)as
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy

%.o: %.S
	$(AS) -R $(ASFLAGS) -o $@ $<
%.elf: %.S
	$(AS) -R $(ASFLAGS) -o $(@:.elf=.o) $<
	$(CROSS_COMPILE)ld -o $@ -T link.lds $(LDFLAGS) $(@:.elf=.o)
%.elf: %.c init.o
	$(CC) $(CFLAGS) -c -o $(@:.elf=.o) $<
	$(CROSS_COMPILE)ld -o $@ -T link.lds $(LDFLAGS) $(@:.elf=.o) init.o

%.asmbin: %.elf
	$(OBJCOPY) -O binary -j .text -j .data $< $@

BINS = \
	fibonacci.asmbin \
	quicksort.asmbin \
	sb.asmbin \
	irqtrap.asmbin \
	uart.asmbin \
	test_rs_mul.asmbin \

# Clear the .DEFAULT_GOAL special variable, so that the following turns
# to the first target after .DEFAULT_GOAL is not set.
.DEFAULT_GOAL :=

# Program targets (add new programs here)
PROGRAMS := nyancat qrcode_vga uart_simple_test
BINARIES := $(PROGRAMS:%=%.asmbin)

all: $(BINS) $(BINARIES)

# Nyancat has minimal init (no trap handler) for correct memory layout
nyancat.asmbin: nyancat.c nyancat-data.h init_minimal.o
	$(CC) $(CFLAGS) -c -o nyancat.o nyancat.c
	$(CROSS_COMPILE)ld -o nyancat.elf -T link.lds $(LDFLAGS) nyancat.o init_minimal.o
	$(OBJCOPY) -O binary -j .text -j .data nyancat.elf $@

# QR code string configuration (receives quoted string from parent Makefile)
# When called from parent: make demo-qrcode QRSTR=your-url
# When called directly: make qrcode_vga.asmbin QRSTR='"your-url"'
QRSTR ?= "https://github.com/AnnTaiwan/ca2025-mycpu"
# QRCODE_GEN=qrcode_gen
QRCODE_GEN=qrcode_gen_v3
qrcode_vga.asmbin: qrcode_vga.c $(QRCODE_GEN).c uperf.c qrcode_data.h uperf.h init_minimal.o
	$(CC) $(CFLAGS) -c -o uperf.o uperf.c
	$(CC) $(CFLAGS) -DQRCODE_INPUT_STRING='$(QRSTR)' -c -o $(QRCODE_GEN).o $(QRCODE_GEN).c
	$(CC) $(CFLAGS) -c -o qrcode_vga.o qrcode_vga.c
	$(CROSS_COMPILE)ld -o qrcode_vga.elf -T link.lds $(LDFLAGS) uperf.o qrcode_vga.o $(QRCODE_GEN).o init_minimal.o
	$(OBJCOPY) -O binary -j .text -j .data qrcode_vga.elf $@

uart_simple_test.asmbin: uart_simple_test.c uperf.c uperf.h init.o
	$(CC) $(CFLAGS) -c -o uperf.o uperf.c
	$(CC) $(CFLAGS) -c -o uart_simple_test.o uart_simple_test.c
	$(CROSS_COMPILE)ld -o uart_simple_test.elf -T link.lds $(LDFLAGS) uperf.o uart_simple_test.o init.o
	$(OBJCOPY) -O binary -j .text -j .data uart_simple_test.elf $@

init_minimal.o: init_minimal.S
	$(AS) -R $(ASFLAGS) -o $@ $<

init_minimal.S:
	@printf '.section .text.init\n.globl _start\n_start:\n  li sp, 0x400000\n  call main\nloop:\n  j loop\n' > $@

# Generate nyancat animation data from upstream source
# Configure compression mode via NYANCAT_COMPRESSION_DELTA (default: 1 for delta-RLE)
# - NYANCAT_COMPRESSION_DELTA=1: delta-RLE compression (91% reduction, 4.7KB)
# - NYANCAT_COMPRESSION_DELTA=0: baseline RLE (87% reduction, 6.7KB)
NYANCAT_COMPRESSION_DELTA ?= 1

nyancat-data.h: ../../scripts/gen-nyancat-data.py
ifeq ($(NYANCAT_COMPRESSION_DELTA),1)
	python3 $< --delta --output $@
else
	python3 $< --output $@
endif

# Nyancat build depends on generated data
nyancat.o: nyancat.c nyancat-data.h

update: $(BINS) $(BINARIES)
	cp -f $(BINS) $(BINARIES) ../src/main/resources
	@echo ""
	@echo "Updated $(words $(BINS)) test programs + $(BINARIES) to ../src/main/resources"
	
clean:
	$(RM) *.o *.elf *.asmbin init_minimal.S nyancat-data.h

see_qrcode_info: qrcode_vga.c qrcode_gen.c qrcode_data.h
	@echo "\nIn qrcode_vga.c"
	@echo "-----------------------------------------------------------------------------------"
	@grep --color=always -i -e "#define QRCODE_VGA" -e "#define USE_HARDCODED_DATA" -e "#define DO_PERF" qrcode_vga.c
	@echo "\nIn qrcode_gen.c"
	@echo "-----------------------------------------------------------------------------------"
	@grep --color=always -i -e '^const char str\[\] = \".*\"' -e "#define PARM_SOURCE_TYPE" -e "#define DEBUG_REED_SOLOMON" qrcode_gen.c
	@echo "\nIn qrcode_data.h"
	@echo "-----------------------------------------------------------------------------------"
	@grep --color=always -i -e "#define QR_.*" qrcode_data.h

.PHONY: all update clean