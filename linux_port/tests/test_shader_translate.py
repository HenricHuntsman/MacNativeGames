#!/usr/bin/env python3
import json
import platform
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RID = {'x86_64': 'linux-x64', 'aarch64': 'linux-arm64', 'arm64': 'linux-arm64'}.get(platform.machine())
if RID is None:
    raise SystemExit(f'unsupported shader translator test architecture: {platform.machine()}')
TRANSLATOR = ROOT / 'games' / 'totk' / 'build' / f'shader-service-{RID}' / 'totk_xlate'


def main():
    if not TRANSLATOR.is_file():
        raise SystemExit(f'missing translator binary: {TRANSLATOR}; run make -C linux_port shader-service-linux')

    with tempfile.TemporaryDirectory(prefix='totk-spv-test-') as temp:
        root = Path(temp)
        out = root / 'out'
        code = root / 'minimal-vs.bin'
        code.write_bytes(bytes(0x50) + bytes(8) + struct.pack('<Q', 0xE30000000007000F))
        base_job = {
            'key': 'minimal', 'stage': 'vs', 'code': str(code), 'c1': '', 'debug': False, 'reduced': True,
            'tex': {}, 'img': {}, 'attr': {}, 'fout': {},
        }
        missing_job = dict(base_job, key='missing', code=str(root / 'missing.bin'))
        requests = [
            '{',
            json.dumps({'job': missing_job, 'out': str(out)}),
            json.dumps({'job': base_job, 'out': str(out)}),
        ]
        result = subprocess.run(
            [str(TRANSLATOR), '--serve'], input='\n'.join(requests) + '\n',
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            raise SystemExit(f'translator exited {result.returncode}: {result.stderr.strip()}')
        replies = result.stdout.splitlines()
        if len(replies) != 3 or not replies[0].startswith('fail <unknown>'):
            raise SystemExit(f'malformed request did not recover: {result.stdout!r}')
        if not replies[1].startswith('fail missing '):
            raise SystemExit(f'missing shader did not return an error: {result.stdout!r}')
        if replies[2] != 'ok minimal':
            raise SystemExit(f'valid shader did not translate after errors: {result.stdout!r}')

        spirv = (out / 'minimal.spv').read_bytes()
        if spirv[:4] != b'\x03\x02#\x07' or len(spirv) <= 20:
            raise SystemExit(f'invalid SPIR-V output: {len(spirv)} bytes, magic {spirv[:4].hex()}')
        validator = shutil.which('spirv-val')
        if not validator:
            raise SystemExit('spirv-val is required to validate generated SPIR-V; install spirv-tools')
        checked = subprocess.run([validator, '--target-env', 'vulkan1.1', str(out / 'minimal.spv')], capture_output=True, text=True)
        if checked.returncode:
            raise SystemExit(f'spirv-val rejected generated module: {checked.stderr.strip()}')
        reflection = json.loads((out / 'minimal.res.json').read_text())
        if reflection.get('key') != 'minimal' or reflection.get('stage') != 'vs':
            raise SystemExit(f'invalid reflection output: {reflection!r}')
        print(f'shader_service_test: 3 requests recovered; SPIR-V {len(spirv)} bytes and spirv-val passed')


if __name__ == '__main__':
    main()
