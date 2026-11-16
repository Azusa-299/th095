"""Inspect version 4 ANM files: pages, sprites and script ids.

Mirrors the container reading in `src/formats/anm.cpp` so that layout questions
can be answered without launching the game. Read-only.
"""
import argparse
import struct
from pathlib import Path


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def f32(data, offset):
    return struct.unpack_from('<f', data, offset)[0]


def parse_anm(data):
    pages = []
    sprites = []
    scripts = {}
    base = 0
    for index in range(4096):
        if base > len(data) or len(data) - base < 64:
            raise ValueError(f'page {index}: header out of range at {base}')
        count_sprites = u32(data, base)
        count_scripts = u32(data, base + 4)
        next_offset = u32(data, base + 56)
        span = next_offset if next_offset else len(data) - base
        if span < 64 or base + span > len(data):
            raise ValueError(f'page {index}: bad span {span}')
        if u32(data, base + 40) != 4:
            raise ValueError(f'page {index}: unsupported version {u32(data, base + 40)}')

        page = {
            'index': index,
            'width': u32(data, base + 12),
            'height': u32(data, base + 16),
            'sprites': count_sprites,
            'scripts': count_scripts,
            'embedded': data[base + 52],
        }
        pages.append(page)

        page_index = len(pages) - 1
        for i in range(count_sprites):
            offset = u32(data, base + 64 + i * 4)
            sprites.append({
                'id': u32(data, base + offset),
                'u': f32(data, base + offset + 4),
                'v': f32(data, base + offset + 8),
                'w': f32(data, base + offset + 12),
                'h': f32(data, base + offset + 16),
                'page': page_index,
            })

        for i in range(count_scripts):
            desc = base + 64 + count_sprites * 4 + i * 8
            script_id = u32(data, desc)
            start = u32(data, desc + 4)
            end = min([span] + [u32(data, base + 48)] * bool(data[base + 52]) +
                      [u32(data, base + 64 + count_sprites * 4 + j * 8 + 4)
                       for j in range(count_scripts)
                       if u32(data, base + 64 + count_sprites * 4 + j * 8 + 4) > start])
            instructions = []
            cursor = start
            while cursor + 8 <= end:
                opcode = u16(data, base + cursor)
                size = u16(data, base + cursor + 2)
                if opcode == 0xFFFF:
                    break
                if size < 8 or cursor + size > span:
                    raise ValueError(f'page {index} script {script_id}: bad size {size}')
                instructions.append({
                    'opcode': opcode,
                    'time': struct.unpack_from('<h', data, base + cursor + 4)[0],
                    'mask': u16(data, base + cursor + 6),
                    'offset': cursor - start,
                    'args': data[base + cursor + 8:base + cursor + size],
                })
                cursor += size
            scripts[script_id] = instructions

        if not next_offset:
            return pages, sprites, scripts
        base += next_offset
    raise ValueError('too many pages')


def describe_instructions(instructions, limit=40):
    lines = []
    for instruction in instructions[:limit]:
        args = struct.unpack_from('<%di' % (len(instruction['args']) // 4),
                                  instruction['args']) if instruction['args'] else ()
        lines.append('    t=%-5d op=%-3d mask=0x%04X args=%s'
                     % (instruction['time'], instruction['opcode'], instruction['mask'], args))
    if len(instructions) > limit:
        lines.append('    ... %d more' % (len(instructions) - limit))
    return '\n'.join(lines)


def write_png(data, width, height, path):
    """Write an RGBA page as PNG."""
    import zlib
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            offset = (y * width + x) * 4
            raw += data[offset:offset + 4]

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack('>I', len(payload)) + body
                + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF))

    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header)
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    path.write_bytes(png)


def extract_page_texture(data, page_index=0):
    """Return (width, height, RGBA pixel bytes) for an embedded page."""
    if len(data) < 64:
        raise ValueError('file too small')
    base = 0
    for _ in range(page_index):
        next_offset = u32(data, base + 56)
        if not next_offset:
            raise ValueError('page index out of range')
        base += next_offset
        if base + 64 > len(data):
            raise ValueError('page header out of range')
    span = u32(data, base + 56) or len(data) - base
    width = u32(data, base + 12)
    height = u32(data, base + 16)
    if not data[base + 52]:
        raise ValueError('page has no embedded texture')
    texture = base + u32(data, base + 48)
    if data[texture:texture + 4] != b'THTX':
        raise ValueError('missing THTX header')
    tex_width = u16(data, texture + 8)
    tex_height = u16(data, texture + 10)
    fmt = u16(data, texture + 6)
    if fmt not in (1, 3, 5, 7):
        raise ValueError('unsupported THTX pixel format')
    start = texture + 16
    bpp = 4 if fmt == 1 else 2
    pixels = bytearray()
    for i in range(tex_width * tex_height):
        if fmt == 1:
            b, g, r, a = data[start + i * 4:start + i * 4 + 4]
        elif fmt in (3, 5):
            value = u16(data, start + i * 2)
            b = (value & 15) * 17
            g = ((value >> 4) & 15) * 17
            r = ((value >> 8) & 15) * 17
            a = ((value >> 12) & 15) * 17
        else:
            b = g = r = data[start + i * 2]
            a = data[start + i * 2 + 1]
        pixels += bytes((r, g, b, a))
    return tex_width, tex_height, bytes(pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='ANM file or directory of them')
    parser.add_argument('--scripts', type=str, default='',
                        help='comma separated script ids to dump')
    parser.add_argument('--ids-only', action='store_true', help='print script ids per file only')
    parser.add_argument('--png', type=Path, help='write the first page texture to this PNG')
    parser.add_argument('--page', type=int, default=0, help='page index for --png')
    args = parser.parse_args()

    files = [args.source] if args.source.is_file() else sorted(args.source.glob('*.anm'))
    wanted = [int(p) for p in args.scripts.split(',') if p.strip()]

    for path in files:
        data = path.read_bytes()
        if args.png:
            try:
                width, height, pixels = extract_page_texture(data, args.page)
                write_png(pixels, width, height, args.png)
                print(f'wrote {args.png} ({width}x{height})')
            except ValueError as error:
                print(f'{path.name}: PNG failed: {error}')
            continue
        try:
            pages, sprites, scripts = parse_anm(data)
        except ValueError as error:
            print(f'{path.name}: FAIL {error}')
            continue
        ids = sorted(scripts)
        if args.ids_only:
            print(f'{path.name}: pages={len(pages)} sprites={len(sprites)} scripts={ids}')
            continue
        print(f'== {path.name}: {len(pages)} page(s), {len(sprites)} sprites')
        for page in pages:
            print('  page %d %dx%d sprites=%d scripts=%d embedded=%d'
                  % (page['index'], page['width'], page['height'],
                     page['sprites'], page['scripts'], page['embedded']))
        print(f'  script ids: {ids}')
        for sprite in sprites[:12]:
            print('  sprite %d page=%d %gx%g at (%g,%g)'
                  % (sprite['id'], sprite['page'], sprite['w'], sprite['h'],
                     sprite['u'], sprite['v']))
        if len(sprites) > 12:
            print(f'  ... {len(sprites) - 12} more sprites')
        for script_id in wanted:
            if script_id not in scripts:
                print(f'  script {script_id}: absent')
                continue
            print(f'  script {script_id} ({len(scripts[script_id])} instructions):')
            print(describe_instructions(scripts[script_id]))


if __name__ == '__main__':
    main()
