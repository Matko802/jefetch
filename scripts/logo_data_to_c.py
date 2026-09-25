import re
import sys


def cstr(s):
    o = []
    for ch in s:
        if ch == '"':
            o.append('\\"')
        elif ch == '\\':
            o.append('\\\\')
        elif ch == '\n':
            o.append('\\n')
        elif ch == '\t':
            o.append('\\t')
        elif ch == '\r':
            o.append('\\r')
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            o.append('\\x%02x' % ord(ch))
        else:
            o.append(ch)
    return '"' + ''.join(o) + '"'


def parse_strlist(body):
    items = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', body):
        raw = m.group(1)
        out = []
        i = 0
        while i < len(raw):
            c = raw[i]
            if c == '\\' and i + 1 < len(raw):
                d = raw[i + 1]
                if d == 'n':
                    out.append('\n')
                elif d == 't':
                    out.append('\t')
                elif d == 'r':
                    out.append('\r')
                elif d == '"':
                    out.append('"')
                elif d == '\\':
                    out.append('\\')
                elif d == 'u' and i + 5 < len(raw):
                    out.append(chr(int(raw[i + 2:i + 6], 16)))
                    i += 6
                    continue
                else:
                    out.append(d)
                i += 2
            else:
                out.append(c)
                i += 1
        items.append(''.join(out))
    return items


def main():
    src, out_c = sys.argv[1], sys.argv[2]
    text = open(src, encoding='utf-8').read()
    blocks = []
    for m in re.finditer(r'Logo\s*\{', text):
        start = m.start()
        i = m.end() - 1
        depth = 0
        instr = False
        esc = False
        while i < len(text):
            c = text[i]
            if instr:
                if esc:
                    esc = False
                elif c == '\\':
                    esc = True
                elif c == '"':
                    instr = False
            else:
                if c == '"':
                    instr = True
                elif c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0:
                        blocks.append(text[start:i + 1])
                        break
            i += 1
    logos = []
    for b in blocks:
        def field(pat):
            m = re.search(pat, b)
            return m.group(1) if m else None

        name = field(r'name:\s*"((?:[^"\\]|\\.)*)"')
        if not name:
            continue
        aliases = []
        m = re.search(r'aliases:\s*&\[(.*?)\]', b, re.S)
        if m:
            aliases = parse_strlist(m.group(1))
        color = field(r'\bcolor:\s*"((?:[^"\\]|\\.)*)"') or ''
        slots = []
        m = re.search(r'slots:\s*&\[(.*?)\]', b, re.S)
        if m:
            slots = parse_strlist(m.group(1))
        ck = field(r'color_keys:\s*Some\("((?:[^"\\]|\\.)*)"\)')
        ct = field(r'color_title:\s*Some\("((?:[^"\\]|\\.)*)"\)')
        lines = []
        m = re.search(r'lines:\s*&\[(.*?)\]\s*,?\s*\n\s*\}', b, re.S)
        if m:
            lines = parse_strlist(m.group(1))
        logos.append((name, aliases, color, slots, ck, ct, lines))
    with open(out_c, 'w', encoding='utf-8') as f:
        f.write('#include "logo.h"\n#include <stddef.h>\n')
        for idx, (name, aliases, color, slots, ck, ct, lines) in enumerate(logos):
            if aliases:
                f.write('static const char *logo_aliases_%d[] = {%s};\n' % (
                    idx, ','.join(cstr(a) for a in aliases)))
            if slots:
                f.write('static const char *logo_slots_%d[] = {%s};\n' % (
                    idx, ','.join(cstr(s) for s in slots)))
            f.write('static const char *logo_lines_%d[] = {%s};\n' % (
                idx, ','.join(cstr(l) for l in lines)))
        f.write('const JfLogo JF_LOGOS[] = {\n')
        for idx, (name, aliases, color, slots, ck, ct, lines) in enumerate(logos):
            f.write('{ %s, %s, %d, %s, %s, %d, %s, %s, logo_lines_%d, %d },\n' % (
                cstr(name),
                ('logo_aliases_%d' % idx) if aliases else 'NULL', len(aliases),
                cstr(color),
                ('logo_slots_%d' % idx) if slots else 'NULL', len(slots),
                cstr(ck) if ck else 'NULL',
                cstr(ct) if ct else 'NULL',
                idx, len(lines)))
        f.write('};\n')
        f.write('const size_t JF_LOGO_COUNT = %d;\n' % len(logos))
    print('logos=%d' % len(logos))


main()
