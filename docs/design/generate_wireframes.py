#!/usr/bin/env python3
"""Generate Taskhog e-Paper wireframes (200x200 1bpp) from epaper-ui-spec.md."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 200, 200
MARGIN = 8
ZONE_A_H = 24
ZONE_C_Y = 180
SCALE = 4

OUT_DIR = Path(__file__).parent / "wireframes"

FONT_PATHS = [
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/Library/Fonts/Arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    paths = FONT_PATHS if bold else FONT_PATHS[1:] + FONT_PATHS[:1]
    for path in paths:
        p = Path(path)
        if p.exists():
            try:
                return ImageFont.truetype(str(p), size)
            except OSError:
                continue
    return ImageFont.load_default()


def canvas(inverted: bool = False) -> tuple[Image.Image, ImageDraw.ImageDraw, int, int]:
    fg = 0 if not inverted else 255
    bg = 255 if not inverted else 0
    img = Image.new("1", (W, H), bg)
    draw = ImageDraw.Draw(img)
    return img, draw, fg, bg


def text_size(draw: ImageDraw.ImageDraw, text: str, font) -> tuple[int, int]:
    box = draw.textbbox((0, 0), text, font=font)
    return box[2] - box[0], box[3] - box[1]


def text_center(
    draw: ImageDraw.ImageDraw,
    y: int,
    text: str,
    font,
    fg: int,
    x0: int = MARGIN,
    x1: int = W - MARGIN,
) -> None:
    tw, _ = text_size(draw, text, font)
    x = x0 + (x1 - x0 - tw) // 2
    draw.text((x, y), text, fill=fg, font=font)


def text_left(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, font, fg: int) -> None:
    draw.text(xy, text, fill=fg, font=font)


def hline(draw: ImageDraw.ImageDraw, y: int, fg: int, x0: int = MARGIN, x1: int = W - MARGIN) -> None:
    draw.line((x0, y, x1, y), fill=fg, width=1)


def draw_wifi(draw: ImageDraw.ImageDraw, x: int, y: int, fg: int, on: bool) -> None:
    if on:
        draw.arc((x, y + 4, x + 12, y + 16), 200, 340, fill=fg, width=1)
        draw.arc((x + 2, y + 7, x + 10, y + 15), 200, 340, fill=fg, width=1)
        draw.point((x + 6, y + 14), fill=fg)
    else:
        draw.arc((x, y + 4, x + 12, y + 16), 200, 340, fill=fg, width=1)
        draw.line((x + 2, y + 14, x + 10, y + 6), fill=fg, width=1)


def draw_battery(draw: ImageDraw.ImageDraw, x: int, y: int, fg: int, pct: int, critical: bool = False) -> None:
    draw.rectangle((x, y + 2, x + 14, y + 12), outline=fg, width=1)
    draw.rectangle((x + 14, y + 4, x + 16, y + 10), fill=fg)
    fill_w = max(0, min(12, int(12 * pct / 100)))
    if critical:
        draw.line((x + 4, y + 4, x + 10, y + 10), fill=fg, width=1)
        draw.line((x + 10, y + 4, x + 4, y + 10), fill=fg, width=1)
    elif fill_w:
        draw.rectangle((x + 1, y + 3, x + 1 + fill_w, y + 11), fill=fg)


def draw_bolt(draw: ImageDraw.ImageDraw, x: int, y: int, fg: int) -> None:
    draw.polygon([(x + 6, y), (x + 2, y + 8), (x + 6, y + 8), (x + 4, y + 14), (x + 10, y + 5), (x + 6, y + 5)], fill=fg)


def status_bar(
    draw: ImageDraw.ImageDraw,
    fg: int,
    time_str: str = "15:30",
    wifi_on: bool = True,
    battery_pct: int = 78,
    queue: int = 0,
    charging: bool = False,
    battery_critical: bool = False,
) -> None:
    f_cap = load_font(10)
    f_sm = load_font(9)
    text_left(draw, (MARGIN, 6), time_str, f_cap, fg)
    draw_wifi(draw, 52, 4, fg, wifi_on)
    bx = 70
    draw_battery(draw, bx, 4, fg, battery_pct, critical=battery_critical)
    text_left(draw, (bx + 18, 6), f"{battery_pct}%", f_sm, fg)
    if charging:
        draw_bolt(draw, bx + 44, 2, fg)
    if queue > 0:
        text_left(draw, (W - MARGIN - 18, 6), f"^{queue}", f_cap, fg)


def footer(draw: ImageDraw.ImageDraw, fg: int, text: str) -> None:
    text_center(draw, ZONE_C_Y + 2, text, load_font(9), fg)


def mic_icon(draw: ImageDraw.ImageDraw, cx: int, cy: int, fg: int, r: int = 20) -> None:
    draw.ellipse((cx - r // 2, cy - r, cx + r // 2, cy), outline=fg, width=2)
    draw.arc((cx - r // 2 - 4, cy - 4, cx + r // 2 + 4, cy + 16), 0, 180, fill=fg, width=1)
    draw.line((cx, cy + 16, cx, cy + 22), fill=fg, width=1)
    draw.line((cx - 8, cy + 22, cx + 8, cy + 22), fill=fg, width=1)


def check_icon(draw: ImageDraw.ImageDraw, cx: int, cy: int, fg: int, size: int = 24) -> None:
    draw.line((cx - size // 2, cy, cx - 4, cy + size // 2), fill=fg, width=2)
    draw.line((cx - 4, cy + size // 2, cx + size // 2, cy - size // 3), fill=fg, width=2)


def warn_icon(draw: ImageDraw.ImageDraw, cx: int, cy: int, fg: int) -> None:
    draw.polygon([(cx, cy - 14), (cx - 12, cy + 10), (cx + 12, cy + 10)], outline=fg, width=1)
    draw.line((cx, cy - 6, cx, cy + 2), fill=fg, width=1)
    draw.point((cx, cy + 6), fill=fg)


def blit_sprite(
    img: Image.Image,
    sprite: list[str],
    cx: int,
    cy: int,
    fg: int = 0,
    bg: int | None = None,
) -> None:
    """Paint a '#' bitmap centered at (cx, cy). bg=None skips transparent holes."""
    h = len(sprite)
    w = len(sprite[0]) if sprite else 0
    ox = cx - w // 2
    oy = cy - h // 2
    for row, line in enumerate(sprite):
        for col, ch in enumerate(line):
            if ch != "#":
                continue
            x, y = ox + col, oy + row
            if 0 <= x < W and 0 <= y < H:
                img.putpixel((x, y), fg)


# 32×32 pixel rabbit variants (1-bit)
RABBIT_BOOT = [
    "....####....####....",
    "...##############...",
    "..####......####....",
    "..###..####..###....",
    "..###..####..###....",
    "..###........###....",
    "...###..##..###.....",
    "....##########......",
    ".....########.......",
    "....##########......",
    "...####....####.....",
    "..####......####....",
    "..###........###....",
    "..###........###....",
    "...###......###.....",
    "....##########......",
    ".....######.........",
    "....##....##........",
    "...##......##.......",
    "...##......##.......",
    "....##....##........",
    ".....######.........",
]

RABBIT_IDLE = [
    "....####....####....",
    "...##############...",
    "..####......####....",
    "..###..####..###....",
    "..###..####..###....",
    "..###........###....",
    "...###..##..###.....",
    "....##########......",
    ".....########.......",
    "....##########......",
    "...####....####.....",
    "..####......####....",
    "..###........###....",
    "..###........###....",
    "...###......###.....",
    "....##########......",
    ".....######.........",
    "....##....##........",
    "...##......##.......",
    "....##....##........",
    ".....######.........",
    "....................",
]

RABBIT_EMPTY = [
    "....####....####....",
    "..##############....",
    ".####......####.....",
    ".###..####..###.....",
    ".###..####..###.....",
    ".###........###.....",
    "..###..##..###......",
    "...##########.......",
    "....########........",
    "...##########.......",
    "..####....####......",
    ".####......####.....",
    ".###...##...###.....",
    "..###......###......",
    "...###....###.......",
    "....########........",
    ".....######.........",
    "....##....##........",
    "...##......##.......",
    "....##....##........",
    ".....######.........",
    "....................",
]


def progress_bar(draw: ImageDraw.ImageDraw, y: int, fg: int, bg: int, pct: float, label: str) -> None:
    x0, x1 = MARGIN, W - MARGIN
    h = 8
    draw.rectangle((x0, y, x1, y + h), outline=fg, width=1)
    fill_w = int((x1 - x0 - 2) * pct)
    if fill_w:
        draw.rectangle((x0 + 1, y + 1, x0 + 1 + fill_w, y + h - 1), fill=fg)
    text_center(draw, y + 12, label, load_font(9), fg)


def screen_t0() -> Image.Image:
    img, draw, fg, _ = canvas()
    text_center(draw, 36, "TASKHOG", load_font(22, bold=True), fg)
    text_center(draw, 58, "v1.0.0", load_font(10), fg)
    blit_sprite(img, RABBIT_BOOT, W // 2, 108, fg=fg)
    text_center(draw, 158, ". . .", load_font(12), fg)
    return img


def screen_t1() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, queue=2)
    hline(draw, ZONE_A_H, fg)
    text_center(draw, 38, "15:30", load_font(32, bold=True), fg)
    text_center(draw, 74, "qua, 17 jun", load_font(11), fg)
    text_center(draw, 108, "12 hoje   ^2 fila", load_font(10), fg)
    text_center(draw, 128, "24C   55%", load_font(10), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "segure REC p/ gravar")
    return img


def screen_t1_empty() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, queue=0)
    hline(draw, ZONE_A_H, fg)
    text_center(draw, 38, "15:30", load_font(32, bold=True), fg)
    text_center(draw, 74, "qua, 17 jun", load_font(11), fg)
    blit_sprite(img, RABBIT_IDLE, W // 2, 118, fg=fg)
    text_center(draw, 148, "Nada na fila", load_font(10), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "segure REC p/ gravar")
    return img


def screen_t2() -> Image.Image:
    img, draw, fg, bg = canvas(inverted=True)
    status_bar(draw, fg, time_str="15:31", queue=0)
    hline(draw, ZONE_A_H, fg)
    mic_icon(draw, W // 2, 72, fg, r=24)
    text_center(draw, 98, "GRAVANDO", load_font(14, bold=True), fg)
    text_center(draw, 118, "0:07", load_font(18, bold=True), fg)
    progress_bar(draw, 140, fg, bg, 7 / 120, "07/120")
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "solte REC p/ finalizar")
    return img


def screen_t3() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, time_str="15:31", queue=3)
    hline(draw, ZONE_A_H, fg)
    check_icon(draw, W // 2, 68, fg, 28)
    text_center(draw, 92, "Gravado!", load_font(16, bold=True), fg)
    text_center(draw, 114, "na fila (^3) . 0:07", load_font(10), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "sincroniza ao reconectar")
    return img


def screen_t4() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, time_str="15:32", queue=2)
    hline(draw, ZONE_A_H, fg)
    text_center(draw, 58, "Sincronizando", load_font(13, bold=True), fg)
    text_center(draw, 84, "2 / 3", load_font(22, bold=True), fg)
    text_center(draw, 118, ". . .", load_font(14), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "segure REC p/ gravar")
    return img


def screen_t5() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, time_str="15:33", queue=0)
    hline(draw, ZONE_A_H, fg)
    text_left(draw, (MARGIN, 34), "Ultima tarefa:", load_font(10), fg)
    text_left(draw, (MARGIN, 50), '"Criar apresent. BB"', load_font(10, bold=True), fg)
    text_left(draw, (MARGIN, 78), "Trabalho", load_font(10), fg)
    text_left(draw, (MARGIN, 94), "alta conf", load_font(10), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "REC . fila")
    return img


def screen_t6() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, time_str="15:31", queue=3)
    hline(draw, ZONE_A_H, fg)
    text_left(draw, (MARGIN, 30), "Fila: 3 pendentes  !1", load_font(10, bold=True), fg)
    hline(draw, 46, fg)
    rows = [
        "15:31 . 0:07 . enviando",
        "15:28 . 0:12 . na fila",
        "15:10 . 0:05 . falha 3x",
    ]
    y = 54
    for row in rows:
        text_left(draw, (MARGIN, y), row, load_font(9), fg)
        y += 16
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "NAV -> info")
    return img


def screen_t6_empty() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, time_str="15:31", queue=0)
    hline(draw, ZONE_A_H, fg)
    text_center(draw, 36, "Fila vazia", load_font(13, bold=True), fg)
    blit_sprite(img, RABBIT_EMPTY, W // 2, 92, fg=fg)
    text_center(draw, 132, "Segure REC e fale", load_font(10), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "NAV -> info")
    return img


def screen_t7() -> Image.Image:
    img, draw, fg, _ = canvas()
    text_center(draw, 8, "MODO SETUP", load_font(12, bold=True), fg)
    hline(draw, 24, fg)
    text_left(draw, (MARGIN, 34), "1. Wi-Fi no celular:", load_font(9), fg)
    text_left(draw, (MARGIN + 6, 48), "Taskhog-Setup", load_font(10, bold=True), fg)
    text_left(draw, (MARGIN + 6, 62), "senha: taskhog123", load_font(9), fg)
    text_left(draw, (MARGIN, 84), "2. Abra no navegador:", load_font(9), fg)
    text_left(draw, (MARGIN + 6, 98), "192.168.4.1", load_font(11, bold=True), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "aguardando config")
    return img


def screen_t8() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, battery_pct=8, wifi_on=False, battery_critical=True)
    hline(draw, ZONE_A_H, fg)
    draw_battery(draw, W // 2 - 8, 48, fg, 8, critical=True)
    text_center(draw, 78, "Bateria critica", load_font(12, bold=True), fg)
    text_center(draw, 102, "Conecte o USB", load_font(10), fg)
    text_center(draw, 116, "p/ sincronizar.", load_font(10), fg)
    text_center(draw, 140, "Tudo salvo local", load_font(9), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "segure REC p/ gravar")
    return img


def screen_t9() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, battery_pct=45, charging=True)
    hline(draw, ZONE_A_H, fg)
    draw_bolt(draw, W // 2 - 6, 48, fg)
    text_center(draw, 78, "Carregando...", load_font(12, bold=True), fg)
    text_center(draw, 102, "45%", load_font(18, bold=True), fg)
    text_center(draw, 128, "Sincronizando fila", load_font(9), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "segure REC p/ gravar")
    return img


def screen_t10() -> Image.Image:
    img, draw, fg, _ = canvas()
    status_bar(draw, fg, wifi_on=False, queue=2)
    hline(draw, ZONE_A_H, fg)
    warn_icon(draw, W // 2, 62, fg)
    text_center(draw, 88, "Erro de conexao", load_font(11, bold=True), fg)
    text_center(draw, 112, "Tudo salvo local", load_font(9), fg)
    text_center(draw, 126, "Tentara de novo", load_font(9), fg)
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "segure REC p/ gravar")
    return img


def screen_t11() -> Image.Image:
    img, draw, fg, _ = canvas()
    text_center(draw, 8, "INFORMACOES", load_font(11, bold=True), fg)
    hline(draw, 24, fg)
    lines = [
        "ID: taskhog-01",
        "FW: 1.0.0",
        "Bat 78%  SD 58GB",
        "Hub: taskhog.exemplo",
        "Sync: 15:28 OK",
        "Hoje:12  Fila:2",
    ]
    y = 32
    for line in lines:
        text_left(draw, (MARGIN, y), line, load_font(9), fg)
        y += 14
    hline(draw, ZONE_C_Y - 1, fg)
    footer(draw, fg, "NAV -> voltar")
    return img


SCREENS = [
    ("t00_boot", "T0 Boot", screen_t0),
    ("t01_home", "T1 Home", screen_t1),
    ("t01_home_empty", "T1 Home empty", screen_t1_empty),
    ("t02_recording", "T2 Recording", screen_t2),
    ("t03_saved", "T3 Saved", screen_t3),
    ("t04_syncing", "T4 Syncing", screen_t4),
    ("t05_result", "T5 Last result", screen_t5),
    ("t06_queue", "T6 Queue", screen_t6),
    ("t06_queue_empty", "T6 Queue empty", screen_t6_empty),
    ("t07_setup", "T7 Setup", screen_t7),
    ("t08_low_battery", "T8 Low battery", screen_t8),
    ("t09_charging", "T9 Charging", screen_t9),
    ("t10_error", "T10 Error", screen_t10),
    ("t11_info", "T11 Info", screen_t11),
]


def save_screen(name: str, label: str, img: Image.Image) -> None:
    native = OUT_DIR / "native" / f"{name}.png"
    scaled = OUT_DIR / f"{name}.png"
    native.parent.mkdir(parents=True, exist_ok=True)
    img.save(native, format="PNG")
    big = img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST)
    big.save(scaled, format="PNG")


def save_overview() -> None:
    cols, rows = 4, 4
    pad = 24
    label_h = 16
    cell_w = W * SCALE + pad
    cell_h = H * SCALE + label_h + pad
    ow = cols * cell_w + pad
    oh = rows * cell_h + pad
    overview = Image.new("1", (ow, oh), 1)
    draw = ImageDraw.Draw(overview)
    font = load_font(11, bold=True)

    for idx, (name, label, factory) in enumerate(SCREENS):
        col, row = idx % cols, idx // cols
        x = pad + col * cell_w
        y = pad + row * cell_h
        screen = factory().resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST)
        overview.paste(screen, (x, y + label_h))
        draw.text((x, y), label, fill=0, font=font)

    overview.save(OUT_DIR / "overview.png", format="PNG")


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, label, factory in SCREENS:
        save_screen(name, label, factory())
        print(f"  {name}.png")
    save_overview()
    print(f"  overview.png")
    print(f"\nOutput: {OUT_DIR}")


if __name__ == "__main__":
    main()
