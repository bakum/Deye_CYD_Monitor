# Генератор иконок вкладки Flow -> src/flow_icons.c
# Иконки рисуются на холсте 96x96 и уменьшаются до 24x24 (сглаживание).
# Формат LV_IMG_CF_ALPHA_8BIT: только прозрачность, цвет задаётся в коде через img_recolor.
# Запуск: python tools/gen_flow_icons.py [папка_для_превью]
import math
import os
import sys

from PIL import Image, ImageDraw

BIG = 96
SIZE = 24
W = 7  # толщина линий на большом холсте (~1.75 px на экране)


def canvas():
    img = Image.new("L", (BIG, BIG), 0)
    return img, ImageDraw.Draw(img)


def line(d, pts, w=W):
    d.line(pts, fill=255, width=w, joint="curve")
    for p in (pts[0], pts[-1]):
        d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=255)


def icon_pv():
    img, d = canvas()
    # Солнце в правом верхнем углу
    cx, cy, r = 74, 20, 9
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    for k in range(8):
        a = k * math.pi / 4
        line(d, [(cx + 15 * math.cos(a), cy + 15 * math.sin(a)),
                 (cx + 21 * math.cos(a), cy + 21 * math.sin(a))], 5)
    # Панель-параллелограмм с сеткой 3x2
    tl, tr, br, bl = (18, 46), (70, 46), (84, 90), (4, 90)
    line(d, [tl, tr, br, bl, tl])
    for t in (1 / 3, 2 / 3):
        top = (tl[0] + (tr[0] - tl[0]) * t, tl[1])
        bot = (bl[0] + (br[0] - bl[0]) * t, bl[1])
        line(d, [top, bot], 5)
    mid_l = ((tl[0] + bl[0]) / 2, (tl[1] + bl[1]) / 2)
    mid_r = ((tr[0] + br[0]) / 2, (tr[1] + br[1]) / 2)
    line(d, [mid_l, mid_r], 5)
    return img


def icon_grid():
    img, d = canvas()
    # Опора ЛЭП: трапеция с плоским верхом, две траверсы, крест внизу.
    # Без острой вершины: иначе на 24 px с крестом получается звезда.
    line(d, [(22, 92), (40, 6), (56, 6), (74, 92)])
    line(d, [(10, 26), (86, 26)])
    line(d, [(18, 48), (78, 48)])
    line(d, [(31, 50), (72, 90)], 5)
    line(d, [(65, 50), (24, 90)], 5)
    return img


def icon_inverter():
    img, d = canvas()
    d.rounded_rectangle((14, 4, 82, 92), radius=12, outline=255, width=W)
    # Синусоида в центре
    pts = []
    for i in range(41):
        x = 26 + i * 44 / 40
        y = 56 - 12 * math.sin(i / 40 * 2 * math.pi)
        pts.append((x, y))
    line(d, pts, 6)
    # Окошко дисплея сверху
    d.rounded_rectangle((30, 16, 66, 30), radius=4, outline=255, width=5)
    return img


def icon_load():
    img, d = canvas()
    # Вилка: два штыря, корпус, провод
    line(d, [(36, 6), (36, 26)], 8)
    line(d, [(60, 6), (60, 26)], 8)
    d.rounded_rectangle((20, 26, 76, 58), radius=8, fill=255)
    d.polygon([(26, 56), (70, 56), (56, 72), (40, 72)], fill=255)
    line(d, [(48, 72), (48, 80), (40, 92)], W)
    return img


def icon_home():
    img, d = canvas()
    # Дом: крыша, стены, дверь
    line(d, [(6, 48), (48, 8), (90, 48)])
    line(d, [(18, 38), (18, 90), (78, 90), (78, 38)])
    d.rectangle((40, 58, 56, 90), fill=255)
    return img


ICONS = [
    ("flow_icon_pv", icon_pv),
    ("flow_icon_grid", icon_grid),
    ("flow_icon_inverter", icon_inverter),
    ("flow_icon_load", icon_load),
    ("flow_icon_home", icon_home),
]


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "src", "flow_icons.c")
    preview_dir = sys.argv[1] if len(sys.argv) > 1 else None

    parts = [
        "// Сгенерировано tools/gen_flow_icons.py, руками не править.\n"
        "// Иконки вкладки Flow, 24x24, LV_IMG_CF_ALPHA_8BIT (цвет через img_recolor).\n"
        "#include <lvgl.h>\n"
    ]
    previews = []
    for name, fn in ICONS:
        small = fn().resize((SIZE, SIZE), Image.LANCZOS)
        data = small.tobytes()
        rows = []
        for y in range(SIZE):
            rows.append("    " + ", ".join("0x%02x" % v for v in data[y * SIZE:(y + 1) * SIZE]) + ",")
        parts.append(
            "\nstatic const uint8_t %s_map[] = {\n%s\n};\n\n"
            "const lv_img_dsc_t %s = {\n"
            "    .header = {.cf = LV_IMG_CF_ALPHA_8BIT, .always_zero = 0, .reserved = 0, .w = %d, .h = %d},\n"
            "    .data_size = %d,\n"
            "    .data = %s_map,\n"
            "};\n" % (name, "\n".join(rows), name, SIZE, SIZE, SIZE * SIZE, name)
        )
        previews.append(small)

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(parts))
    print("written", out)

    if preview_dir:
        # Превью: как на экране (24 px) и увеличенное x8, тёмным по белому
        sheet = Image.new("L", (len(previews) * (SIZE * 8 + 16), SIZE * 8 + SIZE + 24), 255)
        for i, im in enumerate(previews):
            x = i * (SIZE * 8 + 16)
            sheet.paste(Image.eval(im.resize((SIZE * 8, SIZE * 8), Image.NEAREST), lambda v: 255 - v), (x, 0))
            sheet.paste(Image.eval(im, lambda v: 255 - v), (x, SIZE * 8 + 12))
        path = os.path.join(preview_dir, "flow_icons_preview.png")
        sheet.save(path)
        print("preview", path)


if __name__ == "__main__":
    main()
