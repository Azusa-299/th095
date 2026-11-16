"""Inspect a TH095 bestshot file without modifying it.

The original loader copies a 24-byte ``BSTS`` header, an optional 80-byte
metadata block, and decodes the remaining image payload with sub_456220.
The checked-in sample uses the byte-group LZSS layout implemented below.
The IDA pseudocode for sub_456220 appears to describe a bit-interleaved
variant, so both decoders are reported until a controlled original write is
available.
"""
import argparse
import struct
from pathlib import Path


RING_SIZE = 0x2000


def byte_lzss_decode(src: bytes, limit: int = 0) -> tuple[bytes, int, bool]:
    """Decode the byte-group stream observed in bs_01_1.dat.

    A flag byte contains eight MSB-first token flags. A literal consumes one
    byte; a back reference consumes a little-endian 12-bit distance and a
    four-bit length (length + 3). Distance zero terminates the stream.
    """
    ring = bytearray(RING_SIZE)
    ring_pos = 1
    pos = 0
    out = bytearray()
    terminated = False
    while pos < len(src) and (not limit or len(out) < limit):
        flags = src[pos]
        pos += 1
        for bit in range(8):
            if limit and len(out) >= limit:
                break
            if flags & (1 << (7 - bit)):
                if pos >= len(src):
                    return bytes(out), pos, False
                value = src[pos]
                pos += 1
                out.append(value)
                ring[ring_pos] = value
                ring_pos = (ring_pos + 1) & (RING_SIZE - 1)
                continue
            if pos + 1 >= len(src):
                return bytes(out), pos, False
            distance = src[pos] | ((src[pos + 1] & 0xF0) << 4)
            length = (src[pos + 1] & 0x0F) + 3
            pos += 2
            if distance == 0:
                terminated = True
                return bytes(out), pos, terminated
            for index in range(length):
                value = ring[(distance + index) & (RING_SIZE - 1)]
                out.append(value)
                ring[ring_pos] = value
                ring_pos = (ring_pos + 1) & (RING_SIZE - 1)
                if limit and len(out) >= limit:
                    break
    return bytes(out), pos, terminated


class BitReader:
    def __init__(self, src: bytes):
        self.src = src
        self.pos = 0
        self.mask = 0x80
        self.value = 0

    def take(self, count: int) -> int | None:
        result = 0
        for _ in range(count):
            if self.mask == 0x80:
                if self.pos >= len(self.src):
                    return None
                self.value = self.src[self.pos]
                self.pos += 1
            result = (result << 1) | (1 if self.value & self.mask else 0)
            self.mask >>= 1
            if self.mask == 0:
                self.mask = 0x80
        return result


def bit_lzss_decode(src: bytes, limit: int = 0) -> tuple[bytes, int, bool]:
    """Decode the flag/data interleaved stream represented by sub_456220."""
    reader = BitReader(src)
    ring = bytearray(RING_SIZE)
    ring_pos = 1
    out = bytearray()
    while not limit or len(out) < limit:
        flag = reader.take(1)
        if flag is None:
            return bytes(out), reader.pos, False
        if flag:
            value = reader.take(8)
            if value is None:
                return bytes(out), reader.pos, False
            out.append(value)
            ring[ring_pos] = value
            ring_pos = (ring_pos + 1) & (RING_SIZE - 1)
            continue
        distance = reader.take(12)
        if distance is None:
            return bytes(out), reader.pos, False
        if distance == 0:
            return bytes(out), reader.pos, True
        length = reader.take(4)
        if length is None:
            return bytes(out), reader.pos, False
        for index in range(length + 3):
            value = ring[(distance + index) & (RING_SIZE - 1)]
            out.append(value)
            ring[ring_pos] = value
            ring_pos = (ring_pos + 1) & (RING_SIZE - 1)
            if limit and len(out) >= limit:
                break
    return bytes(out), reader.pos, False


def checksum(data: bytes) -> int:
    return sum(struct.unpack_from("<I", data, offset)[0]
               for offset in range(0, len(data) - (len(data) & 3), 4)) & 0xFFFFFFFF


def inspect(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 24 or data[:4] != b"BSTS":
        raise ValueError("expected a 24-byte BSTS header")
    header = struct.unpack_from("<6I", data, 0)
    metadata = data[24:104] if len(data) >= 104 else b""
    payload = data[104:] if len(data) >= 104 else b""
    byte_image, byte_pos, byte_term = byte_lzss_decode(payload)
    bit_image, bit_pos, bit_term = bit_lzss_decode(payload)
    return {
        "file_bytes": len(data),
        "header": header,
        "metadata_bytes": len(metadata),
        "payload_bytes": len(payload),
        "byte_image": byte_image,
        "byte_consumed": byte_pos,
        "byte_terminated": byte_term,
        "bit_image": bit_image,
        "bit_consumed": bit_pos,
        "bit_terminated": bit_term,
        "byte_checksum": checksum(data[:104] + byte_image),
        "bit_checksum": checksum(data[:104] + bit_image),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="bestshot/bs_*.dat")
    parser.add_argument("--dump-byte-image", type=Path,
                        help="write the byte-group decoded payload")
    args = parser.parse_args()
    info = inspect(args.source.resolve())
    h = info["header"]
    print(f"{args.source.name}: {info['file_bytes']} bytes, header={h}")
    print(f"metadata={info['metadata_bytes']} payload={info['payload_bytes']}")
    print("byte-group: "
          f"decoded={len(info['byte_image'])} consumed={info['byte_consumed']} "
          f"terminated={info['byte_terminated']} checksum=0x{info['byte_checksum']:08X}")
    print("bit-interleaved: "
          f"decoded={len(info['bit_image'])} consumed={info['bit_consumed']} "
          f"terminated={info['bit_terminated']} checksum=0x{info['bit_checksum']:08X}")
    if args.dump_byte_image:
        args.dump_byte_image.write_bytes(info["byte_image"])
        print(f"wrote {args.dump_byte_image} ({len(info['byte_image'])} bytes)")


if __name__ == "__main__":
    main()
