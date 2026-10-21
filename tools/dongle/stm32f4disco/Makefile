# SPDX-License-Identifier: MIT
# The USB key for an STM32F4-Discovery: make, then make flash (openocd, the board's ST-Link).
CC = arm-none-eabi-gcc
CFLAGS = -mcpu=cortex-m4 -mthumb -O2 -Wall -Wextra -ffreestanding -nostdlib -fno-builtin
all: fm1_key.elf
fm1_key.elf: fm1_key.c fm1_key.ld
	$(CC) $(CFLAGS) -T fm1_key.ld -Wl,--gc-sections -o $@ fm1_key.c
	arm-none-eabi-size $@
flash: fm1_key.elf
	openocd -f board/stm32f4discovery.cfg -c "program fm1_key.elf verify reset exit"
clean:
	rm -f fm1_key.elf
