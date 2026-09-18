#!/usr/bin/env python3
"""Fetch and prepare the laboratory's PBR texture sets (spec 18 §4, textures).

Downloads the CC0 sets listed in SETS from ambientCG (1K JPG), writes each as
Assets/Textures/<set>/{albedo,normal,roughness[,metallic,ao]}.jpg within the 1.5 MB per-set
budget, and synthesises the two sets no CC0 library carries (pcb, g10). Deterministic: the
procedural sets use a fixed seed. Requires Pillow only. Re-running is idempotent.
    python3 Assets/Textures/fetch_textures.py [--no-download]
"""
import io, math, os, random, sys, urllib.request, zipfile
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

ROOT = os.path.dirname(os.path.abspath(__file__))
BUDGET = 1_500_000  # bytes per set
# set name -> (ambientCG asset id, keep ao map, albedo variation cap, roughness variation cap)
# The renderer divides each map by its mean (gfx::Material::normalizeMaps), so a dark source
# (black powder coat, mean ≈ 0.02) would have its speckle amplified fifty-fold. The caps bound the
# coefficient of variation (std/mean) of every map: a clean laboratory finish varies by a few
# per cent in albedo and by a quarter in roughness; the structure lives in the normal map.
SETS = {
    "brushed_stainless": ("Metal009", False, 0.05, 0.25),
    "blasted_aluminium": ("Metal011", False, 0.04, 0.25),
    "gold_plated": ("Metal042A", False, 0.05, 0.30),
    "copper": ("Metal057A", False, 0.05, 0.25),
    "epoxy_floor": ("Concrete030", True, 0.10, 0.30),
    "painted_wall": ("PaintedPlaster017", True, 0.06, 0.20),
    "black_anodised": ("Metal046A", False, 0.06, 0.25),
    "powder_coated": ("Metal027", False, 0.05, 0.20),
    "rubber": ("Rubber004", True, 0.08, 0.25),
    "plastic_grey": ("Plastic011", False, 0.03, 0.20),
}
ACG_MAPS = {"albedo": "Color", "normal": "NormalGL", "roughness": "Roughness", "metallic": "Metalness", "ao": "AmbientOcclusion"}


def fetch_zip(asset):
    url = f"https://ambientcg.com/get?file={asset}_1K-JPG.zip"
    print("  GET", url)
    req = urllib.request.Request(url, headers={"User-Agent": "QuantumXLab-fetch-textures/1.0"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return zipfile.ZipFile(io.BytesIO(r.read()))


def cap_variation(img, target_cv):
    """Scales the map's deviation from its per-channel mean so that std/mean <= target_cv."""
    bands = img.split()
    out = []
    for band in bands:
        st = ImageStat.Stat(band)
        mean, std = st.mean[0], st.stddev[0]
        if mean <= 0 or std <= 0:
            out.append(band)
            continue
        k = min(1.0, target_cv * mean / std)
        out.append(band.point(lambda v, m=mean, k=k: max(0, min(255, round(m + k * (v - m))))))
    return Image.merge(img.mode, out) if len(out) > 1 else out[0]


def is_constant(img):
    lo, hi = img.convert("L").getextrema()
    return hi - lo < 4


def save_set(name, maps):
    """maps: dict kind -> PIL image. Encodes within BUDGET by lowering quality, then size."""
    out = os.path.join(ROOT, name)
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):
        if old.endswith(".jpg"):
            os.remove(os.path.join(out, old))
    for quality, size in [(88, 1024), (82, 1024), (76, 1024), (70, 1024), (82, 768), (76, 768), (76, 512)]:
        total = 0
        encoded = {}
        for kind, img in maps.items():
            im = img if img.size[0] == size else img.resize((size, size), Image.LANCZOS)
            im = im.convert("RGB") if kind in ("albedo", "normal") else im.convert("L")
            buf = io.BytesIO()
            im.save(buf, "JPEG", quality=quality, optimize=True, subsampling=0 if kind == "normal" else 2)
            encoded[kind] = buf.getvalue()
            total += len(encoded[kind])
        if total <= BUDGET:
            break
    for kind, data in encoded.items():
        with open(os.path.join(out, kind + ".jpg"), "wb") as f:
            f.write(data)
    print(f"  {name}: {', '.join(sorted(encoded))} q{quality} {size}px total {total / 1e6:.2f} MB")


def prepare_download(name, asset, keep_ao, albedo_cv, rough_cv):
    z = fetch_zip(asset)
    names = z.namelist()
    maps = {}
    for kind, suffix in ACG_MAPS.items():
        match = [n for n in names if n.endswith(f"_{suffix}.jpg")]
        if not match:
            continue
        img = Image.open(io.BytesIO(z.read(match[0])))
        img.load()
        if img.size[0] != img.size[1]:  # a 2:1 tile (PaintedPlaster017): stack copies into a square
            w, h = img.size
            n = max(w, h)
            sq = Image.new(img.mode, (n, n))
            for x in range(0, n, w):
                for y in range(0, n, h):
                    sq.paste(img, (x, y))
            img = sq
        if kind in ("metallic", "ao") and (is_constant(img) or (kind == "ao" and not keep_ao)):
            continue  # a constant map carries nothing the material factor does not
        if kind == "albedo":
            img = cap_variation(img.convert("RGB"), albedo_cv)
        elif kind == "roughness":
            img = cap_variation(img.convert("L"), rough_cv)
        maps[kind] = img
    save_set(name, maps)


# ---------------------------------------------------------------- procedural sets
def normal_from_height(height, strength):
    """Tangent-space normal map (OpenGL +Y up) from an 'L' height image, tileable."""
    w, h = height.size
    hx = ImageChops.offset(height, -1, 0)
    hy = ImageChops.offset(height, 0, -1)
    dx = hx.load(); dy = hy.load(); hz = height.load()
    out = Image.new("RGB", (w, h))
    px = out.load()
    for y in range(h):
        for x in range(w):
            gx = (dx[x, y] - hz[x, y]) / 255.0 * strength
            gy = (dy[x, y] - hz[x, y]) / 255.0 * strength
            inv = 1.0 / math.sqrt(gx * gx + gy * gy + 1.0)
            # +Y up (OpenGL): a height rising towards +y (down in image rows) tilts the normal to -y
            px[x, y] = (int(round((-gx * inv * 0.5 + 0.5) * 255)), int(round((gy * inv * 0.5 + 0.5) * 255)),
                        int(round((inv * 0.5 + 0.5) * 255)))
    return out


def noise(size, seed, blur):
    rnd = Image.effect_noise((size, size), 48)
    rnd = ImageChops.offset(rnd, seed % size, (seed * 7) % size)
    return rnd.filter(ImageFilter.GaussianBlur(blur)) if blur else rnd


def wrapped(draw_fn, size):
    """Call draw_fn(dx, dy) for the 9 wrap offsets so the drawing tiles seamlessly."""
    for dx in (-size, 0, size):
        for dy in (-size, 0, size):
            draw_fn(dx, dy)


def make_pcb(size=1024, seed=7):
    """Green solder mask over a Manhattan trace pattern with plated pads (spec 18 §4)."""
    rnd = random.Random(seed)
    height = Image.new("L", (size, size), 0)
    albedo = Image.new("RGB", (size, size), (28, 96, 44))
    rough = Image.new("L", (size, size), 118)
    hd, ad, rd = ImageDraw.Draw(height), ImageDraw.Draw(albedo), ImageDraw.Draw(rough)
    pitch = 32
    for _ in range(70):  # traces: axis-aligned polylines on a 32 px grid
        x, y = rnd.randrange(0, size, pitch), rnd.randrange(0, size, pitch)
        pts = [(x, y)]
        for _ in range(rnd.randint(2, 6)):
            if rnd.random() < 0.5:
                x += rnd.choice((-1, 1)) * rnd.randint(1, 8) * pitch
            else:
                y += rnd.choice((-1, 1)) * rnd.randint(1, 8) * pitch
            pts.append((x, y))
        wdt = rnd.choice((5, 5, 7, 9))
        def trace(dx, dy, pts=pts, wdt=wdt):
            sh = [(px + dx, py + dy) for px, py in pts]
            hd.line(sh, fill=150, width=wdt, joint="curve")
            ad.line(sh, fill=(52, 132, 62), width=wdt, joint="curve")
            rd.line(sh, fill=100, width=wdt, joint="curve")
        wrapped(trace, size)
    for _ in range(90):  # plated pads / vias: gold ring, dark drill
        cx, cy, r = rnd.randrange(0, size, pitch), rnd.randrange(0, size, pitch), rnd.choice((7, 9, 12))
        def pad(dx, dy, cx=cx, cy=cy, r=r):
            box = (cx + dx - r, cy + dy - r, cx + dx + r, cy + dy + r)
            hd.ellipse(box, fill=230); ad.ellipse(box, fill=(214, 178, 96)); rd.ellipse(box, fill=70)
            r2 = r // 2
            box2 = (cx + dx - r2, cy + dy - r2, cx + dx + r2, cy + dy + r2)
            hd.ellipse(box2, fill=40); ad.ellipse(box2, fill=(40, 34, 30)); rd.ellipse(box2, fill=140)
        wrapped(pad, size)
    for _ in range(24):  # silkscreen outlines: white rectangles
        x, y = rnd.randrange(0, size, pitch), rnd.randrange(0, size, pitch)
        w, h = rnd.randint(2, 6) * pitch, rnd.randint(1, 4) * pitch
        def silk(dx, dy, x=x, y=y, w=w, h=h):
            ad.rectangle((x + dx, y + dy, x + dx + w, y + dy + h), outline=(228, 228, 218), width=3)
            rd.rectangle((x + dx, y + dy, x + dx + w, y + dy + h), outline=180, width=3)
        wrapped(silk, size)
    grain = noise(size, seed, 2.0).point(lambda v: 128 + (v - 128) // 16)   # faint mask grain
    albedo = ImageChops.multiply(albedo, Image.merge("RGB", [grain.point(lambda v: v * 2)] * 3))
    rough = ImageChops.add(rough, grain.point(lambda v: (v - 128) // 3), scale=1.0, offset=0)
    height = height.filter(ImageFilter.GaussianBlur(1.0))
    return {"albedo": albedo, "normal": normal_from_height(height, 1.6), "roughness": rough}


def make_g10(size=1024, seed=3, cell=12):
    """Glass-epoxy laminate: a fine plain weave under a resin skin, olive-tinted."""
    height = Image.new("L", (size, size), 128)
    albedo = Image.new("RGB", (size, size), (150, 146, 104))
    rough = Image.new("L", (size, size), 128)
    hp, ap, rp = height.load(), albedo.load(), rough.load()
    for y in range(size):
        for x in range(size):
            cx, cy = (x // cell) % 2, (y // cell) % 2
            over_h = (cx + cy) % 2 == 0          # horizontal thread on top in this cell
            t = ((y % cell) if over_h else (x % cell)) / cell
            bump = math.sin(t * math.pi)         # thread cross-section
            hp[x, y] = int(96 + 64 * bump)
            shade = 0.9 + 0.14 * bump
            ap[x, y] = (int(150 * shade), int(146 * shade), int(104 * shade))
            rp[x, y] = int(150 - 40 * bump)
    grain = noise(size, seed, 2.0)
    albedo = ImageChops.multiply(albedo, Image.merge("RGB", [grain.point(lambda v: 192 + (v - 128) // 4)] * 3).point(lambda v: min(255, v * 255 // 192)))
    height = height.filter(ImageFilter.GaussianBlur(0.8))
    return {"albedo": cap_variation(albedo, 0.08), "normal": normal_from_height(height, 1.2), "roughness": cap_variation(rough, 0.25)}


def main():
    download = "--no-download" not in sys.argv
    if download:
        for name, (asset, keep_ao, albedo_cv, rough_cv) in SETS.items():
            print(name, "<-", asset)
            prepare_download(name, asset, keep_ao, albedo_cv, rough_cv)
    print("pcb (procedural)")
    save_set("pcb", make_pcb())
    print("g10 (procedural)")
    save_set("g10", make_g10())


if __name__ == "__main__":
    main()
