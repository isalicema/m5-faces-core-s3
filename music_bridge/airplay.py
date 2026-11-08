"""Single-flight system output routing, independent of playback metadata."""
import json
import os
import subprocess
import threading
from pathlib import Path
from .backend import HELPER, ROOT


class AirPlayService:
    def __init__(self, directory=None, helper=HELPER, runner=subprocess.run):
        self.directory = Path(directory or ROOT / '.local')
        self.helper, self.runner = str(helper), runner
        self.lock = threading.Lock()

    def call(self, *args):
        result = self.runner([self.helper, '--audio', *args], capture_output=True,
                             text=True, timeout=4 if args[0] == "status" else 35, check=False)
        data = json.loads(result.stdout)
        if not isinstance(data, dict): raise ValueError('invalid helper response')
        return data

    def save(self, state):
        self.directory.mkdir(parents=True, exist_ok=True)
        target = self.directory / 'airplay-state.json'
        temporary = target.with_suffix('.tmp')
        fd = os.open(temporary, os.O_CREAT | os.O_TRUNC | os.O_WRONLY, 0o600)
        with os.fdopen(fd, 'w') as f:
            json.dump(state, f); f.flush(); os.fsync(f.fileno())
        os.replace(temporary, target)

    def toggle(self, request_id):
        if not isinstance(request_id, str) or not 8 <= len(request_id) <= 80:
            raise ValueError('invalid request_id')
        if not self.lock.acquire(blocking=False):
            return {'ok': False, 'code': 'busy', 'error': '正在切换声音，请稍候'}
        try:
            try:
                state = json.loads((self.directory / 'airplay-state.json').read_text())
            except FileNotFoundError:
                state = {'results': {}}
            results = state['results']
            if request_id in results: return results[request_id]
            try:
                target = json.loads((self.directory / 'airplay.json').read_text())['target']
            except FileNotFoundError:
                return {'ok': False, 'code': 'unconfigured', 'error': '请先在 Mac 配置投放设备'}
            if not isinstance(target, str) or not target.strip(): raise ValueError('invalid target')
            status = self.call('status')
            if not status.get('ok'):
                return {'ok': False, 'code': 'unavailable', 'error': '暂时读不到 Mac 声音输出'}
            output = status['output']
            restoring = output['airplay']
            if restoring:
                if not state.get('previous_uid') or state.get('active_uid') != output['uid']:
                    return {'ok': False, 'code': 'external_route', 'error': '声音输出已变化，请在 Mac 检查'}
                command = ('restore', state['previous_uid'])
            else:
                state['previous_uid'] = output['uid']
                command = ('select', target)
            uncertain = {'ok': False, 'code': 'uncertain', 'error': '投放结果未知，请先检查 Mac'}
            results[request_id] = uncertain
            # Persist BEFORE mutation: replay after restart must not toggle a second time.
            while len(results) > 256: del results[next(iter(results))]
            self.save(state)
            try:
                reply = self.call(*command)
            except (OSError, ValueError, subprocess.SubprocessError):
                return uncertain
            if reply.get('ok'):
                state['active_uid'] = None if restoring else reply['output']['uid']
                if restoring: state['previous_uid'] = None
                result = {'ok': True, 'route': 'local' if restoring else 'airplay'}
            else:
                code = reply.get('code', 'uncertain')
                messages = {
                    'accessibility_required': '请在 Mac 检查辅助功能权限',
                    'not_found': '未找到投放设备，请检查 Mac',
                    'restore_unavailable': '原输出设备不可用，请检查 Mac',
                    'already_airplay': '声音输出已变化，请在 Mac 检查',
                    'output_changed': '声音输出已变化，请在 Mac 检查',
                }
                result = {'ok': False, 'code': code, 'error': messages.get(code, uncertain['error'])}
            results[request_id] = result
            self.save(state)
            return result
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError):
            return {'ok': False, 'code': 'unavailable', 'error': '投放暂时不可用，请检查 Mac'}
        finally:
            self.lock.release()
