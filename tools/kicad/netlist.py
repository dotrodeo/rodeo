#!/usr/bin/env python3
"""Inspect a KiCad schematic's connectivity from the command line.

  netlist.py parts PROJECT.kicad_sch [--sheet "/Audio Interface/"]
  netlist.py nets  PROJECT.kicad_sch [--sheet ...] [--net NAME ...]
  netlist.py pin   PROJECT.kicad_sch A1            # every pin of one part and its net

The netlist is exported fresh with kicad-cli each run, so it reflects the saved
schematic. With --sheet, `nets` lists every net touching a part on that sheet.
"""
import argparse
import re
import sys

from kicadlib import Netlist


def refkey(r):
    m = re.match(r'([A-Za-z#]+)(\d*)', r)
    return (m.group(1), int(m.group(2) or 0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['parts', 'nets', 'pin'])
    ap.add_argument('schematic')
    ap.add_argument('ref', nargs='?')
    ap.add_argument('--sheet')
    ap.add_argument('--net', action='append')
    a = ap.parse_args()
    nl = Netlist.from_schematic(a.schematic)
    on_sheet = {r for r, p in nl.parts.items() if not a.sheet or p['sheet'] == a.sheet}

    if a.cmd == 'parts':
        for r in sorted(on_sheet, key=refkey):
            if r.startswith('#'):
                continue
            p = nl.parts[r]
            print(f"{r:6} {p['value'][:22]:22} {p['footprint'] or 'NO FOOTPRINT':55} {nl.lcsc(r) or 'NO LCSC'}")
    elif a.cmd == 'nets':
        for name, nodes in sorted(nl.nets.items()):
            if a.net and name not in a.net:
                continue
            if a.sheet and not any(r in on_sheet for r, _, _ in nodes):
                continue
            print(f"{name} : " + ' '.join(f'{r}.{p}' for r, p, _ in nodes))
    else:
        if not a.ref:
            sys.exit('pin needs a reference, e.g. A1')
        rows = [(p, f or '', name) for name, nodes in nl.nets.items() for r, p, f in nodes if r == a.ref]
        for p, f, name in sorted(rows, key=lambda x: refkey('X' + x[0]) if x[0].isdigit() else ('~', 0)):
            print(f'{a.ref}.{p:4} {f:20} {name}')


if __name__ == '__main__':
    main()
