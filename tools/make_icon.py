#!/usr/bin/env python3
"""Convert a logo PNG into the FH2 HUD radio icon: a 196x104 BC7 "burG" .swatchbin (see docs/how-fh2-radio-works.md,
"Icon texture format").

    pip install pillow etcpak            (texture2ddecoder too, for --preview)
    python tools/make_icon.py assets/logo.png installer/data/HUD_RadioIcon_MostWanted.swatchbin

The logo is cropped to its visible pixels, scaled to fit 196x104 (premultiplied, so transparent edges do not go dark),
centred, BC7-compressed on the 256x128 padded surface and stored in the game's swizzled block order.
With --preview out.png it also writes what the game will decode, for a quick look.
With --solid R,G,B every pixel gets that colour and only the alpha carries the shape (for a one-colour logo).
--shadow bakes in a soft dark drop shadow like the stock icons have (HUD_RadioIcon_Pulse: black, up to ~40% alpha,
down and to the right). --premultiply stores the colours premultiplied by alpha, which is how the stock icons are
stored and how the HUD blends them: a pixel's colour is added on top of the background, so a see-through pixel must
be black (a straight-alpha icon with white see-through pixels shows as a white box).
"""
import argparse
import struct
import sys

import etcpak
from PIL import Image, ImageChops, ImageFilter

W, H = 196, 104                 # icon size (stock icons are 196x96 or 196x104)
PW, PH = 256, 128               # padded surface the BC7 data covers
HEADER_SIZE, FILE_SIZE = 0x53, 32851


def header(w: int, h: int) -> bytes:
    """The 0x53-byte header; stock icons differ only in the width/height fields."""
    hd = bytearray(HEADER_SIZE)
    hd[0:4] = b"burG"
    struct.pack_into("<III", hd, 0x04, 0x00010001, HEADER_SIZE, FILE_SIZE)
    hd[0x10:0x14] = b"BCXT"
    struct.pack_into("<IIIII", hd, 0x14, 0x10002, 0x28, HEADER_SIZE, 0x8000, 0x8000)
    hd[0x28:0x2C] = b"HCXT"
    struct.pack_into("<I", hd, 0x2C, 0x80230)
    hd[0x3F] = 1                      # a lone flag byte the stock icons all have (found by diffing against them)
    struct.pack_into("<HHH", hd, 0x40, w, h, 1)
    struct.pack_into("<HHH", hd, 0x46, w, h, 1)
    hd[0x4C:0x53] = bytes([0x01, 0x01, 0x00, 0x2E, 0x01, 0x00, 0xB8])   # 0x2E = BC7
    return bytes(hd)


def swizzled_xy(i: int):
    """Stored block index (0..2047) -> block (x, y) on the 64x32 grid."""
    b = lambda k: (i >> k) & 1
    x = b(0) | b(2) << 1 | b(9) << 2 | (b(4) ^ b(5) ^ b(7) ^ b(8)) << 3 | (b(5) ^ b(7)) << 4 | b(6) << 5
    y = b(1) | b(3) << 1 | b(10) << 2 | b(8) << 3 | b(7) << 4
    return x, y


def fit(logo: Image.Image) -> Image.Image:
    box = logo.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    logo = logo.crop(box)
    margin = 4
    scale = min((W - 2 * margin) / logo.width, (H - 2 * margin) / logo.height)
    size = (max(1, round(logo.width * scale)), max(1, round(logo.height * scale)))
    # resize premultiplied so the transparent (black) surround does not bleed into the edge pixels
    px = logo.load()
    pre = Image.new("RGBA", logo.size)
    pp = pre.load()
    for y in range(logo.height):
        for x in range(logo.width):
            r, g, b, a = px[x, y]
            pp[x, y] = (r * a // 255, g * a // 255, b * a // 255, a)
    small = pre.resize(size, Image.LANCZOS)
    sp = small.load()
    for y in range(small.height):
        for x in range(small.width):
            r, g, b, a = sp[x, y]
            sp[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a) if a else (0, 0, 0, 0)
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    canvas.paste(small, ((W - size[0]) // 2, (H - size[1]) // 2))
    return canvas


def add_shadow(icon: Image.Image, offset=(2, 2), blur=1.6, strength=0.45) -> Image.Image:
    """A soft black copy of the shape behind it, shifted down and right (straight alpha in, straight alpha out)."""
    shape = Image.new("L", icon.size, 0)
    shape.paste(icon.getchannel("A"), offset)
    shade = shape.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: int(v * strength))
    black = Image.new("L", icon.size, 0)
    return Image.alpha_composite(Image.merge("RGBA", (black, black, black, shade)), icon)


def premultiply(icon: Image.Image) -> Image.Image:
    r, g, b, a = icon.split()
    return Image.merge("RGBA", (ImageChops.multiply(r, a), ImageChops.multiply(g, a), ImageChops.multiply(b, a), a))


def build(icon: Image.Image) -> bytes:
    surface = Image.new("RGBA", (PW, PH), (0, 0, 0, 0))
    surface.paste(icon, (0, 0))
    blocks = etcpak.compress_bc7(surface.tobytes(), PW, PH)         # row-major 4x4 blocks, 16 bytes each
    gw = PW // 4
    used_x, used_y = (W + 3) // 4, (H + 3) // 4
    out = bytearray()
    for i in range(gw * (PH // 4)):
        x, y = swizzled_xy(i)
        # the stock icons keep the padding blocks zero
        out += blocks[(y * gw + x) * 16:(y * gw + x + 1) * 16] if x < used_x and y < used_y else bytes(16)
    data = header(W, H) + bytes(out)
    assert len(data) == FILE_SIZE, len(data)
    return data


def decode(data: bytes) -> Image.Image:
    """What the game sees: undo the swizzle and decode the BC7 blocks."""
    gw = PW // 4
    blocks = bytearray(len(data) - HEADER_SIZE)
    for i in range(gw * (PH // 4)):
        x, y = swizzled_xy(i)
        blocks[(y * gw + x) * 16:(y * gw + x + 1) * 16] = data[HEADER_SIZE + i * 16:HEADER_SIZE + (i + 1) * 16]
    import texture2ddecoder                                          # preview only: pip install texture2ddecoder
    bgra = texture2ddecoder.decode_bc7(bytes(blocks), PW, PH)
    return Image.frombytes("RGBA", (PW, PH), bgra, "raw", "BGRA").crop((0, 0, W, H))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png")
    ap.add_argument("out")
    ap.add_argument("--preview", help="also write the decoded icon (on a grey background) to this PNG")
    ap.add_argument("--solid", help="R,G,B: paint every pixel this colour, keeping only the alpha (one-colour logos)")
    ap.add_argument("--shadow", action="store_true", help="bake in a soft drop shadow like the stock icons")
    ap.add_argument("--premultiply", action="store_true", help="store premultiplied colours, as the stock icons are")
    a = ap.parse_args()
    icon = fit(Image.open(a.png).convert("RGBA"))
    if a.solid:
        r, g, b = (int(v) for v in a.solid.split(","))
        icon = Image.merge("RGBA", (Image.new("L", icon.size, r), Image.new("L", icon.size, g), Image.new("L", icon.size, b), icon.getchannel("A")))
    if a.shadow:
        icon = add_shadow(icon)
    if a.premultiply:
        icon = premultiply(icon)
    data = build(icon)
    with open(a.out, "wb") as f:
        f.write(data)
    print(f"wrote {a.out} ({len(data)} bytes)")
    if a.preview:
        d = decode(data)
        bg = Image.new("RGBA", d.size, (70, 70, 70, 255))
        bg.alpha_composite(d)
        bg.resize((d.width * 4, d.height * 4), Image.NEAREST).save(a.preview)
        print(f"preview: {a.preview}")


if __name__ == "__main__":
    sys.exit(main())
