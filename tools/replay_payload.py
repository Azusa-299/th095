"""Decrypt and LZSS-decode TH095 replay payloads using the original parameters.

Parameters come from the original `th095.exe` at `0x435130` (see
docs/replay-format.md): after the 36-byte `t95r` container header the payload is
decrypted twice with (key, step, block, limit) = (170, 225, 1024, payload_size)
then (61, 122, 128, payload_size), and the result is one LZSS stream that must
expand to the container's decoded size.
"""
import argparse
import struct
from pathlib import Path

MAGIC = b't95r'
HEADER_SIZE = 36
USER_MAGIC = b'USER'

# 0x4352A7 / 0x4352D1 call sites of sub_41A5C0.
REPLAY_KEYS = ((170, 225, 1024), (61, 122, 128))


def decrypt(buf, key, step, block, limit):
    size = len(buf)
    if size == 0 or block < 2:
        return bytes(buf)
    rem = size % block
    tail = rem if rem < block // 4 else 0
    skipped = tail + (size & 1)
    if skipped >= size or limit == 0:
        return bytes(buf)

    keep = min(limit, size)
    src = bytes(buf[:keep])
    out = bytearray(buf)
    src_pos = 0
    span = size - skipped
    pos = 0
    room = limit

    while span > 0 and room > 0:
        if span < block:
            block = span
        if block < 2:
            break

        w = pos + block - 1
        for _ in range((block + 1) // 2):
            if src_pos >= keep:
                break
            out[w] = key ^ src[src_pos]
            w -= 2
            key = (key + step) & 0xFF
            src_pos += 1

        w = pos + block - 2
        for _ in range(block // 2):
            if src_pos >= keep:
                break
            out[w] = key ^ src[src_pos]
            w -= 2
            key = (key + step) & 0xFF
            src_pos += 1

        span -= block
        pos += block
        room = room - block if room > block else 0

    return bytes(out)


def lzss_decode(src, out_size):
    """Mirror of sub_456220: one stream, 8K ring, 13-bit address, 4-bit length."""
    out = bytearray()
    ring = bytearray(0x2000)
    rp = 1
    pos = 0
    bits = 0
    nbits = 0

    def bit():
        nonlocal pos, bits, nbits
        if nbits == 0:
            if pos >= len(src):
                return None
            bits = src[pos]
            pos += 1
            nbits = 8
        value = (bits >> 7) & 1
        bits = (bits << 1) & 0xFF
        nbits -= 1
        return value

    def take(count):
        value = 0
        for _ in range(count):
            b = bit()
            if b is None:
                return None
            value = (value << 1) | b
        return value

    while len(out) < out_size:
        flag = bit()
        if flag is None:
            break
        if flag == 1:
            literal = take(8)
            if literal is None:
                break
            out.append(literal)
            ring[rp] = literal
            rp = (rp + 1) & 0x1FFF
            continue

        dist = take(13)
        if dist is None or dist == 0:
            break
        length = take(4)
        if length is None:
            break
        for i in range(length + 3):
            if len(out) >= out_size:
                break
            byte = ring[(dist + i) & 0x1FFF]
            out.append(byte)
            ring[rp] = byte
            rp = (rp + 1) & 0x1FFF

    return bytes(out)


def decode_payload(data):
    payload_size = struct.unpack_from('<I', data, 28)[0]
    decoded_size = struct.unpack_from('<I', data, 32)[0]
    payload = bytearray(data[HEADER_SIZE:HEADER_SIZE + payload_size])
    if len(payload) != payload_size:
        raise ValueError('payload truncated')
    for key, step, block in REPLAY_KEYS:
        payload = bytearray(decrypt(payload, key, step, block, payload_size))
    return lzss_decode(bytes(payload), decoded_size), decoded_size


def describe(decoded):
    """Print a few structural landmarks of a decoded replay body."""
    fields = []
    if len(decoded) >= 0xF8:
        offset = struct.unpack_from('<I', decoded, 0xF0)[0]
        fields.append(f'u32@0xF0={offset}')
        fields.append(f'0xF8+offset=0x{0xF8 + offset:X}')
    return ', '.join(fields)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='a replay file or a directory of .rpy files')
    parser.add_argument('--dump', type=Path, help='write the decoded first sample here')
    args = parser.parse_args()

    source = args.source.resolve()
    files = [source] if source.is_file() else sorted(source.rglob('*.rpy'))
    if not files:
        parser.error(f'no replay files found under {source}')

    ok = 0
    short = 0
    for path in files:
        data = path.read_bytes()
        if data[:4] != MAGIC:
            print(f'FAIL {path.name}: bad magic')
            continue
        try:
            decoded, want = decode_payload(data)
        except ValueError as error:
            print(f'FAIL {path.name}: {error}')
            continue
        status = 'exact' if len(decoded) == want else 'short'
        if status == 'exact':
            ok += 1
        else:
            short += 1
        if len(files) <= 3 or status != 'exact':
            print(f'{status:5} {path.name}: {len(decoded)}/{want} {describe(decoded)}')
            if args.dump and len(files) <= 3:
                args.dump.write_bytes(decoded)

    print(f'\n{len(files)} files: exact={ok} short={short}')


if __name__ == '__main__':
    main()
