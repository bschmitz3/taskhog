#!/usr/bin/env python3
"""
Gera assets C (fontes bitmap + imagens 1-bit) para o firmware Taskhog.

- Fontes: rasteriza SpaceMono (TTF) em tamanhos fixos -> arrays C monoespaçados.
- Imagens: rasteriza os SVG de mascote/ícones -> bitmaps 1-bit.

Saída:
  taskhog-fw/components/ui/assets_fonts.c / .h
  taskhog-fw/components/ui/assets_images.c / .h

Uso: tools/.venv/bin/python tools/gen_assets.py
"""
import io
import os

from PIL import Image, ImageFont, ImageDraw
import cairosvg

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, "ui")
OUT = os.path.join(ROOT, "taskhog-fw", "components", "ui")

FONT_DIR = os.path.join(UI, "fonts")

# (símbolo C, arquivo TTF, tamanho px)
FONTS = [
    ("g_font_sm", "SpaceMono-Regular.ttf", 13),
    ("g_font_md", "SpaceMono-Regular.ttf", 16),
    ("g_font_hd", "SpaceMono-Bold.ttf", 18),
    ("g_font_tt", "SpaceMono-Bold.ttf", 26),
    ("g_font_xl", "SpaceMono-Bold.ttf", 40),
]

FIRST_CHAR = 32
LAST_CHAR = 126

# (símbolo C, caminho SVG, altura alvo px)
IMAGES = [
    ("g_img_mascot_default", "assets/mascot/default.svg", 104),
    ("g_img_mascot_happy", "assets/mascot/happy.svg", 104),
    ("g_img_mascot_sleeping", "assets/mascot/sleeping.svg", 104),
    ("g_img_mascot_angry", "assets/mascot/angry.svg", 104),
    ("g_img_mascot_listening", "assets/mascot/listening.svg", 104),

    ("g_img_wifi", "assets/icon/wifi.svg", 16),
    ("g_img_no_wifi", "assets/icon/no_wifi.svg", 16),
    ("g_img_wifi_error", "assets/icon/wifi_error.svg", 16),
    ("g_img_batt_full", "assets/icon/battery_full.svg", 16),
    ("g_img_batt_mid", "assets/icon/battery_mid.svg", 16),
    ("g_img_batt_low", "assets/icon/battery_low.svg", 16),
    ("g_img_batt_empty", "assets/icon/battery_empty.svg", 16),
    ("g_img_batt_charging", "assets/icon/battery_charging.svg", 16),
    ("g_img_cloud_sm", "assets/icon/up_to_cloud.svg", 16),
    ("g_img_folder_sm", "assets/icon/folder.svg", 16),

    ("g_img_mic_sm", "assets/icon/microphone.svg", 22),
    ("g_img_check_sm", "assets/icon/check.svg", 20),
    ("g_img_mic_lg", "assets/icon/microphone.svg", 56),
    ("g_img_check_lg", "assets/icon/check.svg", 52),
    ("g_img_cloud_lg", "assets/icon/up_to_cloud.svg", 64),
    ("g_img_warning_lg", "assets/icon/warning.svg", 52),
    ("g_img_batt_charging_lg", "assets/icon/battery_charging.svg", 64),
    ("g_img_batt_low_lg", "assets/icon/battery_low.svg", 64),
]

INK = 128  # limiar


def pack_bits(pixels, w, h):
    """pixels[y][x] bool(ink) -> (bytes_per_row, bytearray) MSB-first."""
    bpr = (w + 7) // 8
    out = bytearray()
    for y in range(h):
        for bx in range(bpr):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < w and pixels[y][x]:
                    byte |= 1 << (7 - bit)
            out.append(byte)
    return bpr, out


def c_array(name, data):
    lines = [f"static const uint8_t {name}[] = {{"]
    row = "    "
    for i, b in enumerate(data):
        row += f"0x{b:02X},"
        if (i + 1) % 16 == 0:
            lines.append(row)
            row = "    "
    if row.strip():
        lines.append(row)
    lines.append("};")
    return "\n".join(lines)


def gen_fonts():
    decls = []
    defs = []
    for sym, ttf, size in FONTS:
        font = ImageFont.truetype(os.path.join(FONT_DIR, ttf), size)
        ascent, descent = font.getmetrics()
        cell_h = ascent + descent
        advance = round(font.getlength("M"))
        cell_w = advance
        count = LAST_CHAR - FIRST_CHAR + 1

        all_bytes = bytearray()
        bpr = (cell_w + 7) // 8
        for code in range(FIRST_CHAR, LAST_CHAR + 1):
            img = Image.new("L", (cell_w, cell_h), 0)
            d = ImageDraw.Draw(img)
            d.text((0, 0), chr(code), fill=255, font=font)
            px = img.load()
            pixels = [[px[x, y] >= INK for x in range(cell_w)] for y in range(cell_h)]
            _, data = pack_bits(pixels, cell_w, cell_h)
            all_bytes.extend(data)

        defs.append(c_array(f"{sym}_data", all_bytes))
        defs.append(
            f"const gfx_font_t {sym} = {{ {FIRST_CHAR}, {count}, {cell_w}, "
            f"{cell_h}, {advance}, {bpr}, {sym}_data }};\n"
        )
        decls.append(f"extern const gfx_font_t {sym};")
        print(f"  {sym}: cell {cell_w}x{cell_h} adv {advance} ({len(all_bytes)} bytes)")

    with open(os.path.join(OUT, "assets_fonts.h"), "w") as f:
        f.write("#pragma once\n\n#include \"gfx.h\"\n\n")
        f.write("\n".join(decls))
        f.write("\n")
    with open(os.path.join(OUT, "assets_fonts.c"), "w") as f:
        f.write("/* GERADO por tools/gen_assets.py — não editar à mão. */\n")
        f.write("#include \"assets_fonts.h\"\n\n")
        f.write("\n".join(defs))
        f.write("\n")


def render_svg(path, target_h):
    png = cairosvg.svg2png(url=path, output_height=target_h)
    im = Image.open(io.BytesIO(png)).convert("RGBA")
    return im


def gen_images():
    decls = []
    defs = []
    for sym, rel, th in IMAGES:
        im = render_svg(os.path.join(UI, rel), th)
        w, h = im.size
        alpha = im.split()[3].load()
        rgb = im.convert("L").load()
        pixels = [
            [alpha[x, y] >= INK and rgb[x, y] < INK for x in range(w)]
            for y in range(h)
        ]
        bpr, data = pack_bits(pixels, w, h)
        defs.append(c_array(f"{sym}_data", data))
        defs.append(
            f"const gfx_image_t {sym} = {{ {w}, {h}, {bpr}, {sym}_data }};\n"
        )
        decls.append(f"extern const gfx_image_t {sym};")
        print(f"  {sym}: {w}x{h} ({len(data)} bytes)")

    with open(os.path.join(OUT, "assets_images.h"), "w") as f:
        f.write("#pragma once\n\n#include \"gfx.h\"\n\n")
        f.write("\n".join(decls))
        f.write("\n")
    with open(os.path.join(OUT, "assets_images.c"), "w") as f:
        f.write("/* GERADO por tools/gen_assets.py — não editar à mão. */\n")
        f.write("#include \"assets_images.h\"\n\n")
        f.write("\n".join(defs))
        f.write("\n")


if __name__ == "__main__":
    print("Fontes:")
    gen_fonts()
    print("Imagens:")
    gen_images()
    print("OK")
