"""Read-only TH095 ECL instruction/timeline dump (12-byte enemy headers)."""
import struct
import sys
from pathlib import Path

for name in sys.argv[1:]:
    d = Path(name).read_bytes()
    count, timelines = struct.unpack_from('<HH', d, 4)
    print('\nFILE', name, 'scripts', count, 'timelines', timelines)
    for i in range(count):
        p = struct.unpack_from('<I', d, 0x48 + i * 4)[0]
        print('SCRIPT', i, hex(p))
        while p + 12 <= len(d):
            t, op, size, rank, mask = struct.unpack_from('<IHHHH', d, p)
            if size < 12 or p + size > len(d):
                raise ValueError((p, size))
            args = []
            for j in range((size - 12) // 4):
                n = struct.unpack_from('<i', d, p + 12 + j * 4)[0]
                f = struct.unpack_from('<f', d, p + 12 + j * 4)[0]
                args.append(f'{n}' if abs(n) < 100000 else f'{f:g}f')
            print(f' {p:04x} t={t:4} op={op:3} mask={mask:04x}', ', '.join(args))
            p += size
            if op == 65535:
                break
    for i in range(timelines):
        start, end = struct.unpack_from('<II', d, 8 + 4 * i)
        print('TIMELINE', i, hex(start), hex(end))
        print(d[start:end].hex(' '))
