#!/usr/bin/env python3
"""A raw machine code binary as a .cas that CLOADM (and the menu) can load.

    python bin2cas.py prog.bin PROG.cas NAME LOAD EXEC     (LOAD and EXEC in hex)
"""
import sys


def block(btype, data):
    return b'\x55' * 2 + b'\x3c' + bytes([btype, len(data)]) + data + \
        bytes([(btype + len(data) + sum(data)) & 0xFF]) + b'\x55'


def cas(name, body, load, exec_):
    head = name.ljust(8)[:8].encode() + bytes([2, 0, 0, exec_ >> 8, exec_ & 0xFF, load >> 8, load & 0xFF])
    out = b'\x55' * 128 + block(0, head) + b'\x55' * 128
    for i in range(0, len(body), 255):
        out += block(1, body[i:i + 255])
    return out + block(0xFF, b'')


if __name__ == '__main__':
    src, dst, name, load, exec_ = sys.argv[1:6]
    open(dst, 'wb').write(cas(name, open(src, 'rb').read(), int(load, 16), int(exec_, 16)))
