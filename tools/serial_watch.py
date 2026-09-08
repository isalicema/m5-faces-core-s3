"""Read test logs without sending commands; native USB attach may reboot the chip."""
import argparse
import time
from pathlib import Path

import serial

parser = argparse.ArgumentParser()
parser.add_argument("port")
parser.add_argument("--seconds", type=float, default=45)
parser.add_argument("--log", type=Path)
parser.add_argument("--play-radio", action="store_true", help="Start radio once Wi-Fi is connected")
args = parser.parse_args()

connection = serial.Serial()
connection.port = args.port
connection.baudrate = 115200
connection.timeout = 0.2
connection.dtr = False
connection.rts = False
connection.open()
log = None
try:
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        log = args.log.open("a")
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        line = connection.readline()
        if line:
            message = line.decode("utf-8", errors="replace").rstrip()
            print(message, flush=True)
            if args.play_radio and "HEALTH wifi=1" in message:
                connection.write(b"RADIO PLAY\n")
                args.play_radio = False
            if log:
                log.write(message + "\n")
                log.flush()
finally:
    connection.close()
    if log:
        log.close()
