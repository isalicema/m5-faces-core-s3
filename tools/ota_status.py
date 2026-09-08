#!/usr/bin/env python3
"""Read the paired Faces OTA boot receipt without printing the pairing token."""
from pathlib import Path
import argparse,json,time,urllib.request
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',type=int,default=8766)
parser.add_argument('--config',type=Path,default=ROOT/'.local/music.json')
parser.add_argument('--candidate',type=Path,default=ROOT/'.local/ota/current.json')
args=parser.parse_args()
token=json.loads(args.config.read_text())['token']
request=urllib.request.Request(f'http://127.0.0.1:{args.port}/api/ota/status',headers={'Authorization':'Bearer '+token})
with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request,timeout=5) as response:status=json.load(response)
candidate=json.loads(args.candidate.read_text()) if args.candidate.is_file() else None
device=status.get('device')
status['matches_candidate']=bool(device and candidate and device['event']=='ready' and device['elf_sha256']==candidate.get('elf_sha256'))
status['receipt_age_seconds']=round(time.time()-device['received_at'],1) if device else None
print(json.dumps(status,ensure_ascii=False,indent=2))
