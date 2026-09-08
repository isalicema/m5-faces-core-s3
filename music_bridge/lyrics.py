"""Read-only NetEase LRC lookup; no account cookies, audio, or other providers."""
import bisect
import json
import re
import threading
import time
import unicodedata
import urllib.parse
import urllib.request
from collections import OrderedDict

STAMP=re.compile(r'\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\]')
def parse_lrc(text):
    offset=re.search(r'\[offset:([+-]?\d+)\]',text,re.I)
    offset=int(offset[1])/1000 if offset else 0
    cues={}
    for line in text.splitlines():
        stamps=list(STAMP.finditer(line))
        if not stamps:continue
        words=STAMP.sub('',line).strip()[:120]
        for stamp in stamps:
            if int(stamp[2])>=60:continue
            sec=int(stamp[1])*60+int(stamp[2])+float('0.'+(stamp[3] or '0'))+offset
            cues[max(0,round(sec,3))]=words
    return sorted(cues.items())[:3000]

def canon(text):return ''.join(c for c in unicodedata.normalize('NFKC',str(text)).casefold() if c.isalnum())
def choose_song(songs,state):
    matches=[]
    for song in songs:
        artists=song.get('artists',song.get('ar',[]))
        if canon(song.get('name'))!=canon(state['title']):continue
        if canon(''.join(a.get('name','') for a in artists))!=canon(state['artist']):continue
        duration=song.get('duration',song.get('dt',0))/1000
        if not state.get('duration') or abs(duration-state['duration'])>2:continue
        matches.append(song)
    albums=[s for s in matches if canon(s.get('album',s.get('al',{})).get('name'))==canon(state.get('album'))]
    matches=albums or matches
    ids={s['id'] for s in matches if isinstance(s.get('id'),int)}
    return next(iter(ids)) if len(ids)==1 else None

class OnlyNetEase(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,req,fp,code,msg,headers,newurl):
        url=urllib.parse.urlparse(newurl)
        if url.scheme!='https' or url.hostname!='music.163.com':raise ValueError('unexpected redirect')
        return super().redirect_request(req,fp,code,msg,headers,newurl)

def query(path,params):
    url='https://music.163.com'+path+'?'+urllib.parse.urlencode(params)
    request=urllib.request.Request(url,headers={'User-Agent':'FacesMusic/0.1','Referer':'https://music.163.com/'})
    with urllib.request.build_opener(OnlyNetEase()).open(request,timeout=5) as response:
        data=response.read(1_000_001)
    if len(data)>1_000_000:raise ValueError('response too large')
    data=json.loads(data)
    if data.get('code')!=200:raise ValueError('provider unavailable')
    return data

def lookup(state):
    songs=query('/api/search/get/web',{'s':state['title']+' '+state['artist'],'type':1,'limit':10}).get('result',{}).get('songs',[])
    song=choose_song(songs,state)
    if song is None:return {'status':'unavailable','lines':[]}
    raw=query('/api/song/lyric/v1',{'id':song,'cp':'false','lv':0,'tv':0,'rv':0,'kv':0,'yv':0,'ytv':0,'yrv':0})
    lines=parse_lrc(raw.get('lrc',{}).get('lyric',''))
    status='instrumental' if raw.get('nolyric') else 'synced' if lines else 'unavailable'
    return {'status':status,'lines':lines,'source':'netease','song_id':song}

def cache_key(state):
    # A media-session identifier can change on pause; lyrics belong to a recording.
    return (state.get('source',''),canon(state.get('title','')),canon(state.get('artist','')),
            canon(state.get('album','')),round(state.get('duration',0)))

class LyricsService:
    def __init__(self,loader=lookup):
        self.loader=loader;self.lock=threading.Lock();self.event=threading.Event();self.stopped=False
        self.cache=OrderedDict();self.wanted={};self.pending=None
        self.worker=threading.Thread(target=self.run,daemon=True);self.worker.start()
    def request(self,state):
        if not state.get('available'):return
        key=cache_key(state)
        with self.lock:
            cached=self.cache.get(key)
            if cached and cached[0]>time.monotonic():return
            if self.pending==key:return
            self.wanted=dict(state);self.pending=key;self.event.set()
    def run(self):
        while True:
            self.event.wait();self.event.clear()
            with self.lock:
                if self.stopped:return
                state=dict(self.wanted)
            try:result=self.loader(state)
            except Exception:result={'status':'retrying','lines':[]}
            ttl=30 if result['status']=='retrying' else 600
            with self.lock:
                key=cache_key(state)
                old=self.cache.get(key)
                if result['status']=='retrying' and old and old[1]['status'] in ('synced','instrumental'):
                    result=old[1]  # Valid lyrics do not expire just because a refresh failed.
                self.cache[key]=(time.monotonic()+ttl,result)
                self.cache.move_to_end(key)
                while len(self.cache)>32:self.cache.popitem(last=False)
                if self.pending==key:self.pending=None
    def snapshot(self,state):
        with self.lock:
            cached=self.cache.get(cache_key(state))
            result=dict(cached[1]) if cached else {'status':'loading','lines':[]}
        lines=result['lines'];idx=bisect.bisect_right([l[0] for l in lines],state['position'])-1
        result['lines']=lines[max(0,idx):max(0,idx)+8]
        return result
    def close(self):
        with self.lock:self.stopped=True
        self.event.set()
