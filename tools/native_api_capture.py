"""Convert already locally sanitized native observations into a bounded HAR.

This is an offline projection, not a proxy or an authenticated request runner.
Protobuf bodies are decoded JSON projections; original entity digests remain
separate. Never accepts credential header values or original account strings.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

HEADERS = {'accept', 'content-type', 'accept-language', 'app-platform', 'spotify-app-version'}
CAP = 262144


def clean_body(value, key=''):
    if isinstance(value, dict):
        for k, child in value.items():
            clean_body(child, k)
    elif isinstance(value, list):
        for child in value:
            clean_body(child, key)
    elif isinstance(value, str) and value not in ('', 'redacted'):
        if key == 'set' and re.fullmatch(r'[A-Za-z_]{1,63}', value):
            return
        if key == 'protobuf_type' and re.fullmatch(r'(spotify|google)\.[A-Za-z0-9_.]+', value):
            return
        if key == 'omitted' and value in ('body cap', 'unknown binary or text'):
            return
        raise ValueError('Unredacted body string')


def convert(source, output):
    selected = {}
    data = source.read_bytes()
    if len(data) > 4 * 1024 * 1024:
        raise ValueError('Capture exceeds local observation bound')
    for line in data.splitlines():
        row = json.loads(line)
        if row['authority'] != 'locally-redacted native proxy observation':
            raise ValueError('Not an approved sanitizer output')
        request, response = row['request'], row['response']
        if request['method'] not in ('GET', 'POST') or not re.fullmatch(r'[a-z0-9-]+spclient\.spotify\.com', request['host']):
            raise ValueError('Unexpected service authority/method')
        if any(k.lower() not in HEADERS for k in request['headers']):
            raise ValueError('Credential or unknown header value')
        clean_body(request['body'])
        clean_body(response['body'])
        if row['original_response_bytes'] > CAP:
            continue
        case = row['case']
        key = (case,) if case != 'official-client' else (case, request['path'], response['status'],
            request['body'].get('decoded', {}).get('set', ''))
        selected.setdefault(key, row)
    if len(selected) > 64:
        raise ValueError('Representative observation bound exceeded')
    entries = []
    for row in selected.values():
        req, resp = row['request'], row['response']
        text = json.dumps(resp['body'], separators=(',', ':'))
        if len(text.encode()) > CAP:
            continue
        entries.append(dict(startedDateTime='1970-01-01T00:00:00Z', time=0,
            request=dict(method=req['method'], url='https://' + req['host'] + req['path'],
                httpVersion='HTTP/2', headers=[dict(name=k, value=v) for k, v in req['headers'].items()],
                queryString=[dict(name=k, value=v) for k, v in req.get('query', {}).items()],
                cookies=[], headersSize=-1, bodySize=-1,
                postData=dict(mimeType='application/json', text=json.dumps(req['body'], separators=(',', ':')))),
            response=dict(status=resp['status'], statusText='', httpVersion='HTTP/2', headers=[],
                cookies=[], redirectURL='', headersSize=-1, bodySize=len(text.encode()),
                content=dict(size=len(text.encode()), mimeType='application/json', text=text)),
            cache={}, timings=dict(send=0, wait=0, receive=0),
            _sanitization=dict(authority=row['authority'], source_case=row['case'],
                source_capture_sha256=hashlib.sha256(data).hexdigest(),
                decoder_descriptor_set_sha256=row['decoder_descriptor_set_sha256'],
                original_response_bytes=row['original_response_bytes'],
                original_response_sha256=row['original_response_sha256'], header_names=req['header_names'],
                representation='Decoded current protobuf projection, strings/binary values removed before disk; not original wire body',
                timestamp='Unix epoch sentinel; wall-clock timing intentionally not claimed')))
    output.write_text(json.dumps(dict(log=dict(version='1.2',
        creator=dict(name='spoti-c local native sanitizer projection', version='1'), entries=entries)), indent=2)+'\n', encoding='utf-8')
    print(f'Projected {len(entries)} bounded sanitized native records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    convert(args.source, args.output)
