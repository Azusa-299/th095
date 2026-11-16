"""Read-only checker for TH095 `scoreth095.dat` (version 2 container).

Layout and the single decryption pass come from the original `th095.exe`:
`sub_4356D0` (load), `sub_435910` (save) and `sub_435500` (fresh record init).
Payload is decrypted once with (key 172, step 53, block 16, limit = payload size)
and then LZSS-decoded to the declared size. Records are variable length.
"""
import argparse
import struct
from pathlib import Path

MAGIC = 0x35394854  # 'TH95'
HEADER_SIZE = 24
VERSION = 2
KEY, STEP, BLOCK = 172, 53, 16

RECORD_SCORE = 0x5453  # 'ST', 1112 bytes: scores
RECORD_SHOT = 0x4353   # 'SC', 96 bytes: bestshot metadata


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


def checksum(data, size):
    """sub_41BA80: sum of 32-bit words over the size rounded down to 4."""
    total = 0
    for offset in range(0, size - (size & 3), 4):
        total += struct.unpack_from('<I', data, offset)[0]
    return total & 0xFFFFFFFF


def parse_records(body):
    records = []
    position = 0
    while position < len(body):
        if position + 16 > len(body):
            raise ValueError(f'record header truncated at {position}')
        magic, kind, size, stored = struct.unpack_from('<HHII', body, position)
        if size < 16 or position + size > len(body):
            raise ValueError(f'record size {size} invalid at {position}')
        data = body[position:position + size]
        # sub_4356D0 tests (checksum(record) - stored) == stored. The original
        # accumulates in a signed 32-bit int, so both sides wrap modulo 2^32.
        total = checksum(data, size)
        valid = ((total - stored) & 0xFFFFFFFF) == stored
        index = struct.unpack_from('<I', data, 12)[0]
        records.append({
            'magic': magic,
            'kind': kind,
            'size': size,
            'index': index,
            'checksum_ok': valid,
            'data': data,
        })
        position += size
    return records


def read_score_file(path):
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise ValueError('file shorter than the 24-byte header')
    magic, file_size, version, field_0c, payload_size, decoded_size = struct.unpack_from(
        '<IIH2xIII', data, 0)
    if magic != MAGIC:
        raise ValueError(f'bad magic 0x{magic:08X}')
    if version != VERSION:
        raise ValueError(f'container version {version}, expected {VERSION}')
    if 24 + payload_size != len(data):
        raise ValueError(f'payload size {payload_size} disagree with file size {len(data)}')

    payload = bytearray(data[HEADER_SIZE:])
    payload = bytearray(decrypt(payload, KEY, STEP, BLOCK, payload_size))
    body = lzss_decode(bytes(payload), decoded_size)
    return {
        'file_bytes': len(data),
        'file_size_field': file_size,
        'version': version,
        'field_0c': field_0c,
        'payload_bytes': payload_size,
        'decoded_bytes': decoded_size,
        'decoded_actual': len(body),
        'records': parse_records(body),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='a scoreth095.dat sample')
    args = parser.parse_args()

    info = read_score_file(args.source.resolve())
    print(f'{args.source.name}: {info["file_bytes"]} bytes, version {info["version"]}, '
          f'field_0c {info["field_0c"]}, payload {info["payload_bytes"]}, '
          f'decoded {info["decoded_actual"]}/{info["decoded_bytes"]}')
    if info['decoded_actual'] != info['decoded_bytes']:
        print('WARNING: decoded length differs from the declared size')

    score_records = [r for r in info['records'] if r['magic'] == RECORD_SCORE]
    shot_records = [r for r in info['records'] if r['magic'] == RECORD_SHOT]
    bad = [r for r in info['records'] if not r['checksum_ok']]
    print(f'records: {len(info["records"])} total, {len(score_records)} score, '
          f'{len(shot_records)} bestshot, checksum failures {len(bad)}')
    for record in info['records']:
        label = {RECORD_SCORE: 'score', RECORD_SHOT: 'bestshot'}.get(record['magic'], 'unknown')
        state = 'ok' if record['checksum_ok'] else 'BAD'
        index = f'{record["index"]:3}' if record['magic'] == RECORD_SHOT else '  -'
        print(f'  {label:9} kind={record["kind"]} size={record["size"]:5} '
              f'index={index} checksum={state}')

    for record in score_records:
        data = record['data']
        name = data[12:20].decode('cp932', errors='replace')
        print(f'  score record: name={name!r} bytes[20:34]={data[20:34].hex(" ")}')


if __name__ == '__main__':
    main()
