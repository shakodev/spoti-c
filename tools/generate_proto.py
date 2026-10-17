"""Offline, pinned nanopb generation. Install tools/proto-requirements.txt first."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def generate(output):
    for name, version in (('grpcio-tools', '1.68.1'), ('protobuf', '5.28.3')):
        if importlib.metadata.version(name) != version:
            raise RuntimeError(f'{name} must be {version}; install tools/proto-requirements.txt')
    version = subprocess.check_output([sys.executable, '-m', 'grpc_tools.protoc', '--version'], text=True).strip()
    if version != 'libprotoc 28.1':
        raise RuntimeError(f'Expected libprotoc 28.1, got {version}')
    manifest = json.loads((ROOT / 'proto/manifest.json').read_text())
    for entry in manifest['files']:
        path = ROOT / 'proto' / entry['path']
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError(f'Schema hash mismatch: {path}')
    for entry in json.loads((ROOT / 'vendor/manifest.json').read_text()):
        if entry['name'] == 'nanopb':
            for item in entry['files']:
                if hashlib.sha256((ROOT / item['path']).read_bytes()).hexdigest() != item['sha256']:
                    raise RuntimeError(f'Nanopb hash mismatch: {item["path"]}')
    output.mkdir(parents=True, exist_ok=True)
    import grpc_tools
    include = Path(grpc_tools.__file__).parent / '_proto'
    descriptor = output / 'spotify.pb'
    names = [entry['path'] for entry in manifest['files']]
    subprocess.run([sys.executable, '-m', 'grpc_tools.protoc', '-I' + str(ROOT / 'proto'),
                    '-I' + str(include), '--descriptor_set_out=' + str(descriptor),
                    '--include_imports', *names], check=True, cwd=ROOT / 'proto')
    # Call the vendored generator directly; no PATH-selected plugin or network.
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', NANOPB_PB2_TEMP_DIR=str(output))
    from google.protobuf import descriptor_pb2
    from google.protobuf.compiler import plugin_pb2
    request = plugin_pb2.CodeGeneratorRequest()
    request.proto_file.extend(descriptor_pb2.FileDescriptorSet.FromString(descriptor.read_bytes()).file)
    request.file_to_generate.extend(names)
    request.parameter = '-T,-I' + str(ROOT / 'proto')
    result = subprocess.run([sys.executable,
        str(ROOT / 'vendor/nanopb/generator/nanopb_generator.py'), '--protoc-plugin'],
        input=request.SerializeToString(), stdout=subprocess.PIPE, check=True, cwd=ROOT, env=env)
    response = plugin_pb2.CodeGeneratorResponse.FromString(result.stdout)
    if response.error:
        raise RuntimeError(response.error)
    for item in response.file:
        path = output / item.name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(item.content.encode('utf-8'))
    generated = sorted(output.rglob('*.pb.[ch]'))
    if len(generated) != len(names) * 2:
        raise RuntimeError('Generated closure differs from manifest')
    for path in generated:
        text = path.read_text()
        if 'PB_ATYPE_POINTER' in text or 'POINTER,' in text:
            raise RuntimeError(f'Dynamic protobuf field: {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.output.resolve())
