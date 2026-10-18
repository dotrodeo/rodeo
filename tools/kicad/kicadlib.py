"""Shared helpers for the Rodeo KiCad tools: kicad-cli, netlists, PCB parsing."""
import math
import os
import re
import shutil
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def kicad_cli():
    """Path to kicad-cli: $KICAD_CLI, then PATH, then the macOS app bundle."""
    for c in (os.environ.get('KICAD_CLI'), shutil.which('kicad-cli'),
              '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'):
        if c and os.path.exists(c):
            return c
    raise SystemExit('kicad-cli not found; set KICAD_CLI')


def run(*args):
    """Run kicad-cli quietly; raise with its output if it fails."""
    p = subprocess.run([kicad_cli(), *args], capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit(f'kicad-cli {" ".join(args)} failed:\n{p.stdout}\n{p.stderr}')
    return p.stdout


# --- S-expressions -------------------------------------------------------

def parse_sexpr(text):
    toks = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)
    pos = 0

    def node():
        nonlocal pos
        out = []
        pos += 1
        while toks[pos] != ')':
            if toks[pos] == '(':
                out.append(node())
            else:
                out.append(toks[pos].strip('"'))
                pos += 1
        pos += 1
        return out
    return node()


def find(lst, key):
    return [x for x in lst if isinstance(x, list) and x and x[0] == key]


def val(lst, key):
    f = find(lst, key)
    return f[0][1] if f and len(f[0]) > 1 else None


# --- Netlist ---------------------------------------------------------------

class Netlist:
    """A schematic netlist exported by kicad-cli (kicadsexpr format)."""

    def __init__(self, path):
        root = parse_sexpr(open(path).read())
        self.parts = {}          # ref -> dict(value, footprint, sheet, fields)
        for c in find(root, 'components')[0][1:]:
            fields = {}
            for f in (find(find(c, 'fields')[0], 'field') if find(c, 'fields') else []):
                fields[val(f, 'name')] = f[2] if len(f) > 2 else ''
            sp = find(c, 'sheetpath')
            self.parts[val(c, 'ref')] = dict(
                value=val(c, 'value') or '', footprint=val(c, 'footprint') or '',
                sheet=val(sp[0], 'names') if sp else '/', fields=fields)
        self.nets = {}           # name -> [(ref, pin, pinfunction)]
        for n in find(root, 'nets')[0][1:]:
            self.nets[val(n, 'name')] = [(val(x, 'ref'), val(x, 'pin'), val(x, 'pinfunction'))
                                         for x in find(n, 'node')]

    @classmethod
    def from_schematic(cls, sch):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'net.net')
            run('sch', 'export', 'netlist', '--format', 'kicadsexpr', '-o', out, sch)
            return cls(out)

    # Field names that hold an LCSC number, in order of preference. The Rev B
    # motherboard uses "LCSC Part"; some older boards use the others.
    LCSC_FIELDS = ('LCSC Part', 'LCSC', 'LCSC Part #', 'JLCPCB Part #', 'Supplier Part')

    def lcsc(self, ref):
        f = self.parts[ref]['fields']
        for name in self.LCSC_FIELDS:
            if f.get(name, '').strip().startswith('C'):
                return f[name].strip()
        return ''


# --- PCB -------------------------------------------------------------------

def rotate(x, y, deg):
    a = math.radians(deg)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


def pcb_footprints(pcb_path):
    """Footprints on a .kicad_pcb with pads in board coordinates.

    Returns {ref: dict(lib, x, y, rot, layer, pads=[dict(num, type, shape, X, Y, w, h, rot)])}.
    X/Y are cartesian (y up), the same frame kicad-cli's position export and the
    Gerbers use.
    """
    return dict(pcb_footprint_list(pcb_path))


def pcb_footprint_list(pcb_path):
    """pcb_footprints as a list of (ref, dict), keeping repeated references (a panel)."""
    t = open(pcb_path).read()
    out = []
    for f in re.split(r'\n\t\(footprint ', t)[1:]:
        ref = re.search(r'\(property "Reference" "([^"]*)"', f).group(1)
        at = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', f)
        x0, y0, r0 = float(at.group(1)), float(at.group(2)), float(at.group(3) or 0)
        pads = []
        for m in re.finditer(r'\n\t\t\(pad "([^"]*)" (\w+) (\w+)\s*\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)'
                             r'\s*\(size ([\d.]+) ([\d.]+)\)', f):
            dx, dy = rotate(float(m.group(4)), float(m.group(5)), -r0)
            pads.append(dict(num=m.group(1), type=m.group(2), shape=m.group(3),
                             X=x0 + dx, Y=-(y0 + dy), w=float(m.group(7)), h=float(m.group(8)),
                             rot=float(m.group(6) or 0)))
        layer = re.search(r'\n\t\t\(layer "([^"]+)"\)', f).group(1)
        out.append((ref, dict(lib=f.split('"')[1], x=x0, y=-y0, rot=r0, layer=layer, pads=pads)))
    return out


def pcb_outline(pcb_path):
    """Edge.Cuts graphics as (kind, [(x, y), ...]) in cartesian coordinates."""
    t = open(pcb_path).read()
    out = []
    for m in re.finditer(r'\(gr_(line|rect|arc|circle)\b(.*?)\n\t\)', t, re.S):
        if 'Edge.Cuts' in m.group(2):
            pts = [(float(a), -float(b)) for a, b in
                   re.findall(r'\((?:start|end|mid|center) ([-\d.]+) ([-\d.]+)\)', m.group(2))]
            out.append((m.group(1), pts))
    return out
