"""Check flash command routing without touching hardware."""
import os
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='flash test ') as directory:
    work = Path(directory)
    stub = work / 'pio'
    stub.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
with open(os.environ['UBERCLICKER_TEST_ARGS'], 'a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\\n')
target = sys.argv[sys.argv.index('-t') + 1] if '-t' in sys.argv else 'build'
if target == os.environ.get('UBERCLICKER_TEST_FAIL_TARGET', 'upload'):
    sys.exit(int(os.environ.get('UBERCLICKER_TEST_STATUS', '0')))
''')
    stub.chmod(0o755)
    args_file = work / 'arguments'
    env = dict(os.environ, PATH=f'{work}:{os.environ["PATH"]}', UBERCLICKER_TEST_ARGS=str(args_file))
    def calls():
        return [json.loads(line) for line in args_file.read_text().splitlines()]

    for role, options, port, erase in [
        ('tx', [], None, False),
        ('rx', [], None, False),
        ('rx', ['/dev/port with spaces'], '/dev/port with spaces', False),
        ('tx', ['--erase'], None, True),
        ('rx', ['--erase'], None, True),
        ('rx', ['--erase', '/dev/port with spaces'], '/dev/port with spaces', True),
        ('rx', ['/dev/port with spaces', '--erase'], '/dev/port with spaces', True),
    ]:
        args_file.unlink(missing_ok=True)
        result = subprocess.run([root / 'flash.sh', role, *options], cwd=work, env=env, capture_output=True)
        assert result.returncode == 0, result.stderr
        base = ['run', '-d', str(root / 'firmware'), '-e', f'thinknode_m5_{role}']
        port_args = ['--upload-port', port] if port else []
        expected = [base, base + ['-t', 'erase'] + port_args] if erase else []
        expected.append(base + ['-t', 'upload'] + port_args)
        assert calls() == expected
    args_file.unlink()
    for args in [[], ['bad'], ['TX'], ['tx', 'port', 'extra'], ['rx', '--unknown'], ['rx', '']]:
        result = subprocess.run([root / 'flash.sh', *args], env=env, capture_output=True)
        assert result.returncode == 2 and not args_file.exists()
    env['UBERCLICKER_TEST_STATUS'] = '17'
    for options in [[], ['--erase']]:
        for target in (['build', 'erase', 'upload'] if options else ['upload']):
            args_file.unlink(missing_ok=True)
            env['UBERCLICKER_TEST_FAIL_TARGET'] = target
            result = subprocess.run([root / 'flash.sh', 'rx', *options], env=env, capture_output=True)
            assert result.returncode == 17
            targets = [call[call.index('-t') + 1] if '-t' in call else 'build' for call in calls()]
            expected_targets = ['build', 'erase', 'upload'] if options else ['upload']
            assert targets == expected_targets[:expected_targets.index(target) + 1]
print('Flash script: role routing, optional port, opt-in erase, build-before-erase, invalid arguments and failure propagation passed')
