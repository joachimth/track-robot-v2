#!/usr/bin/env node
// extract_ui.js — Reconstruct the embedded web UI HTML from the firmware source.
//
// The web UI is emitted by controller_http.c as a series of
// httpd_resp_send_chunk(req, "...", -1) calls. Each call's second argument is a
// C string literal (often several adjacent literals concatenated across lines)
// containing escaped HTML/CSS/JS. This script extracts every chunk, unescapes
// the C escape sequences, concatenates them in source order, injects a mock
// fetch() so the page renders with realistic data, and writes screenshots/ui.html.

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', 'firmware', 'components', 'control', 'controller_http.c');
const OUT = path.join(__dirname, 'ui.html');

// ---------------------------------------------------------------------------
// Extract the concatenated string literals passed to each
// httpd_resp_send_chunk(req, ...) call. Returns one raw (still-escaped) string
// per call. Comments and the trailing ", -1" / "NULL, 0" args are ignored.
// ---------------------------------------------------------------------------
function extractChunks(src) {
    const chunks = [];
    const re = /httpd_resp_send_chunk\s*\(/g;
    let m;
    while ((m = re.exec(src)) !== null) {
        let i = re.lastIndex; // first char after the '('
        let depth = 1;
        let raw = '';
        while (i < src.length && depth > 0) {
            const ch = src[i];
            if (ch === '"') {
                // Consume a string literal, preserving escape sequences verbatim.
                let j = i + 1;
                while (j < src.length) {
                    if (src[j] === '\\') { raw += src[j] + (src[j + 1] || ''); j += 2; continue; }
                    if (src[j] === '"') break;
                    raw += src[j];
                    j++;
                }
                i = j + 1;
                continue;
            }
            if (ch === '/' && src[i + 1] === '/') {              // line comment
                while (i < src.length && src[i] !== '\n') i++;
                continue;
            }
            if (ch === '/' && src[i + 1] === '*') {              // block comment
                i += 2;
                while (i < src.length && !(src[i] === '*' && src[i + 1] === '/')) i++;
                i += 2;
                continue;
            }
            if (ch === '(') depth++;
            else if (ch === ')') depth--;
            i++;
        }
        if (raw.length > 0) chunks.push(raw);
    }
    return chunks;
}

// ---------------------------------------------------------------------------
// Unescape one raw C string literal into a byte array. \xHH escapes (used for
// UTF-8 byte sequences like the robot emoji) are emitted as raw bytes; all
// other characters are encoded as UTF-8. \\ -> \ is preserved so JS unicode
// escapes such as ● survive into the output for the browser to interpret.
// ---------------------------------------------------------------------------
function unescapeToBytes(s) {
    const bytes = [];
    const push = (str) => { for (const b of Buffer.from(str, 'utf8')) bytes.push(b); };
    for (let i = 0; i < s.length; i++) {
        if (s[i] !== '\\') { push(s[i]); continue; }
        const n = s[i + 1];
        switch (n) {
            case 'n': bytes.push(0x0a); i++; break;
            case 't': bytes.push(0x09); i++; break;
            case 'r': bytes.push(0x0d); i++; break;
            case 'a': bytes.push(0x07); i++; break;
            case 'b': bytes.push(0x08); i++; break;
            case 'f': bytes.push(0x0c); i++; break;
            case 'v': bytes.push(0x0b); i++; break;
            case '0': bytes.push(0x00); i++; break;
            case '"': push('"'); i++; break;
            case "'": push("'"); i++; break;
            case '\\': push('\\'); i++; break;
            case '?': push('?'); i++; break;
            case 'x': {
                const hex = s.substr(i + 2, 2);
                bytes.push(parseInt(hex, 16) & 0xff);
                i += 3;
                break;
            }
            default: push(n); i++; break; // unknown escape: keep the char
        }
    }
    return bytes;
}

const MOCK_SCRIPT = `<script>
// Mock fetch for screenshot rendering - simulates a live robot in ARMED state
const MOCK_STATUS = {
  state: "ARMED",
  source: "PS4",
  uptime_ms: 42000,
  wifi: { ap: true, sta: false, sta_ssid: "", sta_ip: "", ap_ip: "192.168.4.1" },
  input: { throttle: 0.65, steering: 0.2, slow_mode: false, estop: false, armed: true },
  output: { left_target: 0.72, right_target: 0.58, left_actual: 0.70, right_actual: 0.56 },
  monitor: { left_ma: 4200, right_ma: 3800, left_overcurrent: false, right_overcurrent: false, battery_enabled: true, battery_mv: 11800, battery_low: false }
};
const MOCK_CONFIG = { deadzone: 5, expo: 30, max_speed: 100, slow_factor: 50 };
window._originalFetch = window.fetch;
window.fetch = async (url, opts) => {
  if (url === '/status' || url.startsWith('/status?')) return { ok: true, json: async () => ({...MOCK_STATUS}) };
  if (url === '/config') return { ok: true, json: async () => ({...MOCK_CONFIG}) };
  return { ok: true, json: async () => ({ status: 'ok', message: 'mock' }) };
};
</script>`;

function main() {
    const src = fs.readFileSync(SRC, 'utf8');
    const chunks = extractChunks(src);
    if (chunks.length === 0) {
        console.error('ERROR: no httpd_resp_send_chunk string literals found in', SRC);
        process.exit(1);
    }

    const allBytes = [];
    for (const raw of chunks) allBytes.push(...unescapeToBytes(raw));
    let html = Buffer.from(allBytes).toString('utf8');

    if (!html.includes('</body>')) {
        console.error('ERROR: reconstructed HTML has no </body> tag — extraction likely failed');
        process.exit(1);
    }

    // Inject the mock fetch just before </body> so the UI populates with data.
    html = html.replace('</body>', MOCK_SCRIPT + '</body>');

    fs.writeFileSync(OUT, html, 'utf8');
    console.log(`Extracted ${chunks.length} chunks -> ${OUT} (${html.length} bytes)`);
}

main();
