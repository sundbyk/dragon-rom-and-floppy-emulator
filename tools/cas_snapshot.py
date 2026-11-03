#!/usr/bin/env python3
"""Turn a Dragon 32 tape that the menu can not start into one it can.

Some tapes do more than CLOADM or CLOAD: a BASIC program that CLOADMs the rest, or machine code
with its own tape loader (turbo, protection, blocks marked as EOF ...). The menu has no tape to
give them. So this lets XRoar load the tape the real way, and takes a snapshot each time the
tape motor goes off. The last one is the machine just after the tape has loaded. That becomes a
new .cas:

    'SNAP'  machine code file, all 32K of RAM, at $0000
    'STATE' a file of type 'S' (ours): the PIA and SAM registers, and the 6809's registers

The stm32f4 hands both to the menu, which copies RAM in, sets up the PIAs and SAM, and carries on
from the registers with an RTI (see CAS_RESUME in menu/menu.s). Games that load more from tape
later on (levels) still can not work.

While XRoar runs, its GDB target is used to see when the loading is done, and to take a picture
of the screen a little after it, so the results can be looked through (--report).

    python cas_snapshot.py --xroar D:/xroar/xroar.exe Game.cas Game-fixed.cas
    python cas_snapshot.py --xroar ... --batch list.txt --out DIR --report report.html -j 6

Needs XRoar (1.x, with -gdb and -snap-motoroff) and a Dragon 32 ROM it can find. For Coco
tapes add --xroar-arg=-machine --xroar-arg=coco --xroar-arg=-ram --xroar-arg=32
--xroar-arg=-no-machine-cart (see the README).
"""
import argparse, base64, concurrent.futures, html, json, os, shutil, socket, struct, subprocess
import sys, tempfile, time, zlib

RAM_SIZE = 0x8000
FILETYPE_MC, FILETYPE_STATE = 2, ord('S')


# ---------------------------------------------------------------------------------------------
# XRoar snapshots. A tree of parts; each has fields: tag, length, value, 0. A length of 0 opens a
# struct of fields that ends with a 0 tag. Numbers are variable length, like UTF-8.

def vuint(b):
    if not b:
        return 0
    c = b[0]
    n, mask = 0, 0x7F
    while c & (0x80 >> n):
        n += 1
        mask >>= 1
    v = c & mask
    for k in range(1, n + 1 if n else 1):
        v = v << 8 | b[k]
    return v


def fields(d, i):
    """dict tag -> bytes (or dict for a struct), and where it ended"""
    out = {}
    while True:
        tag = d[i]
        i += 1
        if tag == 0:
            return out, i
        n = d[i]
        i += 1
        if n == 0:
            out[tag], i = fields(d, i)
        else:
            out[tag] = d[i:i + n]
            i += n + 1


def part(d, name):
    """the fields of the part called name (eg. CPU, PIA0)"""
    key = bytes([1, len(name)]) + name.encode()
    i = d.find(key)
    if i < 0:
        raise ValueError('no %s in the snapshot' % name)
    i += len(key)
    i += 2 + d[i + 1]               # its type, eg. MC6809
    f, _ = fields(d, i)
    return f


def read_snapshot(path):
    d = open(path, 'rb').read()
    i = d.find(b'\x01\x03RAM')
    j = d.find(b'\xc0\x80\x00', i)  # the 32K bank, as a length
    if i < 0 or j < 0 or j - i > 64:
        raise ValueError('no 32K RAM in the snapshot')
    ram = d[j + 3:j + 3 + RAM_SIZE]
    cpu = {k: vuint(v) for k, v in part(d, 'CPU').items() if isinstance(v, bytes)}
    regs = dict(cc=cpu[0x09], d=cpu[0x0a], dp=cpu[0x0b], x=cpu[0x0c], y=cpu[0x0d], u=cpu[0x0e],
                s=cpu[0x0f], pc=cpu[0x10])
    pias = {}
    for name, base in (('PIA0', 0xFF00), ('PIA1', 0xFF20)):
        p = part(d, name)
        for side, off in ((1, 0), (2, 2)):
            f = p[side]
            pias[base + off] = dict(cr=vuint(f.get(1, b'')), ddr=vuint(f.get(2, b'')),
                                    out=vuint(f.get(3, b'')))
    sam = part(d, 'SAM')
    v = vuint(sam.get(0x11, b''))
    f = vuint(sam.get(0x12, b'')) >> 9
    return ram, regs, pias, (v, f)


def state_writes(pias, sam):
    """the (address, byte) writes that put the PIAs and SAM back"""
    w = []
    for data, p in sorted(pias.items()):
        cr = data + 1
        w += [(cr, p['cr'] & 0x3B), (data, p['ddr']), (cr, p['cr'] & 0x3F), (data, p['out'])]
    v, f = sam
    for bit in range(3):
        w.append((0xFFC0 + 2 * bit + (v >> bit & 1), 0))
    for bit in range(7):
        w.append((0xFFC6 + 2 * bit + (f >> bit & 1), 0))
    return w


def make_cas(ram, regs, pias, sam):
    def block(btype, data):
        return b'\x55' * 2 + b'\x3c' + bytes([btype, len(data)]) + data + \
            bytes([(btype + len(data) + sum(data)) & 0xFF]) + b'\x55'

    def file(name, ftype, load, body):
        out = b'\x55' * 128 + block(0, name.ljust(8).encode() + bytes([ftype, 0, 0, 0, 0,
                                    load >> 8, load & 0xFF])) + b'\x55' * 128
        for i in range(0, len(body), 255):
            out += block(1, body[i:i + 255])
        return out + block(0xFF, b'')

    w = state_writes(pias, sam)
    st = struct.pack('>H', len(w))
    for a, b in w:
        st += struct.pack('>HB', a, b)
    st += struct.pack('>HHHHHBBBB', regs['s'], regs['pc'], regs['u'], regs['y'], regs['x'],
                      regs['dp'], regs['d'] & 0xFF, regs['d'] >> 8, regs['cc'])
    return file('SNAP', FILETYPE_MC, 0, bytes(ram)) + file('STATE', FILETYPE_STATE, 0, st)


# ---------------------------------------------------------------------------------------------
# Running XRoar

class Gdb:
    def __init__(self, port, proc, wait=20):
        t = time.time() + wait
        while True:
            try:
                self.s = socket.create_connection(('127.0.0.1', port), timeout=10)
                break
            except OSError:
                if time.time() > t or proc.poll() is not None:
                    raise OSError('XRoar did not open its GDB port')
                time.sleep(0.2)
        self.buf = b''

    def _packet(self):
        while True:
            if b'$' in self.buf:
                j = self.buf.index(b'$')
                k = self.buf.find(b'#', j)
                if k >= 0 and len(self.buf) >= k + 3:
                    body = self.buf[j + 1:k]
                    self.buf = self.buf[k + 3:]
                    self.s.sendall(b'+')
                    return body.decode('latin1')
            self.buf += self.s.recv(65536)

    def cmd(self, c, reply=True):
        p = c.encode('latin1')
        self.s.sendall(b'$' + p + b'#%02x' % (sum(p) & 0xFF))
        return self._packet() if reply else None

    def stop(self):
        self.s.sendall(b'\x03')
        return self._packet()

    def go(self):
        self.cmd('c', reply=False)

    def mem(self, addr, n):
        out = b''
        while n:
            k = min(n, 1024)
            out += bytes.fromhex(self.cmd('m%x,%x' % (addr, k)))
            addr, n = addr + k, n - k
        return out

    def poke(self, addr, data):
        return self.cmd('M%x,%x:%s' % (addr, len(data), data.hex()))

    def regs(self):
        g = bytes.fromhex(self.cmd('g').replace('x', '0'))
        return dict(cc=g[0], a=g[1], b=g[2], dp=g[3], x=g[4] << 8 | g[5], y=g[6] << 8 | g[7],
                    u=g[8] << 8 | g[9], s=g[10] << 8 | g[11], pc=g[12] << 8 | g[13])


def motor_on(g):
    return bool(g.mem(0xFF21, 1)[0] & 0x08)


def gdb_state(g):
    """The machine as read_snapshot gives it, read over GDB with the CPU stopped. For tapes that
    leave the motor on, so there is no motor-off snapshot. A PIA data register reads its pins,
    which for the output bits is what was written. The DDR is read by clearing bit 2 of the
    control register for a moment."""
    ram = g.mem(0, RAM_SIZE)
    r = g.regs()
    regs = dict(cc=r['cc'], d=r['a'] << 8 | r['b'], dp=r['dp'], x=r['x'], y=r['y'], u=r['u'],
                s=r['s'], pc=r['pc'])
    pias = {}
    for data in (0xFF00, 0xFF02, 0xFF20, 0xFF22):
        cr = g.mem(data + 1, 1)[0]
        g.poke(data + 1, bytes([cr & 0x3B]))
        ddr = g.mem(data, 1)[0]
        g.poke(data + 1, bytes([cr & 0x3F]))
        pias[data] = dict(cr=cr, ddr=ddr, out=g.mem(data, 1)[0])
    sam = int(g.cmd('qxroar.sam') or '0', 16)
    return ram, regs, pias, (sam & 7, sam >> 3 & 0x7F)


def screen_state(g):
    """what is needed to draw the screen"""
    sam = int(g.cmd('qxroar.sam') or '0', 16)
    base = (sam >> 3 & 0x7F) << 9
    return dict(sam=sam, base=base, ff22=g.mem(0xFF22, 1)[0], vram=g.mem(base, 6144).hex())


def run_tape(xroar, cas, work, port, settle=4.0, limit=240.0, extra=(), typed=None,
             motor_left_on=False):
    """Load cas in XRoar. Returns the machine just after the last motor-off (ram, regs, pias,
    sam), or None, and how it went. typed: keys to type instead of -run. motor_left_on: if the
    motor is still on at the limit, take the machine as it is then."""
    snap = os.path.join(work, 'motoroff.sna')
    if os.path.exists(snap):
        os.remove(snap)
    args = [xroar, '-machine', 'dragon32', '-no-ratelimit', '-ao', 'null', '-vo', 'null',
            '-gdb', '-gdb-port', str(port), '-snap-motoroff', snap.replace('\\', '/')] + \
        list(extra) + (['-run', cas] if typed is None else ['-load-tape', cas, '-type', typed])
    # from its own folder, where it looks for the ROMs
    proc = subprocess.Popen(args, cwd=os.path.dirname(os.path.abspath(xroar)),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    info = dict(result='?')
    try:
        g = Gdb(port, proc)
        g.go()
        # Loading is too quick to watch the motor. Done when the motor is off and there has been
        # no new motor-off snapshot for a while
        start = time.time()
        last = None
        while True:
            time.sleep(0.5)
            now = time.time()
            mtime = os.path.getmtime(snap) if os.path.exists(snap) else None
            if mtime != last:
                last, changed = mtime, now
            if mtime is not None and now - changed >= settle:
                g.stop()
                if not motor_on(g):
                    break
                g.go()
            if now - start > limit:
                g.stop()
                break
        info.update(screen_state(g))
        r = g.regs()
        info['pc'] = r['pc']
        info['motor'] = motor_on(g)
        state = None
        if now - start > limit and info['motor'] and motor_left_on:
            # It may well be running, with the motor left on. Take it as it is now, motor off
            state = gdb_state(g)
            state[2][0xFF20]['cr'] &= ~0x08
            info['result'] = 'motor left on'
        elif last is None:
            info['result'] = 'tape never stopped'
        elif now - start > limit and info['motor']:
            info['result'] = 'timeout'
        else:
            info['result'] = 'loaded'
        # BASIC's prompt, with no program running? (CURLIN is $FFFF in direct mode)
        info['at_basic'] = 0x8000 <= r['pc'] < 0xC000 and g.mem(0x68, 2) == b'\xff\xff'
    finally:
        proc.kill()
        proc.wait()
    if state is None and info['result'] == 'loaded':
        state = read_snapshot(snap)
    return state, info


# ---------------------------------------------------------------------------------------------
# Pictures of the screen, for the report

TEXT_COLOURS = [(0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 0),
                (255, 255, 255), (0, 255, 255), (255, 0, 255), (255, 128, 0)]


def png(w, h, rows):
    raw = b''.join(b'\x00' + bytes(sum(r, ())) for r in rows)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


QUADRANTS = ' ▗▖▄▝▐▞▟▘▚▌▙▀▜▛█'


def draw_text(info):
    """HTML for a text screen: 32x16 characters, semigraphics as quadrant blocks"""
    vram = bytes.fromhex(info['vram'])
    css = info['ff22'] >> 3 & 1
    fg, bg = ('#0f0', '#030') if not css else ('#f80', '#310')
    out = []
    for y in range(16):
        for x in range(32):
            c = vram[y * 32 + x]
            if c & 0x80:
                col = '#%02x%02x%02x' % TEXT_COLOURS[c >> 4 & 7]
                out.append('<i style="color:%s">%s</i>' % (col, QUADRANTS[c & 15]))
            else:
                ch = html.escape(chr((c & 0x3F) ^ 0x40 if (c & 0x3F) < 0x20 else c & 0x3F))
                out.append('<i class="r">%s</i>' % ch if not c & 0x40 else ch)
        out.append('\n')
    return '<pre style="color:%s;background:%s">%s</pre>' % (fg, bg, ''.join(out))


def draw(info):
    """256x192 PNG of a graphics screen"""
    vram = bytes.fromhex(info['vram'])
    ff22 = info['ff22']
    css = ff22 >> 3 & 1
    black = (0, 0, 0)
    gm = ff22 >> 4 & 7
    width = [64, 128, 128, 128, 128, 128, 128, 256][gm]
    height = [64, 64, 64, 96, 96, 192, 192, 192][gm]
    two = gm & 1
    pal = ([(0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 0)] if not css else
           [(255, 255, 255), (0, 255, 255), (255, 0, 255), (255, 128, 0)])
    on = (0, 255, 0) if not css else (255, 255, 255)
    bpr = width // 8 if two else width // 4
    rows = []
    for y in range(192):
        sy = y * height // 192
        row = []
        for x in range(256):
            sx = x * width // 256
            if two:
                b = vram[sy * bpr + sx // 8] if sy * bpr + sx // 8 < len(vram) else 0
                row.append(on if b >> (7 - sx % 8) & 1 else black)
            else:
                b = vram[sy * bpr + sx // 4] if sy * bpr + sx // 4 < len(vram) else 0
                row.append(pal[b >> (6 - 2 * (sx % 4)) & 3])
        rows.append(row)
    return png(256, 192, rows)


# ---------------------------------------------------------------------------------------------

def convert(xroar, src, dst, port, **kw):
    work = tempfile.mkdtemp(prefix='cas_snapshot_')
    # XRoar can not open a path with eg. an accent in it
    tape = os.path.join(work, 'tape.cas')
    shutil.copyfile(src, tape)
    state, info = run_tape(xroar, tape, work, port, **kw)
    info['src'] = src
    if state:
        ram, regs, pias, sam = state
        os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
        open(dst, 'wb').write(make_cas(ram, regs, pias, sam))
        info['dst'] = dst
        info['resume_pc'] = regs['pc']
    for f in os.listdir(work):
        os.remove(os.path.join(work, f))
    os.rmdir(work)
    return info


def verdict(i):
    if i['result'] == 'motor left on' and i['at_basic']:
        return 'motor left on, back at BASIC'
    if i['result'] != 'loaded':
        return i['result']
    if i['motor']:
        return 'wants more tape'
    if i['at_basic']:
        return 'back at BASIC'
    return 'looks fine'


def report(results, path):
    cells = []
    for i in sorted(results, key=lambda r: r['src'].lower()):
        img = ''
        if 'vram' in i and not i['ff22'] & 0x80:
            img = draw_text(i)
        elif 'vram' in i:
            img = '<img src="data:image/png;base64,%s">' % base64.b64encode(draw(i)).decode()
        v = verdict(i)
        cells.append('<figure class="%s">%s<figcaption><b>%s</b><br>%s</figcaption></figure>' % (
            'ok' if v == 'looks fine' else 'bad', img, html.escape(i['src']), html.escape(v)))
    open(path, 'w', encoding='utf-8').write('''<!doctype html><meta charset="utf-8">
<title>Tape snapshots</title><style>
body{font:13px system-ui;background:#111;color:#ddd;margin:16px}
figure{display:inline-block;width:256px;margin:8px;vertical-align:top}
img{width:256px;height:192px;image-rendering:pixelated;display:block}
pre{width:256px;height:192px;margin:0;font:12px/12px monospace;overflow:hidden;letter-spacing:-0.2px}
pre i{font-style:normal}pre .r{filter:invert(1);background:#0f0;color:#030}
.bad figcaption{color:#f88}</style>
<h1>Tape snapshots</h1>''' + ''.join(cells))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--xroar', required=True)
    ap.add_argument('src', nargs='?')
    ap.add_argument('dst', nargs='?')
    ap.add_argument('--batch', help='file with one .cas path per line')
    ap.add_argument('--root', help='paths in --batch are relative to this')
    ap.add_argument('--out', help='--batch: write the new .cas files under this, same layout')
    ap.add_argument('--report', help='HTML page with a picture of each screen')
    ap.add_argument('--json', help='write the results here too')
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('--limit', type=float, default=240, help='give up after this many seconds')
    ap.add_argument('--motor-left-on', action='store_true',
                    help='if the motor is still on at --limit, take the machine as it is then')
    ap.add_argument('--type', help='type this (eg. CLOAD\\r) instead of letting XRoar -run it')
    ap.add_argument('--xroar-arg', action='append', default=[],
                    help='more for XRoar, eg. --xroar-arg=-no-tape-fast')
    a = ap.parse_args()
    kw = dict(limit=a.limit, motor_left_on=a.motor_left_on, extra=a.xroar_arg,
              typed=a.type and a.type.replace('\\r', '\r'))
    if a.batch:
        jobs = [l.strip() for l in open(a.batch, encoding='utf-8') if l.strip()]
        root = a.root or '.'
        results = []
        with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
            futs = {ex.submit(convert, a.xroar, os.path.join(root, p), os.path.join(a.out, p),
                              65000 + n % 500, **kw): p for n, p in enumerate(jobs)}
            for n, f in enumerate(concurrent.futures.as_completed(futs)):
                try:
                    r = f.result()
                except Exception as e:      # one bad tape should not stop the rest
                    r = dict(src=os.path.join(root, futs[f]), result='error: %s' % e)
                r['src'] = futs[f]
                results.append(r)
                print('%4d/%d %-15s %s' % (n + 1, len(jobs), verdict(r), futs[f]), flush=True)
    else:
        results = [convert(a.xroar, a.src, a.dst, 65520, **kw)]
        print(verdict(results[0]))
    if a.report:
        report(results, a.report)
    if a.json:
        json.dump([{k: v for k, v in r.items() if k != 'vram'} for r in results],
                  open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
