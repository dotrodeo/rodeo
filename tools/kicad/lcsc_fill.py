#!/usr/bin/env python3
"""Set the "LCSC Part" field on placed symbols in a .kicad_sch file.

  lcsc_fill.py power.kicad_sch J2=C157915 J3=C160388 R12=C25744
  lcsc_fill.py power.kicad_sch --field Footprint D5=Diode_SMD:D_SMB

Updates the field if the symbol has one (empty or not), otherwise adds a hidden
one next to the footprint field. `--field` sets another field instead. Close the schematic in KiCad first: KiCad
overwrites the file on its next save. Check afterwards with
`netlist.py parts PROJECT.kicad_sch --sheet ...`.
"""
import re
import sys


def fill(path, codes, field="LCSC Part"):
    t = open(path).read()
    parts = re.split(r'(\n\t\(symbol\n)', t)
    done = []
    for i in range(2, len(parts), 2):
        b = parts[i]
        r = re.search(r'\(property "Reference" "([^"]*)"', b)
        if not r or r.group(1) not in codes:
            continue
        code = codes[r.group(1)]
        m = re.search(r'\(property "%s" "([^"]*)"' % re.escape(field), b)
        if m:
            b = b[:m.start()] + f'(property "{field}" "{code}"' + b[m.end():]
        else:
            fm = re.search(r'\n\t\t\(property "Footprint" .*?\n\t\t\)', b, re.S)
            at = re.search(r'\(at ([^)]*)\)', fm.group(0)).group(1)
            b = (b[:fm.end()] + f'\n\t\t(property "{field}" "{code}"\n\t\t\t(at {at})\n\t\t\t(hide yes)'
                 f'\n\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n\t\t\t(effects\n\t\t\t\t(font'
                 f'\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)' + b[fm.end():])
        parts[i] = b
        done.append(r.group(1))
    open(path, 'w').write(''.join(parts))
    return done


def main():
    args = sys.argv[1:]
    field = "LCSC Part"
    if '--field' in args:
        i = args.index('--field')
        field = args[i + 1]
        del args[i:i + 2]
    if len(args) < 2:
        sys.exit(__doc__)
    codes = dict(a.split('=', 1) for a in args[1:])
    done = fill(args[0], codes, field)
    # multi-unit symbols appear once per unit, so a ref can be filled twice
    print('filled:', ' '.join(sorted(set(done))))
    missing = sorted(set(codes) - set(done))
    if missing:
        sys.exit('not found on this sheet: ' + ' '.join(missing))


if __name__ == '__main__':
    main()
