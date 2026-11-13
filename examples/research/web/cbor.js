// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
// Restricted definite-length CBOR used by research.v1. No eval, indefinite
// containers, tags, extension objects, or prototype-bearing map instances.
export function decode(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  let offset = 0;
  const byte = () => { if (offset >= bytes.length) throw Error('Truncated CBOR'); return bytes[offset++]; };
  const size = additional => {
    if (additional < 24) return additional;
    const count = {24: 1, 25: 2, 26: 4, 27: 8}[additional];
    if (!count) throw Error('Unsupported CBOR length');
    let n = 0; for (let i = 0; i < count; i++) n = n * 256 + byte();
    if (!Number.isSafeInteger(n)) throw Error('CBOR integer exceeds safe range');
    return n;
  };
  function item(depth = 0) {
    if (depth > 24) throw Error('CBOR depth limit');
    const head = byte(), major = head >> 5, extra = head & 31;
    if (major === 7) {
      if (extra === 20) return false; if (extra === 21) return true; if (extra === 22) return null;
      throw Error('Unsupported CBOR simple value');
    }
    const n = size(extra);
    if (major === 0) return n;
    if (major === 1) { if (!Number.isSafeInteger(-1 - n)) throw Error('Unsafe integer'); return -1 - n; }
    if (n > bytes.length - offset) throw Error('CBOR length exceeds input');
    if (major === 2 || major === 3) {
      const data = bytes.slice(offset, offset += n);
      return major === 2 ? data : new TextDecoder('utf-8', {fatal: true}).decode(data);
    }
    if (major === 4) return Array.from({length: n}, () => item(depth + 1));
    if (major === 5) {
      const out = Object.create(null);
      for (let i = 0; i < n; i++) {
        const key = item(depth + 1);
        if (typeof key !== 'string' || Object.hasOwn(out, key)) throw Error('Invalid or duplicate map key');
        out[key] = item(depth + 1);
      }
      return out;
    }
    throw Error('Unsupported CBOR type');
  }
  const result = item(); if (offset !== bytes.length) throw Error('Trailing CBOR'); return result;
}
export function encode(value) {
  const out = [], byte = n => out.push(n & 255);
  function head(major, n) {
    if (!Number.isSafeInteger(n) || n < 0) throw Error('Invalid integer');
    if (n < 24) return byte((major << 5) | n);
    const count = n <= 255 ? 1 : n <= 65535 ? 2 : n <= 0xffffffff ? 4 : 8;
    byte((major << 5) | ({1: 24, 2: 25, 4: 26, 8: 27})[count]);
    const v = BigInt(n); for (let i = count - 1; i >= 0; i--) byte(Number((v >> BigInt(i * 8)) & 255n));
  }
  function item(v) {
    if (typeof v === 'boolean') return byte(v ? 0xf5 : 0xf4);
    if (v === null) return byte(0xf6);
    if (typeof v === 'number') return v >= 0 ? head(0, v) : head(1, -1 - v);
    if (typeof v === 'string') { const b = new TextEncoder().encode(v); head(3, b.length); out.push(...b); return; }
    if (v instanceof Uint8Array) { head(2, v.length); out.push(...v); return; }
    if (Array.isArray(v)) { head(4, v.length); v.forEach(item); return; }
    if (typeof v === 'object') { const keys = Object.keys(v); head(5, keys.length); for (const k of keys) { item(k); item(v[k]); } return; }
    throw Error('Unsupported value');
  }
  item(value); return Uint8Array.from(out);
}
