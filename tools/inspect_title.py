from pathlib import Path
import struct
from PIL import Image

root = Path(__file__).resolve().parents[1]
data = (root / 'build/audit-fixed/title.anm').read_bytes()
offset = 0
block = 0
while True:
    h = struct.unpack_from('<16I', data, offset)
    name_start = offset + h[7]
    name = data[name_start:data.index(0, name_start)].decode()
    print('BLOCK', block, 'at', offset, h, name)
    for i in range(h[0]):
        sp = struct.unpack_from('<I', data, offset + 64 + i * 4)[0]
        print('SPRITE', struct.unpack_from('<I4f', data, offset + sp))
    for i in range(h[1]):
        ident, pos = struct.unpack_from('<II', data, offset + 64 + h[0] * 4 + i * 8)
        print('SCRIPT', ident, 'at', pos)
        cursor = offset + pos
        for _ in range(500):
            opcode, size, time, mask = struct.unpack_from('<HHhH', data, cursor)
            if opcode == 0xFFFF:
                break
            if size < 8 or cursor + size > len(data):
                break
            args = data[cursor+8:cursor+size]
            print(' ', cursor - offset - pos, opcode, time, mask, args.hex(),
                  list(struct.unpack('<'+'f'*(len(args)//4), args)) if len(args)%4==0 else '')
            cursor += size
    pixels = data[offset+h[12]+16:]
    if h[13] and h[5] in (1,3,5):
        rgba = bytearray()
        for i in range(h[3]*h[4]):
            if h[5] == 5:
                value = struct.unpack_from('<H', pixels, i*2)[0]
                rgba.extend((((value>>8)&15)*17,((value>>4)&15)*17,(value&15)*17,((value>>12)&15)*17))
            else:
                b,g,r,a = pixels[i*4:i*4+4]
                rgba.extend((r,g,b,a if h[5]==1 else 255))
        Image.frombytes('RGBA', (h[3],h[4]), bytes(rgba)).save(root/f'build/title-block-{block}.png')
    if not h[14]:
        break
    offset += h[14]
    block += 1
