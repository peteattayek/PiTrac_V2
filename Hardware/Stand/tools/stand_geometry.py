# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""Where the built stand's parts are, relative to the ball.

Reads the SolidWorks assembly STEP of the stand (text parse of its product tree and
placements; no CAD kernel needed), the V2 board's .kicad_pcb, and the optimizer's
reference datums (reference_003/COMPONENT_DATUMS_MM.csv). Prints every key position
in the RH ball frame, the checks that tie the assembly to that frame, and the
impact-sound and first-flash timings that follow from the geometry.

RH ball frame (the optimizer's RH floor frame, Hardware/Stand/README.md section 1):
  origin on the floor under the ball; +X downrange; +Y from the stand towards the
  ball; +Z up. Floor Z = 0, mat top Z = --mat-mm (36 in the design).

Usage (from the repo root):
  python Hardware/Stand/tools/stand_geometry.py --step <FullStand.STEP> [--csv out.csv]

Defaults find the board and datums in this repo. The assembly frame is registered by
the origin of --frame-part (default Base_V2): the printed parts were drawn in the RH
floor frame of reference_003, so that part's origin IS the RH origin.
"""
import argparse
import csv
import math
import re
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
STAND = HERE.parent
REPO = STAND.parents[1]
PCB = REPO / 'Hardware/The_One_Board_To_Rule_Them_All/The_One_Board_To_Rule_Them_All.kicad_pcb'
DATUMS = STAND / 'reference_003/COMPONENT_DATUMS_MM.csv'

BALL_D = 42.67
BALL_R = BALL_D / 2

# ---------------------------------------------------------------------------
# STEP AP214 product tree
# ---------------------------------------------------------------------------
HEAD = re.compile(r'#(\d+)\s*=\s*(\(?)\s*([A-Z0-9_]+)')
REF = re.compile(r'#(\d+)')
STR = re.compile(r"'((?:[^']|'')*)'")
NUM = re.compile(r'\(\s*([-0-9.Ee+]+)\s*,\s*([-0-9.Ee+]+)\s*,\s*([-0-9.Ee+]+)\s*\)')
KEEP = ('PRODUCT', 'PRODUCT_DEFINITION_FORMATION', 'PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE',
        'PRODUCT_DEFINITION', 'NEXT_ASSEMBLY_USAGE_OCCURRENCE', 'PRODUCT_DEFINITION_SHAPE',
        'SHAPE_DEFINITION_REPRESENTATION', 'CONTEXT_DEPENDENT_SHAPE_REPRESENTATION',
        'ITEM_DEFINED_TRANSFORMATION')


def step_records(path):
    buf = []
    with open(path, 'r', encoding='latin-1') as f:
        for line in f:
            buf.append(line)
            if line.rstrip().endswith(';'):
                yield ''.join(buf)
                buf = []


def ids(rec):
    return [int(r) for r in REF.findall(rec)[1:]]


def read_tree(path):
    """Return occurrences as dicts: product, parent (index or None), T (4x4, root frame, mm)."""
    ents, rrwt, units = {}, {}, set()
    for rec in step_records(path):
        if 'LENGTH_UNIT' in rec:
            units.add('MILLI' if '.MILLI.' in rec else rec.strip())
        m = HEAD.match(rec.lstrip())
        if not m:
            continue
        eid, kind = int(m.group(1)), m.group(3)
        if m.group(2) and 'REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION' in rec:
            rr = re.search(r'REPRESENTATION_RELATIONSHIP\s*\(([^)]*)\)', rec).group(1)
            r1, r2 = [int(x) for x in REF.findall(rr)]
            idt = int(REF.findall(re.search(r'WITH_TRANSFORMATION\s*\(([^)]*)\)', rec).group(1))[0])
            rrwt[eid] = (r1, r2, idt)
        elif kind in KEEP:
            ents[eid] = (kind, rec)
    if units != {'MILLI'}:
        sys.exit(f'{path}: expected millimetre length units, found {units}')
    need_ax = {a for k, r in ents.values() if k == 'ITEM_DEFINED_TRANSFORMATION' for a in ids(r)}
    ax, need_v = {}, set()
    for rec in step_records(path):
        m = HEAD.match(rec.lstrip())
        if m and int(m.group(1)) in need_ax:
            ax[int(m.group(1))] = ids(rec)
            need_v.update(ids(rec))
    vec = {}
    for rec in step_records(path):
        m = HEAD.match(rec.lstrip())
        if m and int(m.group(1)) in need_v:
            vec[int(m.group(1))] = np.array([float(v) for v in NUM.search(rec).groups()])

    def placement(aid):
        o, z, x = (vec[r] for r in ax[aid][:3])
        z = z / np.linalg.norm(z)
        x = x - z * (x @ z)
        x = x / np.linalg.norm(x)
        t = np.eye(4)
        t[:3, 0], t[:3, 1], t[:3, 2], t[:3, 3] = x, np.cross(z, x), z, o
        return t

    name = {e: STR.findall(r)[0] for e, (k, r) in ents.items() if k == 'PRODUCT'}
    pdf = {e: ids(r)[0] for e, (k, r) in ents.items() if k.startswith('PRODUCT_DEFINITION_FORMATION')}
    pd = {e: name[pdf[ids(r)[0]]] for e, (k, r) in ents.items() if k == 'PRODUCT_DEFINITION'}
    pds = {e: ids(r)[0] for e, (k, r) in ents.items() if k == 'PRODUCT_DEFINITION_SHAPE'}
    rep_pd = {}
    for e, (k, r) in ents.items():
        if k == 'SHAPE_DEFINITION_REPRESENTATION':
            p, rep = ids(r)[:2]
            if pds.get(p) in pd:
                rep_pd[rep] = pds[p]
    nauo = {e: dict(parent=ids(r)[0], child=ids(r)[1]) for e, (k, r) in ents.items()
            if k == 'NEXT_ASSEMBLY_USAGE_OCCURRENCE'}
    local = {}
    for e, (k, r) in ents.items():
        if k != 'CONTEXT_DEPENDENT_SHAPE_REPRESENTATION':
            continue
        rel, p = ids(r)[:2]
        n = pds.get(p)
        if n not in nauo or rel not in rrwt:
            continue
        r1, r2, idt = rrwt[rel]
        a1, a2 = ids(ents[idt][1])[:2]
        if rep_pd.get(r2) == nauo[n]['child']:          # rep_1 is the parent
            local[n] = placement(a1) @ np.linalg.inv(placement(a2))
        else:                                            # rep_1 is the child (SolidWorks)
            local[n] = placement(a2) @ np.linalg.inv(placement(a1))
    kids = {}
    for n, v in nauo.items():
        kids.setdefault(v['parent'], []).append(n)
    roots = [p for p in pd if p not in {v['child'] for v in nauo.values()}]
    occ = []

    def walk(p, t, parent):
        for n in kids.get(p, []):
            tc = t @ local.get(n, np.eye(4))
            occ.append(dict(product=pd[nauo[n]['child']], parent=parent, T=tc))
            walk(nauo[n]['child'], tc, len(occ) - 1)

    for r in roots:
        walk(r, np.eye(4), None)
    return occ


# ---------------------------------------------------------------------------
# KiCad footprints (board coordinates, mm; KiCad +Y points down the page)
# ---------------------------------------------------------------------------
def footprints(path):
    text = Path(path).read_text(encoding='utf-8')
    starts = [m.start() for m in re.finditer(r'\(footprint "', text)] + [len(text)]
    out = {}
    for a, b in zip(starts, starts[1:]):
        block = text[a:b]
        at = re.search(r'\(at ([-0-9.]+) ([-0-9.]+)', block)
        ref = re.search(r'\(property "Reference" "([^"]+)"', block)
        if at and ref:
            out[ref.group(1)] = (float(at.group(1)), float(at.group(2)))
    return out


def reference_datums(path):
    rows = {}
    with open(path, newline='', encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r['frame'] == 'RH':
                rows[(r['component'], r['datum'])] = dict(
                    p=np.array([float(r[k]) for k in ('x_mm', 'y_mm', 'z_mm')]),
                    fwd=np.array([float(r[k]) for k in ('forward_x', 'forward_y', 'forward_z')]))
    return rows


def pitch_deg(axis):
    return math.degrees(math.asin(max(-1., min(1., axis[2]))))


def fmt(p):
    return '(' + ', '.join(f'{v:8.2f}' for v in p) + ')'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--step', required=True, help='assembly STEP exported from FullStand.SLDASM')
    ap.add_argument('--pcb', default=str(PCB))
    ap.add_argument('--datums', default=str(DATUMS))
    ap.add_argument('--frame-part', default='Base_V2', help='printed part whose origin is the RH origin')
    ap.add_argument('--mat-mm', type=float, default=36.0, help='mat top above the floor the stand stands on')
    ap.add_argument('--sound-mps', type=float, default=343.0, help='speed of sound (343 at 20 C)')
    ap.add_argument('--board-face-mm', type=float, default=1.6, help='component face above the KiCad STEP origin')
    ap.add_argument('--csv', help='write the key positions here')
    a = ap.parse_args()

    occ = read_tree(a.step)
    top = [o for o in occ if o['parent'] is None]
    frame = [o for o in top if o['product'] == a.frame_part]
    if len(frame) != 1 or not np.allclose(frame[0]['T'][:3, :3], np.eye(3), atol=1e-9):
        sys.exit(f'need exactly one unrotated {a.frame_part} at the top level')
    off = frame[0]['T'][:3, 3]
    same = [o['product'] for o in top if np.allclose(o['T'], frame[0]['T'], atol=1e-6)]
    print(f'Assembly -> RH: subtract {fmt(off)} mm (origin of {a.frame_part}).')
    print(f'  Printed parts sharing that placement: {", ".join(same)}')

    def rh(o):
        return o['T'][:3, 3] - off

    def one(prefix):
        hits = [o for o in top if o['product'].startswith(prefix)]
        if len(hits) != 1:
            sys.exit(f'expected one top-level {prefix}*, found {len(hits)}')
        return hits[0]

    ref = reference_datums(a.datums)
    out = []   # (name, xyz, note)

    # Cameras: Mira220 modules; local +Z is the optical axis.
    cams = sorted((o for o in top if o['product'].startswith('MIRA220')), key=lambda o: o['T'][2, 3])
    if len(cams) != 2:
        sys.exit(f'expected two Mira220 modules, found {len(cams)}')
    targets = [ref[('CAM0', 'PUPIL')]['p'], ref[('CAM1', 'PUPIL')]['p']]
    lhs = np.vstack([c['T'][:3, :3] for c in cams])
    rhs = np.concatenate([t - rh(c) for c, t in zip(cams, targets)])
    d, *_ = np.linalg.lstsq(lhs, rhs, rcond=None)
    print('\nCameras (pupil = module origin + R * d, d fitted to both reference pupils at once):')
    print(f'  module-local pupil offset d = {fmt(d)} mm')
    for name, c, t in zip(('CAM0', 'CAM1'), cams, targets):
        pupil = rh(c) + c['T'][:3, :3] @ d
        axis = c['T'][:3, 2]
        ang = math.degrees(math.acos(min(1., axis @ ref[(name, 'PUPIL')]['fwd'])))
        print(f'  {name}: pupil {fmt(pupil)}  pitch {pitch_deg(axis):+6.2f} deg  '
              f'| miss vs reference {np.linalg.norm(pupil - t):.3f} mm, axis {ang:.3f} deg')
        out.append((f'{name} pupil', pupil, f'pitch {pitch_deg(axis):+.2f} deg; fitted, see README'))

    # Strobe: IR LED board; centroid of its LED parts against the reference strobe axis.
    led_board = one('IRLED')
    k = occ.index(led_board)
    leds = [rh(o) for o in occ if o['parent'] == k and o['product'].startswith('VSMA')]
    s_ref = ref[('STROBE', 'SOURCE_ANCHOR')]
    cen = np.mean(leds, axis=0)
    along = (cen - s_ref['p']) @ s_ref['fwd']
    off_axis = np.linalg.norm(cen - s_ref['p'] - along * s_ref['fwd'])
    axis = led_board['T'][:3, 2]
    print(f'\nStrobe: {len(leds)} LEDs, centroid of their part origins {fmt(cen)}, board pitch {pitch_deg(axis):+.2f} deg')
    print(f'  vs reference strobe axis: {along:+.2f} mm along it, {off_axis:.2f} mm off it')
    out.append(('strobe LED centroid', cen, f'pitch {pitch_deg(axis):+.2f} deg; {len(leds)} LED part origins'))

    # V2 board: KiCad page origin; board +X -> CADmodel x-axis, KiCad +Y -> -(CADmodel y-axis).
    board = one('CADmodel')
    bx, bz = board['T'][:3, 0], board['T'][:3, 2]
    by = np.cross(bz, bx)
    fp = footprints(a.pcb)

    def on_board(r):
        x, y = fp[r]
        return rh(board) + bx * x - by * y + bz * a.board_face_mm

    m2 = [rh(o) for o in top if o['product'].startswith('Standoff_M2_')]
    miss = []
    for h in ('H1', 'H2', 'H7', 'H8'):
        p = on_board(h)
        if m2:
            q = min(m2, key=lambda s: np.linalg.norm((s - p) - ((s - p) @ bz) * bz))
            miss.append(np.linalg.norm((q - p) - ((q - p) @ bz) * bz))
    print(f'\nV2 board: normal {fmt(bz)}; component face at Y = {on_board("U16")[1]:.2f} mm (+-1)')
    print(f'  M2 holes H1/H2/H7/H8 vs nearest M2 standoff, in the board plane: '
          + ', '.join(f'{v:.2f}' for v in miss) + ' mm')
    for r, label in (('D11', 'beam LED D11'), ('D12', 'photodiode D12 (36 V)'), ('U16', 'mic U16')):
        p = on_board(r)
        print(f'  {label:<22} {fmt(p)}')
        out.append((label, p, f'KiCad {fp[r]}'))
    t_ref = ref[('TRIGGER', 'SOURCE_ANCHOR')]['p']
    print(f'  reference trigger anchor {fmt(t_ref)}; D11 - anchor = {fmt(on_board("D11") - t_ref)}')

    print('\nOther parts (occurrence origins, not centres):')
    for prefix in ('Lens_LED', 'Lens_Photodiode', 'rpi', 'LRS', '141_'):
        hits = [o for o in top if o['product'].startswith(prefix)]
        for o in hits:
            print(f'  {o["product"][:30]:<30} {fmt(rh(o))}')

    # Distances and timing.
    ball = {'ball on the mat': np.array([0, 0, a.mat_mm + BALL_R]),
            'ball on a 40 mm-centre tee': np.array([0, 0, a.mat_mm + 40.])}
    mic = on_board('U16')
    print(f'\nDistances (mat top Z = {a.mat_mm:g} mm):')
    for label, b in ball.items():
        dm = np.linalg.norm(mic - b)
        print(f'  mic U16 -> {label}: {dm:.1f} mm, sound {dm / a.sound_mps:.3f} ms at {a.sound_mps:g} m/s')
    for name in ('CAM0', 'CAM1'):
        p = ref[(name, 'PUPIL')]['p']
        print(f'  {name} pupil -> ball on the mat: {np.linalg.norm(p - ball["ball on the mat"]):.1f} mm')
    sheet_x = t_ref[0]
    print(f'  light-sheet plane X = {sheet_x:.1f} mm (trigger rod plane); ball centre starts at X = 0')

    if a.csv:
        Path(a.csv).parent.mkdir(parents=True, exist_ok=True)
        with open(a.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f, lineterminator='\n')
            w.writerow(['item', 'x_mm', 'y_mm', 'z_mm', 'note'])
            for label, p, note in out:
                w.writerow([label, *(f'{v:.2f}' for v in p), note])
            for label, b in ball.items():
                w.writerow([label, *(f'{v:.2f}' for v in b), f'mat top Z = {a.mat_mm:g}'])
        print(f'\nWrote {a.csv}')


if __name__ == '__main__':
    main()
