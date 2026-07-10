#!/usr/bin/env python3
"""Dynamic MIDI-CI compliance test over native UMP (Linux ALSA rawmidi).

Companion to tools/compliance-audit.py: the audit checks recipe sources
statically; this script confronts a REAL device on the wire, the same way the
official MIDI 2.0 Workbench does, without needing its UI. It sends MIDI-CI
inquiries as native UMP SysEx7 (MT 0x3) packets and verifies the replies:

  1. Discovery                        -> Discovery Reply (0x71)
  2. Set Profile Off (listed)         -> Profile Enabled (0x24)   [PF3.303]
  3. Set Profile On  (listed)         -> Profile Enabled (0x24)   [PF3.291]
  4. Set Profile On  (unlisted)       -> NAK (0x7F)               [PF3.307]
  5. MIDI Message Report (FB 0x7F)    -> Reply 0x43 + End 0x44    [pInq2.7]
  6. MIDI Message Report (ch in use)  -> Reply 0x43 + End 0x44    [pInq2.7]
  7. MIDI Message Report (ch not in use) -> NAK (0x7F)            [pInq2.2]

Why native UMP: the kernel's legacy MIDI 1.0 -> UMP converter corrupts SysEx
payload bytes, so testing MIDI-CI through the legacy rawmidi port (amidi)
produces false NAKs. This script switches the rawmidi fd to UMP packet mode
via SNDRV_RAWMIDI_IOCTL_USER_PVERSION and writes 32-bit UMP words directly.

Requires: Linux kernel >= 6.5 (snd-usb-midi2), access to /dev/snd/umpC*D0
(audio group or root).

Usage:
    python3 tools/compliance-wire-test.py                    # autodetect device
    python3 tools/compliance-wire-test.py --device /dev/snd/umpC3D0
    python3 tools/compliance-wire-test.py --profile 7E00000100 --channel 1

Exit code 0 when every check passes.
"""
import argparse
import fcntl
import glob
import os
import struct
import sys
import time

SNDRV_RAWMIDI_IOCTL_USER_PVERSION = 0x40045702
USER_PVERSION_UMP = (2 << 16) | (0 << 8) | 2

SRC_MUID = [0x0A, 0x0B, 0x0C, 0x0D]
BROADCAST = [0x7F, 0x7F, 0x7F, 0x7F]

SUBID_NAMES = {
    0x71: "Discovery Reply",
    0x24: "Profile Enabled",
    0x25: "Profile Disabled",
    0x43: "MM Report Reply",
    0x44: "MM Report End",
    0x7F: "NAK",
}


def enable_ump_mode(fd):
    """Switch the rawmidi fd to UMP packet mode. Without this the kernel
    exposes the legacy MIDI 1.0 byte-stream view of the same endpoint."""
    fcntl.ioctl(fd, SNDRV_RAWMIDI_IOCTL_USER_PVERSION,
                struct.pack("i", USER_PVERSION_UMP))


def sysex7_ump(data):
    """Fragment MIDI-CI bytes (without F0/F7) into UMP MT 0x3 packets."""
    packets = []
    chunks = [data[i:i + 6] for i in range(0, len(data), 6)] or [[]]
    for i, c in enumerate(chunks):
        if len(chunks) == 1:
            status = 0x0
        elif i == 0:
            status = 0x1
        elif i == len(chunks) - 1:
            status = 0x3
        else:
            status = 0x2
        b = list(c) + [0] * (6 - len(c))
        w0 = (0x3 << 28) | (status << 20) | (len(c) << 16) | (b[0] << 8) | b[1]
        w1 = (b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5]
        packets.append(struct.pack("<II", w0, w1))
    return b"".join(packets)


class Sysex7Collector:
    """Reassemble inbound MT 0x3 packets into complete SysEx7 messages."""

    def __init__(self):
        self.buf = []
        self.msgs = []

    def feed_words(self, w0, w1):
        if (w0 >> 28) != 0x3:
            return
        status = (w0 >> 20) & 0x0F
        n = (w0 >> 16) & 0x0F
        bs = [(w0 >> 8) & 0x7F, w0 & 0x7F,
              (w1 >> 24) & 0x7F, (w1 >> 16) & 0x7F,
              (w1 >> 8) & 0x7F, w1 & 0x7F][:n]
        if status in (0x0, 0x1):
            self.buf = []
        self.buf.extend(bs)
        if status in (0x0, 0x3):
            self.msgs.append(list(self.buf))
            self.buf = []


def drain(fd, collector, raw):
    while True:
        try:
            chunk = os.read(fd, 8192)
            if not chunk:
                return
            raw.extend(chunk)
            while len(raw) >= 8:
                w0, w1 = struct.unpack("<II", bytes(raw[:8]))
                del raw[:8]
                collector.feed_words(w0, w1)
        except BlockingIOError:
            return


def transact(fd, name, ci_bytes, expect, wait_s):
    collector = Sysex7Collector()
    raw = bytearray()
    os.write(fd, sysex7_ump(ci_bytes))
    deadline = time.time() + wait_s
    replies = []
    while time.time() < deadline:
        drain(fd, collector, raw)
        while collector.msgs:
            m = collector.msgs.pop(0)
            if len(m) >= 4 and m[0] == 0x7E and m[2] == 0x0D:
                replies.append(m)
        if replies and all(any(r[3] == e for r in replies) for e in expect):
            break
        time.sleep(0.01)
    print(f"=== {name} ===")
    if not replies:
        print("  NO CI REPLY")
        return False
    ok = True
    got = [r[3] for r in replies]
    for r in replies:
        nm = SUBID_NAMES.get(r[3], f"0x{r[3]:02X}")
        extra = ""
        if r[3] in (0x24, 0x25):
            extra = "  profile=" + " ".join(f"{x:02X}" for x in r[13:18])
        if r[3] == 0x7F and len(r) > 13:
            extra = f"  orig-sub-id=0x{r[13]:02X}"
        print(f"  reply sub-id 0x{r[3]:02X} ({nm}){extra}")
    for e in expect:
        if e not in got:
            print(f"  MISSING expected 0x{e:02X} ({SUBID_NAMES.get(e, '?')})")
            ok = False
    unexpected = [g for g in got if g not in expect]
    if unexpected:
        print(f"  UNEXPECTED: {['0x%02X' % u for u in unexpected]}")
        ok = False
    print("  PASS" if ok else "  FAIL")
    return ok


def autodetect_device():
    devices = sorted(glob.glob("/dev/snd/umpC*D0"))
    if not devices:
        sys.exit("no /dev/snd/umpC*D0 device found (is the board plugged in, "
                 "kernel >= 6.5 with snd-usb-midi2?)")
    if len(devices) > 1:
        print(f"note: multiple UMP devices, using {devices[0]} "
              f"(pass --device to override): {devices}")
    return devices[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--device", default=None,
                    help="UMP rawmidi device (default: autodetect)")
    ap.add_argument("--profile", default="7E00000100",
                    help="listed profile id, 5 hex bytes (default 7E00000100)")
    ap.add_argument("--channel", type=int, default=1,
                    help="MIDI channel the device declares in use (default 1)")
    ap.add_argument("--timeout", type=float, default=2.0,
                    help="reply timeout per check in seconds (default 2.0)")
    args = ap.parse_args()

    device = args.device or autodetect_device()
    profile = [int(args.profile[i:i + 2], 16) for i in range(0, 10, 2)]
    unlisted = list(profile)
    unlisted[3] = (unlisted[3] + 1) & 0x7F  # perturb one byte -> not listed
    ch_in_use = args.channel - 1            # device_id 0x00 = channel 1
    ch_not_in_use = (ch_in_use + 4) % 16

    print(f"device: {device}  profile: {args.profile}  "
          f"channel in use: {args.channel}")
    fd = os.open(device, os.O_RDWR | os.O_NONBLOCK)
    try:
        enable_ump_mode(fd)
        hdr = lambda subid, dev=0x7F: \
            [0x7E, dev, 0x0D, subid, 0x02] + SRC_MUID + BROADCAST

        results = []
        disc = hdr(0x70) + [0x7D, 0, 0, 0, 0, 1, 0, 0, 0, 3, 0] + \
               [0x00, 0x00, 0x10, 0x00, 0x00, 0x00]
        results.append(transact(fd, "Discovery", disc, [0x71], args.timeout))

        off = hdr(0x23) + profile + [0x00, 0x00]
        results.append(transact(fd, "Set Profile Off (listed)", off, [0x24],
                                args.timeout))

        on = hdr(0x22) + profile + [0x01, 0x00]
        results.append(transact(fd, "Set Profile On (listed)", on, [0x24],
                                args.timeout))

        bad = hdr(0x22) + unlisted + [0x01, 0x00]
        results.append(transact(fd, "Set Profile On (unlisted)", bad, [0x7F],
                                args.timeout))

        mm_body = [0x7F, 0x01, 0x00, 0x3F, 0x03]
        results.append(transact(fd, "MM Report (Function Block)",
                                hdr(0x42, 0x7F) + mm_body, [0x43, 0x44],
                                args.timeout))
        results.append(transact(fd, f"MM Report (channel {args.channel}, in use)",
                                hdr(0x42, ch_in_use) + mm_body, [0x43, 0x44],
                                args.timeout))
        results.append(transact(fd, f"MM Report (channel {ch_not_in_use + 1}, "
                                    "not in use)",
                                hdr(0x42, ch_not_in_use) + mm_body, [0x7F],
                                args.timeout))

        print(f"\nTOTAL: {sum(results)}/{len(results)} PASS")
        return 0 if all(results) else 1
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
