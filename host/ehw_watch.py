#!/usr/bin/env python3
"""Watch and decode EHW mailbox words through a U-Boot serial console."""

from __future__ import annotations

import argparse
import re
import sys
import time


ADDR = 0x41200000
LINE_RE = re.compile(rb"%08x:\s*([0-9a-fA-F]{8})" % ADDR)


def decode(word: int) -> str:
    tag = (word >> 24) & 0xFF
    if word == 0xE0000000:
        return "BOOT: EHW firmware reached main"
    if tag == 0xE1:
        return f"ARRAY_OK: self-check acc0={word & 0xFFFF} (expect 14)"
    if tag == 0xE2:
        correct = (word >> 16) & 0xFF
        sse_lo = word & 0xFFFF
        return f"SCORE: correct={correct}/40 sse_low16={sse_lo}"
    if tag == 0xE3:
        return f"FITNESS: low24=0x{word & 0xFFFFFF:06x}"
    return f"UNKNOWN: 0x{word:08x}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ebaz-uart")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--interval", type=float, default=0.25)
    ap.add_argument("--count", type=int, default=0, help="0 means run until interrupted")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("pyserial is required for board watching; install it in .env when board work starts", file=sys.stderr)
        return 2

    seen = None
    n = 0
    with serial.Serial(args.port, args.baud, timeout=0.15) as ser:
        ser.write(b"\r")
        time.sleep(0.1)
        ser.read(512)
        while args.count == 0 or n < args.count:
            ser.reset_input_buffer()
            ser.write(b"md 0x%08x 1\r" % ADDR)
            time.sleep(args.interval)
            data = ser.read(512)
            match = LINE_RE.search(data)
            if match:
                word = int(match.group(1), 16)
                if word != seen:
                    print(f"0x{word:08x}  {decode(word)}")
                    seen = word
            n += 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
