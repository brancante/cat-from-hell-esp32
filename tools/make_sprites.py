#!/usr/bin/env python3
"""Draws the v1 sprites (kid-drawing style: thick black outline, flat marker colours), renders them with a headless
browser, and writes components/game_art/sprites.c (RGB565, magenta 0xF81F = transparent) plus art/preview/sheet.png.
Usage: NODE_PATH=$(npm root -g) python3 tools/make_sprites.py      (needs: node + playwright, python3 + Pillow)"""
import json, os, subprocess, sys, tempfile
from PIL import Image

OR, GR, BL, WH, BK = "#f58a1f", "#1f8a3b", "#2d7ff9", "#ffffff", "#111111"
S = 'stroke="#111" stroke-linecap="round" stroke-linejoin="round"'

def cat(legs="a", strike=False, duck=False, jump=False):
    # 40x32 box. Hand-drawn look: round head, spiral eyes, big grin, zigzag striped tail.
    ly = 0
    legs_svg = {"a": [(11, 22, 4, 8), (28, 22, 4, 8)], "b": [(8, 22, 4, 8), (31, 22, 4, 8)],
                "j": [(6, 21, 4, 6), (33, 21, 4, 6)]}[legs if not jump else "j"]
    body_y = 14 if not duck else 14
    g = f'<g transform="translate(0,9) scale(1,0.7)">' if duck else '<g>'
    g += f'<path d="M6 20 C0 14 2 6 7 2 L9 6 L7 9 L11 10 L9 14 L13 15 Z" fill="{OR}" stroke-width="1.2" {S}/>'
    g += f'<path d="M3 11 l4 1 l-3 2z M5 6 l3 1 l-3 2z" fill="{GR}"/>'
    g += f'<rect x="9" y="{body_y}" width="21" height="11" rx="4" fill="{OR}" stroke-width="1.2" {S}/>'
    for (lx, lyy, lw, lh) in legs_svg:
        g += f'<rect x="{lx}" y="{lyy}" width="{lw}" height="{lh}" rx="1.5" fill="{OR}" stroke-width="1.2" {S}/><ellipse cx="{lx+lw/2}" cy="{lyy+lh}" rx="3.2" ry="1.4" fill="{WH}" stroke-width="1" {S}/>'
    g += f'<path d="M14 {body_y} l1.4 4 l1.8 -4z M19 {body_y} l1.4 4 l1.8 -4z M24 {body_y} l1.4 4 l1.8 -4z" fill="{GR}"/>'
    g += f'<path d="M25 8 L26 1 L30 5 Z M35 5 L38 1 L38 8 Z" fill="{OR}" stroke-width="1.2" {S}/>'
    g += f'<circle cx="32" cy="13" r="8" fill="{OR}" stroke-width="1.3" {S}/>'
    g += f'<circle cx="29" cy="11.5" r="2.6" fill="{WH}" stroke-width="0.9" {S}/><circle cx="35.5" cy="11.5" r="2.6" fill="{WH}" stroke-width="0.9" {S}/>'
    g += f'<path d="M29 11.5 a0.8 0.8 0 1 1 0.8 0.8 a1.5 1.5 0 1 1 -1.5 -1.5 M35.5 11.5 a0.8 0.8 0 1 1 0.8 0.8 a1.5 1.5 0 1 1 -1.5 -1.5" fill="none" stroke="#111" stroke-width="0.6"/>'
    g += f'<path d="M27.5 16 Q32 20 37 16" fill="{WH}" stroke-width="1" {S}/><path d="M29.5 17 l1 2 l1 -1.4z M33.5 17.4 l1 1.8 l1 -2z" fill="{WH}" stroke-width="0.6" {S}/>'
    g += f'<path d="M32 13.6 l-1 1.4 h2z" fill="#e0457b"/>'
    g += f'<path d="M26.5 6 L27 3 L29 5z M36 5 L37 3 L37.4 6z" fill="#ffb3c1"/>'                       # inner ears
    g += f'<path d="M30 6 l0.8 2 l0.8 -2z M33 5.6 l0.8 2 l0.8 -2z" fill="{GR}"/>'                          # head stripes
    g += f'<path d="M26 14 l-4 -1 M26 15.5 l-4 1 M38 14 l3 -1 M38 15.5 l3 1" stroke="#111" stroke-width="0.5" fill="none"/>'  # whiskers
    g += f'<path d="M12 22 Q20 24.5 28 22" fill="#ffd9a0" stroke="none"/>'                                  # lighter belly
    g += f'<path d="M3 16 l3 1 l-2 2z" fill="{GR}"/>'                                                        # tail stripe
    if strike:
        g += f'<path d="M34 22 L40 21 M34 24 L40 25 M34 26 L39 29" stroke="#111" stroke-width="1.6" stroke-linecap="round" fill="none"/>'
    return g + "</g>"

def sprite(name, w, h, body, bg=None):
    return dict(name=name, w=w, h=h, svg=f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w*8}" height="{h*8}">{body}</svg>')

SP = [
 sprite("cat_run0", 40, 32, cat("a")), sprite("cat_run1", 40, 32, cat("b")),
 sprite("cat_jump", 40, 32, cat(jump=True)),
 sprite("cat_duck", 40, 32, cat("a", duck=True)),
 sprite("cat_strike", 40, 32, cat("a", strike=True)),
 # sofa cushions (jump over)
 sprite("obj_cushion", 24, 20, f'<rect x="2" y="4" width="20" height="14" rx="5" fill="#d9534f" stroke-width="1.3" {S}/><path d="M4 8 q8 -3 16 0" fill="none" stroke="#a02b2b" stroke-width="1"/><circle cx="12" cy="10" r="1.2" fill="#111"/><path d="M5 6 l2 2 M17 6 l2 2 M5 15 l2 -2 M17 15 l2 -2" stroke="#fff" stroke-width="0.8"/>'),
 # hanging lamp (duck under)
 sprite("obj_lamp", 32, 16, f'<path d="M16 0 V4" stroke="#111" stroke-width="1.3"/><path d="M5 14 L10 4 H22 L27 14 Z" fill="#ffd54a" stroke-width="1.3" {S}/><path d="M11 5 L9 14 M16 4 V14 M21 5 L23 14" stroke="#e0a800" stroke-width="0.8"/><ellipse cx="16" cy="14" rx="11" ry="1.6" fill="#fff59d" stroke-width="1" {S}/>'),
 # vase (break with claws)
 sprite("obj_vase", 20, 22, f'<path d="M7 2 H13 L12 7 Q18 10 17 16 Q16 21 10 21 Q4 21 3 16 Q2 10 8 7 Z" fill="{BL}" stroke-width="1.3" {S}/><path d="M6 13 q4 3 8 0" fill="none" stroke="#fff" stroke-width="1"/><path d="M10 7 V3 M10 3 q-3 -2 -4 1 M10 3 q3 -2 4 1" fill="none" stroke="#2e7d32" stroke-width="1"/><circle cx="6" cy="3.6" r="1.3" fill="#ff7043"/><circle cx="14" cy="3.6" r="1.3" fill="#ffd54a"/><path d="M5 17 q2 2 4 0" fill="none" stroke="#bbdefb" stroke-width="0.8"/>'),
 # human (jump on the head) - kid style round head
 sprite("obj_human", 32, 50, f'<g {S} stroke-width="1.2"><path d="M7 20 L2 12 L4 10 L9 17 Z" fill="#f6c9a0"/><path d="M25 20 L30 12 L28 10 L23 17 Z" fill="#f6c9a0"/><circle cx="3" cy="10.5" r="2.2" fill="#f6c9a0"/><circle cx="29" cy="10.5" r="2.2" fill="#f6c9a0"/><rect x="8" y="16" width="16" height="19" rx="3.5" fill="#43a047"/><path d="M8 20 L6 16 L10 15 Z M24 20 L26 16 L22 15 Z" fill="#43a047"/><rect x="8" y="30" width="16" height="3" fill="#6d4c41"/><rect x="14.5" y="30" width="3" height="3" fill="#ffd54a"/><rect x="9" y="34" width="6" height="13" rx="2" fill="#37474f"/><rect x="17" y="34" width="6" height="13" rx="2" fill="#37474f"/><path d="M7 47 h9 v3 h-10 z M16 47 h10 v3 h-9 z" fill="#8d6e63"/><rect x="13" y="12" width="6" height="5" fill="#f6c9a0"/><circle cx="16" cy="8" r="8" fill="#f6c9a0"/><path d="M8 7 Q9 -1 16 -1 Q23 -1 24 7 Q20 3 16 4 Q12 3 8 7Z" fill="#6b3b1f"/><path d="M11 6 l3 1 M21 6 l-3 1" stroke-width="0.8" fill="none"/></g><circle cx="12.5" cy="8.5" r="1.1" fill="#111"/><circle cx="19.5" cy="8.5" r="1.1" fill="#111"/><circle cx="9.5" cy="11" r="1.4" fill="#ff9e9e"/><circle cx="22.5" cy="11" r="1.4" fill="#ff9e9e"/><path d="M12 12.5 q4 3.5 8 0" fill="#fff" stroke="#111" stroke-width="0.9"/>'),
 # pizza slice
 sprite("obj_pizza", 16, 16, f'<path d="M2 3 Q8 0 14 3 L8 15 Z" fill="#ffc83d" stroke-width="1.3" {S}/><path d="M2 3 Q8 0 14 3" fill="none" stroke="#c77700" stroke-width="2"/><circle cx="6.5" cy="6" r="1.4" fill="#d32f2f"/><circle cx="10" cy="7" r="1.4" fill="#d32f2f"/><circle cx="8" cy="10.5" r="1.2" fill="#d32f2f"/>'),
 # punch rocket (flying grinning enemy with a fist)
 sprite("obj_rocket", 28, 18, f'<path d="M9 4 H24 L22 14 H9 Z" fill="#9e9e9e" stroke-width="1.2" {S}/><circle cx="8" cy="9" r="7" fill="#7e57c2" stroke-width="1.3" {S}/><path d="M3 11 q5 4 10 0" fill="#fff" stroke-width="1" {S}/><path d="M4 6 l3 1.5" stroke="#111" stroke-width="1.2"/><circle cx="2" cy="14" r="2.4" fill="#f6c9a0" stroke-width="1" {S}/><path d="M24 7 l4 -2 M24 9 l4 0 M24 11 l4 2" stroke="#ff7043" stroke-width="1.4"/>'),
 sprite("fx_nail", 8, 4, f'<path d="M0 2 L6 0 L8 2 L6 4 Z" fill="#fff" stroke-width="0.8" {S}/>'),
 sprite("ui_heart", 8, 8, '<path d="M4 7 L1 4 Q0 1 2.5 1 Q4 1 4 2.5 Q4 1 5.5 1 Q8 1 7 4 Z" fill="#e53935" stroke="#111" stroke-width="0.6"/>'),
]

def render():
    d = tempfile.mkdtemp()
    for s in SP: open(f"{d}/{s['name']}.svg", "w").write(s["svg"])
    js = f"""const {{chromium}}=require('playwright');(async()=>{{const b=await chromium.launch();
const specs={json.dumps([dict(n=s['name'], w=s['w']*8, h=s['h']*8) for s in SP])};
for(const s of specs){{const p=await b.newPage({{viewport:{{width:s.w,height:s.h}}}});await p.goto('file://{d}/'+s.n+'.svg');
await p.screenshot({{path:'{d}/'+s.n+'.png',omitBackground:true}});await p.close();}}await b.close()}})()"""
    open(f"{d}/r.js", "w").write(js)
    subprocess.run(["node", f"{d}/r.js"], check=True, env=os.environ)
    return d

def rgb565(r, g, b): return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    d = render(); out = []; sheet = []
    for s in SP:
        im = Image.open(f"{d}/{s['name']}.png").convert("RGBA").resize((s["w"], s["h"]), Image.BOX)
        px = []
        for y in range(s["h"]):
            for x in range(s["w"]):
                r, g, b, a = im.getpixel((x, y))
                if a < 110: px.append(0xF81F)
                else:
                    v = rgb565(r * a // 255 + (255 - a) * 0, g * a // 255, b * a // 255); px.append(0xF81E if v == 0xF81F else v)
        out.append((s, px)); sheet.append((s, im))
    os.makedirs(f"{root}/components/game_art/include", exist_ok=True)
    with open(f"{root}/components/game_art/include/sprites.h", "w") as h:
        h.write("// GENERATED by tools/make_sprites.py. RGB565, 0xF81F = transparent.\n#pragma once\n#include <stdint.h>\ntypedef struct { uint8_t w, h; const uint16_t *px; } sprite_t;\n")
        for s, _ in out: h.write(f"extern const sprite_t spr_{s['name']};\n")
    with open(f"{root}/components/game_art/sprites.c", "w") as c:
        c.write('// GENERATED by tools/make_sprites.py. Do not edit.\n#include "sprites.h"\n')
        for s, px in out:
            c.write(f"static const uint16_t d_{s['name']}[{len(px)}] = {{" + ",".join(f"0x{v:04X}" for v in px) + "};\n")
            c.write(f"const sprite_t spr_{s['name']} = {{ {s['w']}, {s['h']}, d_{s['name']} }};\n")
    with open(f"{root}/components/game_art/CMakeLists.txt", "w") as c:
        c.write('idf_component_register(SRCS "sprites.c" INCLUDE_DIRS "include")\n')
    # preview sheet, 6x zoom on a cream background
    Z = 6; W = sum(s["w"] * Z + 16 for s, _ in sheet[:7]) + 16
    os.makedirs(f"{root}/art/preview", exist_ok=True)
    def sheet_row(items):
        w = sum(s["w"] * Z + 16 for s, _ in items) + 16; h = max(s["h"] for s, _ in items) * Z + 32
        img = Image.new("RGB", (w, h), (255, 248, 231)); x = 16
        for s, im in items:
            big = im.resize((s["w"] * Z, s["h"] * Z), Image.NEAREST); img.paste(big, (x, 16), big); x += s["w"] * Z + 16
        return img
    r1, r2 = sheet_row(sheet[:5]), sheet_row(sheet[5:])
    full = Image.new("RGB", (max(r1.width, r2.width), r1.height + r2.height), (255, 248, 231)); full.paste(r1, (0, 0)); full.paste(r2, (0, r1.height))
    full.save(f"{root}/art/preview/sheet.png"); print("wrote", len(out), "sprites")

main()
