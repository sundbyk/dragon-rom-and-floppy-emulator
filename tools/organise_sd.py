"""Build a tidy dragon/ folder for the SD card from a messy collection.

python organise_sd.py SOURCE DEST

Only what the emulator can use is copied (.cas .dsk .vdk .rom .ccc), with cleaner names. In CAS
each game folder (CAS/Publisher/Game/...) becomes CAS/Publisher/Title.cas, or a folder
CAS/Publisher/Title/ when the game has several files that are not just tape sides. The cover
picture of each game is copied next to it (Title.jpg, or cover.jpg in a game folder) and listed
in covers.txt for make_covers.ps1, which turns them into the .pm4 files the menu shows.
Identical files are only copied once.
"""
import hashlib
import os
import re
import shutil
import sys

GAME_EXT = ('.cas', '.dsk', '.vdk', '.rom', '.ccc')
TOP_NAMES = {
    'cas': 'CAS', 'disks': 'Disks', 'gamecomp': 'Game Compilations', 'demos': 'Demos',
    'utils': 'Utilities', 'collections': 'Collections', 'cartidge': 'Cartridges',
    'cartridge': 'Cartridges', 'os9': 'OS-9', 'flex': 'FLEX',
}
PUBLISHER_NAMES = {'zztosort': 'Unsorted'}
LANGS = {'ES', 'DE', 'FR', 'NL', 'IT', 'PT', 'SE', 'NO', 'DK', 'FI', 'EN', 'GR'}
YEAR = re.compile(r'^(19|20|!9)[0-9x]{2}$', re.I)
COVER_ORDER = [r'^front', r'^cover\.', r'^folder\.', r'^inlay\.', r'^inlay-?1\.', r'^inlay', r'^tape\.', r'^tape']
COVER_EXT = ('.jpg', '.jpeg', '.png', '.gif', '.bmp')


def tidy(s):
    s = re.sub(r'\s+', ' ', s).strip()
    s = re.sub(r'^[\s\-_.,]+|[\s\-_,]+$', '', s)
    return s


def side_of(tag):
    m = re.match(r'^(?:side|cara)\s*([0-9a-d])$', tag.strip(), re.I)
    return m.group(1).upper() if m else None


def clean(name, is_file=True):
    """'Fireball (198x)(Abacus)[!][RUN].cas' -> 'Fireball'. Keeps side, language and variants."""
    base = os.path.splitext(name)[0] if is_file else name
    if ' ' not in base and '_' in base:
        base = base.replace('_', ' ')
    extras = []
    side = None

    def bracket(m):
        nonlocal side
        t = m.group(1).strip()
        tl = t.lower()
        if side_of(t):
            side = side_of(t)
        elif re.match(r'^a\d*$', tl):
            extras.append('alt')
        elif tl.startswith('h'):
            extras.append('hack')
        elif tl.startswith('cr'):
            extras.append('cracked')
        elif re.match(r'^b\d*$', tl):
            extras.append('bad dump')
        elif tl.startswith('m ') or tl == 'm':
            extras.append('mod')
        return ' '
    base = re.sub(r'[\[{]([^\]}]*)[\]}]', bracket, base)

    seen_year = False

    def paren(m):
        nonlocal seen_year, side
        t = m.group(1).strip()
        if YEAR.match(t):
            seen_year = True
            return ' '
        if side_of(t):
            side = side_of(t)
            return ' '
        if t.upper() in LANGS:
            extras.append(t.upper())
            return ' '
        if re.match(r'^\d+(st|nd|rd|th) release$', t, re.I):
            return ' '
        if t.lower().startswith('m '):
            extras.append('mod')
            return ' '
        if seen_year or t == '-':
            return ' '          # publisher, author
        return m.group(0)
    base = re.sub(r'\(([^)]*)\)', paren, base)
    base = re.sub(r'\s-\s*$', '', base)
    m = re.search(r'\s-\s*side\s*([a-d1-4])\b', base, re.I)
    if m:
        side = m.group(1).upper()
        base = base[:m.start()] + base[m.end():]
    base = tidy(base)
    for e in extras:
        if '(%s)' % e not in base:
            base += ' (%s)' % e
    return tidy(base), side


def with_side(title, side):
    return '%s - Side %s' % (title, side) if side else title


class Out:
    def __init__(self, dest):
        self.dest = dest
        self.hashes = set()
        self.names = set()
        self.covers = []
        self.log = []

    def unique(self, folder, name, ext):
        n = 1
        cand = name
        while os.path.join(folder, cand + ext).lower() in self.names:
            n += 1
            cand = '%s (%d)' % (name, n)
        self.names.add(os.path.join(folder, cand + ext).lower())
        return cand

    def copy(self, src, folder, name, dedupe=True):
        data = open(src, 'rb').read()
        h = hashlib.md5(data).hexdigest()
        if dedupe and h in self.hashes:
            self.log.append('dup  %s' % src)
            return None
        self.hashes.add(h)
        ext = os.path.splitext(src)[1].lower()
        name = self.unique(folder, name.replace('/', '-').replace(':', ' -').strip(), ext)
        os.makedirs(os.path.join(self.dest, folder), exist_ok=True)
        dst = os.path.join(self.dest, folder, name + ext)
        with open(dst, 'wb') as f:
            f.write(data)
        self.log.append('copy %s -> %s' % (src, dst))
        return dst


def pick_cover(folder):
    pics = [f for f in os.listdir(folder) if f.lower().endswith(COVER_EXT)
            and os.path.isfile(os.path.join(folder, f))]
    for pat in COVER_ORDER:
        for f in sorted(pics, key=str.lower):
            fl = f.lower()
            if re.search(pat, fl) and 'inside' not in fl and 'back' not in fl:
                return os.path.join(folder, f)
    return None


COCO = re.compile(r'(^|[^a-z])coco([^a-z]|$)', re.I)


def game_files(folder):
    res = []
    for dp, dn, fn in os.walk(folder):
        dn[:] = [d for d in dn if not COCO.search(d)]      # Tandy versions are no use on a Dragon
        for f in sorted(fn, key=str.lower):
            if f.lower().endswith(GAME_EXT):
                res.append(os.path.join(dp, f))
    return res


def add_cover(out, cover, dst_noext):
    if not cover:
        return
    ext = os.path.splitext(cover)[1].lower()
    shutil.copyfile(cover, dst_noext + ext)
    # the whole inlay (folder.jpg, Inlay.jpg) is cropped to its front panel
    crop = 0 if re.match(r'^(front|cover)', os.path.basename(cover).lower()) else 1
    out.covers.append('%d|%s|%s' % (crop, dst_noext + ext, dst_noext + '.pm4'))


def do_game(out, folder_rel, game_dir, title):
    files = game_files(game_dir)
    if not files:
        out.log.append('none %s' % game_dir)
        return
    cover = pick_cover(game_dir)
    named = []
    for f in files:
        name, side = clean(os.path.basename(f))
        # folders inside the game: "Side A" becomes the side, anything else stays a folder
        subpath = []
        rel = os.path.relpath(os.path.dirname(f), game_dir)
        if rel != '.':
            for part in rel.split(os.sep):
                pclean, pside = clean(part, False)
                if pside and (not pclean or pclean.lower() in ('side', 'cara')):
                    side = side or pside
                elif pclean and pclean.lower() != title.lower():
                    subpath.append(with_side(pclean, pside))
        # drop "Title - " at the front, it is in the folder name already
        if name.lower().startswith(title.lower() + ' - '):
            name = name[len(title) + 3:]
        named.append((f, os.path.join(*subpath) if subpath else '', name, side))
    only_sides = len(named) == 1 or all(s and not p for _, p, _, s in named)
    if only_sides:
        for f, _, name, side in named:
            dst = out.copy(f, folder_rel, with_side(title, side))
            if dst:
                add_cover(out, cover, os.path.splitext(dst)[0])
    else:
        sub = os.path.join(folder_rel, out.unique(folder_rel, title, ''))
        n = 0
        for f, subpath, name, side in named:
            if out.copy(f, os.path.join(sub, subpath), with_side(name or title, side)):
                n += 1
        if n:
            add_cover(out, cover, os.path.join(out.dest, sub, 'cover'))


def do_cas(out, src, top):
    for pub in sorted(os.listdir(src), key=str.lower):
        pdir = os.path.join(src, pub)
        if not os.path.isdir(pdir):
            if pub.lower().endswith(GAME_EXT):
                out.copy(pdir, top, clean(pub)[0])
            continue
        pname = PUBLISHER_NAMES.get(pub.lower(), pub.strip())
        prel = os.path.join(top, pname)
        for entry in sorted(os.listdir(pdir), key=str.lower):
            p = os.path.join(pdir, entry)
            if os.path.isdir(p):
                title, _ = clean(entry, False)
                do_game(out, prel, p, title or entry)
            elif entry.lower().endswith(GAME_EXT):
                name, side = clean(entry)
                out.copy(p, prel, with_side(name, side))


def do_plain(out, src, top):
    for dp, dn, fn in os.walk(src):
        rel = os.path.relpath(dp, src)
        folder = top if rel == '.' else os.path.join(top, rel)
        for f in sorted(fn, key=str.lower):
            if f.lower().endswith(GAME_EXT):
                name, side = clean(f)
                out.copy(os.path.join(dp, f), folder, with_side(name, side))


def main(src, dest):
    out = Out(dest)
    for top in sorted(os.listdir(src), key=str.lower):
        p = os.path.join(src, top)
        if not os.path.isdir(p):
            if top.lower().endswith(GAME_EXT):
                out.copy(p, '', clean(top)[0])
            continue
        new = TOP_NAMES.get(top.lower(), top)
        if top.lower() == 'cas':
            do_cas(out, p, new)
        else:
            do_plain(out, p, new)
    open(os.path.join(dest, '..', 'covers.txt'), 'w', encoding='utf-8').write('\n'.join(out.covers) + '\n')
    open(os.path.join(dest, '..', 'organise.log'), 'w', encoding='utf-8').write('\n'.join(out.log) + '\n')
    copies = sum(1 for l in out.log if l.startswith('copy'))
    print('copied %d, duplicates %d, covers %d' % (copies, sum(1 for l in out.log if l.startswith('dup')), len(out.covers)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
