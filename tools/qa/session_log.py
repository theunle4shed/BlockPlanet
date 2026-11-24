"""Writes a text-only log of a Claude Code session (the owner's messages and the
assistant's replies; no tool output or images), e-mail addresses and user
names in paths removed. Usage: python3 -I tools/qa/session_log.py SESSION.jsonl OUT.md [REDACT.txt]"""
import json, sys, re
src, out = sys.argv[1], sys.argv[2]
email = re.compile(r'[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}')
# Names or game text to leave out: one per line in an optional private file
# (third argument, e.g. .local/session-backups/redact.txt), never in Git.
redact_names = [l.strip() for l in open(sys.argv[3], encoding='utf-8') if l.strip()] if len(sys.argv) > 3 else []
def clean(t):
    t = email.sub('[email removed]', t)
    t = re.sub(r'([A-Za-z]:\\\\?Users\\\\?)[^\\\\\s"/]+', r'\1<you>', t)
    t = re.sub(r'(/(?:Users|home)/)(?!user/)[^/\s"]+', r'\1<you>', t)
    for n in redact_names: t = t.replace(n, '[name]')
    t = re.sub(r'<system-reminder>.*?</system-reminder>', '', t, flags=re.S)
    return t.strip()
n = 0
with open(src, encoding='utf-8', errors='replace') as f, open(out, 'w', encoding='utf-8') as o:
    o.write('# BlockPlanet session log (text only)\n\nThe owner\'s messages and the assistant\'s replies, from the session ' 
            'that ran 2026-11-01 to 2026-11-24. Tool calls, tool output, screenshots and images are left out (they included '
            'material derived from the game, which stays out of Git, see AGENTS.md); e-mail addresses and the game\'s credit names are removed.\n\n')
    for line in f:
        try: e = json.loads(line)
        except Exception: continue
        t = e.get('type')
        if t not in ('user', 'assistant'): continue
        if e.get('isMeta') or e.get('isCompactSummary'): continue
        msg = e.get('message') or {}
        c = msg.get('content')
        texts = []
        if isinstance(c, str): texts = [c]
        elif isinstance(c, list):
            for b in c:
                if isinstance(b, dict) and b.get('type') == 'text': texts.append(b.get('text', ''))
        text = clean('\n'.join(texts))
        if not text or text.startswith('[Request interrupted') or text.startswith('<command-') or 'tool_use_error' in text: continue
        if t == 'user' and (text.startswith('Stop hook feedback') or text.startswith('This session is being continued')): continue
        ts = (e.get('timestamp') or '')[:16].replace('T', ' ')
        who = 'Owner' if t == 'user' else 'Assistant'
        o.write(f'### {who} ({ts})\n\n{text}\n\n')
        n += 1
print(n)
