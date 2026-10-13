#!/usr/bin/env python3
"""Puts drawn portraits (made-up people, so drawn ones: a head, hair and
shoulders on a coloured background) into some of tests/hatari/sample.vcf's
cards, as the Mac exports them: PHOTO;ENCODING=b;TYPE=JPEG, base64,
folded. Run once; the result is committed."""
import base64
import io
import os
import re
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
# uid: background, skin, hair, shirt, long hair
PEOPLE = {
    "SAMPLE-CARTER": ((120, 170, 230), (236, 196, 160), (60, 35, 20), (200, 60, 80), True),
    "SAMPLE-APPLESEED": ((150, 210, 150), (230, 185, 150), (150, 90, 40), (40, 70, 140), False),
    "SAMPLE-BAKER": ((240, 200, 120), (250, 215, 190), (230, 190, 90), (60, 140, 120), True),
    "SAMPLE-HUGHES": ((180, 180, 200), (205, 150, 110), (30, 25, 20), (230, 140, 40), False),
    "SAMPLE-PATEL": ((230, 150, 150), (180, 120, 85), (20, 15, 15), (90, 50, 150), True),
    "SAMPLE-DUBOIS": ((130, 200, 210), (240, 200, 170), (110, 70, 40), (50, 50, 60), False),
    "SAMPLE-ITO": ((210, 220, 120), (240, 205, 165), (15, 15, 20), (200, 40, 40), False),
    "SAMPLE-JOHNSON": ((200, 160, 220), (245, 210, 185), (170, 80, 40), (40, 120, 200), True),
}


def portrait(bg, skin, hair, shirt, long_hair):
    s = 300
    im = Image.new("RGB", (s, s), bg)
    d = ImageDraw.Draw(im)
    if long_hair:
        d.rounded_rectangle((85, 70, 215, 230), 50, fill=hair)
    d.ellipse((40, 205, 260, 420), fill=shirt)                 # shoulders
    d.rectangle((128, 170, 172, 225), fill=skin)              # neck
    d.ellipse((95, 70, 205, 195), fill=skin)                  # face
    d.pieslice((90, 55, 210, 165), 180, 360, fill=hair)       # hair on top
    for ex in (127, 165):                                     # eyes
        d.ellipse((ex - 6, 120, ex + 6, 132), fill=(40, 30, 30))
    d.arc((125, 135, 175, 170), 20, 160, fill=(150, 60, 60), width=4)   # a smile
    b = io.BytesIO()
    im.save(b, "JPEG", quality=80)
    return b.getvalue()


path = os.path.join(HERE, "sample.vcf")
text = open(path, newline="").read()
for uid, look in PEOPLE.items():
    data = base64.b64encode(portrait(*look)).decode()
    lines = [data[i:i + 74] for i in range(0, len(data), 74)]
    photo = "PHOTO;ENCODING=b;TYPE=JPEG:" + "\n ".join(lines) + "\n"
    text = re.sub(r"(?m)^PHOTO;[^\r\n]*\r?\n(?: [^\r\n]*\r?\n)*(?=UID:%s)" % uid, "", text)
    text = text.replace("UID:%s" % uid, photo + "UID:%s" % uid)
open(path, "w", newline="").write(text)
print("photos in", len(PEOPLE), "cards;", len(text), "bytes")
