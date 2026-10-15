#!/usr/bin/env python3
"""Independent decoder checks for Cruth's PNG and EXR writers.

PNGs are decoded with Python's zlib and the PNG filter rules (and with PIL
when it is installed); EXRs are parsed per the OpenEXR file layout (scanline,
ZIP, HALF/FLOAT). Usage: check_images.py FILE...  Exits non-zero on failure.
"""
import struct
import sys
import zlib


def unfilter_png(data, width, height, bpp):
    stride = width * bpp
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        ftype = data[pos]
        row = bytearray(data[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                row[i] = (row[i] + a) & 255
            elif ftype == 2:
                row[i] = (row[i] + b) & 255
            elif ftype == 3:
                row[i] = (row[i] + (a + b) // 2) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif ftype != 0:
                raise ValueError("bad filter type %d" % ftype)
        out += row
        prev = row
    return bytes(out)


def check_png(path):
    raw = open(path, "rb").read()
    assert raw[:8] == b"\x89PNG\r\n\x1a\n", "bad signature"
    pos, idat, header = 8, b"", None
    while pos < len(raw):
        (length,) = struct.unpack(">I", raw[pos:pos + 4])
        ctype = raw[pos + 4:pos + 8]
        body = raw[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack(">I", raw[pos + 8 + length:pos + 12 + length])
        assert zlib.crc32(ctype + body) & 0xFFFFFFFF == crc, "bad CRC in %r" % ctype
        if ctype == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif ctype == b"IDAT":
            idat += body
        pos += 12 + length
    width, height, depth, color = header[:4]
    channels = {0: 1, 4: 2, 2: 3, 6: 4}[color]
    pixels = unfilter_png(zlib.decompress(idat), width, height, channels * depth // 8)
    assert len(pixels) == width * height * channels * depth // 8
    try:
        from PIL import Image
        Image.open(path).load()
    except ImportError:
        pass
    return "%dx%d %d-bit %d ch" % (width, height, depth, channels)


def check_exr(path):
    raw = open(path, "rb").read()
    magic, version = struct.unpack("<II", raw[:8])
    assert magic == 20000630 and version & 0xFF == 2, "bad magic/version"
    pos, attrs = 8, {}
    while raw[pos] != 0:
        end = raw.index(b"\0", pos)
        name = raw[pos:end].decode()
        tend = raw.index(b"\0", end + 1)
        size = struct.unpack("<i", raw[tend + 1:tend + 5])[0]
        attrs[name] = raw[tend + 5:tend + 5 + size]
        pos = tend + 5 + size
    pos += 1
    channels, cpos, chl = [], 0, attrs["channels"]
    while chl[cpos] != 0:
        end = chl.index(b"\0", cpos)
        ptype = struct.unpack("<i", chl[end + 1:end + 5])[0]
        channels.append((chl[cpos:end].decode(), ptype))
        cpos = end + 17
    assert [c[0] for c in channels] == sorted(c[0] for c in channels), "channels not sorted"
    xmin, ymin, xmax, ymax = struct.unpack("<iiii", attrs["dataWindow"])
    width, height = xmax - xmin + 1, ymax - ymin + 1
    compression = attrs["compression"][0]
    assert compression in (0, 3), "unexpected compression %d" % compression
    lines = 16 if compression == 3 else 1
    blocks = (height + lines - 1) // lines
    offsets = struct.unpack("<%dQ" % blocks, raw[pos:pos + 8 * blocks])
    bytes_per = sum(2 if t == 1 else 4 for _, t in channels)
    values = 0
    for b, off in enumerate(offsets):
        y, size = struct.unpack("<ii", raw[off:off + 8])
        assert y == b * lines, "chunk y mismatch"
        data = raw[off + 8:off + 8 + size]
        rows = min(lines, height - y)
        expected = rows * width * bytes_per
        if compression == 3 and size < expected:
            tmp = bytearray(zlib.decompress(data))
            for i in range(1, len(tmp)):
                tmp[i] = (tmp[i - 1] + tmp[i] - 128) & 255
            half = (len(tmp) + 1) // 2
            data = bytearray(len(tmp))
            data[0::2] = tmp[:half]
            data[1::2] = tmp[half:]
        assert len(data) == expected, "chunk %d has %d bytes, expected %d" % (b, len(data), expected)
        values += rows * width * len(channels)
    assert values == width * height * len(channels)
    return "%dx%d %s" % (width, height, ",".join("%s:%s" % (n, "half" if t == 1 else "float") for n, t in channels))


def main(paths):
    failed = 0
    for p in paths:
        try:
            info = check_png(p) if p.endswith(".png") else check_exr(p)
            print("ok   %s  %s" % (p, info))
        except Exception as e:  # noqa: BLE001 - report every failure
            print("FAIL %s  %s" % (p, e))
            failed += 1
    return 1 if failed or not paths else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
