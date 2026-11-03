#!/usr/bin/env python3
"""Galactic Gus (Quickbeam, 1987) as a .cas the KCDFS menu can load.

The tape has a short machine code file, GUS, that loads at $015E over BASIC's RAM hooks. The
console out hook then runs its own loader at $0200, which reads the rest of the tape with the
ROM's BLKIN. Those blocks are all marked as EOF blocks, so CLOADM and the menu skip them. The
loader puts them, 255 bytes apart:

    $0600 until $1DFF   the title picture
    [$0290] until $7FDB the game ($1E09 on this tape)
    $0334 until $0514   a bit more
    then JMP [$0512]    ($1F40)

This does the same and writes the three parts out as ordinary machine code files, the game last
so its exec address is the one used. The loader itself is left out: the game sets up RSTVEC and
its own stack, and BASIC's hooks stay as they were.

    python fix_galactic_gus.py "Galactic Gus.cas" "Galactic Gus fixed.cas"
"""
import sys

BLOCK_NAME, BLOCK_DATA, BLOCK_EOF = 0x00, 0x01, 0xFF


def read_blocks(data):
    """(type, bytes) of each block with a good checksum, in tape order"""
    blocks = []
    i = 0
    while True:
        j = data.find(b'\x55\x3c', i)
        if j < 0 or j + 4 > len(data):
            return blocks
        btype, length = data[j + 2], data[j + 3]
        body = data[j + 4:j + 4 + length]
        if len(body) < length or j + 4 + length >= len(data):
            return blocks
        if data[j + 4 + length] != (btype + length + sum(body)) & 0xFF:
            sys.exit('checksum error at offset %d' % j)
        blocks.append((btype, body))
        i = j + 5 + length


def load(blocks):
    """Run the tape loader. Returns the RAM and the exec address"""
    ram = bytearray(0x8000)
    written = bytearray(0x8000)

    def store(addr, body):
        ram[addr:addr + len(body)] = body
        written[addr:addr + len(body)] = b'\x01' * len(body)

    # the GUS file, as CLOADM loads it
    if blocks[0][0] != BLOCK_NAME or blocks[0][1][:8] != b'GUS     ':
        sys.exit('this is not the Galactic Gus tape')
    addr = blocks[0][1][13] << 8 | blocks[0][1][14]
    rest = iter(blocks[1:])
    for btype, body in rest:
        if btype == BLOCK_EOF:
            break
        store(addr, body)
        addr += len(body)
    if ram[0x0200:0x0204] != b'\x10\xce\x05\xff':
        sys.exit('the loader at $0200 is not the one this was written for')
    loader = bytes(ram[0x015E:addr])

    def blkin_loop(start, limit):
        x = start
        first = x
        while True:
            btype, body = next(rest)
            store(x, body)
            x += 255
            if x >= limit:
                return first, x - 255 + len(body)

    parts = [blkin_loop(0x0600, 0x1DFF),
             blkin_loop(ram[0x0290] << 8 | ram[0x0291], 0x7FDB),
             blkin_loop(0x0334, 0x0514)]
    exec_addr = ram[0x0512] << 8 | ram[0x0513]
    if any(True for _ in rest):
        print('note: blocks left over after the loader finished')
    return ram, written, parts, exec_addr, loader


def cas_file(name, load_addr, exec_addr, body):
    """One machine code file in .cas form: leader, name block, data blocks, EOF block"""
    def block(btype, data):
        return b'\x55\x3c' + bytes([btype, len(data)]) + data + \
            bytes([(btype + len(data) + sum(data)) & 0xFF]) + b'\x55'

    out = b'\x55' * 128
    out += block(BLOCK_NAME, name.ljust(8).encode() + bytes([2, 0, 0,
                 exec_addr >> 8, exec_addr & 0xFF, load_addr >> 8, load_addr & 0xFF]))
    out += b'\x55' * 128
    for i in range(0, len(body), 255):
        out += block(BLOCK_DATA, body[i:i + 255])
    out += block(BLOCK_EOF, b'')
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    blocks = read_blocks(open(sys.argv[1], 'rb').read())
    ram, written, parts, exec_addr, _ = load(blocks)
    # the picture's last block runs on into the game, which is loaded over it
    parts[0] = (parts[0][0], min(parts[0][1], parts[1][0]))
    names = ['GUSPIC', 'GUSMAIN', 'GUSLOW']
    out = b''
    # the game last, so its exec address is the one that counts
    for i in (0, 2, 1):
        start, end = parts[i]
        print('%-8s $%04X-$%04X' % (names[i], start, end - 1))
        out += cas_file(names[i], start, exec_addr if i == 1 else start, bytes(ram[start:end]))
    print('exec $%04X' % exec_addr)
    open(sys.argv[2], 'wb').write(out)


if __name__ == '__main__':
    main()
