"""The README demo: the states demo_render wrote (the real composer and engine typing a script, the candidate window
drawn by its own code) laid out in a plain text box, then encoded as a GIF with ffmpeg.

    demo_compose.py <demo_render out dir> <out.gif>
Needs Pillow, ffmpeg on PATH, and Microsoft JhengHei (Windows).
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

S = 2                                   # drawn at 2x, encoded at 1x
W, H = 760 * S, 560 * S
FONTS = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')
TEXT = ImageFont.truetype(os.path.join(FONTS, 'msjh.ttc'), 26 * S)
SMALL = ImageFont.truetype(os.path.join(FONTS, 'msjh.ttc'), 15 * S)
CAPTION = ImageFont.truetype(os.path.join(FONTS, 'msjhbd.ttc'), 19 * S)
BLUE = (0x2D, 0x6C, 0xF0)
INK, MUTED, PAGE, PANEL = (28, 28, 30), (120, 124, 132), (243, 244, 247), (255, 255, 255)
BOX = (24 * S, 24 * S, W - 24 * S, H - 112 * S)          # the text box, tall enough for the candidate window
PAD = 22 * S
LINE = 44 * S


def frame(st, cand_dir):
    im = Image.new('RGB', (W, H), PAGE)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(BOX, radius=12 * S, fill=PANEL, outline=(222, 224, 230), width=S)
    lines = st['text'].split('\n')
    x0, y0 = BOX[0] + PAD, BOX[1] + PAD
    for i, line in enumerate(lines):
        d.text((x0, y0 + i * LINE), line, font=TEXT, fill=INK)
    y = y0 + (len(lines) - 1) * LINE
    x = x0 + TEXT.getlength(lines[-1])
    pre, caret = st['preedit'], st['caret']
    cand_at = x
    if pre:
        d.text((x, y), pre, font=TEXT, fill=INK)
        base = y + 34 * S
        for px in range(int(x), int(x + TEXT.getlength(pre)), 4 * S):          # the composition's dotted underline
            d.rectangle((px, base, px + 2 * S - 1, base + S), fill=INK)
        tgt = st.get('target')
        if tgt:                                                                # the character being picked: a solid bar
            tx = x + TEXT.getlength(pre[:tgt[0]])
            d.rectangle((tx, base - S, tx + TEXT.getlength(pre[tgt[0]:tgt[0] + tgt[1]]), base + 2 * S), fill=BLUE)
            cand_at = tx
        cx = x + TEXT.getlength(pre if caret < 0 else pre[:caret])
    else:
        cx = x
    d.rectangle((cx, y + 3 * S, cx + S, y + 33 * S), fill=INK)               # the caret
    if st.get('cand'):
        c = Image.open(os.path.join(cand_dir, st['cand'])).convert('RGBA')     # drawn at 192 dpi = this canvas's 2x
        cy = y + 40 * S
        cxp = int(min(cand_at - 10 * S, W - c.width - 8 * S))
        sh = Image.new('RGBA', im.size, (0, 0, 0, 0))                          # a soft shadow, as the window has on screen
        ImageDraw.Draw(sh).rounded_rectangle((cxp + 2 * S, int(cy) + 4 * S, cxp + c.width + 2 * S, int(cy) + c.height + 6 * S), radius=8 * S, fill=(0, 0, 0, 38))
        im.paste(sh, (0, 0), sh)
        im.paste(c, (cxp, int(cy)), c)
    # caption and the keys typed
    d.text((BOX[0], BOX[3] + 16 * S), st.get('caption', ''), font=CAPTION, fill=INK)
    keys = st.get('keys', '').replace('⏎', ' Enter ')
    d.text((BOX[0], BOX[3] + 52 * S), ('按鍵　' + keys[-28:]) if keys else '', font=SMALL, fill=MUTED)
    note = '示意動畫：以會意注音實際的組字程式、引擎與候選窗繪製'
    d.text((W - 24 * S - SMALL.getlength(note), BOX[3] + 82 * S), note, font=SMALL, fill=(160, 164, 172))
    return im


def main():
    src, out_gif = sys.argv[1], sys.argv[2]
    states = [json.loads(l) for l in open(os.path.join(src, 'states.jsonl'), encoding='utf-8') if l.strip()]
    tmp = tempfile.mkdtemp(prefix='huiyi-demo-')
    try:
        listing = []
        for i, st in enumerate(states):
            nxt = states[i + 1]['t'] if i + 1 < len(states) else st['t'] + 1200
            dur = (nxt - st['t']) / 1000.0
            if dur <= 0:
                continue
            p = os.path.join(tmp, f'f{i:04d}.png')
            frame(st, src).resize((W // S, H // S), Image.LANCZOS).save(p)
            listing.append(f"file '{p}'\nduration {dur:.3f}\n")
        listing.append(f"file '{p}'\n")                                       # concat: the last frame once more
        lst = os.path.join(tmp, 'list.txt')
        open(lst, 'w', encoding='utf-8').write(''.join(listing).replace('\\', '/'))
        vf = 'fps=20,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle'
        subprocess.run(['ffmpeg', '-nostdin', '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', lst, '-vf', vf, out_gif], check=True)
        print(out_gif, os.path.getsize(out_gif), 'bytes')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
