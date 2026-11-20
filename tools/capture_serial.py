"""Capture BCOTI logs; optional USB-JTAG reset, never writes flash."""
import argparse
from pathlib import Path
import time
import serial
from esptool.reset import HardReset

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--seconds", type=int, default=25)
parser.add_argument("--reset", action="store_true")
parser.add_argument("--output", required=True)
args = parser.parse_args()
with Path(args.output).open("xb") as log:
    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 0.25
    port.dtr = False
    port.rts = False
    port.open()
    try:
        if args.reset:
            HardReset(port, uses_usb=True)()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            chunk = port.read(port.in_waiting or 1)
            if chunk:
                log.write(chunk)
                log.flush()
                print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
    finally:
        port.close()
