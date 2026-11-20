import os, sys, json, struct, io, re, shutil, subprocess
from PIL import Image

# usage: python convert_map.py <game root> <map id> <output folder> [startX startY]
game   = sys.argv[1]
map_id = int(sys.argv[2])
out    = sys.argv[3]
TS = 24                     # tile size on 3DS (original is 48)

www = os.path.join(game, "www")
os.makedirs(out, exist_ok=True)

# clear old output so stale sheets from other maps don't linger
for fn in os.listdir(out):
    if fn.endswith((".t3x", ".bin", ".png")):
        os.remove(os.path.join(out, fn))

tex3ds = shutil.which("tex3ds") or r"C:\devkitPro\tools\bin\tex3ds.exe"

def jload(*p):
    with open(os.path.join(www, *p), encoding="utf-8") as fh:
        return json.load(fh)

system = jload("data", "System.json")
key = bytes.fromhex(system["encryptionKey"])

def load_image(folder, name):
    base = os.path.join(www, "img", folder, name)
    if os.path.exists(base + ".rpgmvp"):
        data = bytearray(open(base + ".rpgmvp", "rb").read())[16:]
        for i in range(16):
            data[i] ^= key[i]
        return Image.open(io.BytesIO(bytes(data))).convert("RGBA")
    return Image.open(base + ".png").convert("RGBA")

def p2(n):
    p = 1
    while p < n:
        p *= 2
    return p

def to_t3x(img, slot, fmt):
    w, h = img.size
    img = img.resize((max(1, w * TS // 48), max(1, h * TS // 48)), Image.BOX)
    w, h = img.size
    canvas = Image.new("RGBA", (p2(w), p2(h)), (0, 0, 0, 0))
    canvas.paste(img, (0, 0))
    tmp = os.path.join(out, slot + "_tmp.png")
    canvas.save(tmp)
    subprocess.run([tex3ds, "-f", fmt, "-o", os.path.join(out, slot + ".t3x"), tmp],
                   check=True)
    os.remove(tmp)
    return w, h

# ---------------------------------------------------------------- map + tilesets
m = jload("data", "Map%03d.json" % map_id)
ts = jload("data", "Tilesets.json")[m["tilesetId"]]
names = ts["tilesetNames"]   # [A1,A2,A3,A4,A5,B,C,D,E]
print("tileset:", names)

for slot, idx in (("a1", 0), ("a2", 1), ("a3", 2), ("a4", 3), ("a5", 4),
                  ("b", 5), ("c", 6), ("d", 7), ("e", 8)):
    if not names[idx]:
        continue
    to_t3x(load_image("tilesets", names[idx]), slot, "rgba5551")
    print("converted", slot, names[idx])

# map layers z0..z3
w, h, d = m["width"], m["height"], m["data"]
with open(os.path.join(out, "map.bin"), "wb") as f:
    f.write(struct.pack("<HH", w, h))
    f.write(struct.pack("<%dH" % (w * h * 4), *d[:w * h * 4]))
n_tiles = sum(1 for v in d[:w * h * 4] if v)
print("map", w, "x", h, "| non-empty tiles:", n_tiles)
if n_tiles == 0:
    print("WARNING: this map has no tiles (cutscene/event map). Pick another map id.")

# passability flags (8192 entries)
fl = (list(ts["flags"]) + [0] * 8192)[:8192]
with open(os.path.join(out, "flags.bin"), "wb") as f:
    f.write(struct.pack("<8192H", *fl))

# ---------------------------------------------------------------- start position
def check(x, y, bit):
    if x < 0 or y < 0 or x >= w or y >= h:
        return False
    for z in (3, 2, 1, 0):
        fv = fl[d[(z * h + y) * w + x]]
        if fv & 0x10:
            continue
        if (fv & bit) == 0:
            return True
        if (fv & bit) == bit:
            return False
    return False

if len(sys.argv) >= 6:
    sx, sy = int(sys.argv[4]), int(sys.argv[5])
    print("start override:", sx, sy)
elif system["startMapId"] == map_id:
    sx, sy = system["startX"], system["startY"]
    print("game start tile:", sx, sy)
else:
    best = None
    for y in range(h):
        for x in range(w):
            if all(check(x, y, b) for b in (1, 2, 4, 8)):
                dist = (x - w / 2) ** 2 + (y - h / 2) ** 2
                if best is None or dist < best[0]:
                    best = (dist, x, y)
    sx, sy = (best[1], best[2]) if best else (0, 0)
    print("auto start tile (open floor near middle):", sx, sy)

# ---------------------------------------------------------------- player sprite
actors = jload("data", "Actors.json")
aid = (system.get("partyMembers") or [1])[0]
cname = actors[aid]["characterName"]
cidx = actors[aid]["characterIndex"]
pre = re.match(r"^[!$]+", cname)
pre = pre.group() if pre else ""
single, obj = int("$" in pre), int("!" in pre)
if cname:
    sw, sh = to_t3x(load_image("characters", cname), "player", "rgba4444")
    fw = sw // 3 if single else sw // 12
    fh = sh // 4 if single else sh // 8
    with open(os.path.join(out, "player.bin"), "wb") as f:
        f.write(struct.pack("<7H", sx, sy, cidx, single, obj, fw, fh))
    print("player:", cname, "index", cidx, "frame", fw, "x", fh)
else:
    print("WARNING: first party member has no character sprite")