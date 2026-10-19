# SPDX-License-Identifier: MIT
# MicroPython, any RP2040 board (Pico, XIAO RP2040 ...): the AC791N's USB key, the way czietz's
# gist and kurogedelic's FM-1-transporter send it (both reported working on FM-1s): the 16-bit key
# 0x16EF MSB first on the USB lines, D+ the clock (SPI SCK), D- the data (SPI MOSI), 50 kHz, SPI
# mode 0 (data changes after the falling edge, the ROM latches on the rising edge), push-pull,
# then the 1 kHz "SOF" pulses on D+ the ROM calibrates its clock with. No acknowledge is checked
# (czietz: about one power-on in two enters the ROM; the FM-1 in a reset loop offers a try every
# few seconds). Wire: SCK pin -> D+ (green), MOSI pin -> D- (white), GND -> GND, through ~100 ohm
# each if you have them; no VBUS. The FM-1 runs on its battery (charge it first, switch it on).
#
#   1. power the board with this file as main.py (or paste it into the REPL), FM-1 off
#   2. switch the FM-1 on: the board keys for ~1.5 s, pulses for 2 s, then prints SWAP NOW
#      and waits 10 s with the lines released: in that pause unplug the board from the FM-1 and
#      plug the FM-1 into the computer; the ROM shows as USB 4c4a:8057 "WL80UBOOT1.00"
#      (lsusb; jldevfind.py in jl-uboot-tool); if not, leave the board on and let it loop:
#      every watchdog reset of a boot-looping firmware is another chance
#   3. then tools/fm1_flash.sh (docs/recovery.md, the ladder, rung 3)
from machine import Pin, SPI, PWM
import time

SCK, MOSI = 10, 11            # czietz's pins: GP10 -> D+, GP11 -> D-; change for your board
KEY = b"\x16\xef"
PACKETS = 3000                # ~1.5 s of key at 50 kHz with the gap below
GAP_US = 160
SOF_MS = 2000
PAUSE_MS = 10000
try:
    led = Pin("LED", Pin.OUT)
except Exception:
    led = Pin(25, Pin.OUT)

def release():
    Pin(SCK, Pin.IN); Pin(MOSI, Pin.IN)

while True:
    spi = SPI(1, baudrate=50000, polarity=0, phase=0, sck=Pin(SCK), mosi=Pin(MOSI))
    print("keying 0x16EF, D+ clock on GP%d, D- data on GP%d" % (SCK, MOSI))
    led.on()
    for _ in range(PACKETS):
        spi.write(KEY)
        time.sleep_us(GAP_US)
    spi.deinit()
    Pin(MOSI, Pin.IN)
    pwm = PWM(Pin(SCK)); pwm.freq(1000); pwm.duty_u16(32768)   # 1 kHz 50 % on D+
    print("SOF pulses for %d ms" % SOF_MS)
    time.sleep_ms(SOF_MS)
    pwm.deinit()
    release()
    led.off()
    print("SWAP NOW: move the FM-1 to the computer (lines released for %d s)" % (PAUSE_MS // 1000))
    time.sleep_ms(PAUSE_MS)
