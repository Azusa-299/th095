"""Validate and fingerprint the outer container of TH095 replay files."""
import argparse
import csv
import hashlib
import struct
import sys
from pathlib import Path


MAGIC = b't95r'
HEADER_SIZE = 36
USER_MAGIC = b'USER'


def read_u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def read_user_block(data, offset, expected_type):
    if offset + 12 > len(data) or data[offset:offset + 4] != USER_MAGIC:
        raise ValueError(f'missing USER block at offset {offset}')
    size = read_u32(data, offset + 4)
    block_type = data[offset + 8]
    if size < 12 or offset + size > len(data):
        raise ValueError(f'invalid USER block length at offset {offset}: {size}')
    if block_type != expected_type:
        raise ValueError(
            f'USER block at offset {offset} has type {block_type}, expected {expected_type}'
        )
    return data[offset + 12:offset + size], offset + size


def parse_replay(path):
    data = path.read_bytes()
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise ValueError('invalid t95r header')
    version = read_u32(data, 4)
    if version != 1:
        raise ValueError(f'unsupported replay container version: {version}')

    payload_size = read_u32(data, 28)
    decoded_size = read_u32(data, 32)
    payload_end = HEADER_SIZE + payload_size
    if payload_end > len(data):
        raise ValueError('compressed replay payload exceeds file size')

    metadata, position = read_user_block(data, payload_end, 0)
    _, position = read_user_block(data, position, 1)
    if position != len(data):
        raise ValueError(f'{len(data) - position} unexpected trailing bytes')

    text = metadata.decode('cp932', errors='replace')
    fields = {}
    for line in text.splitlines():
        key, separator, value = line.partition(' ')
        if separator and key in {'Version', 'Name', 'Level', 'Scene', 'Date', 'Score', 'Slow'}:
            if key == 'Slow' and value.startswith('Rate '):
                fields['slow_rate'] = value[5:].strip()
            else:
                fields[key.lower()] = value.strip()

    return {
        'name': path.name,
        'file_bytes': len(data),
        'container_version': version,
        'payload_bytes': payload_size,
        'decoded_bytes': decoded_size,
        'metadata_version': fields.get('version', ''),
        'player_name': fields.get('name', ''),
        'level': fields.get('level', ''),
        'scene': fields.get('scene', ''),
        'date': fields.get('date', ''),
        'score': fields.get('score', ''),
        'slow_rate': fields.get('slow_rate', ''),
        'sha256': hashlib.sha256(data).hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='a replay file or directory containing .rpy files')
    parser.add_argument('--output', type=Path, help='CSV path; defaults to stdout')
    args = parser.parse_args()

    source = args.source.resolve()
    files = [source] if source.is_file() else sorted(source.rglob('*.rpy'))
    if not files:
        parser.error(f'no replay files found under {source}')

    rows = []
    for path in files:
        try:
            rows.append(parse_replay(path))
        except (OSError, ValueError, struct.error) as error:
            parser.error(f'{path}: {error}')

    columns = (
        'name', 'file_bytes', 'container_version', 'payload_bytes', 'decoded_bytes',
        'metadata_version', 'player_name', 'level', 'scene', 'date', 'score',
        'slow_rate', 'sha256',
    )
    if args.output:
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('w', encoding='utf-8', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)
        print(f'Validated {len(rows)} replays; manifest: {output}')
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == '__main__':
    main()
