"""Verify static native API evidence without executing Spotify or derived PEs.

Requires the development protobuf package. The inspection container has synthetic
PE headers and read-only original data sections; it is never an executable test.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, json_format

EXPECTED = '4f8a965aa4492c6cbfa304956ea8428f10538c74dfae5eaadf54be08572a897a'
DESCRIPTORS = [('collection2v2.proto', 29496800, 1207),
               ('playlist_api.proto', 30093952, 11222)]
SELECTED = {'PageRequest', 'CollectionItem', 'PageResponse', 'DeltaRequest',
            'DeltaResponse', 'InitializedRequest', 'InitializedResponse', 'Item',
            'MetaItem', 'ListItems', 'ListAttributes', 'ItemAttributes',
            'SelectedListContent', 'Diff', 'Delta', 'Op', 'ListChanges'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pe_layout(data):
    pe = struct.unpack_from('<I', data, 60)[0]
    optional = pe + 24
    base = struct.unpack_from('<Q', data, optional + 24)[0]
    at = optional + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, at + 40*i + 8)
                for i in range(struct.unpack_from('<H', data, pe + 6)[0])]
    return base, sections


def offset_for(address, base, sections):
    rva = address - base
    _, start, size, offset = next(s for s in sections if s[1] <= rva < s[1]+s[2])
    if rva-start >= size:
        raise ValueError('Address is not file backed')
    return offset + rva-start


def main(source, container, extraction, records, comparison, closure, output):
    original, derived = source.read_bytes(), container.read_bytes()
    if sha(original) != EXPECTED:
        raise ValueError('Source is not the selected current Spotify DLL')
    base, sections = pe_layout(original)
    derived_base, _ = pe_layout(derived)
    pe = struct.unpack_from('<I', derived, 60)[0]
    section_headers = pe + 24 + struct.unpack_from('<H', derived, pe+20)[0]
    if derived_base != base or struct.unpack_from('<I', derived, pe+24+16)[0]:
        raise ValueError('Inspection container has a changed base or executable entry')
    for i in range(struct.unpack_from('<H', derived, pe+6)[0]):
        if struct.unpack_from('<I', derived, section_headers+40*i+36)[0] & 0x20000000:
            raise ValueError('Inspection container unexpectedly has executable data')
    metadata = json.loads(extraction.read_text(encoding='utf-8'))
    if metadata['container']['sha256'] != sha(derived):
        raise ValueError('Inspection-container identity changed')
    for span in metadata['ranges']:
        at, size = span['source_offset'], span['bytes']
        raw = original[at:at+size]
        to = span['container_offset']
        if sha(raw) != span['sha256'] or derived[to:to+size] != raw:
            raise ValueError('Inspection span differs from original source')
    evidence = []
    covered = set()
    for name in ('collectionbytes', 'playlistbytes', 'playlistliterals',
                 'collectionliterals', 'playlistheaders'):
        record = json.loads((records/(name+'.json')).read_text(encoding='utf-8'))
        result = record['normalized_result']
        raw = bytes.fromhex(result['bytes_hex'])
        address = int(result['address'], 16)
        at = offset_for(address, base, sections)
        if (not result['complete'] or result['requested_bytes'] != len(raw)
                or original[at:at+len(raw)] != raw
                or record['subject']['digest']['sha256'] != sha(derived)):
            raise ValueError('REA observation does not match the original bytes')
        evidence.append(dict(name=name, evidence_id=record['evidence_id'],
                             provider=record['provider'],
                             analysis_profile_digest=record['analysis_profile']['digest'],
                             address=result['address'], source_offset=at,
                             bytes=len(raw), sha256=sha(raw), bytes_hex=raw.hex(),
                             original_bytes_verified=True))
        covered.add((at, len(raw)))
    descriptors = []
    for name, at, size in DESCRIPTORS:
        if (at, size) not in covered:
            raise ValueError('Descriptor has no exact complete REA byte observation')
        raw = original[at:at+size]
        proto = descriptor_pb2.FileDescriptorProto.FromString(raw)
        if proto.name != name:
            raise ValueError('Descriptor identity differs')
        body = json_format.MessageToDict(proto, preserving_proto_field_name=True)
        descriptors.append(dict(name=name, source_offset=at, bytes=size,
                                sha256=sha(raw), package=body['package'],
                                syntax=body.get('syntax', 'proto2'),
                                dependencies=body.get('dependency', []),
                                selected_messages=[m for m in body['message_type']
                                                   if m['name'] in SELECTED]))
    schema_comparison = json.loads(comparison.read_text(encoding='utf-8'))
    for ref in schema_comparison['references']:
        if sha(Path(ref['path']).read_bytes()) != ref['sha256']:
            raise ValueError('Historical reference changed')
    closure_input = json.loads(closure.read_text(encoding='utf-8'))
    if closure_input['source_sha256'] != EXPECTED:
        raise ValueError('Dependency closure source changed')
    dependency_closure, parsed = [], {}
    descriptor_set = descriptor_pb2.FileDescriptorSet()
    for row in closure_input['records']:
        at, size = row['source_offset'], row['bytes']
        raw = original[at:at+size]
        proto = descriptor_pb2.FileDescriptorProto.FromString(raw)
        if sha(raw) != row['sha256'] or proto.name != row['name']:
            raise ValueError('Dependency descriptor differs from original bytes')
        parsed[proto.name] = proto
        descriptor_set.file.add().CopyFrom(proto)
        dependency_closure.append(dict(name=proto.name, source_offset=at,
                                       bytes=size, sha256=sha(raw),
                                       dependencies=list(proto.dependency),
                                       original_bytes_hex=raw.hex()))
    pool, loaded = descriptor_pool.DescriptorPool(), set()

    def load(name):
        if name in loaded:
            return
        for dependency in parsed[name].dependency:
            load(dependency)
        pool.AddSerializedFile(parsed[name].SerializeToString())
        loaded.add(name)

    for name, _, _ in DESCRIPTORS:
        load(name)
    if len(loaded) != len(parsed):
        raise ValueError('Closure contains unreferenced descriptors')
    for name in ('spotify.collection.proto.v2.PageRequest',
                 'spotify.collection.proto.v2.PageResponse',
                 'spotify.playlist4.proto.SelectedListContent'):
        pool.FindMessageTypeByName(name)
    descriptor_set_sha = sha(descriptor_set.SerializeToString())
    if descriptor_set_sha != closure_input['file_descriptor_set_sha256']:
        raise ValueError('Descriptor-set identity differs')
    result = dict(source=metadata['source'], classification=metadata['classification'],
                  extraction=dict(image_base=metadata['image_base'],
                                  container=metadata['container'],
                                  synthetic_header_hex=derived[:1024].hex(),
                                  ranges=metadata['ranges']),
                  evidence=evidence, descriptors=descriptors,
                  dependency_closure=dict(file_descriptor_set_sha256=descriptor_set_sha,
                                          bytes=len(descriptor_set.SerializeToString()),
                                          source='Exact current native embedded descriptors; dependency pool resolves all 11 files',
                                          records=dependency_closure),
                  historical_reference_comparison=dict(
                      references=schema_comparison['references'],
                      differences=schema_comparison['reference_differences']),
                  limitations=metadata['limitations']+[
                      'Static literals do not prove URL assembly, methods or headers; use the separate sanitized runtime evidence',
                      'Selected messages are a known-field subset; imported message definitions are not a claimed complete decoder',
                      'Historical runtime projections omit unknown fields; current CollectionItem.added_at is int64, not int32',
                      'Native DLL version differs from the earlier 1.3.4.258 XPUI snapshot; version-specific observations are not interchangeable'])
    output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'Verified {len(evidence)} exact REA observations and {len(descriptors)} current descriptors')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('source', 'container', 'extraction', 'records', 'comparison', 'closure', 'output'):
        parser.add_argument('--'+option, type=Path, required=True)
    main(**vars(parser.parse_args()))
