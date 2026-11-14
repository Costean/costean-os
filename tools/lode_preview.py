#!/usr/bin/env python3
"""Render the Costean OS OLED screens to PNG: builds tools/lode_preview.c with the real firmware
renderer, then writes every frame as
  NAME.png      1:1, exactly the 128x32 pixels sent to the OLED (white = lit)
  NAME@4x.png   the same pixels scaled 4x (nearest neighbour)
  oled-look/NAME.png  an OLED-style render (pixel grid on a dark bezel)
plus _all-screens.png, a labelled contact sheet of the 4x frames."""
import subprocess, sys, tempfile
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1] if len(sys.argv) > 1 else "oled-previews").resolve()
out.mkdir(parents=True, exist_ok=True)
tmp = Path(tempfile.mkdtemp())
exe = tmp / "lode_preview"
subprocess.check_call(["gcc", "-O1", "-I", str(ROOT / "main/costean"), "-I", str(ROOT / "tools"),
                       str(ROOT / "tools/lode_preview.c"), str(ROOT / "main/costean/lode_oled.c"),
                       str(ROOT / "main/costean/lode_font.c"), "-o", str(exe)])
subprocess.check_call([str(exe), str(tmp)])
S, PAD = 6, 18
ON, OFF, BEZEL = (236, 244, 255), (16, 18, 22), (6, 7, 9)
tiles = []
(out / "oled-look").mkdir(exist_ok=True)
for p in sorted(tmp.glob("*.pbm")):
    rows = p.read_text().split("\n")[2:2 + 32]
    one = Image.new("1", (128, 32), 0)
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c == "1": one.putpixel((x, y), 1)
    one.save(out / (p.stem + ".png"))
    x4 = one.resize((512, 128), Image.NEAREST)
    x4.save(out / (p.stem + "@4x.png"))
    img = Image.new("RGB", (128 * S + 2 * PAD, 32 * S + 2 * PAD), BEZEL)
    d = ImageDraw.Draw(img)
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            x0, y0 = PAD + x * S, PAD + y * S
            d.rectangle([x0, y0, x0 + S - 2, y0 + S - 2], fill=ON if c == "1" else OFF)
    img.save(out / "oled-look" / (p.stem + ".png"))
    tiles.append((p.stem, Image.new("RGB", (512 + 8, 128 + 8), (60, 62, 68))))
    tiles[-1][1].paste(x4.convert("RGB"), (4, 4))
# contact sheet
W = tiles[0][1].width
cols, lab = 2, 22
rows_n = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (W + 20) + 20, rows_n * (tiles[0][1].height + lab + 20) + 20), (34, 36, 40))
d = ImageDraw.Draw(sheet)
for i, (n, im) in enumerate(tiles):
    x = 20 + (i % cols) * (W + 20); y = 20 + (i // cols) * (im.height + lab + 20)
    d.text((x, y), n, fill=(238, 138, 76))
    sheet.paste(im, (x, y + lab))
sheet.save(out / "_all-screens.png")
print(f"{len(tiles)} OLED previews -> {out}")
