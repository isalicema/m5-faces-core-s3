import threading
import time
import unittest
from music_bridge.lyrics import parse_lrc, choose_song, LyricsService, cache_key

S={'available':True,'track_id':'a','title':'测试歌','artist':'歌手','album':'专辑','duration':120,'position':0}
class LyricsTests(unittest.TestCase):
    def test_multiple_stamps_offset_empty_cues_and_order(self):
        self.assertEqual(parse_lrc('[offset:100]\n[00:02.5]second\n[00:00.00][00:01.00]first\n[00:03.00]\n[artist:no timestamp]'),[(.1,'first'),(1.1,'first'),(2.6,'second'),(3.1,'')])
    def test_version_artist_and_duration_must_match(self):
        song={'id':1,'name':'测试歌','artists':[{'name':'歌手'}],'duration':120000,'album':{'name':'专辑'}}
        self.assertEqual(choose_song([song],S),1)
        self.assertIsNone(choose_song([dict(song,name='测试歌 Live')],S))
        self.assertIsNone(choose_song([dict(song,duration=130000)],S))
        self.assertIsNone(choose_song([dict(song,artists=[{'name':'别的歌手'}])],S))
        self.assertIsNone(choose_song([song,dict(song,id=2)],S))
    def test_delayed_old_track_cannot_supply_new_track_lyrics(self):
        began=threading.Event();release=threading.Event()
        def load(s):
            if s['track_id']=='a':began.set();release.wait(2)
            return {'status':'synced','lines':[(0,s['title']),(10,'next')]}
        service=LyricsService(load)
        try:
            service.request(S);self.assertTrue(began.wait(1))
            newer=dict(S,track_id='b',title='new');service.request(newer)
            self.assertEqual(service.snapshot(newer)['status'],'loading');release.set()
            until=time.monotonic()+2
            while service.snapshot(newer)['status']=='loading' and time.monotonic()<until:time.sleep(.01)
            self.assertEqual(service.snapshot(newer)['lines'][0][1],'new')
            self.assertEqual(service.snapshot(dict(newer,position=11))['lines'][0],(10,'next'))
            self.assertEqual(service.snapshot(dict(newer,position=0))['lines'][0][1],'new')
        finally:release.set();service.close()

class LyricsCacheTests(unittest.TestCase):
    def wait_idle(self,service):
        until=time.monotonic()+2
        while time.monotonic()<until:
            with service.lock:
                if service.pending is None:return
            time.sleep(.01)
        self.fail('lyrics worker did not finish')
    def test_pause_session_id_reuses_recording_cache(self):
        calls=[]
        def load(state):calls.append(state);return {'status':'synced','lines':[(0,'known line')]}
        service=LyricsService(load)
        try:
            service.request(S);self.wait_idle(service)
            paused=dict(S,track_id='different-media-session',playing=False)
            service.request(paused);self.wait_idle(service)
            self.assertEqual(len(calls),1)
            self.assertEqual(service.snapshot(paused)['lines'],[(0,'known line')])
            self.assertNotEqual(cache_key(S),cache_key(dict(S,album='Live')))
            self.assertNotEqual(cache_key(S),cache_key(dict(S,duration=180)))
        finally:service.close();service.worker.join(1)
    def test_refresh_failure_retains_known_lyrics_but_not_for_another_song(self):
        calls=[]
        def load(state):
            calls.append(state)
            if len(calls)>1:raise TimeoutError('provider timeout')
            return {'status':'synced','lines':[(0,'known line')]}
        service=LyricsService(load)
        try:
            service.request(S);self.wait_idle(service)
            with service.lock:service.cache[cache_key(S)]=(0,service.cache[cache_key(S)][1])
            service.request(S);self.wait_idle(service)
            self.assertEqual(service.snapshot(S)['status'],'synced')
            self.assertEqual(service.snapshot(S)['lines'],[(0,'known line')])
            other=dict(S,title='Different song',track_id='b')
            service.request(other);self.wait_idle(service)
            self.assertEqual(service.snapshot(other)['status'],'retrying')
            self.assertEqual(service.snapshot(other)['lines'],[])
        finally:service.close();service.worker.join(1)

if __name__=='__main__':unittest.main()
