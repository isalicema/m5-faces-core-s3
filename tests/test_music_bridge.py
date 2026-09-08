import base64
import io
import json
import subprocess
import threading
import time
import unittest
import urllib.request
import urllib.error
import http.cookiejar
from unittest import mock
from PIL import Image
from music_bridge.model import normalize
from music_bridge.backend import MusicBackend
from music_bridge.server import make_server

RAW = dict(bundleIdentifier='com.netease.163music', title='中文歌名', artist='歌手',
           album='专辑', playing=True, elapsedTime=10, duration=120, timestamp=1000)

class ModelTests(unittest.TestCase):
    def test_only_music_sources_and_no_fake_likes(self):
        self.assertFalse(normalize(dict(RAW,bundleIdentifier='com.google.Chrome'))['available'])
        self.assertFalse(normalize(None)['available'])
        self.assertIsNone(normalize(RAW)['favorite'])
        self.assertTrue(normalize(dict(RAW,isLiked=True,supportsIsLiked=True))['favorite'])
    def test_elapsed_iso_and_pause(self):
        self.assertEqual(normalize(dict(RAW,timestamp='1970-01-01T00:16:40Z'),1004)['position'],14)
        self.assertEqual(normalize(dict(RAW,playing=False),1004)['position'],10)
        self.assertEqual(normalize(RAW,5000)['position'],120)
    def test_track_identity_independent_of_position(self):
        self.assertEqual(normalize(RAW)['track_id'],normalize(dict(RAW,elapsedTime=99))['track_id'])
        self.assertNotEqual(normalize(RAW)['track_id'],normalize(dict(RAW,title='另一首'))['track_id'])
    def test_nonfinite_time_is_not_sent_in_json(self):
        s=normalize(dict(RAW,duration=float('inf'),elapsedTime=float('nan')),1000)
        json.dumps(s,allow_nan=False)

class BackendTests(unittest.TestCase):
    def setUp(self): self.backend=MusicBackend(); self.track=normalize(RAW)['track_id']
    def prime_apple(self):
        raw=dict(RAW,bundleIdentifier='com.apple.Music',processIdentifier=123)
        with mock.patch.object(self.backend,'call',return_value=raw), mock.patch.object(self.backend,'native',return_value={'favorite':False}):self.backend.refresh()
        return raw,normalize(raw)['track_id']
    def test_apple_favorite_confirmed_in_receipt_without_media_roundtrip(self):
        raw,track=self.prime_apple()
        receipt={'ok':True,'pending':False,'favorite':True,'favorite_track':{k:raw[k] for k in ('title','artist','album')}}
        with mock.patch.object(self.backend,'call') as media, mock.patch.object(self.backend,'native',return_value=receipt) as native:
            self.backend.action('favorite',track,'fast-apple-123')
            self.assertTrue(self.backend.snapshot()['favorite'])
            self.backend.action('favorite',track,'fast-apple-123')
            media.assert_not_called();native.assert_called_once_with('action','com.apple.Music','favorite',expected=raw)
    def test_apple_native_track_rejection_never_fakes_favorite(self):
        _,track=self.prime_apple()
        with mock.patch.object(self.backend,'native',return_value={'ok':False,'code':'track_changed'}):
            r=self.backend.action('favorite',track,'moved-apple-123')
            self.assertEqual(r['code'],'track_changed');self.assertFalse(self.backend.snapshot()['favorite'])
    def test_apple_uncertain_favorite_is_not_replayed(self):
        _,track=self.prime_apple()
        with mock.patch.object(self.backend,'native',side_effect=subprocess.TimeoutExpired('helper',4)) as native:
            self.assertEqual(self.backend.action('favorite',track,'slow-apple-123')['code'],'uncertain')
            self.backend.action('favorite',track,'slow-apple-123');native.assert_called_once()
            self.assertFalse(self.backend.snapshot()['favorite'])
    def test_apple_accepted_but_unread_favorite_is_not_optimistic(self):
        _,track=self.prime_apple()
        with mock.patch.object(self.backend,'native',return_value={'ok':True,'pending':True}):
            self.backend.action('favorite',track,'unread-apple-123')
            self.assertFalse(self.backend.snapshot()['favorite'])
    def test_pending_apple_receipt_cannot_publish_a_favorite_value(self):
        raw,track=self.prime_apple()
        receipt={'ok':True,'pending':True,'favorite':True,
                 'favorite_track':{k:raw[k] for k in ('title','artist','album')}}
        with mock.patch.object(self.backend,'native',return_value=receipt):
            self.backend.action('favorite',track,'pending-value-123')
            self.assertFalse(self.backend.snapshot()['favorite'])
    def test_old_poll_cannot_overwrite_confirmed_apple_favorite(self):
        raw,track=self.prime_apple();entered,release=threading.Event(),threading.Event()
        def native(command,*args,**kwargs):
            if command=='status':
                entered.set();release.wait(2);return {'favorite':False}
            return {'ok':True,'favorite':True,'favorite_track':{k:raw[k] for k in ('title','artist','album')}}
        with mock.patch.object(self.backend,'call',return_value=raw),mock.patch.object(self.backend,'native',side_effect=native):
            worker=threading.Thread(target=self.backend.refresh);worker.start()
            try:
                self.assertTrue(entered.wait(1))
                self.backend.action('favorite',track,'poll-race-123')
            finally:release.set();worker.join(3)
        self.assertTrue(self.backend.snapshot()['favorite'])
    def test_apple_favorite_readback_tracks_true_false_and_unknown(self):
        raw=dict(RAW,bundleIdentifier='com.apple.Music')
        identity={k:raw[k] for k in ('title','artist','album')}
        with mock.patch.object(self.backend,'call',return_value=raw), mock.patch.object(self.backend,'native') as native:
            for liked in (True,False,None):
                native.return_value={'actions':['favorite'],'favorite':liked,'favorite_track':identity}
                self.backend.refresh()
                self.assertIs(self.backend.snapshot()['favorite'],liked)
            native.return_value={'favorite':True,'favorite_track':dict(identity,title='previous song')}
            self.backend.refresh()
            self.assertIsNone(self.backend.snapshot()['favorite'])
    def test_skip_gap_keeps_playing_without_delaying_new_title(self):
        with mock.patch('music_bridge.backend.time.monotonic',return_value=10) as clock, mock.patch.object(self.backend,'call',return_value=RAW) as call, mock.patch.object(self.backend,'native',return_value={}):
            self.backend.refresh()
            self.backend.action('next',self.track,'skip-gap-123')
            clock.return_value=10.2
            call.return_value=dict(RAW,title='new',playing=False)
            self.backend.refresh()
            self.assertEqual(self.backend.snapshot()['title'],'new')
            self.assertTrue(self.backend.snapshot()['playing'])
            clock.return_value=10.8
            call.return_value=dict(RAW,title='new',playing=True)
            self.backend.refresh()
            self.assertTrue(self.backend.snapshot()['playing'])
    def test_pause_after_skip_is_not_hidden_forever(self):
        with mock.patch('music_bridge.backend.time.monotonic',return_value=10) as clock, mock.patch.object(self.backend,'call',return_value=RAW) as call, mock.patch.object(self.backend,'native',return_value={}):
            self.backend.refresh();self.backend.action('next',self.track,'skip-stop-123')
            clock.return_value=10.2;call.return_value=dict(RAW,playing=False)
            self.backend.refresh();self.assertTrue(self.backend.snapshot()['playing'])
            clock.return_value=11.2
            self.assertFalse(self.backend.snapshot()['playing'])
    def test_explicit_pause_bypasses_skip_smoothing(self):
        with mock.patch('music_bridge.backend.time.monotonic',return_value=10), mock.patch.object(self.backend,'call',return_value=RAW) as call, mock.patch.object(self.backend,'native',return_value={}):
            self.backend.refresh();self.backend.action('next',self.track,'skip-pause-123')
            self.backend.action('toggle',self.track,'pause-now-123')
            call.return_value=dict(RAW,playing=False);self.backend.refresh()
            self.assertFalse(self.backend.snapshot()['playing'])
    def test_paused_skip_and_failed_skip_do_not_claim_playing(self):
        with mock.patch.object(self.backend,'call',return_value=dict(RAW,playing=False)), mock.patch.object(self.backend,'native',return_value={}):
            self.backend.refresh();self.backend.action('next',self.track,'paused-skip-123')
            self.backend.refresh();self.assertFalse(self.backend.snapshot()['playing'])
        with mock.patch.object(self.backend,'call',side_effect=[RAW,subprocess.TimeoutExpired('media',4)]):
            self.assertFalse(self.backend.action('next',self.track,'failed-skip-123')['ok'])
        self.assertIsNone(self.backend.skip_transition.source)
    def test_skip_guard_expires_and_missing_metadata_cancels_it(self):
        with mock.patch('music_bridge.backend.time.monotonic',return_value=10) as clock, mock.patch.object(self.backend,'call',return_value=RAW) as call, mock.patch.object(self.backend,'native',return_value={}):
            self.backend.refresh();self.backend.action('next',self.track,'expire-skip-123')
            clock.return_value=13.1;call.return_value=dict(RAW,playing=False)
            self.backend.refresh();self.assertFalse(self.backend.snapshot()['playing'])
            self.backend.skip_transition.arm(('com.netease.163music',None),14)
            call.return_value=None;self.backend.refresh()
            self.assertIsNone(self.backend.skip_transition.source)
            self.assertTrue(self.backend.snapshot()['stale'])
    def test_new_title_is_visible_while_native_status_is_blocked(self):
        entered, release = threading.Event(), threading.Event()
        def slow_native(*args):
            entered.set(); release.wait(2); return {'actions': [], 'favorite': True}
        with mock.patch.object(self.backend, 'call', return_value=RAW), mock.patch.object(self.backend, 'native', side_effect=slow_native):
            worker=threading.Thread(target=self.backend.refresh); worker.start()
            try:
                self.assertTrue(entered.wait(1))
                self.assertTrue(self.backend.lock.acquire(timeout=.2))
                try:
                    self.assertEqual(self.backend.snapshot()['track_id'], self.track)
                    self.assertIsNone(self.backend.snapshot()['favorite'])
                finally: self.backend.lock.release()
            finally: release.set(); worker.join(3)
            self.assertFalse(worker.is_alive())
    def test_cover_render_does_not_block_state_or_command(self):
        entered, release = threading.Event(), threading.Event()
        image=Image.new('RGB',(144,144),'blue'); out=io.BytesIO(); image.save(out,'PNG')
        raw=dict(RAW, artworkData=base64.b64encode(out.getvalue()).decode())
        from music_bridge.theme import stage_from_cover
        def slow_scene(cover):
            entered.set(); release.wait(2); return stage_from_cover(cover)
        with mock.patch.object(self.backend,'call',return_value=raw), mock.patch.object(self.backend,'native',return_value={'actions':[]}), mock.patch('music_bridge.backend.stage_from_cover',side_effect=slow_scene):
            worker=threading.Thread(target=self.backend.refresh); worker.start()
            try:
                self.assertTrue(entered.wait(1))
                self.assertTrue(self.backend.lock.acquire(timeout=.2))
                try:
                    state=self.backend.snapshot()
                    self.assertEqual(state['track_id'],self.track)
                    self.assertEqual(state['artwork_id'],'')
                    self.assertTrue(self.backend.action('next',self.track,'during-cover')['ok'])
                finally: self.backend.lock.release()
            finally: release.set(); worker.join(3)
            self.assertTrue(self.backend.snapshot()['artwork_id'])
    def test_missing_metadata_keeps_last_track_but_disables_commands(self):
        with mock.patch.object(self.backend,'call',side_effect=[RAW,None,RAW]),mock.patch.object(self.backend,'native',return_value={'actions':[]}):
            self.backend.refresh(); first=self.backend.snapshot()
            self.backend.art_id='last-cover'
            self.backend.refresh(); missing=self.backend.snapshot()
            self.assertEqual(missing['track_id'],first['track_id'])
            self.assertEqual(missing['artwork_id'],'last-cover')
            self.assertEqual(missing['connection'],'metadata_missing')
            self.assertTrue(missing['stale']);self.assertEqual(missing['actions'],[])
            self.backend.refresh();recovered=self.backend.snapshot()
            self.assertFalse(recovered['stale']);self.assertIn('toggle',recovered['actions'])
    def test_initial_missing_metadata_does_not_claim_playback_stopped(self):
        with mock.patch.object(self.backend,'call',return_value=None):self.backend.refresh()
        state=self.backend.snapshot()
        self.assertEqual(state['connection'],'metadata_missing')
        self.assertEqual(state['title'],'暂时获取不到曲目信息')
        self.assertIn('可能仍在播放',state['error'])
    def test_apple_repeat_poll_reads_real_mode_and_clears_on_switch(self):
        raw,track=self.prime_apple()
        for mode in ('off','one','all',None,'invalid'):
            with mock.patch.object(self.backend,'call',return_value=raw),mock.patch.object(self.backend,'native',return_value={'repeat_mode':mode}):
                self.backend.refresh()
            self.assertEqual(self.backend.snapshot()['repeat_mode'],mode if mode in ('off','one','all') else None)
        with mock.patch.object(self.backend,'call',return_value=RAW),mock.patch.object(self.backend,'native',return_value={}):self.backend.refresh()
        self.assertIsNone(self.backend.snapshot()['repeat_mode'])
    def test_apple_repeat_action_uses_readback_not_intention(self):
        raw,track=self.prime_apple()
        for i,receipt in enumerate(({'ok':True,'repeat_mode':'all','pending':False}, {'ok':True,'repeat_mode':'off','pending':True}, {'ok':True,'pending':True}, {'ok':False,'code':'uncertain'})):
            with mock.patch.object(self.backend,'call',return_value=raw),mock.patch.object(self.backend,'native',return_value=receipt) as native:
                self.backend.action('repeat_all',track,'apple-repeat-'+str(i))
                self.backend.action('repeat_all',track,'apple-repeat-'+str(i))
                native.assert_called_once()
            self.assertEqual(self.backend.snapshot()['repeat_mode'],receipt.get('repeat_mode'))
            self.assertIsNone(self.backend.snapshot()['repeat_requested'])
        self.backend.updated=0
        self.assertIsNone(self.backend.snapshot()['repeat_mode'])
    def test_repeat_poll_inflight_cannot_overwrite_action_readback(self):
        raw,track=self.prime_apple()
        def native(command,*args,**kwargs):
            if command=='status':
                self.backend.action('repeat_all',track,'repeat-during-poll')
                return {'repeat_mode':'one'}
            return {'ok':True,'pending':False,'repeat_mode':'all'}
        with mock.patch.object(self.backend,'call',return_value=raw),mock.patch.object(self.backend,'native',side_effect=native):self.backend.refresh()
        self.assertEqual(self.backend.snapshot()['repeat_mode'],'all')
    def test_repeat_request_is_not_reported_as_current_mode(self):
        with mock.patch.object(self.backend,'call',return_value=RAW),mock.patch.object(self.backend,'native',return_value={'ok':True,'pending':True}):
            self.backend.refresh()
            self.backend.action('repeat_one',self.track,'repeat-history')
            s=self.backend.snapshot()
            self.assertEqual(s['repeat_requested'],'one');self.assertIsNone(s['repeat_mode'])
            self.backend.metadata_missing=True
            self.assertIsNone(self.backend.snapshot()['repeat_requested'])
            self.backend.metadata_missing=False
        with mock.patch.object(self.backend,'call',return_value=dict(RAW,processIdentifier=999)),mock.patch.object(self.backend,'native',return_value={}):
            self.backend.refresh()
            self.assertIsNone(self.backend.snapshot()['repeat_requested'])
    def test_uncertain_repeat_clears_old_request_and_is_not_replayed(self):
        self.backend.repeat_request={'source':RAW['bundleIdentifier'],'pid':None,'mode':'one'}
        with mock.patch.object(self.backend,'call',return_value=RAW),mock.patch.object(self.backend,'native',side_effect=subprocess.TimeoutExpired('helper',4)) as native:
            r=self.backend.action('repeat_all',self.track,'repeat-timeout')
            self.assertEqual(r['code'],'uncertain');self.assertIsNone(self.backend.repeat_request)
            self.backend.action('repeat_all',self.track,'repeat-timeout');self.assertEqual(native.call_count,1)

    def test_repeat_commands_are_explicit_and_deduplicated(self):
        for action in ('repeat_one', 'repeat_all'):
            with mock.patch.object(self.backend,'call',return_value=RAW), mock.patch.object(self.backend,'native',return_value={'ok':True,'pending':True}) as native:
                request='request-'+action
                first=self.backend.action(action,self.track,request)
                again=self.backend.action(action,self.track,request)
                self.assertEqual(first,again)
                native.assert_called_once_with('action',RAW['bundleIdentifier'],action)
    def test_repeat_rejects_changed_track(self):
        with mock.patch.object(self.backend,'call',return_value=dict(RAW,title='new')), mock.patch.object(self.backend,'native') as native:
            result=self.backend.action('repeat_one',self.track,'repeat-stale')
            self.assertEqual(result['code'],'track_changed');native.assert_not_called()

    def test_duplicate_request_does_not_skip_twice(self):
        with mock.patch.object(self.backend,'call',side_effect=[RAW,None]) as call:
            a=self.backend.action('next',self.track,'request-123')
            b=self.backend.action('next',self.track,'request-123')
        self.assertEqual(a,b);self.assertTrue(a['ok']);self.assertEqual(call.call_count,2)
    def test_changed_song_rejects_delayed_heart(self):
        with mock.patch.object(self.backend,'call',return_value=dict(RAW,title='另一首')),mock.patch.object(self.backend,'native') as native:
            self.assertEqual(self.backend.action('favorite',self.track,'request-123')['code'],'track_changed')
        native.assert_not_called()
    def test_uncertain_heart_not_retried(self):
        with mock.patch.object(self.backend,'call',return_value=RAW),mock.patch.object(self.backend,'native',side_effect=subprocess.TimeoutExpired('helper',4)) as native:
            a=self.backend.action('favorite',self.track,'request-123')
            b=self.backend.action('favorite',self.track,'request-123')
        self.assertFalse(a['ok']);self.assertEqual(a,b);native.assert_called_once()
    def test_invalid_action_never_runs_program(self):
        with mock.patch.object(self.backend,'call') as call:
            with self.assertRaises(ValueError):self.backend.action('shell',self.track,'request-123')
        call.assert_not_called()
    def test_missing_art_kept_only_for_same_track(self):
        image=Image.new('RGB',(240,240),'blue');out=io.BytesIO();image.save(out,'PNG')
        art=base64.b64encode(out.getvalue()).decode()
        with mock.patch.object(self.backend,'call',side_effect=[dict(RAW,artworkData=art),RAW,dict(RAW,title='新歌')]),mock.patch.object(self.backend,'native',return_value={'actions':[]}):
            self.backend.refresh();first=self.backend.snapshot()['artwork_id'];self.assertTrue(first)
            jpeg=self.backend.artworks[first]
            self.assertEqual(Image.open(io.BytesIO(jpeg)).size,(144,144))
            self.backend.refresh();self.assertEqual(self.backend.snapshot()['artwork_id'],first)
            self.backend.refresh();self.assertEqual(self.backend.snapshot()['artwork_id'],'')
    def test_stale_disables_actions_and_freezes_position(self):
        self.backend.raw=RAW;self.backend.updated=time.time()-10;self.backend.capabilities=['next']
        s=self.backend.snapshot();self.assertTrue(s['stale']);self.assertEqual(s['actions'],[]);self.assertFalse(s['playing'])
    def test_request_id_must_not_be_reused_for_other_actions(self):
        with mock.patch.object(self.backend,'call',side_effect=[RAW,None]):self.backend.action('next',self.track,'request-123')
        with self.assertRaises(ValueError):self.backend.action('previous',self.track,'request-123')
    def test_no_accessibility_still_allows_media_transport(self):
        with mock.patch.object(self.backend,'call',return_value=RAW),mock.patch.object(self.backend,'native',return_value={'error':'accessibility required','actions':[]}):
            self.backend.refresh()
        self.assertEqual(self.backend.snapshot()['actions'],['next','previous','toggle'])

class HTTPTests(unittest.TestCase):
    def setUp(self):
        self.backend=MusicBackend();self.server=make_server(self.backend,'a'*40,port=0)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        self.url='http://127.0.0.1:'+str(self.server.server_port)
        self.op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def tearDown(self):self.server.shutdown();self.server.server_close();self.thread.join(2)
    def req(self,path,body=None,headers=None):
        return self.op.open(urllib.request.Request(self.url+path,data=body,headers=headers or {}),timeout=3)
    def test_private_state_requires_auth(self):
        with self.assertRaises(urllib.error.HTTPError) as e:self.req('/api/state')
        self.assertEqual(e.exception.code,401)
        with self.req('/api/state',headers={'Authorization':'Bearer '+'a'*40}) as r:self.assertEqual(r.status,200)
    def test_cookie_session_with_unrelated_localhost_cookie(self):
        with self.req('/session',b'',{'X-Faces-Local':'1'}) as r:cookie=r.headers['Set-Cookie'].split(';')[0]
        with self.req('/api/state',headers={'Cookie':'unrelated=value; '+cookie}) as r:self.assertEqual(r.status,200)
    def test_cross_origin_and_rebinding_rejected(self):
        for h in [{'X-Faces-Local':'1','Origin':'https://other.example'}, {'X-Faces-Local':'1','Host':'evil.example'}]:
            with self.assertRaises(urllib.error.HTTPError) as e:self.req('/session',b'',h)
            self.assertEqual(e.exception.code,403)
    def test_unauthenticated_action_never_dispatches(self):
        with mock.patch.object(self.backend,'action') as action:
            with self.assertRaises(urllib.error.HTTPError):self.req('/api/action',b'{}')
        action.assert_not_called()
    def test_authenticated_action_and_failure_receipt(self):
        with mock.patch.object(self.backend,'action',return_value={'ok':False,'code':'track_changed'}) as action:
            with self.assertRaises(urllib.error.HTTPError) as e:self.req('/api/action',json.dumps({'action':'next','track_id':'t','request_id':'request-123'}).encode(),{'Authorization':'Bearer '+'a'*40})
            self.assertEqual(e.exception.code,409)
            action.assert_called_once_with('next','t','request-123')

if __name__=='__main__':unittest.main()
