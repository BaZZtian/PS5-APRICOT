"""Check the actual report format, replacement and bounded public allowlist."""
import json
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'debug-report.json'
    path.write_text('OLD REPORT SENTINEL')
    subprocess.run([sys.argv[1], str(path)], check=True)
    data = path.read_bytes()
    assert len(data) < 1024
    report = json.loads(data)
    assert report == dict(schema_version=1, app='ApricotGS', version='0.5.7',
                          game_code='IPKD', phase='stopped', frames=2**64-1,
                          stop_arm9=3, stop_arm7=1, automatic_upload=False)
    assert stat.S_IMODE(path.stat().st_mode) == 0o644
    assert sorted(p.name for p in path.parent.iterdir()) == ['debug-report.json']
    assert not any(s in data for s in (b'SENTINEL', b'.nds', b'192.168.', b'save', b'account', b'"ram"'))
print('PASS: fixed report allowlist, atomic replacement, permissions, no leftover temporary or invalid-report overwrite')
