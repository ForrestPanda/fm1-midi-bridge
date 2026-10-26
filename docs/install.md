# Installing, and going back to stock

The short version of `recovery.md`, for a computer running Linux (the backup step needs it).
Once the bridge is on, later updates also install from the web editor's Firmware card
(https://forrestpanda.github.io/fm1-midi-bridge/web/editor.html), in Chrome or Edge on any system.

**Alpha, tested on one FM-1.** Installing is at your own risk. The steps below keep a way back
at every point, but one case (a firmware that never shows on USB) needs a dongle.

## What you need

- The release packages from https://github.com/ForrestPanda/fm1-midi-bridge/releases:
  `fm1bridge-minimal.fwsc`, `fm1bridge.fwsc` and `SHA256SUMS`, saved into the
  `fm1-midi-bridge` clone below; check them there with `sha256sum -c SHA256SUMS`. The
  release also carries `licenses.zip` (the licence texts and third-party notices for what
  the packages contain) and `felucca-v1.6-source.tar.gz` (the source of the update loader
  inside the packages, GPL-3); neither is needed to install.
- M-VAVE's official firmware package `FM-1.fwsc` (V15, from M-VAVE's site), to go back;
  keep it in the clone too.
  SHA-256 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`; the installer
  accepts no other stock file.
- Python 3 and three checkouts side by side:

  ```
  git clone https://github.com/ForrestPanda/fm1-midi-bridge
  git clone --branch v1.6 https://github.com/hugelton/Felucca          # its installer and update loader
  git clone https://github.com/kagaimiq/jl-uboot-tool                  # the flash backup
  pip3 install --user mido python-rtmidi
  pip3 install --user -r jl-uboot-tool/requirements.txt
  cd fm1-midi-bridge
  ```

- Recommended: a USB key dongle for the case where a firmware never shows up on USB
  (`recovery.md`, step 1). Without one, that case has no way back.
- A charged FM-1, one USB cable, and no other program holding the FM-1's MIDI port (close
  DAWs, browser tabs with Web MIDI, M-UPGRADE).

## Install

```
sh tools/fm1_safe_install.sh fm1bridge-minimal.fwsc      # backup, then the minimal build
sh tools/fm1_safe_install.sh fm1bridge.fwsc --no-dump    # then the full bridge
```

The first command reads the whole 1 MB flash twice into `../fm1-dumps/` (it asks for your
password once, for the ROM disk), asks you to power-cycle the FM-1, then installs. **Keep that
dump, and a second copy of it**; it is the only backup of your unit's own data, and the way
back if a firmware ever stops answering. Never share it.

The minimal build (`FM-1_930`) shows a status screen and nothing else: if it boots and
`sh tools/install.sh --info` answers `FM-1_930`, the install path works both ways on your
unit. The full bridge answers `FM-1_931`. Then `docs/manual.md`.

If an install stops halfway, run the same command again (with `--no-dump`): the loader
resumes. If the FM-1 shows nothing on USB, switch it off, unplug, wait 5 s, plug in, switch on,
and run it again.

## Going back to stock

```
sh tools/install.sh FM-1.fwsc
```

Verified on the first unit. M-VAVE's own updater (M-UPGRADE) has not been tried on top of
the bridge.

The same command installs another community firmware over the bridge (a `.fwsc` with Felucca's
update loader), and `fm1_safe_install.sh` installs the bridge over one: both ways verified
with BackBeat 0.9.

## If something goes wrong

`recovery.md`, "The ladder": the soft key into the chip's ROM download mode
(`tools/fm1_recover.py`), and, for a unit that never shows on USB, the dongle and
`tools/fm1_flash.sh restore-app` with your dump. Please report what happened in the
repository's issues, with the Device log from the web editor if the unit still answers.

## Building it yourself

`README.md`, "Build": the JieLi AC79 SDK, its toolchain and a built Felucca tree (for its
update loader, GPL-3; its source is the Felucca tag above).
