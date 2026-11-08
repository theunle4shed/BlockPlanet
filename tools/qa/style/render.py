#!/usr/bin/env python3
"""Approximate captures of BlockPlanet's RmlUi screens in Chromium.

RML/RCSS -> HTML/CSS: dp -> px at (window height / 1080), RmlUi borders get
'solid', font-effect outline/glow -> text-shadow. List and text bindings that
the game fills at run time get sample data. Not the game's own renderer."""
import json, os, re, sys, html

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
OUT = sys.argv[1]
# The game's sheets; or other sheets to compare: render.py OUT MENU.rcss HUD.rcss
SHEETS = {'now': (f'{REPO}/assets/ui/menu.rcss', f'{REPO}/assets/ui/hud.rcss')}
if len(sys.argv) > 3:
    SHEETS['other'] = (sys.argv[2], sys.argv[3])
# Graphics > Text size: BLOCKPLANET_TEXT_SCALE=1.4 renders as Larger (it multiplies every dp).
TEXT_SCALE = float(os.environ.get('BLOCKPLANET_TEXT_SCALE', '1'))

def outline(width, colour):
    w = width
    return ', '.join(f'{x*w:.2f}px {y*w:.2f}px 0 {colour}' for x, y in
                     [(1,0),(-1,0),(0,1),(0,-1),(.7,.7),(-.7,.7),(.7,-.7),(-.7,-.7)])

def css(rcss, dp):
    t = re.sub(r'/\*.*?\*/', '', rcss, flags=re.S)
    t = re.sub(r'(-?\d*\.?\d+)dp', lambda m: f'{float(m.group(1))*dp:.3f}px', t)
    t = re.sub(r'(border(?:-(?:top|bottom|left|right))?\s*:\s*)(-?[\d.]+(?:px|vh|em))\s+(#[0-9a-fA-F]+|transparent)',
               r'\1\2 solid \3', t)
    def fe(m):
        v = m.group(1).strip()
        if v == 'none': return 'text-shadow: none'
        o = re.match(r'outline\(([\d.]+)px\s+(#\w+)\)', v)
        if o: return 'text-shadow: ' + outline(float(o.group(1)), o.group(2))
        g = re.match(r'glow\(([\d.]+)px\s+([\d.]+)px[^#]*(#\w+)\)', v)
        if g: return f'text-shadow: 0 0 {float(g.group(2))*1.5:.1f}px {g.group(3)}, 0 0 {float(g.group(2)):.1f}px {g.group(3)}'
        return ''
    t = re.sub(r'font-effect\s*:\s*([^;}]+)', fe, t)
    t = t.replace('cubic-out', 'cubic-bezier(.33,1,.68,1)').replace('sine-in-out', 'ease-in-out')
    return t

FONTS = ''.join(f"@font-face{{font-family:LatoLatin;src:url('file://{REPO}/assets/ui/fonts/LatoLatin-{n}.ttf');font-weight:{w};font-style:{s}}}"
                for n, w, s in [('Regular', 400, 'normal'), ('Bold', 700, 'normal'), ('Italic', 400, 'italic'), ('BoldItalic', 700, 'italic')])
BASE = ('*{animation:none!important;transition:none!important}html,body{margin:0;overflow:hidden}'
        'body{line-height:1.2}')
# A neutral stand-in for the game's picture (no game art here).
BACKDROP = ('<div style="position:fixed;inset:0;z-index:-1;background:'
            'radial-gradient(circle at 50% 52%, #3b5b8a 0, #23395e 22%, #0e1830 38%, #070b16 60%)"></div>')

def selfclose(s):
    return re.sub(r'<(?!br)(\w+)([^<>]*?)/>', r'<\1\2></\1>', s)

def item(label, note='', focus=False, desc='', extra=''):
    n = f'<span class="note">{note}</span>' if note else ''
    d = f' data-description="{html.escape(desc)}"' if desc else ''
    return f'<div class="item{" focused" if focus else ""}{extra}"{d}>{label}{n}</div>'

# Sample data for run-time lists and texts (the game's own wording where the code has it).
LISTS = {
 'continue': item('Continue', 'Story, episode 3', True, 'Carry on from the start of episode 3, with the score and hearts you had when it began.'),
 'profiles': item('Ryan', 'Wheels · playing now', True) + item('Sam', 'Gyro') + item('New profile') + item('Play as guest', 'nothing is saved'),
 'profile_actions': item('Keep playing as Ryan', focus=True) + item('Rename') + item('Character', 'Wheels') + item('Delete Ryan'),
 'confirm': item('Delete Sam') + item('Keep it', focus=True),
 'characters': ''.join(item(n, 'chosen' if n == 'Wheels' else '', n == 'Wheels') for n in ['Rocket', 'Gear', 'Gyro', 'Stomp', 'Wheels', 'Turbine']),
 'pause': item('Resume', focus=True) + item('Restart level') + item('How to Play') + item('Options') + item('Quit to menu'),
 'chapters': ''.join(item(f'Chapter {i}', ['done', 'done', '2 of 10 stages', '', '', '', '', ''][i-1], i == 3) for i in range(1, 9)),
 'stages': item('Whole episode', 'best 14:02.31', True) + ''.join(item(f'Stage 3:{i}', f'best {i}:{(i*7)%60:02d}.{(i*13)%100:02d}' if i < 3 else '') for i in range(1, 11)),
 'keyboard': ''.join(f'<div class="item{" focused" if c == "R" else ""}">{c}</div>' for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ-.\'!') +
             '<div class="item span2">Space</div><div class="item span2">Delete</div><div class="item span4">Done</div>',
}
TEXTS = {
 'pause_where': 'Story · Rescue 3:2', 'version': 'BlockPlanet 1.1.0', 'chapters_title': 'Story', 'chapters_subtitle': 'Choose a chapter',
 'stages_title': 'Chapter 3', 'stages_subtitle': 'The whole episode, or one stage', 'confirm_title': 'Delete Sam?',
 'confirm_note': 'Their progress and settings go with them.', 'character_for': 'For Ryan',
 'name_edit': 'Ryan|', 'name_hint': 'Up to 12 letters', 'howto_title': 'Story',
 'howto_body': '<div class="para">Drop pieces onto matching blocks to clear them. Clear enough to reach the core.</div>'
               '<div class="para">Slide a piece under others to set up combos; 20 or more give magic.</div>'
               '<div class="para">Running out of time to drop costs a heart. Lose all three and the game is over.</div>',
}
SETTINGS = {'colour': 'Original', 'music_volume': '80%', 'effects_volume': '70%'}

def fill_screen(src, focus_id=None):
    body = re.search(r'<body([^>]*)>(.*)</body>', src, re.S)
    attrs, inner = body.group(1), body.group(2)
    cls = re.search(r'class="([^"]*)"', attrs)
    bcls = cls.group(1) if cls else ''
    if 'data-list="keyboard"' in inner: bcls += ' pad'  # shown with a controller
    for name, items in LISTS.items():
        inner = re.sub(rf'(<div[^>]*data-list="{name}"[^>]*>)(</div>)', lambda m, items=items: m.group(1) + items + m.group(2), inner)
    for name, text in TEXTS.items():
        inner = re.sub(rf'(<div[^>]*(?:data-text|id)="{name}"[^>]*>)[^<]*(</div>)', lambda m: m.group(1) + text + m.group(2), inner)
    # Setting values: the first option, or a sample.
    def val(m):
        tag = m.group(0)
        sid = re.search(r'id="(\w+)"', tag)
        opts = re.search(r'data-options="([^"]*)"', tag)
        v = SETTINGS.get(sid.group(1) if sid else '', opts.group(1).split(',')[0].split(':', 1)[1] if opts else '')
        return tag
    inner = re.sub(r'<div class="item value[^"]*"[^>]*>', val, inner)
    def setv(m):
        head = m.group(1)
        sid = re.search(r'id="(\w+)"', head)
        opts = re.search(r'data-options="([^"]*)"', head)
        v = SETTINGS.get(sid.group(1) if sid else '', opts.group(1).split(',')[0].split(':', 1)[1] if opts else '')
        return head + m.group(2) + f'<span class="value">{v}</span>'
    inner = re.sub(r'(<div class="item value[^"]*"[^>]*>)([^<]*)<span class="value"></span>', setv, inner, flags=re.S)
    # Slider fill widths
    inner = re.sub(r'<div class="fill"[^>]*>', '<div class="fill" style="width:75%">', inner)
    # Focus: first static item unless a list already focused one.
    if 'focused' not in inner:
        inner = re.sub(r'class="item( value[^"]*)?"', lambda m: f'class="item{m.group(1) or ""} focused"', inner, count=1)
    f = re.search(r'class="item[^"]*focused[^"]*"[^>]*data-description="([^"]*)"', inner) or \
        re.search(r'data-description="([^"]*)"[^>]*class="item[^"]*focused', inner)
    if f:
        inner = re.sub(r'(<div id="description" class="description)( empty)?(">)', lambda m: m.group(1) + m.group(3) + f.group(1), inner)
    return bcls, selfclose(inner)

def dpstyle(inner, dp):
    return re.sub(r"style='([^']*)'", lambda m: "style='" + re.sub(r'(-?\d*\.?\d+)dp', lambda n: f'{float(n.group(1))*dp:.3f}px', m.group(1)) + "'", inner)

def page(sheet, dp, bcls, inner, kind):
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{FONTS}{css(open(sheet).read(), dp)}{BASE}</style></head>'
            f'<body class="{bcls}">{BACKDROP}{dpstyle(inner, dp)}</body></html>')

SIZES = {'16x9': (1920, 1080), '4x3': (1440, 1080), 'small': (1280, 720)}
manifest = []
os.makedirs(OUT, exist_ok=True)
screens = sorted(f[:-4] for f in os.listdir(f'{REPO}/assets/ui/screens') if f.endswith('.rml'))
for variant, (menu_sheet, hud_sheet) in SHEETS.items():
    for s in screens:
        bcls, inner = fill_screen(open(f'{REPO}/assets/ui/screens/{s}.rml').read())
        for size, (w, h) in SIZES.items():
            name = f'{variant}-menu-{s}-{size}'
            open(f'{OUT}/{name}.html', 'w').write(page(menu_sheet, h / 1080 * TEXT_SCALE, bcls, inner, 'menu'))
            manifest.append({'name': name, 'w': w, 'h': h})
    hud_dir = os.path.dirname(os.path.abspath(__file__))
    for hfile in sorted(os.listdir(f'{hud_dir}/hud')):
        hname = hfile[:-5]
        inner = open(f'{hud_dir}/hud/{hfile}').read()
        for size, (w, h) in SIZES.items():
            name = f'{variant}-hud-{hname}-{size}'
            open(f'{OUT}/{name}.html', 'w').write(page(hud_sheet, h / 1080 * TEXT_SCALE, '', selfclose(inner), 'hud'))
            manifest.append({'name': name, 'w': w, 'h': h})
json.dump(manifest, open(f'{OUT}/manifest.json', 'w'))
print(len(manifest), 'pages')
