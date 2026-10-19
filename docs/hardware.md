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
  Stock V15 enumerates as 4c4a:c755, no manufacturer string, product "FM-1", serial
  4150354433333610, bcdDevice 1.00, a USB audio composite (audio control, two audio streaming
  interfaces, MIDI); ALSA names its port "FM-1 MIDI 1" (first unit, 2026-10-19). The bridge keeps
  JieLi's 3654:4155 with "M-VAVE" / "FM-1" and the same serial (the chip's), one MIDI interface.
- 3.5 mm TRS **MIDI IN** only: the jack next to the USB-C feeds an optocoupler marked
  `OCIC P2362` (TLP2362 class, designator `PC1` on aroum's photos) through its series
  resistors; both tip and ring go to the opto, nothing on the board drives a MIDI OUT. See
  "Adding a MIDI OUT" below.
- 3.5 mm TRS stereo audio out. Built-in speaker. Battery, charged over USB.

## Pins (from Felucca HAL)

| Function | Pins |
| --- | --- |
| TRS MIDI IN | **PH8** -> input channel 1 -> UART1 RX, 31250 baud (UART clock PLL48M, BAUD = 48e6/31250/4 - 1 = 383). RX DMA ring, polled (Felucca). The bridge (`app/trs_uart.c`) adds the RX and timeout interrupts: CON0 bit 3 RX IE (RXCNT bytes since the last latch), bit 5 timeout IE (OTCNT UART clocks of silence after a byte), bit 7 latches the count into HRXCNT, bits 10/12 clear the pendings (11/14); IRQ 20. **Interrupts: verify on hardware** (the polled set-up is Felucca's, the IRQ bits are the SDK driver's as fm1-emu models them). |
| Key/button/encoder matrix | 11 columns via 2x 74HC595 (PA4 SER, PA3 SRCLK, PA1 RCLK); rows PA0, PA5..PA8, PB7, pull-ups, active low. Encoders: clockwise = contact B closes first (Felucca's decoder; fm1-emu turns the same way). |
| LEDs | PH6, PH9, PA9, PA10 (per column, rows PA7/PA8/PA5/PA6) |
| LCD | SPI1: PC9 CLK, PC10 DO, PC7 CS, PC8 D/C; backlight PA2 active low, **and PA2 is also the key matrix 595s' /OE** (Felucca found it on 1.1.5, the first unit showed it again 2026-10-19): PA2 high = no button, key or knob reads; the screen-off is the panel's sleep (DISPOFF/SLPIN), never PA2. **Verified 2026-10-19:** driven at register level only (SPI1 CON 0x4021, BAUD 4, IOMAP CON1 bit 4, BUF for a byte, ADR/CNT for a block; `app/ui/lcd.c`); the SDK's "spi1" device moved nothing to the panel, and the SPI DMA cannot read a flash address (bus hang, watchdog loop) |
| MASTER pot | PB6, SARADC ch4 |
| Battery divider | PB1, SARADC ch3 |
| Audio out | ALNK0 I2S to an external codec; codec control lines PC0, PC1, PC2, PC6 (Felucca `fm1_audio.h`) |
| Debug UART | none known. The SDK demos print on UART2 TX = **PC3**; no open firmware uses PC3, PC4 or PC5 and nothing documents them, so our board file leaves the console pin unset (`tx_pin = -1`) and the firmware logs into a RAM ring read over SysEx instead |

Felucca source files to read for each: `fm1_uart.h`, `fm1_input.h`, `fm1_lcd_hw.h`,
`fm1_adc.h`, `fm1_audio.h`. In the SDK these map to the board header for the
AC791N (`board_*.h`): UART1 RX on PH8, SPI1 pins, SARADC channels.

## Adding a MIDI OUT (research of 2026-10-14, nothing tried)

The SoC side is not the problem. On the AC79 any GPIO can carry a UART TX through the
output-channel crossbar (`gpio_output_channle(pin, CH0_UT1_TX)` or `CH0_UT2_TX`), the TX
pin is independent of the RX pin, UART2 is unused, and a 3.3 V MIDI OUT needs only two
resistors (33 Ω from TX to the tip, 10 Ω from 3.3 V to the ring, sleeve to ground, Type A).
The problem is pins and holes:

- **Pin budget.** The chip is a 48-pin AC7911B-class part (marking `11B8`; the SDK's
  selection table gives the B8 with in-package 8 Mbit flash **26 GPIO**). The pins open
  firmware uses add up to exactly 26: PA0..PA10, PB1, PB6, PB7, PC0, PC1, PC2, PC6..PC10,
  PH6, PH8, PH9. On the AC7911B package diagram (SDK `doc/datasheet/`) PH7 shares its
  pad with PH8, PH4 with PH6, PC5 with PC2 and PB8 with PC0; PC3, PC4 and PH0..PH3 are
  not bonded at all. So **there is most likely no free GPIO**, and the SDK demo's console
  pin PC3 does not exist on this package. **(verify: the B8 pin table is not in the SDK.)**
- **Without soldering: the USB-C data lines.** D+ and D- are GPIOs on this chip
  (`IO_PORT_DP`/`IO_PORT_DM`, 10 mA drive) and UART1's fixed-port list includes them
  (`PORTUSB_A`: TX on D+, RX on D-); `usb_iomode(0, 1)` hands them to the GPIO block
  after `usb_stop()`. D+ as the TX sink, D- driven high as the 3.3 V source, GND as the
  sleeve, with the two resistors in a passive USB-C-to-TRS adapter. Costs USB MIDI and
  charging while the adapter is in; the bridge's "USB MIDI off" setting is the natural
  switch. Risks: VOH of the 10 mA USB pads under the ~5 mA loop (the TLP2362-class optos
  on the receiving side need 2..3 mA), both D+ pin pairs of the receptacle bridged on
  the board, nothing else on D+/D- (the mask-ROM key is only sampled at power-up).
- **With soldering: borrow an LED line.** The four LED row drivers (PH6, PH9, PA9, PA10)
  each cost 11 of the 44 LEDs; the bridge lights only HOME and GLO. Lift that line's
  series resistor, take the SoC side through 33 Ω to a new 3.5 mm jack (hole drilled next
  to MIDI IN) or to a 4-pole TRRS replacing the MIDI IN jack with a split cable. A 0603
  pad is easier than a 0.5 mm lead. PA2 (backlight, active low) is the other candidate,
  at the price of a permanently lit backlight.
- **Not options:** the MIDI IN jack (both contacts feed the opto), the headphone jack
  (codec output, AC-coupled, 48 kHz cannot form 32 µs bits), USB host to a USB-MIDI-to-DIN
  box (no VBUS supply on the board, no MIDI host class in the SDK).

## Flash map (stock / Felucca layout)

| Range | What |
| --- | --- |
| 0x00000 - 0x04000 | flash head: JLFS entries, SPL `uboot.boot`, `isd_config.ini` (holds the chip key). Never written by updates. |
| 0x04000 - 0x93000 | app area (`app_area_head`, `app.bin`, `cfg_tool.bin`, `eq_cfg_hw.bin`), SFC-encrypted with the chip key |
| 0x93000 - 0xE0000 | VM / user data region. First unit, stock V15 (dump of 2026-10-19): one sector at 0x93000 (the SDK VM, header `55 AA AA 55`), 0x94000..0xDFFFF blank: nothing of the unit's where the drummer's songs go (0xA0000..0xC3FFF), no calibration at 0xC0000..0xC2FFF |
| 0xE0000 - 0xE5000 | update loader staging area (Felucca's choice); blank on stock |
| 0xE8000 | one sector of stock's (first unit) |
| 0xE9000 | BTIF (1 sector; holds data on the first unit) |
| 0xEA000 - 0xFC000 | USR (stock's user data: ten sectors in 0xEA000..0xFB000 on the first unit) |
| 0xFF000 | key_mac (blank on the first unit: the Bluetooth address is not there) |

The SDK's `isd_config.ini` + packer decide this for an SDK build; see
`update-protocol.md` for why the app area bounds matter.

## Still unknown

- Whether the stock firmware already forwards TRS MIDI IN to BLE (it accepts all
  three inputs simultaneously, but nothing says it bridges them).
- Chip key of a given unit: Felucca's package maker encodes one key (0x980F) in
  `isd_config.ini` and the loader refuses a package for another key. The first unit's key
  is 0x980F (jl-uboot-tool, 2026-10-19), its flash ID 0x856014. **(verify: does every
  FM-1 share the key?)**
- Exact BLE device name / appearance the stock firmware advertises (useful to
  behave the same for apps that remember the pairing).
