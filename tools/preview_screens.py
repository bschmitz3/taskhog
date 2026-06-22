#!/usr/bin/env python3
"""Simula as telas 200x200 (mesmos assets/fontes/layout do screens.c) p/ revisão."""
import io, os
from PIL import Image, ImageFont, ImageDraw
import cairosvg

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, "ui")
FONTS = {
    "sm": (ImageFont.truetype(os.path.join(UI, "fonts/SpaceMono-Regular.ttf"), 13), 8, 20),
    "md": (ImageFont.truetype(os.path.join(UI, "fonts/SpaceMono-Regular.ttf"), 16), 10, 24),
    "hd": (ImageFont.truetype(os.path.join(UI, "fonts/SpaceMono-Bold.ttf"), 18), 11, 28),
    "tt": (ImageFont.truetype(os.path.join(UI, "fonts/SpaceMono-Bold.ttf"), 26), 16, 40),
    "xl": (ImageFont.truetype(os.path.join(UI, "fonts/SpaceMono-Bold.ttf"), 40), 24, 60),
}


def svg(path, h):
    png = cairosvg.svg2png(url=os.path.join(UI, "assets", path), output_height=h)
    im = Image.open(io.BytesIO(png)).convert("RGBA")
    w, hh = im.size
    a = im.split()[3].load(); l = im.convert("L").load()
    o = Image.new("1", (w, hh), 0); p = o.load()
    for y in range(hh):
        for x in range(w):
            p[x, y] = 1 if (a[x, y] >= 128 and l[x, y] < 128) else 0
    return o


class FB:
    def __init__(s):
        s.img = Image.new("1", (200, 200), 1)
        s.d = ImageDraw.Draw(s.img)

    def text(s, fk, x, y, t):
        s.d.text((x, y), t, fill=0, font=FONTS[fk][0])

    def tw(s, fk, t):
        return len(t) * FONTS[fk][1]

    def center(s, fk, y, t):
        s.text(fk, (200 - s.tw(fk, t)) // 2, y, t)

    def right(s, fk, xr, y, t):
        s.text(fk, xr - s.tw(fk, t), y, t)

    def image(s, im, x, y):
        s.img.paste(0, (x, y, x + im.width, y + im.height), im)

    def image_center(s, im, y):
        s.image(im, (200 - im.width) // 2, y)

    def icon_label(s, im, fk, t, y, gap):
        ch = FONTS[fk][2]
        total = im.width + gap + s.tw(fk, t)
        x = (200 - total) // 2
        s.image(im, x, y + (ch - im.height) // 2)
        s.text(fk, x + im.width + gap, y, t)


def status_bar(fb, pct=90):
    fb.text("sm", 8, 6, "15:30")
    xr = 192
    fb.right("sm", xr, 6, f"{pct}%")
    xr -= fb.tw("sm", f"{pct}%") + 4
    bat = svg("icon/battery_full.svg", 16)
    fb.image(bat, xr - bat.width, 6); xr -= bat.width + 5
    wf = svg("icon/no_wifi.svg", 16)
    fb.image(wf, xr - wf.width, 6)


def boot():
    fb = FB(); fb.center("tt", 14, "TASKHOG"); fb.center("sm", 52, "v1.0.0")
    fb.image_center(svg("mascot/default.svg", 104), 84); return fb.img


def home(q):
    fb = FB(); status_bar(fb)
    if q > 0:
        fb.image_center(svg("mascot/default.svg", 104), 26)
        fb.icon_label(svg("icon/up_to_cloud.svg", 16), "hd", f"{q} na fila", 142, 6)
    else:
        fb.image_center(svg("mascot/sleeping.svg", 104), 26)
        fb.icon_label(svg("icon/up_to_cloud.svg", 16), "hd", "Nada na fila", 142, 6)
    fb.center("sm", 178, "Segure REC para gravar"); return fb.img


def recording():
    fb = FB(); status_bar(fb)
    fb.image_center(svg("mascot/angry.svg", 104), 26)
    fb.icon_label(svg("icon/microphone.svg", 22), "hd", "Gravando 0:07", 142, 6)
    fb.center("sm", 178, "Solte REC para finalizar"); return fb.img


def saved():
    fb = FB(); status_bar(fb)
    fb.image_center(svg("mascot/happy.svg", 104), 20)
    fb.icon_label(svg("icon/check.svg", 20), "hd", "Salvo", 138, 8)
    fb.center("md", 162, "Salvo no dispositivo"); return fb.img


def sync():
    fb = FB(); status_bar(fb)
    fb.image_center(svg("icon/up_to_cloud.svg", 64), 36)
    fb.center("hd", 110, "Sincronizando"); fb.center("xl", 132, "3"); return fb.img


screens = [("boot", boot()), ("home2", home(2)), ("home0", home(0)),
           ("recording", recording()), ("saved", saved()), ("sync", sync())]
pad = 12; sc = 2
W = len(screens) * (200 * sc + pad) + pad
canvas = Image.new("RGB", (W, 200 * sc + 40), (200, 200, 200))
dr = ImageDraw.Draw(canvas)
x = pad
for name, im in screens:
    big = im.convert("RGB").resize((200 * sc, 200 * sc), Image.NEAREST)
    canvas.paste(big, (x, 30))
    dr.text((x, 8), name, fill=(0, 0, 0))
    x += 200 * sc + pad
canvas.save("/tmp/screens_preview.png")
print("saved /tmp/screens_preview.png", canvas.size)
