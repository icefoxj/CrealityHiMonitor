"""Renders the HM-01 screens with the firmware's own drawing code and
writes the images in docs/img.

    python tools/previews.py            (needs g++ and Pillow)

Steps: compile tools/preview.cpp with firmware/lib/hm_core (gfx, mechfont,
screens), run it into a temporary folder (one PPM per screen plus the
rotation frames), then build:

    docs/img/splash.png          the boot image at panel size, 128 x 128
    docs/img/screen-<name>.png   every screen, 3x, nearest neighbour
    docs/img/screens.png         all screens on one sheet
    docs/img/rotation.gif        the HUD following a quarter turn
    docs/img/hero.png            two AtomS3R mock-ups, one upright, one tilted
    docs/img/cores3-<name>.png   every CoreS3 dashboard screen, 2x
    docs/img/cores3-screens.png  all CoreS3 screens on one sheet
    docs/img/cores3.png          a CoreS3 mock-up showing a print
    docs/img/cores3-portrait-<name>.png    the portrait layout (stood on a side), 2x
    docs/img/cores3-portrait-screens.png   all portrait screens on one sheet
    docs/img/cardputerzero-<name>.png      the CardputerZero's 320 x 170 screens, 2x
    docs/img/cardputerzero-screens.png     all of them on one sheet
    cardputerzero/packaging/hm01_100.png   its launcher icon (transparent background)

The pixels are exactly what the devices show: same code, same RGB565
quantisation.
"""

import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "firmware", "lib", "hm_core", "src")
OUT = os.path.join(ROOT, "docs", "img")
SCREENS = ["splash", "setup", "linking", "offline", "idle", "printing",
           "heating", "paused", "done", "stopped", "error"]


def build_and_run(tmp):
    exe = os.path.join(tmp, "preview.exe" if os.name == "nt" else "preview")
    cxx = os.environ.get("CXX", "g++")
    cmd = [cxx, "-std=gnu++14", "-O2", "-I" + CORE, os.path.join(ROOT, "tools", "preview.cpp")]
    cmd += [os.path.join(CORE, f) for f in ("gfx.cpp", "mechfont.cpp", "screens.cpp", "dash.cpp")]
    cmd += ["-o", exe]
    subprocess.run(cmd, check=True)
    subprocess.run([exe, tmp], check=True)


def load(tmp, name):
    return Image.open(os.path.join(tmp, name + ".ppm")).convert("RGB")


def big(img, k):
    return img.resize((img.width * k, img.height * k), Image.NEAREST)


def sheet(images, cols, pad=12, bg=(30, 31, 34)):
    w, h = images[0].size
    rows = (len(images) + cols - 1) // cols
    out = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad) + pad), bg)
    for i, im in enumerate(images):
        out.paste(im, (pad + (i % cols) * (w + pad), pad + (i // cols) * (h + pad)))
    return out


def device(screen, scale=4):
    """An AtomS3R seen from the front: 24 x 24 mm body, 0.85" panel."""
    px_per_mm = 128 * scale / 15.0          # the panel's active area is about 15 mm wide
    body = int(24.0 * px_per_mm)
    pad = int(body * 0.12)
    size = body + 2 * pad
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(body * 0.16)
    # Shadow
    sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([pad + 6, pad + 14, pad + body + 6, pad + body + 14], r,
                                         fill=(0, 0, 0, 120))
    img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(14)))
    d = ImageDraw.Draw(img)
    # Body: dark grey shell with a lighter rim
    d.rounded_rectangle([pad, pad, pad + body, pad + body], r, fill=(58, 60, 66, 255))
    inset = int(body * 0.035)
    d.rounded_rectangle([pad + inset, pad + inset, pad + body - inset, pad + body - inset], r - inset,
                        fill=(36, 37, 41, 255))
    # Black glass over the panel
    glass = int(body * 0.80)
    g0 = pad + (body - glass) // 2
    d.rounded_rectangle([g0, g0, g0 + glass, g0 + glass], int(r * 0.6), fill=(6, 6, 8, 255))
    s = big(screen, scale).convert("RGBA")
    s0 = pad + (body - s.width) // 2
    img.paste(s, (s0, s0), s)
    # Glass sheen
    sheen = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(sheen).polygon([(g0, g0), (g0 + glass * 0.55, g0), (g0, g0 + glass * 0.55)],
                                  fill=(255, 255, 255, 14))
    return Image.alpha_composite(img, sheen)


def hero(tmp):
    upright = device(load(tmp, "printing"))
    # The cube turned 30 degrees counter-clockwise: the firmware draws the
    # HUD 30 degrees clockwise on the panel, so it stays upright
    # (PIL's rotate() turns counter-clockwise)
    tilted = device(load(tmp, "rot_06")).rotate(30, resample=Image.BICUBIC, expand=True)
    w = upright.width + tilted.width + 40
    h = max(upright.height, tilted.height)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.alpha_composite(upright, (0, (h - upright.height) // 2))
    out.alpha_composite(tilted, (upright.width + 40, (h - tilted.height) // 2))
    bg = Image.new("RGBA", out.size, (238, 238, 234, 255))
    return Image.alpha_composite(bg, out).convert("RGB")


def cores3(screen, scale=2):
    """An M5Stack CoreS3 seen from the front: 54 x 54 mm body, 2.0" panel."""
    px_per_mm = 320 * scale / 40.8          # the panel's active area is 40.8 x 30.6 mm
    body = int(54.0 * px_per_mm)
    pad = int(body * 0.10)
    size = body + 2 * pad
    r = int(body * 0.07)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([pad + 8, pad + 18, pad + body + 8, pad + body + 18], r,
                                         fill=(0, 0, 0, 120))
    img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(18)))
    d = ImageDraw.Draw(img)
    # Body: dark grey shell, a lighter rim, and the front bezel
    d.rounded_rectangle([pad, pad, pad + body, pad + body], r, fill=(58, 60, 66, 255))
    inset = int(body * 0.02)
    d.rounded_rectangle([pad + inset, pad + inset, pad + body - inset, pad + body - inset], r - inset,
                        fill=(30, 31, 35, 255))
    # Black glass over the panel, which sits a little above centre
    s = big(screen, scale).convert("RGBA")
    gx = int(3.0 * px_per_mm)
    s0x = pad + (body - s.width) // 2
    s0y = pad + int(body * 0.40) - s.height // 2
    d.rounded_rectangle([s0x - gx, s0y - gx, s0x + s.width + gx, s0y + s.height + gx], int(gx * 0.8),
                        fill=(6, 6, 8, 255))
    img.paste(s, (s0x, s0y), s)
    # The three touch marks under the panel
    my = s0y + s.height + int(5.5 * px_per_mm)
    for k in (-1, 0, 1):
        cx = pad + body // 2 + k * int(14 * px_per_mm)
        d.ellipse([cx - 5, my - 5, cx + 5, my + 5], outline=(90, 94, 102, 255), width=2)
    sheen = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(sheen).polygon([(s0x - gx, s0y - gx), (s0x + s.width * 0.5, s0y - gx),
                                   (s0x - gx, s0y + s.height * 0.6)], fill=(255, 255, 255, 12))
    img = Image.alpha_composite(img, sheen)
    bg = Image.new("RGBA", img.size, (238, 238, 234, 255))
    return Image.alpha_composite(bg, img).convert("RGB")


def main():
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="hm01-")
    try:
        build_and_run(tmp)
        load(tmp, "splash").save(os.path.join(OUT, "splash.png"), optimize=True)
        shots = []
        for name in SCREENS:
            im = big(load(tmp, name), 3)
            im.save(os.path.join(OUT, f"screen-{name}.png"), optimize=True)
            shots.append(im)
        sheet(shots, 4).save(os.path.join(OUT, "screens.png"), optimize=True)

        frames = [big(load(tmp, f"rot_{k:02d}"), 2) for k in range(19)]
        frames = frames + frames[-2:0:-1]                     # there and back
        durations = [600] + [40] * 17 + [600] + [40] * 17
        frames[0].save(os.path.join(OUT, "rotation.gif"), save_all=True, append_images=frames[1:],
                       duration=durations, loop=0, optimize=True)
        hero(tmp).save(os.path.join(OUT, "hero.png"), optimize=True)

        # The CoreS3 dashboard
        dash = []
        for name in SCREENS:
            im = load(tmp, f"cores3_{name}")
            big(im, 2).save(os.path.join(OUT, f"cores3-{name}.png"), optimize=True)
            dash.append(im)
        sheet(dash, 3).save(os.path.join(OUT, "cores3-screens.png"), optimize=True)
        cores3(load(tmp, "cores3_printing")).save(os.path.join(OUT, "cores3.png"), optimize=True)
        port = []
        for name in SCREENS:
            im = load(tmp, f"cores3p_{name}")
            if name in ("printing", "done", "idle"):
                big(im, 2).save(os.path.join(OUT, f"cores3-portrait-{name}.png"), optimize=True)
            port.append(im)
        sheet(port, 6).save(os.path.join(OUT, "cores3-portrait-screens.png"), optimize=True)

        # The CardputerZero (no setup screen there: the system owns the Wi-Fi)
        cz = []
        for name in ["printing", "details", "heating", "paused", "done", "stopped", "error", "idle",
                     "linking", "offline", "splash"]:
            im = load(tmp, f"cz_{name}")
            if name in ("printing", "details", "done", "idle"):
                big(im, 2).save(os.path.join(OUT, f"cardputerzero-{name}.png"), optimize=True)
            cz.append(im)
        sheet(cz, 3).save(os.path.join(OUT, "cardputerzero-screens.png"), optimize=True)
        # Launcher icon: the drawing's background colour becomes transparent
        icon = load(tmp, "cz_icon").convert("RGBA")
        bg = icon.getpixel((0, 0))
        icon.putdata([(r, g, b, 0) if (r, g, b, a) == bg else (r, g, b, a) for (r, g, b, a) in icon.getdata()])
        icon.save(os.path.join(ROOT, "cardputerzero", "packaging", "hm01_100.png"), optimize=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for f in sorted(os.listdir(OUT)):
        print(f"{os.path.getsize(os.path.join(OUT, f)):>8}  docs/img/{f}")


if __name__ == "__main__":
    sys.exit(main())
