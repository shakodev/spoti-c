// Opt-in local observation. Only sanitized API content is ever written.
// Run alongside ordinary navigation in the approved Spotify debug session.
import { writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';

const [endpoint, output, seconds = '30'] = process.argv.slice(2);
if (!/^http:\/\/127\.0\.0\.1:\d+$/.test(endpoint ?? '')) throw new Error('Loopback endpoint required');
const duration = Number(seconds);
if (!Number.isFinite(duration) || duration < 1 || duration > 120) throw new Error('Duration 1..120 seconds');
const targets = await (await fetch(`${endpoint}/json/list`)).json();
const target = targets.find(t => t.type === 'page' && new URL(t.url).origin === 'https://xpui.app.spotify.com');
if (!target) throw new Error('Official Spotify XPUI page not found');
const socket = new WebSocket(target.webSocketDebuggerUrl);
let sequence = 0;
const pending = new Map(), requests = new Map(), jobs = new Set(), entries = [];
const sha = text => createHash('sha256').update(text).digest('hex');
const enumKeys = new Set(['__typename', 'syncType', 'capStatus', 'operationName']);
const safeHeaders = new Set(['accept', 'content-type', 'accept-language', 'app-platform', 'spotify-app-version']);
const safeMessages = new Set(['PersistedQueryNotFound', 'PersistedQueryNotSupported']);

function sanitize(value, key = '') {
    if (Array.isArray(value)) return value.map(v => sanitize(v, key));
    if (value && typeof value === 'object') return Object.fromEntries(
        Object.entries(value).map(([k, v]) => [k, sanitize(v, k)]));
    if (typeof value !== 'string') return value;
    if (enumKeys.has(key) && /^[A-Za-z][A-Za-z0-9_]{0,95}$/.test(value)) return value;
    if (key === 'sha256Hash' && /^[a-f0-9]{64}$/.test(value)) return value;
    if (['startTimeMs', 'endTimeMs'].includes(key) && /^\d{1,16}$/.test(value)) return value;
    if (key === 'message' && safeMessages.has(value)) return value;
    // Preserve empty-string/null distinctions; remove names, IDs, URLs, lyrics,
    // session IDs, cookies, tracking fields and user-supplied text alike.
    return value === '' ? '' : 'redacted';
}

function headers(values) {
    return Object.entries(values).filter(([k]) => safeHeaders.has(k.toLowerCase()))
        .map(([name, value]) => ({ name: name.toLowerCase(), value: String(value) }));
}

function call(method, params = {}) {
    const id = ++sequence;
    return new Promise((resolveCall, reject) => {
        const timer = setTimeout(() => { pending.delete(id); reject(new Error('CDP timeout')); }, 5000);
        pending.set(id, { resolve: resolveCall, reject, timer });
        socket.send(JSON.stringify({ id, method, params }));
    });
}

async function completed(id) {
    const record = requests.get(id);
    requests.delete(id);
    if (!record?.response) return;
    let body;
    try { body = await call('Network.getResponseBody', { requestId: id }); }
    catch { return; }
    const original = body.base64Encoded ? Buffer.from(body.body, 'base64').toString('utf8') : body.body;
    if (Buffer.byteLength(original) > 1024 * 1024) return;
    let json;
    try { json = JSON.parse(original); }
    catch { if (record.response.status < 400) return; json = null; }
    const text = JSON.stringify(sanitize(json));
    if (Buffer.byteLength(text) > 256 * 1024) return;
    let variables = null;
    try { variables = record.postData ? JSON.parse(record.postData) : null; } catch { /* no body retained */ }
    const url = new URL(record.url);
    // Endpoint templates preserve provenance without retaining personal IDs.
    if (url.pathname.startsWith('/color-lyrics/')) url.pathname = '/color-lyrics/v2/track/redacted/image/redacted';
    const queryString = [];
    for (const [name, value] of url.searchParams) {
        let safe = 'redacted';
        try { safe = JSON.stringify(sanitize(JSON.parse(value), name)); } catch { /* scalar omitted */ }
        queryString.push({ name, value: safe });
    }
    url.search = '';
    entries.push({ startedDateTime: record.time, time: 0,
        request: { method: record.method, url: url.href, httpVersion: 'HTTP/2',
            headers: record.headers, queryString, cookies: [], headersSize: -1, bodySize: -1,
            ...(variables ? { postData: { mimeType: 'application/json', text: JSON.stringify(sanitize(variables)) } } : {}) },
        response: { status: record.response.status, statusText: '', httpVersion: 'HTTP/2',
            headers: headers(record.response.headers), cookies: [], redirectURL: '', headersSize: -1, bodySize: text.length,
            content: { size: Buffer.byteLength(text), mimeType: 'application/json', text } },
        cache: {}, timings: { send: 0, wait: 0, receive: 0 },
        _sanitization: { authority: 'locally-redacted observation', original_response_sha256: sha(original),
            original_response_bytes: Buffer.byteLength(original), header_names: record.headerNames,
            strings: 'non-enum nonempty strings replaced except numeric lyric timestamps; arrays and scalar types preserved' } });
}

socket.addEventListener('message', event => {
    const message = JSON.parse(event.data);
    if (message.id) {
        const p = pending.get(message.id);
        if (!p) return;
        pending.delete(message.id); clearTimeout(p.timer);
        message.error ? p.reject(new Error('CDP command failed')) : p.resolve(message.result);
        return;
    }
    const p = message.params;
    if (message.method === 'Network.requestWillBeSent') {
        const url = new URL(p.request.url);
        if (!((url.hostname === 'api-partner.spotify.com' && /^\/pathfinder\/v[12]\/query$/.test(url.pathname)) ||
            (url.hostname.endsWith('.spotify.com') && url.pathname.startsWith('/color-lyrics/v2/track/')))) return;
        requests.set(p.requestId, { url: p.request.url, method: p.request.method, postData: p.request.postData,
            headers: headers(p.request.headers), headerNames: Object.keys(p.request.headers).map(k => k.toLowerCase()),
            time: new Date().toISOString() });
    } else if (message.method === 'Network.responseReceived') {
        const r = requests.get(p.requestId); if (r) r.response = p.response;
    } else if (message.method === 'Network.loadingFinished' && requests.has(p.requestId)) {
        const job = completed(p.requestId).catch(() => {}).finally(() => jobs.delete(job)); jobs.add(job);
    }
});
await new Promise((ready, reject) => { socket.addEventListener('open', ready); socket.addEventListener('error', reject); });
await call('Network.enable', { maxTotalBufferSize: 8 * 1024 * 1024, maxResourceBufferSize: 1024 * 1024 });
console.log('Sanitized local API observer ready');
await new Promise(done => setTimeout(done, duration * 1000));
await Promise.allSettled([...jobs]);
await call('Network.disable'); socket.close();
await mkdir(resolve(output, '..'), { recursive: true });
await writeFile(output, JSON.stringify({ log: { version: '1.2', creator: { name: 'spoti-c local sanitizer', version: '1' },
    entries } }, null, 2) + '\n', { flag: 'wx' });
console.log(`Saved ${entries.length} sanitized API responses`);
