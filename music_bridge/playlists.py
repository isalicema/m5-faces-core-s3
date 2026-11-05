"""Read-only collected-playlist shelf and explicit client playback via official CLI.

Nothing is loaded at server startup. Account data stays in memory; UI requests
never wait for collection or artwork downloads. Playback is never retried.
"""
import base64
import binascii
import io
import json
import os
import re
import shutil
import subprocess
import threading
import time
import urllib.request
from collections import OrderedDict
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit, urlencode

from PIL import Image, ImageOps


class PlaylistError(Exception):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def artwork_url(url):
    """Only the CDN addresses supplied by the validated collection can be used."""
    if not isinstance(url, str):
        return None
    parts = urlsplit(url)
    if (parts.scheme not in ('https', 'http') or parts.username or parts.password
            or parts.port not in (None, 80, 443)
            or not re.fullmatch(r'p[0-9]+\.music\.126\.net', parts.hostname or '')
            or not parts.path.startswith('/') or len(url) > 2048):
        return None
    return urlunsplit(('https', parts.hostname, parts.path, urlencode({'param': '160y160'}), ''))


def download_artwork(url):
    opener = urllib.request.build_opener(NoRedirect())
    request = urllib.request.Request(url, headers={'User-Agent': 'FacesMusic/1.0'})
    with opener.open(request, timeout=5) as response:
        raw = response.read(2 * 1024 * 1024 + 1)
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError('artwork too large')
    with Image.open(io.BytesIO(raw)) as image:
        if image.width * image.height > 4_000_000:
            raise ValueError('artwork dimensions too large')
        image = ImageOps.fit(image.convert('RGB'), (160, 160))
        output = io.BytesIO()
        image.save(output, format='JPEG', quality=85, progressive=False)
        return output.getvalue()


def resolve_cli():
    override = os.environ.get('FACES_NCM_CLI')
    if override:
        candidate = Path(override).expanduser()
        if candidate.is_absolute() and candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
        raise PlaylistError('FACES_NCM_CLI 路径无效，请在 Mac 检查配置')
    path = shutil.which('ncm-cli')
    if path:
        return path
    # LaunchAgents do not inherit the interactive shell's nvm PATH.
    candidates = list((Path.home() / '.nvm/versions/node').glob('*/bin/ncm-cli'))
    candidates.sort(key=lambda p: tuple(int(x) for x in re.findall(r'\d+', p.parts[-3])), reverse=True)
    for candidate in candidates:
        if os.access(candidate, os.X_OK):
            return str(candidate)
    raise PlaylistError('请在 Mac 安装官方 ncm-cli，再刷新歌单')


def orpheus_playlist_receipt(args, result):
    """Recognize the exact non-JSON receipt emitted by ncm-cli 0.1.7.

    Its orpheus playlist branch calls playPlaylist and returns without the
    normal JSON success formatter. launch() writes one URL to stderr and
    invokes macOS open. Only a clean exit plus this matching command counts
    as a dispatch receipt; it says nothing about actual audible playback.
    """
    if result.returncode != 0 or result.stdout.strip() or not args or args[0] != 'play':
        return None
    try:
        identity = args[args.index('--original-id') + 1]
        player = args[args.index('--player') + 1]
    except (ValueError, IndexError):
        return None
    if ('--playlist' not in args or '--song' in args or player != 'orpheus'
            or not re.fullmatch(r'[1-9][0-9]{0,19}', identity)):
        return None
    if len(result.stderr) > 4096:
        return None
    match = re.fullmatch(r'\[orpheus\] orpheus://([A-Za-z0-9+/]+={0,2})', result.stderr.strip())
    if not match:
        return None
    try:
        payload = json.loads(base64.b64decode(match[1], validate=True).decode('utf-8'))
    except (ValueError, UnicodeError, binascii.Error):
        return None
    expected = {'cmd': 'play', 'type': 'playlist', 'id': identity, 'channel': 'ncmcli'}
    if payload != expected:
        return None
    return {'success': True, 'receipt': 'orpheus-launch'}


class PlaylistService:
    COVER_CACHE_LIMIT = 64
    COVER_CACHE_BYTES = 2 * 1024 * 1024

    def __init__(self, runner=None, downloader=None):
        self.runner = runner or self._run
        self.downloader = downloader or download_artwork
        self.lock = threading.RLock()
        self.items = None
        self.resources = {}
        self.loading = False
        self.started = False
        self.error = ''
        self.covers = OrderedDict()
        self.cover_bytes = 0
        self.cover_pending = set()
        self.cover_failed = {}
        self.requests = OrderedDict()
        self.playing = False

    def _run(self, args):
        executable = resolve_cli()
        env = os.environ.copy()
        env['PATH'] = str(Path(executable).parent) + os.pathsep + env.get('PATH', '/usr/bin:/bin')
        try:
            result = subprocess.run([executable] + args, capture_output=True, text=True,
                                    timeout=10 if args[0] == 'play' else 25, env=env)
        except subprocess.TimeoutExpired:
            if args[0] == 'play':
                raise PlaylistError('播放结果未确认，请查看 Mac 网易云；不会自动重试')
            raise PlaylistError('读取超时，请检查 Mac 网络后刷新')
        except OSError:
            raise PlaylistError('无法运行 ncm-cli，请检查 Mac 上的安装')
        if len(result.stdout) > 8 * 1024 * 1024:
            raise PlaylistError('歌单数据过大，请在 Mac 检查账号')
        receipt = orpheus_playlist_receipt(args, result)
        if receipt is not None:
            return receipt
        try:
            value = json.loads(result.stdout)
        except (ValueError, TypeError):
            combined = result.stdout + result.stderr
            if '登录' in combined or 'login' in combined.lower():
                raise PlaylistError('请在 Mac 运行 ncm-cli login，登录后刷新')
            if args[0] == 'play':
                raise PlaylistError('播放结果未确认，请查看 Mac 网易云；不会自动重试')
            raise PlaylistError('网易云 CLI 暂时不可用，请在 Mac 检查网络和客户端')
        if not isinstance(value, dict):
            raise PlaylistError('网易云返回异常，请刷新重试')
        if result.returncode != 0 or value.get('code') in (401, 403, 301):
            if value.get('code') in (401, 403, 301):
                raise PlaylistError('请在 Mac 运行 ncm-cli login，登录后刷新')
            raise PlaylistError('网易云请求失败，请在 Mac 检查网络和登录')
        return value

    def _list(self, kind):
        records = []
        offset = 0
        seen = set()
        expected_total = None
        while offset < 10000:
            result = self.runner(['playlist', kind, '--limit', '500', '--offset', str(offset),
                                  '--output', 'json', '--userInput', '读取本人收藏的歌单用于Faces唱片架，排除本人创建的歌单'])
            if not isinstance(result, dict) or result.get('code') != 200:
                raise PlaylistError('无法读取收藏歌单，请在 Mac 检查登录后刷新')
            data = result.get('data')
            if not isinstance(data, dict) or not isinstance(data.get('records'), list):
                raise PlaylistError('歌单数据异常，请刷新重试')
            total = data.get('recordCount')
            if type(total) is not int or not 0 <= total <= 10000:
                raise PlaylistError('歌单数量异常，请在 Mac 检查账号')
            page = data['records']
            if len(page) > 501 or any(not isinstance(x, dict) for x in page):
                raise PlaylistError('歌单数据异常，请刷新重试')
            if expected_total is not None and total != expected_total:
                raise PlaylistError('歌单列表正在变化，请刷新重试')
            expected_total = total
            ids = {self._id(x) for x in page}
            # created repeats the special "liked songs" row on every page,
            # outside limit/offset, but includes it in recordCount (CLI 0.1.7).
            ordinary = [x for x in page if not (kind == 'created' and x.get('specialType') == 5)]
            progress = len(ordinary)
            new_ids = ids - seen
            records.extend(page)
            seen.update(ids)
            offset += progress
            if page and not new_ids:
                raise PlaylistError('歌单列表重复，请刷新重试')
            if len(seen) >= total:
                return records
            if offset >= total or not progress:
                raise PlaylistError('歌单列表不完整，请刷新重试')
        raise PlaylistError('歌单数量超出本版支持范围')

    @staticmethod
    def _id(record):
        value = record.get('originalId')
        if isinstance(value, bool) or not re.fullmatch(r'[1-9][0-9]{0,19}', str(value)):
            raise PlaylistError('歌单编号异常，请刷新重试')
        return str(value)

    def _load(self):
        try:
            own = {self._id(x) for x in self._list('created')}
            collected = self._list('collected')
            items, resources = [], {}
            for record in collected:
                identity = self._id(record)
                if identity in own or identity in resources or record.get('subed') is False:
                    continue
                encrypted = record.get('id')
                if not isinstance(encrypted, str) or not re.fullmatch(r'[0-9a-fA-F]{32}', encrypted):
                    raise PlaylistError('歌单播放编号异常，请刷新重试')
                title = record.get('name')
                if not isinstance(title, str) or not title.strip():
                    raise PlaylistError('歌单名称异常，请刷新重试')
                count = record.get('trackCount')
                count = count if type(count) is int and count >= 0 else 0
                try:
                    cover = artwork_url(record.get('coverImgUrl'))
                except ValueError:
                    cover = None
                resources[identity] = {'encrypted': encrypted, 'cover': cover}
                items.append({'id': identity, 'title': title[:300],
                              'creator': str(record.get('creatorNickName') or '')[:100],
                              'track_count': count, 'artwork': '/api/playlist-artwork/' + identity})
            with self.lock:
                # A library refresh need not invalidate unchanged CDN images.
                # Keep LRU order; changed/removed URLs and late downloads from
                # the old library cannot populate the new cache.
                retained = OrderedDict((identity, data) for identity, data in self.covers.items()
                                       if identity in resources and
                                       self.resources.get(identity, {}).get('cover') == resources[identity]['cover'])
                self.items, self.resources, self.error = items, resources, ''
                self.covers = retained
                self.cover_bytes = sum(len(data) for data in retained.values())
                self.cover_failed.clear()
        except PlaylistError as error:
            with self.lock:
                self.error = str(error)
        except Exception:
            with self.lock:
                self.error = '读取歌单失败，请在 Mac 检查网易云后刷新'
        finally:
            with self.lock:
                self.loading = False
        with self.lock:
            first = [x['id'] for x in (self.items or [])[:3]]
        for identity in first:
            self.artwork(identity)

    def snapshot(self, refresh=False):
        with self.lock:
            if (refresh or not self.started) and not self.loading:
                self.started = self.loading = True
                self.error = ''
                threading.Thread(target=self._load, daemon=True).start()
            if self.items is None:
                return {'ok': False, 'loading': self.loading,
                        'message': '正在读取收藏歌单' if self.loading else self.error}
            return {'ok': True, 'loading': self.loading, 'stale': bool(self.error),
                    'message': self.error or ('正在刷新收藏歌单' if self.loading else ''),
                    'items': list(self.items), 'total': len(self.items)}

    def artwork(self, identity):
        with self.lock:
            if identity in self.covers:
                self.covers.move_to_end(identity)
                return self.covers[identity]
            url = self.resources.get(identity, {}).get('cover')
            if (not url or identity in self.cover_pending or len(self.cover_pending) >= 3
                    or time.monotonic() - self.cover_failed.get(identity, -100) < 30):
                return None
            self.cover_pending.add(identity)
            threading.Thread(target=self._artwork, args=(identity, url), daemon=True).start()
        return None

    def _artwork(self, identity, url):
        try:
            data = self.downloader(url)
            if not isinstance(data, bytes) or not 0 < len(data) <= self.COVER_CACHE_BYTES:
                raise ValueError('artwork cache entry too large or invalid')
            with self.lock:
                if self.resources.get(identity, {}).get('cover') == url:
                    self.cover_bytes -= len(self.covers.get(identity, b''))
                    self.covers[identity] = data
                    self.cover_bytes += len(data)
                    self.covers.move_to_end(identity)
                    while (len(self.covers) > self.COVER_CACHE_LIMIT or
                           self.cover_bytes > self.COVER_CACHE_BYTES):
                        _, evicted = self.covers.popitem(last=False)
                        self.cover_bytes -= len(evicted)
        except Exception:
            with self.lock:
                self.cover_failed[identity] = time.monotonic()
        finally:
            with self.lock:
                self.cover_pending.discard(identity)

    def play(self, identity, request_id):
        if (not isinstance(identity, str) or not isinstance(request_id, str)
                or not re.fullmatch(r'[A-Za-z0-9_-]{8,80}', request_id)):
            return {'ok': False, 'message': '播放请求无效'}
        with self.lock:
            if request_id in self.requests:
                saved_id, result = self.requests[request_id]
                return dict(result) if saved_id == identity else {'ok': False, 'message': '播放请求编号冲突'}
            resource = self.resources.get(identity)
            if not resource or self.error or self.loading:
                return {'ok': False, 'message': '请先刷新收藏歌单，再选择播放'}
            if self.playing:
                return {'ok': False, 'message': '正在请求播放，请稍候'}
            self.playing = True
            self.requests[request_id] = (identity, {'ok': False, 'pending': True, 'message': '正在请求播放'})
            while len(self.requests) > 128:
                self.requests.popitem(last=False)
        try:
            result = self.runner(['play', '--playlist', '--encrypted-id', resource['encrypted'],
                                  '--original-id', identity, '--player', 'orpheus', '--output', 'json'])
            receipt = {'ok': True, 'message': '已请求播放'} if result.get('success') is True else {
                'ok': False, 'message': '未能请求播放，请检查 Mac 网易云客户端'}
        except PlaylistError as error:
            receipt = {'ok': False, 'message': str(error)}
        except Exception:
            receipt = {'ok': False, 'message': '播放结果未确认，请查看 Mac 网易云；不会自动重试'}
        finally:
            with self.lock:
                self.playing = False
                self.requests[request_id] = (identity, receipt)
        return dict(receipt)
