# The FM-1's TRS input under test: an STM32F4-Discovery that plays a byte script from the PC on its
# MIDI OUT (PA2, 31250 baud) with microsecond timing. tools/trs_test.py writes the scripts and
# checks what the FM-1 forwards. Board code, TinyUSB and CMSIS come from the TRS MIDI bridge project
# next door (BRIDGE); make, then make flash (openocd, the board's ST-Link).
BRIDGE   ?= ../../../../../midi-trs_to_midi-usb
BUILD    := build
CROSS    ?= arm-none-eabi-
CC       := $(CROSS)gcc
OBJCOPY  := $(CROSS)objcopy
SIZE     := $(CROSS)size

TINYUSB  := $(BRIDGE)/lib/tinyusb
CMSIS    := $(BRIDGE)/lib/CMSIS_5/CMSIS/Core/Include
DEVICE   := $(BRIDGE)/lib/cmsis-device-f4

SRC_C := main.c tester_uart.c usb_descriptors.c \
  $(BRIDGE)/src/board.c \
  $(DEVICE)/Source/Templates/system_stm32f4xx.c \
  $(TINYUSB)/src/tusb.c \
  $(TINYUSB)/src/common/tusb_fifo.c \
  $(TINYUSB)/src/device/usbd.c \
  $(TINYUSB)/src/class/midi/midi_device.c \
  $(TINYUSB)/src/portable/synopsys/dwc2/dcd_dwc2.c \
  $(TINYUSB)/src/portable/synopsys/dwc2/dwc2_common.c
SRC_S := $(DEVICE)/Source/Templates/gcc/startup_stm32f407xx.s
LDSCRIPT := $(BRIDGE)/ld/stm32f407vg.ld

MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFS := -DSTM32F407xx -DHSE_VALUE=8000000U -DCFG_TUSB_MCU=OPT_MCU_STM32F4
INCS := -I. -I$(BRIDGE)/src -I$(TINYUSB)/src -I$(CMSIS) -I$(DEVICE)/Include
CFLAGS := $(MCU) $(DEFS) $(INCS) -std=gnu11 -Os -g3 -Wall -Wextra -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fno-common -MMD -MP
LDFLAGS := $(MCU) -T$(LDSCRIPT) -nostartfiles --specs=nano.specs --specs=nosys.specs \
  -Wl,--gc-sections -Wl,-Map=$(BUILD)/trs_tester.map -Wl,--print-memory-usage

# objects under build/, named after the file (the bridge's sources live outside this directory)
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRC_C:.c=.o)) $(notdir $(SRC_S:.s=.o)))
vpath %.c $(sort $(dir $(SRC_C)))
vpath %.s $(sort $(dir $(SRC_S)))

ELF := $(BUILD)/trs_tester.elf

all: $(ELF)

$(ELF): $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(CC) $(MCU) -c $< -o $@

$(BUILD):
	mkdir -p $@

flash: $(ELF)
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program $(ELF) verify reset exit"

clean:
	rm -rf $(BUILD)

.PHONY: all flash clean
-include $(OBJS:.o=.d)
