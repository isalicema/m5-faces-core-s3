import base64
import io
import json
import subprocess
import tempfile
from pathlib import Path
import threading
import time
import unittest
import urllib.error
import urllib.request
from unittest import mock
from PIL import Image
from music_bridge.playlists import PlaylistError, PlaylistService, artwork_url, download_artwork, resolve_cli
from music_bridge.server import make_server


def row(identity, **kwargs):
    return dict(originalId=identity, id='A'*32, name='歌单'+str(identity),
                coverImgUrl='https://p1.music.126.net/image.jpg', trackCount=12,
                creatorNickName='作者', subed=True, **kwargs)


def page(rows, total=None):
    return {'code':200, 'data':{'recordCount':len(rows) if total is None else total, 'records':rows}}


def wait_loaded(service):
    service.snapshot()
    until=time.monotonic()+2
    while service.loading and time.monotonic()<until: time.sleep(.005)
    assert not service.loading
    return service.snapshot()


class PlaylistTests(unittest.TestCase):
    def service(self, own=None, collected=None):
        runner=mock.Mock(side_effect=lambda args: page(own or []) if args[1]=='created' else page(collected or []))
        service=PlaylistService(runner=runner, downloader=lambda url: b'jpeg')
        return service,runner

    def test_collected_excludes_created_and_deduplicates_preserving_order(self):
        s,runner=self.service([row(2)], [row(3),row(2),row(1),row(3)])
        runner.side_effect=lambda args: page([row(2)]) if args[1]=='created' else page([row(3),row(2),row(1),row(3)],3)
        state=wait_loaded(s)
        self.assertEqual([x['id'] for x in state['items']], ['3','1'])
        self.assertEqual(state['total'],2)
        self.assertNotIn('encrypted', json.dumps(state))
        self.assertNotIn('coverImgUrl', json.dumps(state))
        self.assertEqual([x.args[0][1] for x in runner.call_args_list],['created','collected'])

    def test_unsubscribed_rows_are_excluded(self):
        item=row(1);item['subed']=False
        s,_=self.service(collected=[item])
        self.assertEqual(wait_loaded(s)['total'],0)

    def test_pagination_uses_record_count_including_created_special_extra(self):
        def runner(args):
            if args[1]=='created':return page([row(9999)],0)
            offset=int(args[args.index('--offset')+1])
            return page([row(i+1) for i in range(offset,min(offset+500,501))],501)
        s=PlaylistService(runner,lambda _:b'jpeg')
        self.assertEqual(wait_loaded(s)['total'],501)

    def test_short_pages_advance_by_actual_rows(self):
        offsets=[]
        def runner(args):
            if args[1]=='created':return page([])
            offset=int(args[args.index('--offset')+1]);offsets.append(offset)
            return page([row(i+1) for i in range(offset,min(offset+2,5))],5)
        s=PlaylistService(runner,lambda _:b'jpeg')
        self.assertEqual(wait_loaded(s)['total'],5)
        self.assertEqual(offsets,[0,2,4])

    def test_created_repeated_liked_row_does_not_skip_ordinary_rows(self):
        offsets=[]
        def runner(args):
            if args[1]=='collected':return page([row(1),row(2),row(3),row(4)])
            offset=int(args[args.index('--offset')+1]);offsets.append(offset)
            return page([row(offset+1), row(99,specialType=5)],4)
        s=PlaylistService(runner,lambda _:b'jpeg')
        self.assertEqual([x['id'] for x in wait_loaded(s)['items']],['4'])
        self.assertEqual(offsets,[0,1,2])

    def test_repeated_page_and_changed_total_fail_closed(self):
        for changed in [False,True]:
            def runner(args):
                if args[1]=='created':return page([])
                offset=int(args[args.index('--offset')+1])
                return page([row(1)],3 if changed and offset else 2)
            state=wait_loaded(PlaylistService(runner))
            self.assertFalse(state['ok']);self.assertFalse(state['loading'])

    def test_last_page_duplicates_do_not_fake_complete_library(self):
        def runner(args):
            return page([]) if args[1]=='created' else page([row(1),row(2)],4)
        self.assertFalse(wait_loaded(PlaylistService(runner))['ok'])

    def test_loading_never_blocks_http_poll(self):
        started,release=threading.Event(),threading.Event()
        def runner(args): started.set();release.wait(2);return page([])
        s=PlaylistService(runner)
        before=time.monotonic();state=s.snapshot()
        self.assertLess(time.monotonic()-before,.2)
        self.assertTrue(state['loading']);self.assertFalse(state['ok'])
        self.assertTrue(started.wait(1));release.set()
        self.assertEqual(wait_loaded(s)['total'],0)

    def test_created_failure_never_exposes_unfiltered_collected(self):
        s=PlaylistService(lambda _: {'code':403})
        state=wait_loaded(s)
        self.assertFalse(state['ok']);self.assertFalse(state['loading'])
        self.assertNotIn('items',state)

    def test_malformed_collection_fails_closed_and_can_refresh(self):
        s,runner=self.service(collected=[row(1)])
        wait_loaded(s)
        runner.side_effect=lambda _: {'code':200,'data':{'records':[], 'recordCount':'secret'}}
        s.snapshot(refresh=True)
        state=wait_loaded(s)
        self.assertTrue(state['stale']);self.assertEqual(state['total'],1)
        self.assertNotIn('secret',json.dumps(state))
        self.assertFalse(s.play('1','request-001')['ok'])
        runner.side_effect=lambda _:page([])
        s.snapshot(refresh=True)
        state=wait_loaded(s)
        self.assertFalse(state['stale']);self.assertEqual(state['total'],0)

    def test_play_command_membership_and_dedup(self):
        s,runner=self.service(collected=[row(1)])
        wait_loaded(s);runner.reset_mock();runner.side_effect=None;runner.return_value={'success':True}
        self.assertFalse(s.play('2','request-001')['ok']);runner.assert_not_called()
        receipt=s.play('1','request-001')
        self.assertTrue(receipt['ok']);self.assertEqual(s.play('1','request-001'),receipt)
        runner.assert_called_once_with(['play','--playlist','--encrypted-id','A'*32,
                                       '--original-id','1','--player','orpheus','--output','json'])
        self.assertFalse(s.play('2','request-001')['ok'])

    def test_pending_and_timeout_are_never_replayed(self):
        s,runner=self.service(collected=[row(1)])
        wait_loaded(s)
        entered,release=threading.Event(),threading.Event()
        def play(args):
            entered.set();release.wait(2)
            raise PlaylistError('播放结果未确认')
        runner.reset_mock();runner.side_effect=play
        worker=threading.Thread(target=s.play,args=('1','request-001'));worker.start()
        self.assertTrue(entered.wait(1))
        self.assertTrue(s.play('1','request-001')['pending'])
        self.assertFalse(s.play('1','request-002')['ok'])
        release.set();worker.join(2)
        self.assertFalse(s.play('1','request-001')['ok']);self.assertEqual(runner.call_count,1)

    def test_shell_is_not_used_and_error_output_not_exposed(self):
        s=PlaylistService()
        with mock.patch('music_bridge.playlists.resolve_cli',return_value='/tmp/node/bin/ncm-cli'), mock.patch('music_bridge.playlists.subprocess.run') as run:
            run.return_value=subprocess.CompletedProcess([],1, 'bad SECRET', 'PRIVATE')
            with self.assertRaises(PlaylistError) as caught:s._run(['playlist','collected'])
            self.assertNotIn('SECRET',str(caught.exception));self.assertNotIn('PRIVATE',str(caught.exception))
            self.assertEqual(run.call_args.args[0],['/tmp/node/bin/ncm-cli','playlist','collected'])
            self.assertNotIn('shell',run.call_args.kwargs)
            self.assertEqual(run.call_args.kwargs['timeout'],25)
            self.assertTrue(run.call_args.kwargs['env']['PATH'].startswith('/tmp/node/bin:'))

    def test_cli_failures_distinguish_session_network_and_permissions(self):
        cases = [
            (0, '{"code":301}', '', '登录已失效'),
            (1, '{"code":401,"message":"用户未登录"}', '', '登录已失效'),
            (1, 'not logged in', '', '登录已失效'),
            (1, '', '请先登录', '登录已失效'),
            (1, '{"code":401,"message":"invalid app signature"}', '', '权限异常'),
            (1, '{"code":403}', '', '权限异常'),
            (0, '{"code":503,"message":"login service unavailable"}', '', '暂时失败'),
            (1, '登录刷新失败：fetch failed，请运行 ncm-cli login', '', '暂时失败'),
            (1, '', 'ENOTFOUND login.music.163.com PRIVATE', '暂时失败'),
            (1, 'usage: ncm-cli login', '', '暂时失败'),
            (0, '[]', '', '暂时失败'),
        ]
        service = PlaylistService()
        with mock.patch('music_bridge.playlists.resolve_cli', return_value='/tmp/bin/ncm-cli'), mock.patch('music_bridge.playlists.subprocess.run') as run:
            for code, stdout, stderr, expected in cases:
                with self.subTest(stdout=stdout, stderr=stderr):
                    run.return_value = subprocess.CompletedProcess([], code, stdout, stderr)
                    with self.assertRaises(PlaylistError) as caught:
                        service._run(['playlist', 'collected'])
                    self.assertIn(expected, str(caught.exception))
                    self.assertNotIn('PRIVATE', str(caught.exception))
                    if expected != '登录已失效':
                        self.assertNotIn('ncm-cli login', str(caught.exception))

    def test_play_network_failure_keeps_unknown_outcome_and_never_retries(self):
        with mock.patch('music_bridge.playlists.resolve_cli', return_value='/tmp/bin/ncm-cli'), mock.patch('music_bridge.playlists.subprocess.run') as run:
            run.return_value = subprocess.CompletedProcess([], 1, '', 'login refresh failed: timeout')
            with self.assertRaisesRegex(PlaylistError, '播放结果未确认.*不会自动重试'):
                PlaylistService()._run(['play', '--playlist'])
            run.assert_called_once()

    def test_temporary_cli_failure_then_manual_refresh_recovers(self):
        with mock.patch('music_bridge.playlists.resolve_cli', return_value='/tmp/bin/ncm-cli'), mock.patch('music_bridge.playlists.subprocess.run') as run:
            run.return_value = subprocess.CompletedProcess([], 1, '', 'login refresh: network error')
            service = PlaylistService()
            state = wait_loaded(service)
            self.assertIn('暂时失败', state['message'])
            run.return_value = subprocess.CompletedProcess([], 0, json.dumps(page([])), '')
            service.snapshot(refresh=True)
            state = wait_loaded(service)
            self.assertTrue(state['ok'])
            self.assertFalse(state.get('message'))

    def test_cli_orpheus_playlist_empty_stdout_receipt(self):
        # Installed CLI 0.1.7 playlist branch omits its JSON formatter. The
        # launch method emits exactly this stderr line, then awaits macOS open.
        payload={'cmd':'play','type':'playlist','id':'123','channel':'ncmcli'}
        encoded=base64.b64encode(json.dumps(payload,separators=(',',':')).encode()).decode()
        args=['play','--playlist','--encrypted-id','A'*32,'--original-id','123',
              '--player','orpheus','--output','json']
        s=PlaylistService()
        with mock.patch('music_bridge.playlists.resolve_cli',return_value='/tmp/bin/ncm-cli'), mock.patch('music_bridge.playlists.subprocess.run') as run:
            run.return_value=subprocess.CompletedProcess([],0,'','[orpheus] orpheus://'+encoded+'\n')
            self.assertEqual(s._run(args),{'success':True,'receipt':'orpheus-launch'})

    def test_orpheus_receipt_rejects_failures_mismatch_or_additional_logs(self):
        args=['play','--playlist','--encrypted-id','A'*32,'--original-id','123',
              '--player','orpheus','--output','json']
        payload={'cmd':'play','type':'playlist','id':'123','channel':'ncmcli'}
        def receipt(value):
            return '[orpheus] orpheus://'+base64.b64encode(json.dumps(value).encode()).decode()+'\n'
        valid=receipt(payload)
        cases=[(1,'',valid,args), (0,'', '',args), (0,'',valid+'warning PRIVATE',args),
               (0,'','warning PRIVATE\n'+valid,args), (0,'PRIVATE',valid,args),
               (0,'',receipt(dict(payload,id='456')),args),
               (0,'',receipt(dict(payload,type='song')),args),
               (0,'',receipt(dict(payload,cmd='pause')),args),
               (0,'',receipt(dict(payload,channel='another')),args),
               (0,'','[orpheus] orpheus://not-valid',args),
               (0,'',valid,[x if x!='orpheus' else 'mpv' for x in args]),
               (0,'',valid,[x if x!='--playlist' else '--song' for x in args])]
        s=PlaylistService()
        with mock.patch('music_bridge.playlists.resolve_cli',return_value='/tmp/bin/ncm-cli'), mock.patch('music_bridge.playlists.subprocess.run') as run:
            for code,stdout,stderr,command in cases:
                with self.subTest(code=code,stdout=bool(stdout),stderr=stderr[:20],command=command[1]):
                    run.return_value=subprocess.CompletedProcess([],code,stdout,stderr)
                    with self.assertRaises(PlaylistError) as caught:s._run(command)
                    self.assertNotIn('PRIVATE',str(caught.exception))
            # Explicit JSON success still works; explicit failure cannot be
            # upgraded by a matching stderr URL.
            run.return_value=subprocess.CompletedProcess([],0,'{"success":false}',valid)
            self.assertFalse(s._run(args)['success'])
            run.return_value=subprocess.CompletedProcess([],0,'{"success":true}',valid)
            self.assertTrue(s._run(args)['success'])

    def test_launchagent_short_path_resolves_nvm_and_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp)
            old=home/'.nvm/versions/node/v9.0.0/bin/ncm-cli'
            new=home/'.nvm/versions/node/v24.19.0/bin/ncm-cli'
            for path in [old,new]:
                path.parent.mkdir(parents=True);path.write_text('#!/usr/bin/env node');path.chmod(0o700)
            with mock.patch('music_bridge.playlists.Path.home',return_value=home), mock.patch.dict('os.environ',{'PATH':'/usr/bin:/bin','FACES_NCM_CLI':''}):
                self.assertEqual(resolve_cli(),str(new))
                with mock.patch.dict('os.environ',{'FACES_NCM_CLI':str(old)}):self.assertEqual(resolve_cli(),str(old))
                with mock.patch.dict('os.environ',{'FACES_NCM_CLI':'bad --arg'}):
                    with self.assertRaises(PlaylistError):resolve_cli()

    def test_cover_url_is_constrained_and_https(self):
        self.assertEqual(artwork_url('http://p1.music.126.net/a.jpg?foo=bar'),'https://p1.music.126.net/a.jpg?param=160y160')
        for url in ['http://127.0.0.1/a','file:///tmp/key','https://p1.music.126.net.evil.com/a',
                    'https://user@p1.music.126.net/a','https://p1.music.126.net:8888/a']:
            self.assertIsNone(artwork_url(url))

    def test_download_resizes_to_baseline_jpeg_and_refuses_redirect(self):
        source=io.BytesIO();Image.new('RGBA',(200,100)).save(source,format='PNG')
        response=mock.MagicMock();response.__enter__.return_value.read.return_value=source.getvalue()
        opener=mock.Mock();opener.open.return_value=response
        with mock.patch('music_bridge.playlists.urllib.request.build_opener',return_value=opener) as build:
            data=download_artwork('https://p1.music.126.net/a.jpg')
        im=Image.open(io.BytesIO(data))
        self.assertEqual(im.size,(160,160));self.assertEqual(im.format,'JPEG');self.assertNotIn('progressive',im.info)
        redirect=build.call_args.args[0]
        self.assertIsNone(redirect.redirect_request(None,None,302,'',{},'http://127.0.0.1/'))

    def test_cover_cache_bounded_and_unknown_id_cannot_fetch(self):
        s=PlaylistService(downloader=lambda _:b'jpeg')
        s.resources={str(i):{'cover':'https://p1.music.126.net/'+str(i)} for i in range(80)}
        for i in range(80):s._artwork(str(i),s.resources[str(i)]['cover'])
        self.assertEqual(len(s.covers),64)
        self.assertEqual(s.cover_bytes,64*4)
        self.assertIsNone(s.artwork('unknown'));self.assertEqual(len(s.cover_pending),0)


    def test_cover_cache_total_bytes_and_replacement_accounting(self):
        s=PlaylistService(downloader=lambda _:b'x'*40000)
        s.resources={str(i):{'cover':'https://p1.music.126.net/'+str(i)} for i in range(64)}
        for i in range(64):s._artwork(str(i),s.resources[str(i)]['cover'])
        self.assertLessEqual(s.cover_bytes,s.COVER_CACHE_BYTES)
        self.assertEqual(s.cover_bytes,sum(map(len,s.covers.values())))
        self.assertEqual(len(s.covers),s.COVER_CACHE_BYTES//40000)
        s.downloader=lambda _:b'small'
        s._artwork('63',s.resources['63']['cover'])
        self.assertEqual(s.cover_bytes,sum(map(len,s.covers.values())))
        self.assertGreaterEqual(s.cover_bytes,0)

    def test_oversize_cover_not_cached_or_retried_immediately(self):
        download=mock.Mock(return_value=b'x'*(PlaylistService.COVER_CACHE_BYTES+1))
        s=PlaylistService(downloader=download)
        s.resources={'1':{'cover':'https://p1.music.126.net/1'}}
        s._artwork('1',s.resources['1']['cover'])
        self.assertEqual(s.cover_bytes,0);self.assertEqual(len(s.covers),0)
        self.assertIsNone(s.artwork('1'));download.assert_called_once()
        self.assertEqual(len(s.cover_pending),0)

    def test_sixty_cover_return_trip_never_redownloads(self):
        download=mock.Mock(return_value=b'jpeg')
        s=PlaylistService(downloader=download)
        s.resources={str(i):{'cover':'https://p1.music.126.net/'+str(i)} for i in range(60)}
        for i in range(60):s._artwork(str(i),s.resources[str(i)]['cover'])
        for i in list(reversed(range(60)))+list(range(60)):
            self.assertEqual(s.artwork(str(i)),b'jpeg')
        self.assertEqual(download.call_count,60)
        self.assertEqual(s.cover_bytes,240)

    def test_library_refresh_preserves_only_unchanged_cover_urls(self):
        s,runner=self.service(collected=[row(i) for i in range(1,6)])
        wait_loaded(s)
        # Avoid racing the initial three background prefetches in this test.
        until=time.monotonic()+2
        while s.cover_pending and time.monotonic()<until:time.sleep(.005)
        for i in range(1,6):s._artwork(str(i),s.resources[str(i)]['cover'])
        changed=row(4);changed['coverImgUrl']='https://p1.music.126.net/changed.jpg'
        runner.side_effect=lambda args:page([]) if args[1]=='created' else page([row(1),row(2),row(3),changed])
        with mock.patch.object(s,'artwork'):
            s._load()
        self.assertEqual(set(s.covers),{'1','2','3'})
        self.assertEqual(s.cover_bytes,12)

    def test_late_cover_cannot_replace_new_url_after_refresh(self):
        entered,release=threading.Event(),threading.Event()
        def download(url):entered.set();release.wait(2);return b'old-image'
        s=PlaylistService(downloader=download)
        old='https://p1.music.126.net/old.jpg'
        s.resources={'1':{'cover':old}}
        self.assertIsNone(s.artwork('1'));self.assertTrue(entered.wait(1))
        with s.lock:s.resources['1']['cover']='https://p1.music.126.net/new.jpg'
        release.set()
        until=time.monotonic()+2
        while s.cover_pending and time.monotonic()<until:time.sleep(.005)
        self.assertFalse(s.cover_pending);self.assertNotIn('1',s.covers)
        self.assertEqual(s.cover_bytes,0)

    def test_three_download_slots_do_not_block_get_or_spawn_more(self):
        release=threading.Event();entered=[];guard=threading.Lock()
        def download(url):
            with guard:entered.append(url)
            release.wait(2);return b'jpeg'
        s=PlaylistService(downloader=download)
        s.resources={str(i):{'cover':'https://p1.music.126.net/'+str(i)} for i in range(8)}
        started=time.monotonic()
        for i in range(8):self.assertIsNone(s.artwork(str(i)))
        self.assertLess(time.monotonic()-started,.2)
        self.assertEqual(len(s.cover_pending),3)
        release.set()
        until=time.monotonic()+2
        while s.cover_pending and time.monotonic()<until:time.sleep(.005)
        self.assertFalse(s.cover_pending);self.assertEqual(len(entered),3)


class PlaylistServerTests(unittest.TestCase):
    def setUp(self):
        self.service=mock.Mock()
        self.service.snapshot.return_value={'ok':True,'items':[],'total':0}
        self.service.artwork.return_value=b'jpeg'
        self.service.play.return_value={'ok':True,'message':'已请求播放'}
        self.server=make_server(mock.Mock(), 'a'*32, port=0, playlists=self.service)
        self.worker=threading.Thread(target=self.server.serve_forever,daemon=True);self.worker.start()
        self.url='http://127.0.0.1:'+str(self.server.server_port)
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.worker.join(2)
    def request(self,path,data=None,auth=True,origin=None):
        headers={'Authorization':'Bearer '+'a'*32} if auth else {}
        if origin:headers['Origin']=origin
        body=json.dumps(data).encode() if data is not None else None
        return urllib.request.urlopen(urllib.request.Request(self.url+path,data=body,headers=headers),timeout=2)
    def test_all_shelf_routes_require_auth(self):
        for path,data in [('/api/playlists',None),('/api/playlist-artwork/1',None),('/api/playlists/play',{'id':'1'})]:
            with self.assertRaises(urllib.error.HTTPError) as caught:self.request(path,data,auth=False)
            self.assertEqual(caught.exception.code,401)
        self.service.snapshot.assert_not_called();self.service.play.assert_not_called();self.service.artwork.assert_not_called()
    def test_refresh_artwork_play_and_origin(self):
        with self.request('/api/playlists?refresh=1') as r:self.assertEqual(json.load(r)['total'],0)
        self.service.snapshot.assert_called_once_with(refresh=True)
        with self.request('/api/playlist-artwork/1') as r:self.assertEqual(r.headers['Content-Type'],'image/jpeg')
        with self.request('/api/playlists/play',{'id':'1','request_id':'request-001'}) as r:self.assertTrue(json.load(r)['ok'])
        self.service.play.assert_called_once_with('1','request-001')
        with self.assertRaises(urllib.error.HTTPError) as caught:self.request('/api/playlists/play',{},origin='http://evil.example')
        self.assertEqual(caught.exception.code,403)

if __name__=='__main__':unittest.main()
