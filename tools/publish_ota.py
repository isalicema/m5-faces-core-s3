#!/usr/bin/env python3
"""Publish a Faces-only OTA candidate; does not flash USB or restart the Bridge."""
from pathlib import Path
import argparse,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from music_bridge.ota import publish
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware',type=Path,default=ROOT/'.pio/build/suite/firmware.bin')
    parser.add_argument('--directory',type=Path,default=ROOT/'.local/ota')
    parser.add_argument('--device-mac',required=True,help='MAC address of the paired Faces device; do not commit it')
    args=parser.parse_args();m=publish(args.firmware,args.directory,args.device_mac)
    print('Faces OTA published: %d bytes sha256=%s'%(m['size'],m['sha256']))
