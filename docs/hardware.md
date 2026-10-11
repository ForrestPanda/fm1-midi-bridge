# FM-1 hardware facts

Collected from the Felucca HAL headers (hugelton/Felucca, `firmware/hal/*.h`), the
aroum/fm1-custom-fw teardown notes and product reviews. Facts, not code. Items marked
**(verify)** have not been confirmed on our own unit.

## SoC

- JieLi **AC791N** (WL82 series), pi32v2 core, Wi-Fi 802.11 b/g/n + dual-mode
  Bluetooth (BR/EDR + BLE 5.0). Felucca and forks never enable the radio; the
  stock firmware does BLE MIDI (hold HOME to pair), so the antenna is populated.
- RAM 512 KB, SPI NOR flash 1 MB (vendor stack: ~250 KB flash, 60-80 KB RAM).
- No JTAG/SWD/UART debug pads, no boot button. Recovery only through the mask-ROM
  USB download mode (UBOOT) forced with a dongle on D+/D-, or FM-1-transporter.

## Jacks and ports

- USB-C: full-speed USB device (MUSB-style controller, USB0). Audio + MIDI + updates.
- 3.5 mm TRS **MIDI IN** only. Spec sheets and reviews list no MIDI OUT. **(verify
  on the PCB whether the UART1 TX pin reaches anything.)**
- 3.5 mm TRS stereo audio out. Built-in speaker. Battery, charged over USB.

## Pins (from Felucca HAL)

| Function | Pins |
| --- | --- |
| TRS MIDI IN | **PH8** -> input channel 1 -> UART1 RX, 31250 baud (UART clock PLL48M, BAUD = 48e6/31250/4 - 1 = 383). RX DMA ring, polled. |
| Key/button/encoder matrix | 11 columns via 2x 74HC595 (PA4 SER, PA3 SRCLK, PA1 RCLK); rows PA0, PA5..PA8, PB7, pull-ups, active low |
| LEDs | PH6, PH9, PA9, PA10 (per column, rows PA7/PA8/PA5/PA6) |
| LCD | SPI1: PC9 CLK, PC10 DO, PC7 CS, PC8 D/C; backlight PA2 active low |
| MASTER pot | PB6, SARADC ch4 |
| Battery divider | PB1, SARADC ch3 |
| Audio out | ALNK0 I2S to an external codec |

Felucca source files to read for each: `fm1_uart.h`, `fm1_input.h`, `fm1_lcd_hw.h`,
`fm1_adc.h`, `fm1_audio.h`. In the SDK these map to the board header for the
AC791N (`board_*.h`): UART1 RX on PH8, SPI1 pins, SARADC channels.

## Flash map (stock / Felucca layout)

| Range | What |
| --- | --- |
| 0x00000 - 0x04000 | flash head: JLFS entries, SPL `uboot.boot`, `isd_config.ini` (holds the chip key). Never written by updates. |
| 0x04000 - 0x93000 | app area (`app_area_head`, `app.bin`, `cfg_tool.bin`, `eq_cfg_hw.bin`), SFC-encrypted with the chip key |
| 0x93000 - ... | VM / user data region |
| 0xE0000 - 0xE5000 | update loader staging area (Felucca's choice) |
| 0xE9000 | BTIF (1 sector) |
| 0xEA000 - 0xFC000 | USR |
| 0xFF000 | key_mac |

The SDK's `isd_config.ini` + packer decide this for an SDK build; see
`update-protocol.md` for why the app area bounds matter.

## Still unknown

- Whether the stock firmware already forwards TRS MIDI IN to BLE (it accepts all
  three inputs simultaneously, but nothing says it bridges them).
- Chip key of a given unit: Felucca's package maker encodes one key (0x980F) in
  `isd_config.ini` and the loader refuses a package for another key. **(verify:
  does every FM-1 share the key?)**
- Exact BLE device name / appearance the stock firmware advertises (useful to
  behave the same for apps that remember the pairing).
