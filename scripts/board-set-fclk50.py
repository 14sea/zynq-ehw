#!/usr/bin/env python3
"""Set and verify Zynq FCLK0=50 MHz from a U-Boot UART prompt.

The EBAZ4205 miner boot chain leaves FCLK0 at 125 MHz. This project signs off
the PL at 50 MHz, so every board run must pin FCLK0 before `fpga loadb`.
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover - board-host dependency
    print("pyserial is required: install it in the board-control environment", file=sys.stderr)
    raise


PROMPT = b"zynq-uboot>"
SLCR_UNLOCK = 0xF8000008
FPGA0_CLK_CTRL = 0xF8000170
UNLOCK_KEY = 0x0000DF0D
FCLK0_50MHZ = 0x00200A00


def ub_cmd(ser: serial.Serial, line: str, timeout: float = 1.5) -> bytes:
    ser.reset_input_buffer()
    ser.write(line.encode("ascii") + b"\r")
    out = b""
    start = time.time()
    while time.time() - start < timeout:
        chunk = ser.read(256)
        if chunk:
            out += chunk
            if PROMPT in out:
                break
    return out


def md1(ser: serial.Serial, addr: int) -> int:
    out = ub_cmd(ser, f"md 0x{addr:08x} 1")
    pat = re.compile(rb"^[0-9a-fA-F]{8}:\s+([0-9a-fA-F]{8})", re.MULTILINE)
    match = pat.search(out)
    if not match:
        raise RuntimeError(f"could not parse md output for 0x{addr:08x}: {out!r}")
    return int(match.group(1), 16)


def mw1(ser: serial.Serial, addr: int, value: int) -> None:
    ub_cmd(ser, f"mw 0x{addr:08x} 0x{value:08x} 1")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="/dev/ebaz-uart")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--verify-only", action="store_true", help="only read and verify FPGA0_CLK_CTRL")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.1) as ser:
        ub_cmd(ser, "")  # sync to prompt
        before = md1(ser, FPGA0_CLK_CTRL)
        print(f"before FPGA0_CLK_CTRL=0x{before:08x}")

        if not args.verify_only and before != FCLK0_50MHZ:
            mw1(ser, SLCR_UNLOCK, UNLOCK_KEY)
            mw1(ser, FPGA0_CLK_CTRL, FCLK0_50MHZ)

        after = md1(ser, FPGA0_CLK_CTRL)
        print(f"after  FPGA0_CLK_CTRL=0x{after:08x}")
        if after != FCLK0_50MHZ:
            print("FAIL: FCLK0 is not pinned to 50 MHz (expected 0x00200a00)", file=sys.stderr)
            return 1

    print("PASS: FCLK0 pinned to 50 MHz")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
