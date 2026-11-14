#!/usr/bin/env python3
"""The 128x32 OLED shows LVGL black as a lit pixel. Check that the Costean screens write lit
pixels in the same colour as the ink of the stock Bitaxe logo (whose background is the
logo's corner colour), so the Costean pages can't come out inverted (white background)."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
logo = (ROOT / "main/logo.c").read_text()
b = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", re.search(r"bitaxe_logo_map\[\]\s*=\s*\{(.*?)\};", logo, re.S).group(1))]
px = [b[i] | (b[i + 1] << 8) for i in range(0, len(b) - 1, 2)]
W, H = 77, 30
background = max({px[0], px[W - 1], px[(H - 1) * W], px[H * W - 1]}, key=[px[0], px[W - 1], px[(H - 1) * W], px[H * W - 1]].count)
ink = 0x0000 if background == 0xFFFF else 0xFFFF
scr = (ROOT / "main/screen.c").read_text()
lit = int(re.search(r"#define LODE_RGB565_LIT\s+0x([0-9A-Fa-f]+)", scr).group(1), 16)
dark = int(re.search(r"#define LODE_RGB565_DARK\s+0x([0-9A-Fa-f]+)", scr).group(1), 16)
ok = lit == ink and dark == background and "lode_get(&fb, x, y) ? LODE_RGB565_LIT : LODE_RGB565_DARK" in scr
ok = ok and "ls->buf[i] = LODE_RGB565_DARK" in scr                       # new screens start dark
ok = ok and "lv_obj_set_style_text_color(l, lv_color_black()" in scr      # small-OLED text lines lit
print(f"oled polarity: stock logo ink 0x{ink:04X}, Costean lit 0x{lit:04X}, dark 0x{dark:04X}: {'ok' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
