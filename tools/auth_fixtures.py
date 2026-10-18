"""Independent protobuf/hashlib auth vectors; never contacts Spotify."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/auth/vectors.json"
REVISION = "19959512c212cbc48a3ff64382fe204f52c659a7"


def rebuild(descriptor):
    from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

    descriptors = descriptor_pb2.FileDescriptorSet.FromString(descriptor.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    pending = list(descriptors.file)
    while pending:
        admitted = []
        for item in pending:
            try:
                pool.Add(item)
                admitted.append(item)
            except TypeError:
                pass
        if not admitted:
            raise RuntimeError("Unresolved descriptor dependency")
        pending = [item for item in pending if item not in admitted]

    def message(name):
        return message_factory.GetMessageClass(pool.FindMessageTypeByName(name))()

    ct = "spotify.clienttoken.http.v0."
    login = "spotify.login5.v3."
    vectors = {}

    def save(name, value):
        vectors[name] = value.SerializeToString(deterministic=True).hex()

    device = "0123456789abcdef0123456789abcdef01234567"
    request = message(ct + "ClientTokenRequest")
    request.request_type = 1
    data = request.client_data
    data.client_version = "0.1.0-test"
    data.client_id = "fixture-client"
    data.connectivity_sdk_data.device_id = device
    windows = data.connectivity_sdk_data.platform_specific_data.desktop_windows
    windows.os_version, windows.os_build, windows.platform_id = 10, 26100, 2
    windows.unknown_value_5 = windows.unknown_value_6 = 9
    windows.image_file_machine = windows.pe_machine = 34404
    windows.unknown_value_10 = False  # Native descriptor: is_windows_store.
    save("client_request", request)
    response = message(ct + "ClientTokenResponse")
    response.response_type = 1
    granted = response.granted_token
    granted.token = "fixture-client-token"
    granted.expires_after_seconds = 120
    granted.refresh_after_seconds = 60
    granted.domains.add().domain = "spotify.com"
    save("client_granted", response)
    granted.refresh_after_seconds = 121
    save("client_bad_refresh", response)
    granted.refresh_after_seconds = 60
    granted.token = "x" * 1024
    save("client_long_token", response)
    granted.token = "fixture-client-token"
    granted.ClearField("domains")
    save("client_no_domains", response)
    response = message(ct + "ClientTokenResponse")
    response.response_type = 2
    response.challenges.state = "client-context"
    challenge = response.challenges.challenges.add()
    challenge.type = 3
    challenge.evaluate_hashcash_parameters.length = 4
    challenge.evaluate_hashcash_parameters.prefix = "0102030405060708"
    save("client_challenge", response)
    challenge.evaluate_hashcash_parameters.length = 20
    save("client_work_limit", response)
    challenge.evaluate_hashcash_parameters.length = 21
    save("client_hard_challenge", response)
    challenge.Clear()
    challenge.type = 2
    challenge.evaluate_js_parameters.code = "fixture_must_never_execute()"
    save("client_unsupported", response)

    def suffix(context, prefix):
        seed = int.from_bytes(hashlib.sha1(context).digest()[12:20], "big")
        for counter in range(1 << 20):
            candidate = ((seed + counter) % (1 << 64)).to_bytes(8, "big") + counter.to_bytes(8, "big")
            if int.from_bytes(hashlib.sha1(prefix + candidate).digest()[12:20], "big") & 15 == 0:
                return candidate
        raise RuntimeError("Fixture hashcash did not converge")

    answer = message(ct + "ClientTokenRequest")
    answer.request_type = 2
    answer.challenge_answers.state = "client-context"
    solution = answer.challenge_answers.answers.add()
    solution.ChallengeType = 3
    solution.hash_cash.suffix = suffix(b"", bytes.fromhex("0102030405060708")).hex().upper()
    save("client_answer", answer)
    request = message(login + "LoginRequest")
    request.client_info.client_id = "fixture-client"
    request.client_info.device_id = device
    request.stored_credential.username = "fixture-user"
    request.stored_credential.data = b"fixture-ap-credential"
    save("login_request", request)
    response = message(login + "LoginResponse")
    response.ok.username = "fixture-user"
    response.ok.access_token = "fixture-bearer"
    response.ok.access_token_expires_in = 120
    response.ok.stored_credential = b"fixture-login5-credential"
    save("login_ok", response)
    response.ok.username = "different-user"
    save("login_wrong_user", response)
    response.ok.username = "fixture-user"
    response.ok.access_token = "x" * 1017
    save("login_long_token", response)
    response = message(login + "LoginResponse")
    response.login_context = b"login-context\x00\xff"
    challenge = response.challenges.challenges.add()
    challenge.hashcash.prefix = bytes.fromhex("1122334455667788")
    challenge.hashcash.length = 4
    save("login_challenge", response)
    challenge.MergeFromString(b"\x1a\x00")  # Future unknown variant alongside hashcash.
    save("login_unknown_variant", response)
    challenge.DiscardUnknownFields()
    request.login_context = response.login_context
    solution = request.challenge_solutions.solutions.add()
    solution.hashcash.suffix = suffix(response.login_context, challenge.hashcash.prefix)
    solution.hashcash.duration.seconds = 0
    save("login_answer", request)
    challenge.hashcash.prefix = b"x" * 129
    save("login_large_prefix", response)
    challenge.hashcash.prefix = bytes.fromhex("1122334455667788")
    response.login_context = b"x" * 1025
    save("login_large_context", response)
    response.login_context = b"login-context\x00\xff"
    challenge.Clear()
    challenge.code.SetInParent()
    save("login_unsupported", response)
    response = message(login + "LoginResponse")
    response.error = 1
    save("login_rejected", response)
    response.error = 4
    save("login_timeout", response)
    FIXTURES.write_text(json.dumps({
        "provenance": "Constructed sanitized offline vectors, not live captures",
        "schema_revision": REVISION,
        "native_artifact_sha256": "ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde",
        "vectors": vectors,
    }, indent=2) + "\n", encoding="utf-8")


def header(path):
    data = json.loads(FIXTURES.read_text(encoding="utf-8"))
    if data["schema_revision"] != REVISION:
        raise RuntimeError("Unexpected auth fixture revision")
    lines = ["/* Synthetic fixtures: independent Python protobuf and hashlib. */", "#pragma once"]
    for name, value in data["vectors"].items():
        payload = bytes.fromhex(value)
        lines.append(f"static const unsigned char auth_{name}[] = {{")
        for at in range(0, len(payload), 16):
            lines.append("    " + ", ".join(f"0x{x:02x}" for x in payload[at:at + 16]) + ",")
        lines.append("};")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rebuild", type=Path, help="Explicit descriptor set for fixture regeneration")
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    if args.rebuild:
        rebuild(args.rebuild)
    if args.header:
        header(args.header)
