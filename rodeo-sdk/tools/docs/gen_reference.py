#!/usr/bin/env python3
"""Generate the SDK's API reference pages from its headers.

  gen_reference.py           write rodeo-sdk/docs/reference/api/*.mdx
  gen_reference.py --check   exit 1 if any page is missing or out of date (CI)

The headers' own comments are the documentation: the comment at the top of a
header becomes its page's introduction, the comment above a declaration becomes
that declaration's description, and a trailing comment on a constant or field
becomes its note. Both comment styles in the SDK work, `//` blocks and `/** */`.
Signatures are copied from the source, default arguments included.

clang (through the libclang Python package, see requirements.txt) finds the
declarations; nothing is guessed from the text. Only namespace `rodeo`, public
members, and macros not ending in `_H` are documented; names ending in `_` are
treated as private.

Which headers get a page is the HEADERS list below: the ones that are part of the
Rev B SDK. A header that is rewritten or added joins the list when its code lands.
"""
import argparse
import difflib
import os
import re
import subprocess
import sys

import clang.cindex as ci

SDK = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(SDK, 'docs', 'reference', 'api')
REPO_URL = 'https://github.com/dotrodeo/rodeo'

# (header under rodeo-sdk/common/, sidebar order). Pure-logic headers first.
HEADERS = [
    ('version.h', 1),
    ('board.h', 2),
    ('sat_frame.h', 3),
    ('adc_convert.h', 10),
    ('pot_ui.h', 11),
    ('rodeo_dsp.h', 12),
    ('rodeo_scope.h', 13),
    ('rodeo_screen.h', 14),
    ('oled_ssd130x_bulk.h', 20),
]

# The SDK's namespaces: `rodeo` for the library, `board` for board.h. A
# namespace nested in one of them (board::pin) is documented with its prefix.
NAMESPACES = ('rodeo', 'board')

# Plain C headers have no namespace; their top-level declarations are documented
# when they carry the header's prefix (sat_frame.h is C so the satellites can
# build it).
C_PREFIXES = ('sat_', 'SAT_')

K = ci.CursorKind
FUNCS = (K.FUNCTION_DECL, K.FUNCTION_TEMPLATE, K.CXX_METHOD, K.CONSTRUCTOR)
RECORDS = (K.STRUCT_DECL, K.CLASS_DECL, K.CLASS_TEMPLATE)
VARS = (K.VAR_DECL, K.FIELD_DECL)


def clang_args():
    args = ['-x', 'c++', '-std=gnu++17', '-fsyntax-only', '-Wno-pragma-once-outside-header',
            '-I' + os.path.join(SDK, 'common'), '-I' + os.path.join(SDK, 'libDaisy', 'src'),
            '-I' + os.path.join(SDK, 'libDaisy', 'Drivers', 'CMSIS', 'Include'),
            '-DSTM32H750xx', '-DCORE_CM7']
    if sys.platform == 'darwin':
        sdk = subprocess.run(['xcrun', '--show-sdk-path'], capture_output=True, text=True).stdout.strip()
        args += ['-isysroot', sdk, '-I' + os.path.join(sdk, 'usr', 'include', 'c++', 'v1')]
    return args


# ---- comments ---------------------------------------------------------------

def strip_comment(lines):
    """Comment source lines -> text lines, markers removed."""
    out = []
    block = lines and lines[0].lstrip().startswith('/*')
    for ln in lines:
        s = ln.strip()
        if not block:
            s = ln.split('//', 1)[1] if '//' in ln else ''
            s = s[1:] if s.startswith(' ') else s
        else:
            s = re.sub(r'^\s*/\*\*?', '', ln)
            s = re.sub(r'\*/\s*$', '', s)
            s = re.sub(r'^\s*\*(?!/)', '', s) if not ln.strip().startswith('/*') else s.lstrip()
        out.append(s.rstrip())
    if block and len(out) > 1:  # /** */ continuation lines: drop their common indent
        rest = [l for l in out[1:] if l.strip()]
        pad = min((len(l) - len(l.lstrip()) for l in rest), default=0)
        out = [out[0].strip()] + [l[pad:] for l in out[1:]]
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return out


def comment_before(src, line):
    """The comment block ending on the line above `line` (1-based), or []."""
    i = line - 2
    if i < 0:
        return []
    if src[i].rstrip().endswith('*/'):
        j = i
        while j >= 0 and '/*' not in src[j]:
            j -= 1
        return strip_comment(src[j:i + 1]) if j >= 0 else []
    j = i
    while j >= 0 and src[j].strip().startswith('//'):
        j -= 1
    return strip_comment(src[j + 1:i + 1])


def trailing_comment(src, line):
    """A `// note` after code on a line (1-based), or ''."""
    m = re.search(r'\S.*?//\s?(.*)$', src[line - 1])
    return m.group(1).strip() if m and not src[line - 1].strip().startswith('//') else ''


def file_comment(src):
    """The header's opening comment, after #pragma once."""
    i = 0
    while i < len(src) and (not src[i].strip() or src[i].startswith('#pragma')):
        i += 1
    j = i
    if j < len(src) and src[j].lstrip().startswith('/*'):
        while j < len(src) and '*/' not in src[j]:
            j += 1
        return strip_comment(src[i:j + 1])
    while j < len(src) and (src[j].strip().startswith('//') or not src[j].strip()):
        j += 1
    return strip_comment(src[i:j])


# ---- MDX --------------------------------------------------------------------

def esc(text):
    """Escape MDX's JSX characters outside inline code spans."""
    parts = re.split(r'(`[^`]*`)', text)
    return ''.join(p if p.startswith('`') else
                   p.replace('{', '\\{').replace('}', '\\}').replace('<', '&lt;').replace('>', '&gt;')
                   for p in parts)


LIST_ITEM = re.compile(r'^(\*|-|\d+[.)])\s+')
CODE_LINE = re.compile(r'[;{}]\s*$|^(using|#|static_assert|//|template)\b|^\w[\w:<>]*\s*=|\w\(.*\)\s*;?$')


def indented(block):
    """A run of comment lines indented two or more spaces: a list, code, or a quote."""
    while block and not block[-1].strip():
        block.pop()
    pad = min(len(b) - len(b.lstrip()) for b in block if b.strip())
    lines = [b[pad:] for b in block]
    if LIST_ITEM.match(lines[0]):
        items = []
        for l in lines:
            if LIST_ITEM.match(l):
                items.append(LIST_ITEM.sub('', l).strip())
            elif l.strip() and items:
                items[-1] += ' ' + l.strip()
        num = re.match(r'\d', lines[0])
        return [f'{i + 1}. ' + esc(x) if num else '- ' + esc(x) for i, x in enumerate(items)] + ['']
    if any(CODE_LINE.search(l.strip()) for l in lines if l.strip()):
        return ['```cpp'] + lines + ['```', '']
    return ['> ' + esc(l.strip()) if l.strip() else '>' for l in lines] + ['']


def prose(lines):
    """Comment text -> markdown. Runs indented two or more spaces become a list,
    a code block or a quote, by what they contain."""
    out, block = [], []

    def flush():
        if block:
            if out and out[-1].strip():
                out.append('')
            out.extend(indented(block))
            block.clear()
    for ln in lines:
        if ln.startswith('  ') and ln.strip():
            block.append(ln)
        elif not ln.strip() and block:
            block.append('')
        else:
            flush()
            out.append(esc(ln))
    flush()
    md = '\n'.join(out).strip()
    return re.sub(r'\n{3,}', '\n\n', md)


def text_of(src, start, end):
    """Source text between two (line, column) pairs, 1-based, end exclusive."""
    (l0, c0), (l1, c1) = start, end
    if l0 == l1:
        return src[l0 - 1][c0 - 1:c1 - 1]
    return '\n'.join([src[l0 - 1][c0 - 1:]] + src[l0:l1 - 1] + [src[l1 - 1][:c1 - 1]])


def signature(src, c):
    """A declaration as written, without its body, on as few lines as reads well."""
    ext = c.extent
    end = (ext.end.line, ext.end.column)
    for k in c.get_children():
        if k.kind == K.COMPOUND_STMT:
            end = (k.extent.start.line, k.extent.start.column)
    s = text_of(src, (ext.start.line, ext.start.column), end).strip()
    s = re.sub(r'\s*//[^\n]*', '', s)  # comments inside a multi-line signature
    s = re.sub(r'\s+', ' ', s).strip()
    s = re.sub(r'\(\s+', '(', s)
    s = re.sub(r'\s+\)', ')', s)
    if c.kind in RECORDS:
        s = re.sub(r'\s*\{.*$', '', s)
    return s.rstrip(' {;') + ('' if c.kind in RECORDS else ';')


def public(c):
    return c.access_specifier in (ci.AccessSpecifier.PUBLIC, ci.AccessSpecifier.INVALID,
                                  ci.AccessSpecifier.NONE) and not c.spelling.endswith('_')


def kind_word(c):
    return {K.STRUCT_DECL: 'struct', K.CLASS_DECL: 'class', K.CLASS_TEMPLATE: 'class template',
            K.ENUM_DECL: 'enum', K.TYPE_ALIAS_DECL: 'type'}.get(c.kind, '')


def render(header):
    path = os.path.join(SDK, 'common', header)
    src = open(path, encoding='utf-8').read().split('\n')
    tu = ci.Index.create().parse(path, args=clang_args(),
                                 options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
                                 | ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES * 0)
    here = lambda c: c.location.file and os.path.samefile(c.location.file.name, path)

    intro = file_comment(src)
    first_para = ' '.join(l.strip() for l in intro[:next((i for i, l in enumerate(intro) if not l.strip()),
                                                           len(intro))])
    desc = re.sub(r'\s+', ' ', first_para).strip() or f'API reference for {header}.'

    sections = []

    def doc_of(c):
        return comment_before(src, c.extent.start.line)

    def member_lines(rec):
        """Fields and constants of a record, as list items; methods as subsections."""
        items, methods = [], []
        for m in rec.get_children():
            if not here(m) or not public(m):
                continue
            if m.kind in VARS:
                note = trailing_comment(src, m.extent.start.line) or ' '.join(doc_of(m))
                items.append(f'- `{signature(src, m).rstrip(";")}`' + (f' — {esc(note)}' if note else ''))
            elif m.kind in FUNCS:
                methods.append(m)
            elif m.kind == K.TYPE_ALIAS_DECL:
                note = trailing_comment(src, m.extent.start.line) or ' '.join(doc_of(m))
                items.append(f'- `{signature(src, m).rstrip(";")}`' + (f' — {esc(note)}' if note else ''))
        return items, methods

    def emit(c, level='##', prefix=''):
        name = prefix + c.spelling
        doc = prose(doc_of(c))
        if c.kind in RECORDS:
            sections.append(f'{level} {name}\n\n```cpp\n{signature(src, c)[:-0 or None]}\n```\n'
                            + (f'\n{doc}\n' if doc else ''))
            items, methods = member_lines(c)
            if items:
                sections.append('**Members**\n\n' + '\n'.join(items) + '\n')
            seen = set()
            for m in methods:
                if m.spelling in seen and not prose(doc_of(m)):
                    continue
                seen.add(m.spelling)
                mdoc = prose(doc_of(m))
                sections.append(f'### {name}::{m.spelling}\n\n```cpp\n{signature(src, m)}\n```\n'
                                + (f'\n{mdoc}\n' if mdoc else ''))
        elif c.kind == K.ENUM_DECL:
            consts = [e for e in c.get_children() if e.kind == K.ENUM_CONSTANT_DECL]
            vals = [f'- `{enumerator(e)}`' + (f' — {esc(trailing_comment(src, e.extent.start.line))}'
                                             if trailing_comment(src, e.extent.start.line) else '')
                    for e in consts]
            if c.spelling and not c.is_anonymous():
                head = f'```cpp\n{signature(src, c).split("{")[0].strip()}\n```\n'
            else:  # an anonymous enum of constants: name it by its range
                name = consts[0].spelling + (f' … {consts[-1].spelling}' if len(consts) > 1 else '')
                head = ''
            sections.append(f'{level} {esc(name)}\n\n{head}'
                            + (f'\n{doc}\n' if doc else '') + ('\n' + '\n'.join(vals) + '\n' if vals else ''))
        else:
            note = trailing_comment(src, c.extent.start.line)
            body = doc or (esc(note) if note else '')
            sections.append(f'{level} {name}\n\n```cpp\n{signature(src, c)}\n```\n'
                            + (f'\n{body}\n' if body else ''))

    def enumerator(e):
        """An enumerator as written: `NAME = value`, or just NAME."""
        s = re.sub(r'\s+', ' ', text_of(src, (e.extent.start.line, e.extent.start.column),
                                         (e.extent.end.line, e.extent.end.column))).strip()
        return s.rstrip(',')

    # Enums named by a typedef are documented with the typedef, not again alone.
    typedefd = {k.underlying_typedef_type.get_declaration().hash
                for k in tu.cursor.get_children() if k.kind == K.TYPEDEF_DECL}

    def emit_typedef(c):
        """`typedef struct { ... } name;` and `typedef enum { ... } name;`."""
        under = [k for k in c.get_children() if k.kind in (K.STRUCT_DECL, K.ENUM_DECL)]
        doc = prose(doc_of(c))
        sections.append(f'## {c.spelling}\n\n```cpp\n{"struct" if under and under[0].kind == K.STRUCT_DECL else "enum" if under else "typedef"} {c.spelling}\n```\n'
                        + (f'\n{doc}\n' if doc else ''))
        if not under:
            return
        lines = []
        for m in under[0].get_children():
            if m.kind in (K.FIELD_DECL, K.ENUM_CONSTANT_DECL):
                note = trailing_comment(src, m.extent.start.line)
                code = signature(src, m).rstrip(';').rstrip(',') if m.kind == K.FIELD_DECL else enumerator(m)
                lines.append(f'- `{code}`' + (f' — {esc(note)}' if note else ''))
        if lines:
            sections.append(('**Members**\n\n' if under[0].kind == K.STRUCT_DECL else '') + '\n'.join(lines) + '\n')

    def walk(cur, prefix='', top=True):
        for c in cur.get_children():
            if not here(c):
                continue
            if c.kind == K.NAMESPACE:
                if top and c.spelling in NAMESPACES:
                    walk(c, '', False)
                elif not top and c.spelling and c.spelling != 'detail':
                    walk(c, prefix + c.spelling + '::', False)
            elif top and c.kind == K.TYPEDEF_DECL and c.spelling.startswith(C_PREFIXES):
                emit_typedef(c)
            elif top and c.kind == K.ENUM_DECL and c.hash not in typedefd and any(
                    e.spelling.startswith(C_PREFIXES) for e in c.get_children()):
                emit(c)
            elif top and c.kind == K.FUNCTION_DECL and c.spelling.startswith(C_PREFIXES):
                emit(c)
            elif top and c.kind != K.MACRO_DEFINITION:
                continue  # only namespaced or prefixed declarations, and macros
            elif c.kind in RECORDS + FUNCS + (K.VAR_DECL, K.ENUM_DECL, K.TYPE_ALIAS_DECL):
                if c.spelling and public(c) and (c.kind not in RECORDS or c.is_definition()):
                    emit(c, prefix=prefix)
            elif c.kind == K.MACRO_DEFINITION and not c.spelling.endswith('_H') \
                    and not c.spelling.startswith('_'):
                lines = src[c.extent.start.line - 1:c.extent.end.line]
                doc = prose(comment_before(src, c.extent.start.line))
                sections.append(f'## {c.spelling}\n\n```cpp\n' + '\n'.join(l.rstrip() for l in lines)
                                + '\n```\n' + (f'\n{doc}\n' if doc else ''))
    walk(tu.cursor)

    rel = f'rodeo-sdk/common/{header}'
    head = [
        '---',
        f'title: {header}',
        f'description: {json_str(desc)}',
        f'editUrl: {REPO_URL}/edit/main/{rel}',
        '---',
        '',
        f'{{/* Generated from {rel} by rodeo-sdk/tools/docs/gen_reference.py. Do not edit:',
        '    change the header\'s comments and run `make docs` in rodeo-sdk/. */}',
        '',
        f':::note[Generated from the header]',
        f'This page is [`{rel}`]({REPO_URL}/blob/main/{rel}) — its comments and declarations.',
        'To change it, change the header.',
        ':::',
        '',
        '',
    ]
    body = prose(intro[len(intro[:next((i for i, l in enumerate(intro) if not l.strip()), len(intro))]):])
    return '\n'.join(head) + (body + '\n\n' if body else '') + '\n'.join(sections).rstrip() + '\n'


def json_str(s):
    s = s if len(s) <= 300 else s[:297].rsplit(' ', 1)[0] + '…'
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def page_name(header):
    return os.path.splitext(header)[0].replace('_', '-') + '.mdx'


def sidebar(text, order):
    return text.replace('editUrl:', f'sidebar:\n  order: {order}\neditUrl:', 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='fail if a page is missing or stale; write nothing')
    a = ap.parse_args()
    want = {page_name(h): sidebar(render(h), o) for h, o in HEADERS}
    have = {f for f in os.listdir(OUT) if f.endswith('.mdx')} if os.path.isdir(OUT) else set()
    stale = sorted(f for f in want if f not in have or open(os.path.join(OUT, f), encoding='utf-8').read() != want[f])
    extra = sorted(have - set(want))
    if a.check:
        for f in stale:
            old = open(os.path.join(OUT, f), encoding='utf-8').read().split('\n') if f in have else []
            sys.stdout.writelines(l + '\n' for l in difflib.unified_diff(old, want[f].split('\n'), f, f, lineterm='', n=1))
        if stale or extra:
            print(f'API reference out of date: {", ".join(stale + extra)}. Run `make docs` in rodeo-sdk/.')
            return 1
        print(f'API reference up to date ({len(want)} pages).')
        return 0
    os.makedirs(OUT, exist_ok=True)
    for f in stale:
        open(os.path.join(OUT, f), 'w', encoding='utf-8').write(want[f])
    for f in extra:
        os.remove(os.path.join(OUT, f))
    print(f'{len(stale)} written, {len(extra)} removed, {len(want) - len(stale)} unchanged: {OUT}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
