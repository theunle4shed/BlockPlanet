#!/usr/bin/env python3
"""BlockPlanet's own profile coins (original art, the owner's choice of 2026-11-11).

Each coin is a height map drawn in greys (white raised, mid grey the coin's
field, dark cut in), lit from the top left by an SVG filter, so it reads as a
stamped silver coin. The menus tint it with the profile's colour (RmlUi's
image-color multiplies). Writes assets/ui/coins/<name>.svg and renders each to
<name>.png (the menus cannot draw SVG) with the pre-installed Playwright.

    python3 tools/art/coins.py            # writes and renders all coins
"""
import json, os, subprocess, sys
HI, MID, LO = "#ffffff", "#808080", "#3a3a3a"

def face_bot():  # round head, antenna, two eyes, smile slot
    return f'''<rect x="47" y="14" width="6" height="14" fill="{HI}"/><circle cx="50" cy="14" r="5" fill="{HI}"/>
<circle cx="50" cy="52" r="22" fill="{HI}"/>
<circle cx="42" cy="48" r="5" fill="{LO}"/><circle cx="58" cy="48" r="5" fill="{LO}"/>
<rect x="40" y="60" width="20" height="5" rx="2.5" fill="{LO}"/>
<rect x="24" y="47" width="5" height="10" rx="2" fill="{HI}"/><rect x="71" y="47" width="5" height="10" rx="2" fill="{HI}"/>'''

def visor_bot():  # square head with a wide visor and bolts
    return f'''<rect x="28" y="28" width="44" height="40" rx="6" fill="{HI}"/>
<rect x="33" y="38" width="34" height="11" rx="5.5" fill="{LO}"/>
<circle cx="40" cy="43.5" r="3" fill="{HI}"/><circle cx="60" cy="43.5" r="3" fill="{HI}"/>
<rect x="38" y="56" width="4" height="6" fill="{LO}"/><rect x="45" y="56" width="4" height="6" fill="{LO}"/><rect x="52" y="56" width="4" height="6" fill="{LO}"/><rect x="59" y="56" width="4" height="6" fill="{LO}"/>
<circle cx="24" cy="48" r="4" fill="{HI}"/><circle cx="76" cy="48" r="4" fill="{HI}"/>
<rect x="42" y="68" width="16" height="8" fill="{HI}"/>'''

def cyclops():  # dome with one big eye and little legs
    return f'''<path d="M26 64 A24 26 0 0 1 74 64 Z" fill="{HI}"/>
<circle cx="50" cy="48" r="11" fill="{LO}"/><circle cx="50" cy="48" r="5" fill="{HI}"/>
<rect x="31" y="64" width="7" height="12" rx="3" fill="{HI}"/><rect x="62" y="64" width="7" height="12" rx="3" fill="{HI}"/>
<path d="M40 30 L34 20 M60 30 L66 20" stroke="{HI}" stroke-width="4" stroke-linecap="round"/>'''

def cat_bot():  # bot with pointed ears and whisker vents
    return f'''<path d="M28 40 L32 18 L46 32 L54 32 L68 18 L72 40 Q74 72 50 74 Q26 72 28 40 Z" fill="{HI}"/>
<path d="M38 46 l6 4 l-6 4 Z" fill="{LO}"/><path d="M62 46 l-6 4 l6 4 Z" fill="{LO}"/>
<circle cx="50" cy="60" r="3" fill="{LO}"/>
<path d="M22 58 H36 M22 64 H36 M64 58 H78 M64 64 H78" stroke="{HI}" stroke-width="3" stroke-linecap="round"/>'''

def planet():  # a sphere of bricks: BlockPlanet's own mark
    lines = "".join(f'<path d="M24 {y} H76" stroke="{LO}" stroke-width="2.2"/>' for y in (38, 50, 62))
    lines += "".join(f'<ellipse cx="50" cy="50" rx="{rx}" ry="26" fill="none" stroke="{LO}" stroke-width="2.2"/>' for rx in (9, 19))
    return f'''<circle cx="50" cy="50" r="26" fill="{HI}"/>
<clipPath id="pc"><circle cx="50" cy="50" r="26"/></clipPath><g clip-path="url(#pc)">{lines}</g>'''

def bricks():  # a little stack of three bricks
    return f'''<rect x="27" y="52" width="22" height="16" rx="2" fill="{HI}"/><rect x="51" y="52" width="22" height="16" rx="2" fill="{HI}"/>
<rect x="39" y="34" width="22" height="16" rx="2" fill="{HI}"/>
<rect x="31" y="56" width="6" height="4" fill="{MID}"/><rect x="55" y="56" width="6" height="4" fill="{MID}"/><rect x="43" y="38" width="6" height="4" fill="{MID}"/>'''

def rocket():
    return f'''<path d="M50 18 Q64 34 62 62 H38 Q36 34 50 18 Z" fill="{HI}"/>
<circle cx="50" cy="40" r="6" fill="{LO}"/>
<path d="M38 50 L28 66 L38 64 Z M62 50 L72 66 L62 64 Z" fill="{HI}"/>
<path d="M43 64 L50 80 L57 64 Z" fill="{HI}"/>'''

def spark():  # a four-point star with a small one beside it
    return f'''<path d="M46 20 Q49 44 72 48 Q49 52 46 78 Q43 52 20 48 Q43 44 46 20 Z" fill="{HI}"/>
<path d="M68 20 Q69 28 76 29 Q69 30 68 38 Q67 30 60 29 Q67 28 68 20 Z" fill="{HI}"/>'''

def crown():
    return f'''<path d="M24 66 L22 34 L37 48 L50 26 L63 48 L78 34 L76 66 Z" fill="{HI}"/>
<rect x="24" y="68" width="52" height="7" rx="2" fill="{HI}"/>
<circle cx="50" cy="56" r="4" fill="{LO}"/><circle cx="36" cy="58" r="3" fill="{LO}"/><circle cx="64" cy="58" r="3" fill="{LO}"/>'''

def helmet():  # a digger's hard hat with a lamp
    return f'''<path d="M24 62 Q24 30 50 28 Q76 30 76 62 Z" fill="{HI}"/>
<rect x="18" y="62" width="64" height="8" rx="4" fill="{HI}"/>
<path d="M50 30 V62" stroke="{LO}" stroke-width="3"/>
<circle cx="50" cy="46" r="8" fill="{LO}"/><circle cx="50" cy="46" r="5" fill="{HI}"/>'''

def heart():
    return f'''<path d="M50 76 C20 56 22 30 38 28 C45 27 49 32 50 36 C51 32 55 27 62 28 C78 30 80 56 50 76 Z" fill="{HI}"/>
<path d="M36 36 Q32 42 34 48" stroke="{LO}" stroke-width="3" fill="none" stroke-linecap="round"/>'''

def music():  # two joined notes
    return f'''<path d="M36 30 L70 22 V60" stroke="{HI}" stroke-width="5" fill="none" stroke-linejoin="round"/>
<path d="M36 30 V66" stroke="{HI}" stroke-width="5"/>
<ellipse cx="30" cy="67" rx="8" ry="6" fill="{HI}" transform="rotate(-20 30 67)"/>
<ellipse cx="64" cy="61" rx="8" ry="6" fill="{HI}" transform="rotate(-20 64 61)"/>
<path d="M36 38 L70 30" stroke="{HI}" stroke-width="5"/>'''

DESIGNS = dict(bolt=face_bot, visor=visor_bot, cyclops=cyclops, cat=cat_bot, helmet=helmet, rocket=rocket,
               planet=planet, bricks=bricks, spark=spark, crown=crown, heart=heart, music=music)

def coin(body):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="256" height="256">
<defs>
<filter id="emboss" x="0" y="0" width="100%" height="100%" color-interpolation-filters="sRGB">
  <feColorMatrix in="SourceGraphic" type="luminanceToAlpha" result="h"/>
  <feGaussianBlur in="h" stdDeviation="0.9" result="hb"/>
  <feDiffuseLighting in="hb" surfaceScale="5" diffuseConstant="1.05" lighting-color="#ffffff" result="lit">
    <feDistantLight azimuth="225" elevation="52"/>
  </feDiffuseLighting>
  <feComposite in="lit" in2="SourceAlpha" operator="in"/>
</filter>
</defs>
<g filter="url(#emboss)">
<circle cx="50" cy="50" r="48" fill="{HI}"/>
<circle cx="50" cy="50" r="41.5" fill="{LO}"/>
<circle cx="50" cy="50" r="40" fill="{MID}"/>
{body}
</g>
</svg>'''

# The order the coin screen lists them in.
ORDER = ["bolt", "visor", "cyclops", "cat", "helmet", "rocket", "planet", "bricks", "spark", "crown", "heart", "music"]
SIZE = 160

RENDER = """
const {chromium} = require('playwright');
const fs = require('fs');
(async () => {
  const [dir, size, ...names] = process.argv.slice(1);
  const browser = await chromium.launch();
  const page = await browser.newPage({viewport: {width: +size, height: +size}});
  for (const name of names) {
    const svg = fs.readFileSync(dir + '/' + name + '.svg', 'utf8')
      .replace('width="256" height="256"', 'width="' + size + '" height="' + size + '"');
    await page.setContent('<html><body style="margin:0;background:transparent">' + svg + '</body></html>');
    await page.screenshot({path: dir + '/' + name + '.png', omitBackground: true});
  }
  await browser.close();
})();
"""

def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "ui", "coins")
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    assert sorted(ORDER) == sorted(DESIGNS)
    for name in ORDER:
        with open(os.path.join(out, f"{name}.svg"), "w") as f:
            f.write(coin(DESIGNS[name]()))
    # "none": a plain coin, for the coin screen's None.
    with open(os.path.join(out, "none.svg"), "w") as f:
        f.write(coin(""))
    subprocess.run(["node", "-e", RENDER, out, str(SIZE), *ORDER, "none"], check=True)
    print(f"{len(ORDER)} coins in {out}")

if __name__ == "__main__":
    main()
