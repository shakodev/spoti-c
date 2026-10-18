"""Reproduce issue #13's bounded static native evidence manifest.

Requires the pinned original DLL and existing proto-tools Python dependencies.
Optional REA read_bytes records must match every original byte before their IDs
are retained. This tool never executes Spotify or sends a service request.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from google.protobuf import descriptor_pb2


EXPECTED = "ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde"
RANGES = (
    ("metadata-builder", 0x1805592F0, 908),
    ("storage-path-builder", 0x1806B9D3C, 956),
    ("storage-request-builder", 0x1806BA3F0, 620),
    ("storage-mode-helper", 0x1806BA714, 47),
    ("request-default-get", 0x18157B61C, 874),
    ("method-constants", 0x18265FA48, 96),
    ("metadata-literals", 0x181A222A0, 160),
    ("storage-literals", 0x181A3B400, 656),
    ("storage-descriptor", 0x181A3C260, 261),
    ("metadata-descriptor-exact", 0x181C9FFE0, 1521),
    ("entity-extension-descriptor", 0x181CA09D0, 747),
    ("audio-format-descriptor", 0x181A81720, 777),
    ("extension-kind-descriptor", 0x181CF0280, 6853),
    ("file-format-getter", 0x18155CBEC, 56),
    ("file-picker-format-transfer", 0x18155CC24, 392),
    ("file-object-constructor", 0x18155F6D8, 63),
    ("storage-variant-transfer", 0x1806A6C14, 877),
    ("storage-factory-transfer", 0x1806A52F0, 229),
    ("storage-object-constructor", 0x1806B9B30, 445),
    ("manifest-format-transfer", 0x18069E33B, 146),
    ("manifest-file-constructor", 0x18155CAB4, 60),
    ("track-audio-descriptor", 0x181CF2EF0, 9268),
    ("media-manifest-descriptor", 0x181B58E90, 1250),
)
DESCRIPTORS = (
    ("extended_metadata.proto", 30008800, 1521),
    ("entity_extension_data.proto", 30011344, 747),
    ("extension_kind.proto", 30337152, 6853),
    ("storage-resolve.proto", 27502688, 261),
    ("audio_format.proto", 27786528, 777),
    ("metadata.proto", 30348528, 9268),
    ("media_manifest.proto", 28669072, 1250),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pe_mapping(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    optional = pe + 24
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    start = optional + struct.unpack_from("<H", data, pe + 20)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    sections = [struct.unpack_from("<IIII", data, start + 40 * i + 8)[1:]
                for i in range(count)]

    def offset(address, length):
        rva = address - base
        for section_rva, size, raw in sections:
            if section_rva <= rva and rva + length <= section_rva + size:
                return raw + rva - section_rva
        raise ValueError(f"Unmapped original byte range {address:#x}, {length}")

    return base, offset


def enum_row(enum):
    return {"name": enum.name,
            "values": {value.name: value.number for value in enum.value}}


def descriptor_row(name, start, size, data):
    payload = data[start:start + size]
    descriptor = descriptor_pb2.FileDescriptorProto.FromString(payload)
    if descriptor.name != name:
        raise ValueError(f"Descriptor identity changed: {name}")
    messages = []
    for message in descriptor.message_type:
        messages.append({"name": message.name,
                         "fields": [{"name": field.name, "number": field.number,
                                     "type": field.type, "type_name": field.type_name,
                                     "label": field.label} for field in message.field],
                         "enums": [enum_row(enum) for enum in message.enum_type]})
    return {"name": name, "package": descriptor.package, "file_offset": start,
            "bytes": size, "sha256": digest(payload), "messages": messages,
            "enums": [enum_row(enum) for enum in descriptor.enum_type]}


def rea_record(directory, tag, payload, address):
    path = directory / f"{tag}.json"
    record = json.loads(path.read_text(encoding="utf-8-sig"))
    if record.get("isError"):
        raise ValueError(f"REA record failed: {path}")
    evidence = record.get("structuredContent")
    if not evidence:
        evidence = json.loads(next(item["text"] for item in record["content"]
                                   if item["type"] == "text"))
    result = evidence["normalized_result"]
    if (not result["complete"] or int(result["address"], 16) != address
            or bytes.fromhex(result["bytes_hex"]) != payload
            or evidence["subject"]["digest"]["sha256"] != EXPECTED):
        raise ValueError(f"REA bytes/identity disagree with pinned DLL: {path}")
    return {"evidence_id": evidence["evidence_id"], "operation": evidence["operation"],
            "analysis_profile_digest": evidence["analysis_profile"]["digest"],
            "provider": evidence["provider"], "limitations": evidence["limitations"]}


def generate(source, output, rea_directory):
    data = source.read_bytes()
    if digest(data) != EXPECTED:
        raise ValueError("This evidence map requires the pinned Spotify 1.3.4.258 DLL")
    base, offset = pe_mapping(data)
    rows = []
    for tag, address, length in RANGES:
        start = offset(address, length)
        payload = data[start:start + length]
        row = {"name": tag, "address": hex(address), "file_offset": start,
               "bytes": length, "sha256": digest(payload)}
        if rea_directory:
            row["rea"] = rea_record(rea_directory, tag, payload, address)
        rows.append(row)
    manifest = {
        "classification": "bounded static shipped-artifact observations, not captured traffic",
        "source": {"name": "Spotify.dll", "version": "1.3.4.258",
                   "sha256": EXPECTED, "image_base": hex(base)},
        "ranges": rows,
        "descriptors": [descriptor_row(*item, data) for item in DESCRIPTORS],
        "limitations": [
            "DUMPBIN instruction interpretation is supplemental static analyst work.",
            "REA byte reads corroborate original bytes; disabled analyzers supply no complete call graph.",
            "Static construction does not establish live service acceptance or runtime path selection.",
            "No account, secret, live service, request capture or target execution was used.",
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(rows)} ranges and {len(DESCRIPTORS)} descriptors to {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rea-directory", type=Path)
    args = parser.parse_args()
    generate(args.source, args.output, args.rea_directory)
