#!/usr/bin/env python3
"""Stage an EHW-2 framebank image into neorv32_soc_icap's AXI-Lite framebuf.

The loader writes word[1..] first and word[0] last, so word[0] acts as the firmware
ready flag. This mirrors the proven zynq_xpart T2.3 staging flow while keeping the
script local to zynq_ehw.

  ehw2-framebank-load.py runs/ehw2/framebank.bin 0x40000000
"""

from __future__ import annotations

import argparse
import struct
import time

import serial


PORT = "/dev/ebaz-uart"


def cmd(ser: serial.Serial, line: str, timeout: float = 1.0) -> bytes:
    ser.reset_input_buffer()
    ser.write(line.encode() + b"\r")
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        chunk = ser.read(128)
        if chunk:
            buf += chunk
            if buf.rstrip().endswith(b"zynq-uboot>"):
                return buf
    return buf


def read_words(path: str, little_endian: bool = False) -> list[int]:
    data = open(path, "rb").read()
    if len(data) % 4:
        raise SystemExit(f"{path}: size is not a multiple of 4")
    fmt = "<" if little_endian else ">"
    return list(struct.unpack(f"{fmt}{len(data) // 4}I", data))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("base", type=lambda s: int(s, 16))
    ap.add_argument("--port", default=PORT)
    ap.add_argument("--le", action="store_true",
                    help="image words are little-endian (e.g. ehw54-param-pack.py "
                         "output); default is big-endian framebank format. "
                         "Board-caught gotcha: staging an LE param image without "
                         "this flag byte-swaps every word (magic 0xE5400001 "
                         "arrives as 0x010040E5).")
    args = ap.parse_args()

    words = read_words(args.image, little_endian=args.le)
    if not words:
        raise SystemExit("empty image")

    print(f"[*] staging {len(words)} words to framebuf @0x{args.base:08x}; word0 last", flush=True)
    ser = serial.Serial(args.port, 115200, timeout=0.1)
    cmd(ser, "")
    for i, value in enumerate(words[1:], start=1):
        cmd(ser, f"mw 0x{args.base + 4 * i:08x} 0x{value:08x} 1")
        if i % 64 == 0:
            print(f"  {i}/{len(words)}", flush=True)
    cmd(ser, f"mw 0x{args.base:08x} 0x{words[0]:08x} 1")

    print("[*] readback word0/word1/word4:")
    for off in (0, 4, 16):
        reply = cmd(ser, f"md 0x{args.base + off:08x} 1")
        for line in reply.split(b"\n"):
            if f"{args.base + off:08x}".encode() in line.lower():
                print("   ", line.strip().decode(errors="replace"))
    ser.close()
    print("[*] done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
