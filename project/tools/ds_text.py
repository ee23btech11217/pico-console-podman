# RP2350 datasheet text extractor (Type0/Identity-H fonts with ToUnicode), no PDF tools needed.
#   python3 -I tools/ds_text.py RP-008373-DS-3-rp2350-datasheet.pdf > ds.txt   -> "page|text" lines
#   (page = PDF page index; the printed page number is one lower). Search with grep, e.g. "RP2350-E9".
import re, sys, zlib
data = open(sys.argv[1], 'rb').read()
objs = {}
for m in re.finditer(rb'(\d+) 0 obj\s*(.*?)endobj', data, re.S):
    objs[int(m.group(1))] = m.group(2)
def stream(num):
    o = objs[num]
    i = o.find(b'stream')
    if i < 0: return b''
    j = i + 6
    if o[j:j+2] == b'\r\n': j += 2
    elif o[j:j+1] == b'\n': j += 1
    raw = o[j:]
    if b'/FlateDecode' in o[:i]:
        try: return zlib.decompressobj().decompress(raw)
        except Exception: return b''
    return raw
cmaps = {}
def cmap(num):
    if num in cmaps: return cmaps[num]
    s = stream(num); m = {}
    for blk in re.findall(rb'beginbfchar(.*?)endbfchar', s, re.S):
        for a, b in re.findall(rb'<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>', blk):
            m[int(a, 16)] = bytes.fromhex(b.decode()).decode('utf-16-be', 'replace')
    for blk in re.findall(rb'beginbfrange(.*?)endbfrange', s, re.S):
        for a, b, c in re.findall(rb'<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>', blk):
            a, b, c = int(a, 16), int(b, 16), int(c, 16)
            for k in range(a, b + 1): m[k] = chr(c + k - a)
    cmaps[num] = m; return m
def unesc(s):
    out = bytearray(); i = 0
    while i < len(s):
        ch = s[i]
        if ch == 0x5c:
            i += 1; c = s[i:i+1]
            if c in b'01234567':
                k = re.match(rb'[0-7]{1,3}', s[i:]).group(0); out.append(int(k, 8) & 255); i += len(k); continue
            out += {b'n': b'\n', b'r': b'\r', b't': b'\t', b'b': b'\b', b'f': b'\f'}.get(c, c); i += 1; continue
        out.append(ch); i += 1
    return bytes(out)
pages = [n for n, o in objs.items() if re.search(rb'/Type\s*/Page[^s]', o)]
def page_key(n):
    return n
res_cache = {}
def fonts_of(o):
    r = re.search(rb'/Resources\s*(\d+) 0 R', o)
    rd = objs.get(int(r.group(1)), b'') if r else o
    f = re.search(rb'/Font\s*<<(.*?)>>', rd, re.S)
    fm = {}
    if f:
        for name, num in re.findall(rb'/(\w+)\s+(\d+) 0 R', f.group(1)):
            fo = objs.get(int(num), b'')
            t = re.search(rb'/ToUnicode\s+(\d+) 0 R', fo)
            if t: fm[name] = cmap(int(t.group(1)))
    return fm
# page order: follow /Kids from the root /Pages tree
root = [n for n, o in objs.items() if re.search(rb'/Type\s*/Pages', o) and b'/Parent' not in o]
order = []
def walk(n):
    o = objs[n]
    if re.search(rb'/Type\s*/Pages', o):
        k = re.search(rb'/Kids\s*\[(.*?)\]', o, re.S)
        for c in re.findall(rb'(\d+) 0 R', k.group(1)): walk(int(c))
    else: order.append(n)
for r in root: walk(r)
for pno, n in enumerate(order, 1):
    o = objs[n]; fm = fonts_of(o)
    cs = re.search(rb'/Contents\s*(\d+) 0 R', o)
    if not cs: continue
    s = stream(int(cs.group(1)))
    cur = None; line = []
    for tok in re.finditer(rb'/(\w+)\s+[\d.]+\s+Tf|\[(.*?)\]\s*TJ|\((.*?)(?<!\\)\)\s*Tj|(ET)|(T\*|Td|TD|Tm)', s, re.S):
        if tok.group(1): cur = fm.get(tok.group(1), {}); continue
        if tok.group(4) or tok.group(5):
            if line: print(f"{pno}|{''.join(line)}"); line = []
            continue
        parts = re.findall(rb'\(((?:\\.|[^\\)])*)\)|(-?[\d.]+)', tok.group(2)) if tok.group(2) else [(tok.group(3), b'')]
        for strg, num in parts:
            if num and float(num) < -200: line.append(' ')
            if strg:
                b = unesc(strg)
                for k in range(0, len(b) - 1, 2):
                    line.append((cur or {}).get((b[k] << 8) | b[k+1], ''))
    if line: print(f"{pno}|{''.join(line)}")
