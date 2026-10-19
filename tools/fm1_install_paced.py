#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""fm1_install.py with the host's SysEx sent in paced slices (Linux, ALSA sequencer).

Why: a firmware whose USB-MIDI receive ring is smaller than one update reply (512 bytes of
data = 808 bytes of USB-MIDI packets) loses the reply's tail when the host sends it as one
bulk burst; its step 1 then asks three times and gives up ("stopped answering after 3
requests; nothing was written"). The bridge's builds before 2026-10-19 had a 512-byte ring.
Paced, the same firmware installs; the install path itself is unchanged (the drummer's
fm1_install.py does the work, this file only supplies the MIDI link: mido for the input,
libasound through ctypes for the output, so a SysEx goes out as a chain of sequencer events
of SLICE bytes with a GAP between them; the kernel's rawmidi bridge writes each as it comes).

  fm1_install_paced.py PACKAGE.fwsc [--slice N] [--gap MS] [fm1_install.py options]
  fm1_install_paced.py --info

Env: FM1_INSTALL  the fm1_install.py to drive (default: fm1-drummer's copy, else Felucca's).
"""
import ctypes
import os
import queue
import re
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
top = os.path.abspath(os.path.join(here, "..", ".."))
cand = [os.environ.get("FM1_INSTALL"), os.path.join(top, "fm1-drummer", "tools", "fm1_install.py"),
        os.path.join(top, "..", "Felucca", "tools", "fm1_install.py")]
INSTALL = next((c for c in cand if c and os.path.isfile(c)), None)
if not INSTALL:
    sys.exit("no fm1_install.py found (FM1_INSTALL)")
sys.path.insert(0, os.path.dirname(INSTALL))
import fm1_install as fi  # noqa: E402

SLICE, GAP = 48, 0.002        # 48 wire bytes (16 USB-MIDI packets) every 2 ms: 24 KB/s

# ---- libasound sequencer, just enough to emit SysEx slices as events ----
asound = ctypes.CDLL("libasound.so.2")
SND_SEQ_OPEN_OUTPUT, SND_SEQ_EVENT_SYSEX, SND_SEQ_EVENT_LENGTH_VARIABLE, SND_SEQ_QUEUE_DIRECT = 1, 130, 0x04, 253
CAP_READ, CAP_SUBS_READ, TYPE_MIDI_GENERIC, TYPE_APPLICATION = 0x01, 0x20, 0x02, 0x100000


class SeqEvent(ctypes.Structure):
    _pack_ = 1
    _fields_ = [("type", ctypes.c_ubyte), ("flags", ctypes.c_ubyte), ("tag", ctypes.c_ubyte), ("queue", ctypes.c_ubyte),
                ("time", ctypes.c_ubyte * 8),
                ("src_client", ctypes.c_ubyte), ("src_port", ctypes.c_ubyte),
                ("dst_client", ctypes.c_ubyte), ("dst_port", ctypes.c_ubyte),
                ("ext_len", ctypes.c_uint), ("ext_ptr", ctypes.c_void_p)]


assert ctypes.sizeof(SeqEvent) == 28
asound.snd_seq_open.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
asound.snd_seq_set_client_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
asound.snd_seq_create_simple_port.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint]
asound.snd_seq_connect_to.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
asound.snd_seq_event_output_direct.argtypes = [ctypes.c_void_p, ctypes.POINTER(SeqEvent)]
asound.snd_seq_close.argtypes = [ctypes.c_void_p]


class SeqOut:
    def __init__(self, client, port):
        self.h = ctypes.c_void_p()
        if asound.snd_seq_open(ctypes.byref(self.h), b"default", SND_SEQ_OPEN_OUTPUT, 0) < 0:
            raise OSError("snd_seq_open")
        asound.snd_seq_set_client_name(self.h, b"fm1 paced install")
        self.port = asound.snd_seq_create_simple_port(self.h, b"out", CAP_READ | CAP_SUBS_READ, TYPE_MIDI_GENERIC | TYPE_APPLICATION)
        if self.port < 0 or asound.snd_seq_connect_to(self.h, self.port, client, port) < 0:
            asound.snd_seq_close(self.h)
            raise OSError(f"cannot connect to {client}:{port}")
        self.dst = (client, port)

    def write(self, data):
        buf = ctypes.create_string_buffer(bytes(data), len(data))
        ev = SeqEvent()
        ev.type, ev.flags, ev.queue = SND_SEQ_EVENT_SYSEX, SND_SEQ_EVENT_LENGTH_VARIABLE, SND_SEQ_QUEUE_DIRECT
        ev.src_port = self.port
        ev.dst_client, ev.dst_port = self.dst
        ev.ext_len, ev.ext_ptr = len(data), ctypes.cast(buf, ctypes.c_void_p)
        if asound.snd_seq_event_output_direct(self.h, ctypes.byref(ev)) < 0:
            raise OSError("snd_seq_event_output_direct")

    def close(self):
        if self.h:
            asound.snd_seq_close(self.h)
            self.h = None


class PacedLink(fi.MidoLink):
    """MidoLink's input; the output goes through SeqOut in slices"""

    def __init__(self, backend, in_name, out_name):
        import mido
        m = re.search(r"(\d+):(\d+)$", out_name)          # mido's ALSA names end with client:port
        if not m:
            raise OSError(f"{out_name}: no client:port")
        self.backend, self.name, self.mido = backend, in_name, mido
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.seq = SeqOut(int(m[1]), int(m[2]))
        except Exception:
            self.inp.close()
            raise
        time.sleep(fi.DELAY["open"])

    def send(self, pkt):
        try:
            for i in range(0, len(pkt), self.backend.slice):
                self.seq.write(pkt[i:i + self.backend.slice])
                time.sleep(self.backend.gap)
            return True
        except Exception:
            return False

    def close(self):
        try:
            self.inp.close()
        except Exception:
            pass
        self.seq.close()


class PacedBackend(fi.MidoBackend):
    def __init__(self, slice_=SLICE, gap=GAP):
        super().__init__()
        self.slice, self.gap = slice_, gap

    def open(self, in_name, out_name):
        return PacedLink(self, in_name, out_name)


def main():
    argv, slice_, gap = [], SLICE, GAP
    it = iter(sys.argv[1:])
    for a in it:
        if a == "--slice":
            slice_ = int(next(it))
        elif a == "--gap":
            gap = float(next(it)) / 1000
        else:
            argv.append(a)
    print(f"paced: {slice_} bytes every {gap * 1000:g} ms, driving {INSTALL}", flush=True)
    return fi.main(argv, backend=PacedBackend(slice_, gap))


if __name__ == "__main__":
    sys.exit(main())
