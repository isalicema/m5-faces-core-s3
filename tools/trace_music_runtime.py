"""Temporary one-action latency trace; no titles, lyrics, tokens or request IDs."""
import json,runpy,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from music_bridge.backend import MusicBackend
original_action,original_call,original_snapshot=MusicBackend.action,MusicBackend.call,MusicBackend.snapshot
trace={'start':0.,'captured':False,'reads':0}
def emit(event,**fields):print(json.dumps(dict(event=event,at_ms=round((time.monotonic()-trace['start'])*1000),**fields)),flush=True)
def active():return trace['captured'] and time.monotonic()-trace['start']<12
def action(self,kind,track,request):
 if kind=='favorite' and not trace['captured']:
  trace.update(start=time.monotonic(),captured=True,identity={k:(self.raw or {}).get(k) for k in ('title','artist','album')});emit('action_received',favorite_before=self.favorite)
 start=time.monotonic()
 result=original_action(self,kind,track,request)
 if active():emit('action_returned',duration_ms=round((time.monotonic()-start)*1000),ok=result.get('ok'),pending=result.get('pending'),code=result.get('code'),favorite=result.get('favorite'))
 return result
def call(self,args,payload=None):
 start=time.monotonic();label=(payload or {}).get('command') or ' '.join(args[1:])
 try:
  result=original_call(self,args,payload)
  if active():emit('native_returned',operation=label,duration_ms=round((time.monotonic()-start)*1000),track_same=(result.get('favorite_track')==trace.get('identity')) if isinstance(result,dict) and 'favorite_track' in result else None,guarded=bool(payload and 'expected_pid' in payload),favorite=result.get('favorite') if isinstance(result,dict) else None)
  return result
 except Exception as e:
  if active():emit('native_error',operation=label,duration_ms=round((time.monotonic()-start)*1000),error_type=type(e).__name__)
  raise
def snapshot(self):
 start=time.monotonic();s=original_snapshot(self)
 if active() and trace['reads']<40:
  trace['reads']+=1;emit('state_served',duration_ms=round((time.monotonic()-start)*1000),favorite=s.get('favorite'),track_same=all(s.get(k)==v for k,v in trace.get('identity',{}).items()),stale=s.get('stale'))
 return s
MusicBackend.action,MusicBackend.call,MusicBackend.snapshot=action,call,snapshot
runpy.run_module('music_bridge.server',run_name='__main__')
