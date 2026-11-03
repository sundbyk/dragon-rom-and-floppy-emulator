"""Dragon/CoCo cassette recording (.wav) to .cas.

The tape is FSK: a 1 bit is one cycle at about 2400 Hz, a 0 bit one cycle at about 1200 Hz,
least significant bit first. Blocks are a leader of $55s, the $3C sync byte, type, length, data
and checksum. The cycles are timed between rising zero crossings, then the bits are searched
for the $55 $3C pattern and the blocks read out. Only blocks with a good checksum are kept.

python wav2cas.py in.wav out.cas    exit code 0 if at least one good file was found
"""
import struct
import sys
import wave


def samples(path):
    w = wave.open(path, 'rb')
    ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    w.close()
    if width == 1:
        data = [b - 128 for b in raw[::ch]]
    elif width == 2:
        data = list(struct.unpack('<%dh' % (len(raw) // 2), raw))[::ch]
    else:
        step = width * ch
        data = [int.from_bytes(raw[i + width - 2:i + width], 'little', signed=True) for i in range(0, len(raw), step)]
    return data, rate


def bits(data, rate, invert=False):
    """Cycle lengths between rising zero crossings -> bits (1 short, 0 long)."""
    # remove DC with a slow running average
    avg = 0.0
    prev = 0
    last = None
    out = []
    threshold = rate / 1800.0      # between 1200 Hz and 2400 Hz cycle lengths
    for i, s in enumerate(data):
        avg += (s - avg) * 0.001
        v = s - avg
        if invert:
            v = -v
        if prev < 0 <= v:
            if last is not None:
                n = i - last
                if n < rate / 4800.0 * 0.6 or n > rate / 600.0:
                    out.append(None)        # noise or silence
                else:
                    out.append(1 if n < threshold else 0)
            last = i
        prev = v
    return out


def to_bytes_at(b, pos, count):
    res = []
    for k in range(count):
        v = 0
        for j in range(8):
            bit = b[pos + k * 8 + j] if pos + k * 8 + j < len(b) else None
            if bit is None:
                return None
            v |= bit << j
        res.append(v)
    return res


def blocks(b):
    """Find $55 $3C and read the blocks after it."""
    out = []
    i = 0
    n = len(b)
    pat = [1, 0, 1, 0, 1, 0, 1, 0] + [0, 0, 1, 1, 1, 1, 0, 0]     # $55 then $3C, LSB first
    while i < n - 40:
        if b[i:i + 16] == pat:
            p = i + 16
            hdr = to_bytes_at(b, p, 2)
            if hdr is not None:
                typ, ln = hdr
                body = to_bytes_at(b, p + 16, ln + 1)
                if body is not None and (typ + ln + sum(body[:ln])) & 0xFF == body[ln] and typ in (0, 1, 0xFF):
                    out.append((typ, bytes(body[:ln])))
                    i = p + 16 + (ln + 1) * 8
                    continue
        i += 1
    return out


def cas(blks):
    o = bytearray()
    for typ, data in blks:
        o += b'\x55' * (128 if typ == 0 else 2)
        o += bytes([0x55, 0x3C, typ, len(data)]) + data + bytes([(typ + len(data) + sum(data)) & 0xFF, 0x55])
    return bytes(o)


def convert(src):
    data, rate = samples(src)
    best = []
    for inv in (False, True):
        bl = blocks(bits(data, rate, inv))
        if len(bl) > len(best):
            best = bl
    return best


if __name__ == '__main__':
    bl = convert(sys.argv[1])
    names = sum(1 for t, _ in bl if t == 0)
    if names:
        open(sys.argv[2], 'wb').write(cas(bl))
    print('%d blocks, %d files' % (len(bl), names))
    sys.exit(0 if names else 1)
