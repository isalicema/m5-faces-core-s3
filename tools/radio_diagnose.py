"""Bounded Faces radio serial session; preserve DTR/RTS to avoid USB resets."""
import argparse
import select
import sys
import time
from pathlib import Path

import serial


class PassiveSerial(serial.Serial):
    def _update_dtr_state(self):
        pass

    def _update_rts_state(self):
        pass


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("port")
parser.add_argument("--log", type=Path, required=True)
parser.add_argument("--seconds", type=int, default=600)
args = parser.parse_args()
allowed = {"HOME STATUS", "COMPANION OPEN", "COMPANION HOME", "RADIO OPEN", "RADIO PLAY", "RADIO STOP", "RADIO PROBE", "RADIO MIRROR 0", "RADIO MIRROR 1"}
args.log.parent.mkdir(parents=True, exist_ok=True)
with PassiveSerial(args.port, 115200, timeout=.1, exclusive=True) as connection, args.log.open("a") as log:
    deadline = time.monotonic() + min(900, max(1, args.seconds))
    started = time.monotonic()
    while time.monotonic() < deadline:
        if select.select([sys.stdin], [], [], 0)[0]:
            command = sys.stdin.readline().strip()
            if command == "QUIT":
                break
            if command in allowed:
                connection.write((command + "\n").encode("ascii"))
                log.write(f"COMMAND {command}\n")
                log.flush()
        line = connection.readline()
        if line:
            message = f"{time.monotonic()-started:.3f} " + line.decode("utf-8", errors="replace").rstrip()
            print(message, flush=True)
            log.write(message + "\n")
            log.flush()
