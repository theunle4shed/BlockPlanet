#!/usr/bin/env python3
"""Writes BlockPlanet's flat power-up icons (its own art, the owner's request,
2026-11-21) as SVG into assets/ui/powerups: each power-up at levels 1, 2 and 3
(NAME.svg, NAME-2.svg, NAME-3.svg; level 2 and 3 a little grander). Colours
follow the game's models (a purple firework rocket with an orange tip, red
dynamite with a yellow band, red magnet claws with pale tips, a purple atom, a
red and white bomb, a green and gold ray gun). Render to PNG with
node tools/art/render_powerups.js."""
import os

OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'assets', 'ui', 'powerups')
INK = '#1a1033'
O = f'stroke="{INK}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"'


# The level's trim (the owner, 2026-11-21: "different colour stripes", one rule
# for levelling up; level 2 more blue): each power-up's trim (stripes, band,
# tips, orbits, rings) is its own colour at level 1 (as the game's models), blue
# at level 2 and gold at level 3, the same parts on every power-up.
BLUE, GOLD = '#4fb4ff', '#ffc53d'


def accent(level, base, yellow=False):
    return base if level == 1 else BLUE if level == 2 else GOLD


def fuse(d):
    # A light fuse with a dark edge, so it shows on the dark panel.
    return (f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="6" stroke-linecap="round"/>'
            f'<path d="{d}" fill="none" stroke="#f3dc9c" stroke-width="2.6" stroke-linecap="round"/>')


def spark(x, y, r=8):
    a = r * 0.28
    return (f'<path transform="translate({x} {y})" d="M0 {-r} L{a} {-a} L{r} 0 L{a} {a} L0 {r} L{-a} {a} L{-r} 0 L{-a} {-a} Z" '
            f'fill="#ffd23f" {O} stroke-width="2"/>')


def rocket(cx, cy, angle, scale=1.0, stripe='#c9a2ff'):
    # A firework rocket: a purple tube with light stripes, an orange cone, a fuse.
    return (f'<g transform="translate({cx} {cy}) rotate({angle}) scale({scale})">'
            f'{fuse("M0 22 C0 27 -4 29 -6 31")}'
            f'<rect x="-7" y="-12" width="14" height="34" rx="2" fill="#8a3df0" {O}/>'
            f'<path d="M-7 -4 L7 -9 M-7 6 L7 1 M-7 16 L7 11" stroke="{stripe}" stroke-width="3"/>'
            f'<rect x="-7" y="-12" width="14" height="34" rx="2" fill="none" {O}/>'
            f'<path d="M-10 -12 L0 -28 L10 -12 Z" fill="#f28a2e" {O}/>'
            f'</g>')


def firecracker(level):
    if level == 1:
        return rocket(34, 31, 35) + spark(16, 52, 6)
    if level == 2:
        return rocket(34, 31, 35, 1.1, accent(2, '')) + spark(15, 54, 7) + spark(55, 9, 5)
    g = accent(3, '')
    return (rocket(20, 38, 0, 0.85, g) + rocket(44, 38, 35, 0.85, g) + rocket(32, 34, 15, 0.95, g)
            + spark(54, 8, 6) + spark(9, 12, 5))


def stick(x, y, h=34, colour='#d93636'):
    return f'<rect x="{x}" y="{y}" width="11" height="{h}" rx="4" fill="{colour}" {O}/>'


def dynamite(level):
    if level == 1:
        # Five sticks at every level (the owner, 2026-11-23: the icon should
        # match what is thrown; the game's Dynamite throws five).
        xs = [(6, '#c92f2f'), (47, '#c92f2f'), (17, '#d93636'), (36, '#d93636'), (26.5, '#ef4a4a')]
        fuses = fuse("M32 22 C32 14 38 12 42 8") + spark(45, 7, 7)
    elif level == 2:
        xs = [(6, '#c92f2f'), (47, '#c92f2f'), (17, '#d93636'), (36, '#d93636'), (26.5, '#ef4a4a')]
        fuses = fuse("M32 22 C32 14 38 12 42 8") + spark(45, 7, 7) + fuse("M22 23 C21 17 17 15 14 12") + spark(12, 10, 6)
    else:
        xs = [(2, '#b92a2a'), (51, '#b92a2a'), (11, '#c92f2f'), (42, '#c92f2f'), (19, '#d93636'), (34, '#d93636'), (26.5, '#ef4a4a')]
        fuses = (fuse("M32 22 C32 14 38 12 42 8") + spark(45, 7, 7) + fuse("M22 23 C21 17 17 15 14 12") + spark(12, 10, 6)
                 + fuse("M41 24 C44 18 50 18 54 15") + spark(56, 14, 5))
    body = ''.join(stick(x, 22 if c == '#ef4a4a' else 24, 34 if c == '#ef4a4a' else 32, c) for x, c in xs)
    return fuses + body + f'<rect x="{min(x for x, _ in xs) - 2}" y="38" width="{max(x for x, _ in xs) - min(x for x, _ in xs) + 15}" height="7" rx="2" fill="{accent(level, '#f2c53d', True)}" {O}/>'


def arcs(n):
    out = ''
    for i in range(n):
        dy = i * 9
        out += (f'<path d="M8 {18 + dy} L4 {22 + dy} L9 {23 + dy} L5 {28 + dy}" fill="none" stroke="#6fd3ff" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>'
                f'<path d="M56 {18 + dy} L60 {22 + dy} L55 {23 + dy} L59 {28 + dy}" fill="none" stroke="#6fd3ff" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>')
    return out


def ring_base(cx, cy, rx, ry):
    # The game's model's base: a bright green ring.
    return (f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="#2a9e3a" {O}/>'
            f'<ellipse cx="{cx}" cy="{cy - 1.5}" rx="{rx * 0.62}" ry="{ry * 0.5}" fill="#123d1a" stroke="#7dff8a" stroke-width="1.5"/>')


def horseshoe(cx, top, scale, tip='#f0d0f0'):
    # A red horseshoe opening upwards, pale pink tips (the model's colours).
    return (f'<g transform="translate({cx} {top}) scale({scale}) translate(-32 -8)">'
            f'<path d="M14 10 H26 V34 A6 6 0 0 0 38 34 V10 H50 V34 A18 18 0 0 1 14 34 Z" fill="#d93636" {O}/>'
            f'<rect x="14" y="8" width="12" height="9" fill="{tip}" {O}/>'
            f'<rect x="38" y="8" width="12" height="9" fill="{tip}" {O}/>'
            f'</g>')


def claw(d):
    # A curved red claw (dark edge, then red), its pale tip drawn after.
    return (f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="9" stroke-linecap="round"/>'
            f'<path d="{d}" fill="none" stroke="#d93636" stroke-width="5.5" stroke-linecap="round"/>')


def magnet(level):
    # Level 1 a horseshoe magnet; level 2 two, fanned out (the owner: a clear
    # step from level 1); level 3 the game's own form, four red claws that curve
    # out and hook back in at the top, like a claw (the owner); all on the
    # game's green ring.
    if level == 3:
        out = '<circle cx="32" cy="30" r="27" fill="#6fd3ff" fill-opacity="0.14"/>'
        claws = ["M25 48 C15 40 8 27 11 14 C12 9 16 8 19 11",
                 "M29 47 C24 36 20 24 23 12 C24 8 27 7 29 10",
                 "M35 47 C40 36 44 24 41 12 C40 8 37 7 35 10",
                 "M39 48 C49 40 56 27 53 14 C52 9 48 8 45 11"]
        out += ''.join(claw(d) for d in claws[1:3]) + ''.join(claw(d) for d in (claws[0], claws[3]))
        for x, y in [(19, 11), (29, 10), (35, 10), (45, 11)]:
            out += f'<circle cx="{x}" cy="{y}" r="3.6" fill="{accent(3, "")}" {O} stroke-width="2"/>'
        return out + ring_base(32, 52, 15, 6)
    if level == 2:
        tip = accent(2, '')
        left = f'<g transform="rotate(-16 32 54)">{horseshoe(22, 13, 0.62, tip)}</g>'
        right = f'<g transform="rotate(16 32 54)">{horseshoe(42, 13, 0.62, tip)}</g>'
        return ring_base(32, 44, 24, 7) + left + right
    return ring_base(32, 46, 19, 6.5) + horseshoe(32, 11, 0.8, accent(1, '#f0d0f0')) + arcs(1)


def atom(level):
    orbits = 3 if level < 3 else 4
    out = f'<g fill="none" stroke="{accent(level, "#b78cff")}" stroke-width="3.5">'
    for i in range(orbits):
        out += f'<ellipse cx="32" cy="32" rx="26" ry="10" transform="rotate({i * 180 / orbits} 32 32)"/>'
    out += '</g>'
    if level >= 2:
        out += '<circle cx="32" cy="32" r="14" fill="#a24bff" fill-opacity="0.25"/>'
    r = 8 if level == 1 else 9.5
    out += f'<circle cx="32" cy="32" r="{r}" fill="#a24bff" {O}/><circle cx="29.5" cy="29.5" r="2.5" fill="#e9d8ff"/>'
    import math
    electrons = 3 if level == 1 else 6 if level == 2 else 8
    for i in range(electrons):
        o = i % orbits
        t = (i // orbits) * math.pi + 0.3
        a = math.radians(o * 180 / orbits)
        x, y = 26 * math.cos(t), 10 * math.sin(t)
        X, Y = 32 + x * math.cos(a) - y * math.sin(a), 32 + x * math.sin(a) + y * math.cos(a)
        out += f'<circle cx="{X:.1f}" cy="{Y:.1f}" r="3.4" fill="#ffd23f" {O} stroke-width="2"/>'
    return out


def missile(cx, cy, angle, scale=1.0, flame=1.0, band='#d93636'):
    return (f'<g transform="translate({cx} {cy}) rotate({angle}) scale({scale})">'
            f'<path d="M-4 18 L0 {18 + 10 * flame} L4 18 Z" fill="#ffa63f" {O} stroke-width="2"/>'
            f'<path d="M-8 14 L-16 24 L-8 22 Z" fill="#d93636" {O}/>'
            f'<path d="M8 14 L16 24 L8 22 Z" fill="#d93636" {O}/>'
            f'<path d="M-8 18 V-10 C-8 -20 0 -28 0 -28 C0 -28 8 -20 8 -10 V18 Z" fill="#f4f1ea" {O}/>'
            f'<path d="M-7.5 -12 C-7 -19 0 -27 0 -27 C0 -27 7 -19 7.5 -12 Z" fill="#d93636" {O}/>'
            f'<rect x="-8" y="2" width="16" height="5" fill="{band}" {O} stroke-width="2"/>'
            f'</g>')


def bomb(level):
    if level == 1:
        return missile(34, 30, 45, 0.95)
    if level == 2:
        return missile(33, 31, 45, 1.08, 1.6, accent(2, ''))
    return missile(20, 36, 20, 0.72, 1.4, accent(3, '')) + missile(44, 30, 20, 0.72, 1.4, accent(3, ''))


def raygun(level):
    beam = {1: '<path d="M55 23 H62" stroke="#6fffb0" stroke-width="3" stroke-linecap="round"/>'
               '<path d="M55 17 L61 13 M55 29 L61 33" stroke="#6fffb0" stroke-width="2.5" stroke-linecap="round"/>',
            2: '<path d="M54 23 H63" stroke="#6fffb0" stroke-width="5" stroke-linecap="round"/>'
               '<path d="M55 15 L62 10 M55 31 L62 36" stroke="#6fffb0" stroke-width="3" stroke-linecap="round"/>',
            3: '<circle cx="58" cy="23" r="7" fill="#6fffb0" fill-opacity="0.35"/>'
               '<path d="M53 23 H64" stroke="#d8ffe9" stroke-width="6" stroke-linecap="round"/>'
               '<path d="M55 14 L63 8 M55 32 L63 38 M56 19 L64 17 M56 27 L64 29" stroke="#6fffb0" stroke-width="3" stroke-linecap="round"/>'}[level]
    ring_colour = accent(level, '#f2c53d', True)
    rings = '' if level == 1 else f'<rect x="39" y="15" width="4" height="16" fill="{ring_colour}" {O} stroke-width="2"/>'
    return (f'<path d="M18 34 L14 54 H24 L28 38 Z" fill="#1f8a4c" {O}/>'
            f'<path d="M8 22 C8 16 14 14 20 14 H40 C44 14 46 18 46 22 C46 28 44 34 40 34 H20 C14 34 8 30 8 22 Z" fill="#2fbf68" {O}/>'
            f'<rect x="21" y="13" width="5" height="22" fill="{ring_colour}" {O} stroke-width="2.5"/>'
            f'<rect x="30" y="13" width="5" height="22" fill="{ring_colour}" {O} stroke-width="2.5"/>'
            + rings +
            f'<path d="M46 19 H52 V27 H46" fill="#f2c53d" {O}/>' + beam)


# The four power-ups (the owner, 2026-11-21: the Firecracker and Bomb merged as
# the Missile, the Magnet and Ray Gun as the Magnet). Each level its own form,
# its trim blue at level 2 and gold at level 3, level 3 with its power surge.

def missile_family(level):
    if level == 1:
        return missile(34, 31, 45, 0.85)                                   # Missile
    if level == 2:
        return missile(33, 31, 45, 1.08, 1.6, accent(2, ''))              # Big Missile
    # Missile Barrage: three strikes, each with a shockwave.
    g = accent(3, '')
    waves = ''.join(f'<path d="M{x - r} {y} A{r} {r * 0.55} 0 0 1 {x + r} {y}" fill="none" stroke="{GOLD}" stroke-width="2.2" stroke-linecap="round"/>'
                    for x, y, r in [(13, 56, 8), (32, 60, 9), (51, 56, 8)])
    return (waves + missile(13, 34, 0, 0.6, 1.2, g) + missile(51, 34, 0, 0.6, 1.2, g) + missile(32, 30, 0, 0.7, 1.4, g))


def dynamite_family(level):
    if level < 3:
        return dynamite(level)
    # Cluster sticks: five sticks, a gold band, and bomblets hopping out evenly
    # round them, inside and out.
    out = dynamite(2).replace(BLUE, GOLD)
    for x, y in [(5, 14), (59, 20), (5, 34), (59, 40), (14, 59), (50, 59)]:
        out += (f'<circle cx="{x}" cy="{y}" r="4.4" fill="#ff7a2e" {O} stroke-width="1.8"/>'
                f'<circle cx="{x - 1.3}" cy="{y - 1.3}" r="1.5" fill="#ffd23f"/>')
    return out


def magnet_family(level):
    if level < 3:
        return magnet(level)                                               # Magnet, Power Magnet
    # Ray Gun: hits harder, faster, deeper; pulses round its beam.
    pulses = ''.join(f'<ellipse cx="58" cy="23" rx="{r}" ry="{r * 1.4}" fill="none" stroke="{c}" stroke-width="2"/>'
                     for r, c in [(4, '#d8ffe9'), (7, GOLD)])
    return raygun(3) + pulses


def atom_family(level):
    if level == 1:
        return atom(1)
    if level == 2:
        # The crater: a bowl under a smaller atom.
        crater = (f'<ellipse cx="32" cy="52" rx="27" ry="8" fill="#8a6a4a" {O}/>'
                  f'<ellipse cx="32" cy="53" rx="20" ry="5" fill="#2a1c12"/>'
                  f'<path d="M14 49 L10 45 M50 49 L55 45 M32 44 L32 41" stroke="#c9a57a" stroke-width="2" stroke-linecap="round"/>')
        return crater + f'<g transform="translate(7 -4) scale(0.78)">{atom(2)}</g>'
    # Splitting the sphere: a purple and gold ring round the atom.
    ring_back = (f'<path d="M3 38 A29 9 0 0 1 61 26" fill="none" stroke="#b84dff" stroke-width="5" stroke-linecap="round"/>'
                 f'<path d="M5 37 A27 8 0 0 1 59 27" fill="none" stroke="{GOLD}" stroke-width="2"/>')
    ring_front = (f'<path d="M61 26 A29 9 0 0 1 3 38" fill="none" stroke="#b84dff" stroke-width="5" stroke-linecap="round"/>'
                  f'<path d="M59 27 A27 8 0 0 1 5 37" fill="none" stroke="{GOLD}" stroke-width="2"/>')
    return ring_back + f'<g transform="translate(5 5) scale(0.84)">{atom(3)}</g>' + ring_front


FAMILIES = {'missile': missile_family, 'dynamite': dynamite_family, 'magnet': magnet_family, 'atom': atom_family}

ICONS = {'firecracker': firecracker, 'dynamite': dynamite, 'magnet': magnet, 'atom': atom, 'bomb': bomb, 'raygun': raygun}

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    # The game's six (shown in the HUD until the merges are built), then the
    # four families (the same names win for Dynamite, the Magnet and the Atom).
    for table in (ICONS, FAMILIES):
        for name, draw in table.items():
            for level in (1, 2, 3):
                file = f'{name}.svg' if level == 1 else f'{name}-{level}.svg'
                with open(os.path.join(OUT, file), 'w') as out:
                    out.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">\n'
                              f'<!-- BlockPlanet power-up icon: {name}, level {level} (tools/art/powerups.py) -->\n'
                              f'{draw(level)}\n</svg>\n')
