import json
import subprocess
import tempfile
import unittest
import threading
import urllib.request
import urllib.error
from pathlib import Path
from types import SimpleNamespace
from music_bridge.airplay import AirPlayService
from music_bridge.server import make_server
from unittest.mock import Mock


class AirPlayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        (self.path / 'airplay.json').write_text(json.dumps({'target': 'Test Speaker'}))
        self.output = {'uid': 'local-uid', 'name': 'Local', 'airplay': False}
        self.mutations = []
        self.fail = None

    def runner(self, args, **kwargs):
        command = args[2]
        if command == 'status':
            reply = {'ok': True, 'output': dict(self.output)}
        else:
            self.mutations.append(command)
            if self.fail: raise self.fail
            self.output = ({'uid': 'airplay-uid', 'name': 'AirPlay', 'airplay': True}
                           if command == 'select' else {'uid': args[3], 'name': 'Local', 'airplay': False})
            reply = {'ok': True, 'output': dict(self.output)}
        return SimpleNamespace(stdout=json.dumps(reply), returncode=0)

    def service(self):
        return AirPlayService(self.path, helper='/helper', runner=self.runner)

    def test_restart_replay_and_restore_original_uid(self):
        first = self.service().toggle('request-0001')
        self.assertEqual(first['route'], 'airplay')
        self.assertEqual(self.service().toggle('request-0001'), first)
        self.assertEqual(self.mutations, ['select'])
        self.assertEqual(self.service().toggle('request-0002')['route'], 'local')
        self.assertEqual(self.output['uid'], 'local-uid')
        self.assertEqual(self.mutations, ['select', 'restore'])

    def test_uncertain_execution_not_repeated_after_restart(self):
        self.fail = subprocess.TimeoutExpired('helper', 35)
        result = self.service().toggle('request-0001')
        self.assertEqual(result['code'], 'uncertain')
        self.fail = None
        self.assertEqual(self.service().toggle('request-0001'), result)
        self.assertEqual(self.mutations, ['select'])

    def test_manual_airplay_route_is_not_owned(self):
        self.service().toggle('request-0001')
        self.output['uid'] = 'different-airplay'
        self.assertEqual(self.service().toggle('request-0002')['code'], 'external_route')
        self.assertEqual(self.mutations, ['select'])

    def test_busy_does_not_queue_another_toggle(self):
        service = self.service()
        service.lock.acquire()
        try: self.assertEqual(service.toggle('request-0001')['code'], 'busy')
        finally: service.lock.release()
        self.assertEqual(self.mutations, [])

    def test_corrupt_state_fails_without_mutation(self):
        (self.path / 'airplay-state.json').write_text('invalid')
        self.assertFalse(self.service().toggle('request-0001')['ok'])
        self.assertEqual(self.mutations, [])

    def test_unconfigured_and_invalid_request(self):
        (self.path / 'airplay.json').unlink()
        service = self.service()
        self.assertEqual(service.toggle('request-0001')['code'], 'unconfigured')
        with self.assertRaises(ValueError): service.toggle('short')
        self.assertEqual(self.mutations, [])

    def test_http_route_requires_auth_and_does_not_read_music(self):
        backend = Mock()
        service = self.service()
        server = make_server(backend, 'test-token', port=0, airplay=service)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        url = f'http://127.0.0.1:{server.server_port}/api/audio/toggle'
        body = json.dumps({'request_id': 'request-0001'}).encode()
        try:
            with self.assertRaises(urllib.error.HTTPError) as error:
                opener.open(urllib.request.Request(url, data=body), timeout=3)
            self.assertEqual(error.exception.code, 401)
            self.assertEqual(self.mutations, [])
            request = urllib.request.Request(url, data=body, headers={'Authorization': 'Bearer test-token'})
            with opener.open(request, timeout=3) as response:
                self.assertEqual(json.load(response)['route'], 'airplay')
            backend.action.assert_not_called()
            backend.snapshot.assert_not_called()
        finally:
            server.shutdown(); server.server_close(); thread.join()


if __name__ == '__main__': unittest.main()
