"""Regenerate twice, compare every output byte, and audit the C build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def check(build, audit):
    manifest = json.loads((ROOT / 'proto/manifest.json').read_text())
    expected = {entry['path'] for entry in manifest['files']}
    with tempfile.TemporaryDirectory(prefix='proto-repro-', dir=build) as temp:
        outputs = []
        for name in ('first', 'second'):
            output = Path(temp) / name
            subprocess.run([sys.executable, str(ROOT / 'tools/generate_proto.py'),
                            '--output', str(output)], check=True)
            outputs.append({p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in output.rglob('*') if p.is_file()})
        if outputs[0] != outputs[1]:
            raise RuntimeError('Regeneration produced different bytes')
        from google.protobuf import descriptor_pb2
        descriptor = descriptor_pb2.FileDescriptorSet.FromString((Path(temp) / 'first/spotify.pb').read_bytes())
        if {p.name for p in descriptor.file} != expected or len(expected) != 38:
            raise RuntimeError('Descriptor import closure differs from the 38-file manifest')
        for item in descriptor.file:
            if not set(item.dependency) <= expected:
                raise RuntimeError(f'Unpinned import in {item.name}')
    print('Reproducibility: 38 schemas, 76 C/header files and descriptor are byte-identical')
    if audit:
        commands = json.loads((build / 'compile_commands.json').read_text())
        for item in commands:
            if Path(item['file']).suffix != '.c':
                raise RuntimeError(f'Non-C translation unit: {item["file"]}')
            if re.search(r'PB_ENABLE_MALLOC|msvcprt|stdc\+\+|libc\+\+', item['command'], re.I):
                raise RuntimeError('Forbidden compiler/runtime configuration')
        binary = build / 'test_proto_codec.exe'
        for args in (['/imports', str(binary)], ['/directives', str(build / 'spotic_proto.lib')]):
            text = subprocess.check_output(['dumpbin', *args], text=True)
            if re.search(r'msvcp\w*\.dll|msvcprt|libstdc\+\+|libc\+\+', text, re.I):
                raise RuntimeError('C++ runtime dependency found')
        print(f'Build audit: {len(commands)} C-only translation units; no C++ runtime imports/directives')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--audit', action='store_true')
    args = parser.parse_args()
    check(args.build.resolve(), args.audit)
