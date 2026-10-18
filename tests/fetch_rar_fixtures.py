"""Explicit optional network setup for RAR tests; never used by the application.

Public test archives from libarchive v3.8.2. They are downloaded only into the
ignored cache, with source URLs and SHA-256 retained. No executable is run.
Source license: https://github.com/libarchive/libarchive/blob/v3.8.2/COPYING
"""
import binascii
import hashlib
import json
from pathlib import Path
import sys
import urllib.request

ARCHIVES = {
    'test_read_format_rar_subblock.rar': 'e871277670529329cc2c06f178ced453c560d03fd26c76614f42ef9c06b50af0',
    'test_read_format_rar.rar': 'd421b86f6290aefad61b2a36737253b2b30fe27c156bd95abfc230f24fe0307e',
    'test_read_format_rar5_stored.rar': '35d75e315d164d2e329afc28f7d844f013271b4fcffd4ddd78efcdd114a383a7',
    'test_read_format_rar5_multiple_files_solid.rar': '0aa2c652c06304dedd93019663c1d52d5e85055d5fdb8045bfcc4345b90b4a0f',
    'test_read_format_rar5_multiarchive.part01.rar': '2f3894dc4b8a1bba90830ef1f89f457b0295b6f9030c70febd57283721bc2ce3',
}

def fetch(output):
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for name, digest in ARCHIVES.items():
        source = 'https://raw.githubusercontent.com/libarchive/libarchive/v3.8.2/libarchive/test/' + name + '.uu'
        with urllib.request.urlopen(source, timeout=30) as response:
            raw = response.read(4 * 1024 * 1024 + 1)
        if len(raw) > 4 * 1024 * 1024:
            raise ValueError('Fixture exceeds download limit: ' + name)
        lines = raw.splitlines()
        start = next(i for i, line in enumerate(lines) if line.startswith(b'begin ')) + 1
        data = bytearray()
        for line in lines[start:]:
            if line == b'end':
                break
            data += binascii.a2b_uu(line)
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('Fixture digest mismatch: ' + name)
        (output / name).write_bytes(data)
        records.append(dict(name=name, source=source, bytes=len(data), sha256=digest))
    (output / 'provenance.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    print(str(output.resolve()))

if __name__ == '__main__':
    fetch(Path(sys.argv[1]) if len(sys.argv) > 1 else Path('.cache/rar-fixtures'))
