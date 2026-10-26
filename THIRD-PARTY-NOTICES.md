# Third-party notices

What a release of fm1-midi-bridge carries besides this project's own code, and under which
terms. `LICENSE` is the map of the source tree; this file is about the binaries in a release
(`fm1bridge.fwsc`, `fm1bridge-minimal.fwsc`) and the bench tools. The license texts are in
`LICENSES/`; `tools/release.sh` puts them, with this file, next to the packages.

## In the firmware image (`app.bin` inside each package)

The firmware is linked against libraries of the JieLi AC79 AIoT SDK
(https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK, tag `AC79NN_SDK_V1.2.1_2023-12-13`), which JieLi
publishes, libraries included, under the Apache License 2.0 (`LICENSES/Apache-2.0.txt`). The
libraries ship as binaries only (`cpu/wl82/liba/*.a`, `include_lib/newlib/pi32v2-lib/*.a`):
`fs`, `event`, `system`, `cfg_tool`, `cpu`, `common_lib`, `wl_rf_common`, `btctrler`, `btstack`,
`crypto_toolbox_Osize`, `lib_ccm_aes`, `lib_sig_mesh`, `update`, `libc`, `libm`,
`libcompiler_rt`. The SDK carries no NOTICE file and no notices for the third-party code
inside those binaries. What can be told from the SDK's headers and the libraries' symbols:

| Component | Where | License | Notice |
| --- | --- | --- | --- |
| micro-ecc by Kenneth MacKay | `crypto_toolbox_Osize.a`; header `include_lib/utils/crypto_toolbox/micro-ecc/uECC.h` | BSD 2-Clause | `LICENSES/BSD-2-Clause-micro-ecc.txt` |
| newlib (the C and maths libraries) | `libc.a`, `libm.a` | a collection of BSD-style notices (newlib's `COPYING.NEWLIB`) | version unknown; the notices are in the newlib source, https://sourceware.org/newlib/ |
| LLVM compiler-rt | `libcompiler_rt.a` | Apache-2.0 with LLVM exception, or MIT | `LICENSES/Apache-2.0.txt`; version unknown |
| FreeRTOS, with its POSIX layer | `system.a` (`FreeRTOS_POSIX_*` objects) | MIT | version unknown; the SDK gives no notice |
| JieLi's Bluetooth controller and host stack | `btctrler.a`, `btstack.a` | Apache-2.0 per the SDK; the headers carry only JieLi's copyright | — |

The versions are unknown because the SDK does not say; the notices above are reproduced as
far as they can be identified. If you know of other third-party code in these libraries,
open an issue.

Also in the firmware: bitmaps of DejaVu Sans Mono glyphs (`app/ui/font.h`, `font_big.h`),
Bitstream Vera Fonts license, `LICENSES/Bitstream-Vera.txt`.

## In the package around the firmware (`flash.bin` head and `ota.bin`)

| File | What | Copyright, license |
| --- | --- | --- |
| `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` | the SDK's first-stage boot loader, chip configuration block and hardware EQ table, unmodified, from `cpu/wl82/tools/` of the SDK | JieLi Technology, Apache-2.0 (`LICENSES/Apache-2.0.txt`) |
| `ota.bin` | Felucca's update loader, unmodified, built from `firmware/loader/` of Felucca tag `v1.6` (commit `1727537624390cc3f9c7dff2456b43b3a8edb005`, https://github.com/hugelton/Felucca/tree/v1.6); a separate program the SPL runs during an install, never linked with the firmware; SHA-256 of the binary `cc98eed224299fa42ca22a546073e0f8b98d8362e26b5f325a9aaeab287bdf5d` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0-only (`LICENSES/GPL-3.0-only.txt`) |

The loader's complete corresponding source, the whole Felucca tree at that tag including its
build scripts, is attached to every release as `felucca-v1.6-source.tar.gz`, next to the
packages (GPL-3 section 6; `tools/release.sh` makes it from the tag).

## Host-side tools

| Tool | Third-party part | License |
| --- | --- | --- |
| `tools/fm1pkg_make.py` | a fork of Felucca's packager, modified 2026-10-11 | GPL-3.0-only, `LICENSES/GPL-3.0-only.txt` |
| `tools/fm1_install_paced.py` | imports and subclasses Felucca's `fm1_install.py` (not in this tree); the running program is a GPL-3.0-only combination | GPL-3.0-only for the combination; the file itself MIT |
| `tools/install.sh`, `tools/fm1_safe_install.sh` | run Felucca's `fm1_install.py` as a separate process | external GPL-3.0-only program |
| `tools/fm1_flash.sh` | runs kagaimiq's jl-uboot-tool as a separate process | external, MIT |
| `tools/trs_tester/` | `usb_descriptors.c` is adapted from TinyUSB's `midi_test` example; the tester links TinyUSB 0.21.0 and the author's STM32 board code | MIT, `LICENSES/MIT-TinyUSB.txt` |
| `tools/mkfont.py` | reads DejaVu Sans Mono from the system | Bitstream Vera, `LICENSES/Bitstream-Vera.txt` |

## Trademarks

"M-VAVE" and "FM-1" belong to their owners; this project is independent firmware for FM-1
hardware and is not affiliated with, endorsed by or supported by them. The USB descriptor
keeps the stock manufacturer and product strings so that hosts and installers find the
device by name. "Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
