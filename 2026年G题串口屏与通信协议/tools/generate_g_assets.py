from __future__ import annotations

from pathlib import Path
from typing import Callable, Tuple

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets"
W, H = 1024, 600


COLORS = {
    "bg": (17, 26, 37),
    "header": (14, 22, 31),
    "panel": (30, 43, 58),
    "panel2": (39, 55, 73),
    "field": (7, 14, 19),
    "grid": (50, 67, 84),
    "line": (87, 110, 132),
    "text": (238, 244, 249),
    "muted": (158, 176, 193),
    "blue": (42, 126, 255),
    "cyan": (30, 188, 186),
    "green": (45, 166, 98),
    "yellow": (242, 188, 62),
    "white": (255, 255, 255),
    "black": (0, 0, 0),
}


FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\simsun.ttc",
]


def font(size: int, bold: bool = False) -> ImageFont.ImageFont:
    candidates = [
        r"C:\Windows\Fonts\msyhbd.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
    ] if bold else FONT_CANDIDATES
    for name in candidates:
        path = Path(name)
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


F_TITLE = font(40, True)
F_PAGE = font(32, True)
F_H2 = font(28, True)
F_LABEL = font(23)
F_VALUE = font(34, True)
F_COMP = font(28, True)
F_STATUS = font(32, True)
F_BUTTON = font(26)
F_BODY = font(20)
F_SMALL = font(18)


def rgb565(color: Tuple[int, int, int]) -> int:
    r, g, b = color
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def text_size(draw: ImageDraw.ImageDraw, text: str, fnt: ImageFont.ImageFont) -> Tuple[int, int]:
    box = draw.textbbox((0, 0), text, font=fnt)
    return box[2] - box[0], box[3] - box[1]


def center_text(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int], text: str,
                fnt: ImageFont.ImageFont, fill=COLORS["text"]) -> None:
    x1, y1, x2, y2 = box
    tb = draw.textbbox((0, 0), text, font=fnt)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    draw.text((x1 + (x2 - x1 - tw) / 2 - tb[0],
               y1 + (y2 - y1 - th) / 2 - tb[1]),
              text, font=fnt, fill=fill)


def rect(draw: ImageDraw.ImageDraw, xy: Tuple[int, int, int, int], fill: str = "panel",
         outline: str = "line", width: int = 1) -> None:
    draw.rounded_rectangle(xy, radius=7, fill=COLORS[fill], outline=COLORS[outline], width=width)


def button(draw: ImageDraw.ImageDraw, xy: Tuple[int, int, int, int], label: str,
           active: bool = False, color: str = "blue") -> None:
    rect(draw, xy, color if active else "panel2", "white" if active else "line",
         2 if active else 1)
    center_text(draw, xy, label, F_BUTTON, COLORS["white"])


def panel(draw: ImageDraw.ImageDraw, xy: Tuple[int, int, int, int], title: str = "") -> None:
    rect(draw, xy)
    if title:
        draw.text((xy[0] + 18, xy[1] + 14), title, font=F_H2, fill=COLORS["text"])


def value_box(draw: ImageDraw.ImageDraw, xy: Tuple[int, int, int, int], text: str = "",
              color: str = "text", fnt: ImageFont.ImageFont = F_VALUE) -> None:
    rect(draw, xy, "panel2")
    if text:
        center_text(draw, xy, text, fnt, COLORS[color])


def draw_header(draw: ImageDraw.ImageDraw, title: str) -> None:
    draw.rectangle((0, 0, W, 78), fill=COLORS["header"])
    draw.text((34, 13), title, font=F_TITLE, fill=COLORS["text"])
    draw.text((650, 27), "周期信号测量分析装置", font=F_BODY, fill=COLORS["muted"])
    draw.line((0, 78, W, 78), fill=COLORS["line"], width=1)


def draw_footer(draw: ImageDraw.ImageDraw, active: int) -> None:
    labels = [("参数", 320, 0), ("波形", 452, 1), ("频谱", 584, 2)]
    for label, x, idx in labels:
        button(draw, (x, 536, x + 120, 580), label, idx == active, "blue")


def new_page(title: str, active: int) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (W, H), COLORS["bg"])
    draw = ImageDraw.Draw(img)
    draw_header(draw, title)
    draw_footer(draw, active)
    return img, draw


def draw_grid(draw: ImageDraw.ImageDraw, xy: Tuple[int, int, int, int], midline: bool = True) -> None:
    x1, y1, x2, y2 = xy
    rect(draw, xy, "field")
    for i in range(11):
        x = x1 + (x2 - x1) * i // 10
        draw.line((x, y1, x, y2), fill=COLORS["grid"], width=1)
    for i in range(7):
        y = y1 + (y2 - y1) * i // 6
        draw.line((x1, y, x2, y), fill=COLORS["grid"], width=1)
    if midline:
        draw.line((x1, (y1 + y2) // 2, x2, (y1 + y2) // 2), fill=COLORS["line"], width=1)


def page_overview() -> Image.Image:
    img, draw = new_page("总览参数", 0)

    panel(draw, (42, 100, 982, 190), "")
    draw.text((70, 116), "FPGA模式", font=F_H2, fill=COLORS["text"])
    value_box(draw, (224, 112, 392, 172), "--", "yellow", F_VALUE)
    draw.text((430, 116), "当前状态", font=F_H2, fill=COLORS["text"])
    value_box(draw, (584, 112, 912, 172), "等待数据", "yellow", F_STATUS)

    panel(draw, (42, 212, 396, 514), "时域参数")
    rows = [
        ("Vpp", "-- mV", 308, "text"),
        ("Urms", "-- mV", 386, "text"),
        ("f1", "-- kHz", 464, "yellow"),
    ]
    for label, value, y, color in rows:
        draw.text((78, y - 20), label, font=F_LABEL, fill=COLORS["muted"])
        value_box(draw, (154, y - 34, 356, y + 22), value, color, F_VALUE)

    panel(draw, (430, 212, 982, 514), "分量峰值")
    for i, y in enumerate([308, 386, 464], 1):
        draw.text((464, y - 20), f"C{i}", font=F_LABEL, fill=COLORS["muted"])
        value_box(draw, (520, y - 34, 944, y + 22), "-- kHz        -- mV", "text", F_COMP)
    draw.text((512, 488), "场景由FPGA板上按键选择，屏幕只显示最新结果", font=F_SMALL, fill=COLORS["muted"])
    return img


def page_wave() -> Image.Image:
    img, draw = new_page("波形", 1)
    panel(draw, (42, 102, 724, 514), "")
    draw.text((68, 120), "波形重现区", font=F_H2, fill=COLORS["text"])
    draw_grid(draw, (60, 166, 706, 452), midline=True)
    draw.text((72, 472), "横向按 1/3 周期重画，纵向按峰值自动缩放", font=F_SMALL, fill=COLORS["muted"])

    panel(draw, (740, 102, 982, 514), "")
    button(draw, (760, 116, 864, 164), "1周期", True, "green")
    button(draw, (878, 116, 962, 164), "3周期")
    for text, y, color in [
        ("Vpp  -- mV", 194, "text"),
        ("Urms -- mV", 260, "text"),
        ("f1   -- kHz", 326, "yellow"),
    ]:
        value_box(draw, (760, y, 962, y + 54), text, color, F_COMP)
    value_box(draw, (760, 402, 962, 452), "周期 1", "text", F_COMP)
    value_box(draw, (760, 462, 962, 506), "等待数据", "yellow", F_STATUS)
    return img


def page_spectrum() -> Image.Image:
    img, draw = new_page("频谱", 2)
    panel(draw, (42, 102, 724, 514), "")
    draw.text((68, 120), "正频率轴 0~500kHz", font=F_H2, fill=COLORS["text"])
    grid = (60, 166, 706, 452)
    draw_grid(draw, grid, midline=False)
    x1, _y1, x2, _y2 = grid
    for idx, label in enumerate(["0", "100k", "200k", "300k", "400k", "500k"]):
        x = x1 + (x2 - x1) * idx / 5
        tw, _ = text_size(draw, label, F_SMALL)
        draw.text((x - tw / 2, 472), label, font=F_SMALL, fill=COLORS["muted"])

    panel(draw, (740, 102, 982, 514), "")
    draw.text((760, 120), "分量与状态", font=F_H2, fill=COLORS["text"])
    for i, y in enumerate([212, 292, 372], 1):
        draw.text((760, y - 28), f"C{i}", font=F_LABEL, fill=COLORS["muted"])
        value_box(draw, (804, y - 40, 962, y + 22), "--", "text", F_COMP)
    draw.text((764, 414), "峰值mV / 频率kHz", font=F_SMALL, fill=COLORS["muted"])
    value_box(draw, (760, 452, 962, 506), "等待数据", "yellow", F_STATUS)
    return img


PAGES: list[tuple[str, Callable[[], Image.Image]]] = [
    ("page0_overview_g", page_overview),
    ("page1_wave_g", page_wave),
    ("page2_spectrum_g", page_spectrum),
]


def cleanup_old_assets(wanted: set[str]) -> None:
    for path in ASSET_DIR.glob("page*_g.*"):
        if path.name not in wanted:
            path.unlink()


def main() -> None:
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    wanted = {f"{name}.{ext}" for name, _ in PAGES for ext in ("png", "jpg")}
    cleanup_old_assets(wanted)

    for name, maker in PAGES:
        img = maker()
        if img.size != (W, H):
            raise RuntimeError(f"{name} size is {img.size}, expected {(W, H)}")
        img.save(ASSET_DIR / f"{name}.png")
        img.save(ASSET_DIR / f"{name}.jpg", quality=92, subsampling=0)
        print(f"{name}: {img.size[0]}x{img.size[1]}")

    print("\nRGB565 colors for HMI scripts:")
    for key in ["black", "white", "panel", "panel2", "blue", "cyan", "green", "yellow"]:
        print(f"{key:>7}: {rgb565(COLORS[key])}")


if __name__ == "__main__":
    main()
