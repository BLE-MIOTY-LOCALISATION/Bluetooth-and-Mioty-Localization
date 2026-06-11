import re
import openpyxl
from collections import defaultdict, Counter

sch_path = r'c:\Users\rohit\OneDrive\Documents\FAU\BLE MIOTY\Bluetooth-and-Mioty-Localization\KICAD\BLE_MIOTY_MODULE\BLE MIOTY MODULE.kicad_sch'
bom_path = r'c:\Users\rohit\OneDrive\Documents\FAU\BLE MIOTY\Bluetooth-and-Mioty-Localization\BOM.xlsx'

with open(sch_path, 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# -----------------------------------------------------------------------
# Parse placed symbol instances from the schematic body
# -----------------------------------------------------------------------
placed_components = []
symbol_blocks = re.finditer(r'^\s{1,2}\(symbol\s*\n\s+\(lib_id\s+"([^"]+)"\)', content, re.MULTILINE)

for match in symbol_blocks:
    lib_id = match.group(1)
    start = match.start()
    block = content[start:start+3000]

    ref_match   = re.search(r'\(property\s+"Reference"\s+"([^"]+)"', block)
    val_match   = re.search(r'\(property\s+"Value"\s+"([^"]+)"', block)
    mpn_match   = re.search(r'\(property\s+"MPN"\s+"([^"]+)"', block)
    pn_match    = re.search(r'\(property\s+"Part\s+Number"\s+"([^"]+)"', block)
    mfr_match   = re.search(r'\(property\s+"Manufacturer_Part_Number"\s+"([^"]+)"', block)
    fp_match    = re.search(r'\(property\s+"Footprint"\s+"([^"]+)"', block)
    in_bom_match = re.search(r'\(in_bom\s+(yes|no)\)', block)
    excl_sim_match = re.search(r'\(exclude_from_sim\s+(yes|no)\)', block)

    ref     = ref_match.group(1)   if ref_match   else "??"
    val     = val_match.group(1)   if val_match   else "??"
    fp      = fp_match.group(1)    if fp_match    else ""
    in_bom  = in_bom_match.group(1) if in_bom_match else "yes"
    mpn     = (mpn_match.group(1) if mpn_match else
               pn_match.group(1)  if pn_match  else
               mfr_match.group(1) if mfr_match else "")

    placed_components.append({
        'ref': ref, 'value': val, 'lib_id': lib_id,
        'mpn': mpn, 'footprint': fp, 'in_bom': in_bom
    })

# -----------------------------------------------------------------------
# Parse BOM (col indices: 0=seq, 1=ref, 2=component, 3=value, 4=MPN, 5=URL)
# -----------------------------------------------------------------------
wb = openpyxl.load_workbook(bom_path)
ws = wb.active

bom_components = []   # {ref, component, value, mpn, url}
current_section = ""

for i, row in enumerate(ws.iter_rows(values_only=True)):
    if i == 0:
        continue   # header row

    # Section header rows: first cell is string (not int), rest None
    if isinstance(row[0], str) and all(c is None for c in row[1:]):
        current_section = row[0]
        continue

    # Skip empty rows
    if all(c is None for c in row):
        continue

    # Data row: (seq_int, ref_str, component, value, mpn, url, ...)
    if len(row) >= 2 and row[1] is not None:
        ref_str = str(row[1]).strip()
        if not ref_str:
            continue
        # split comma/space-separated refs
        refs = [r.strip() for r in re.split(r'[,;\s]+', ref_str) if r.strip()]
        for r in refs:
            bom_components.append({
                'ref': r,
                'section': current_section,
                'component': str(row[2]) if row[2] is not None else "",
                'value': str(row[3]) if row[3] is not None else "",
                'mpn': str(row[4]) if row[4] is not None else "",
                'url': str(row[5]) if row[5] is not None else "",
            })

# -----------------------------------------------------------------------
# BUILD LOOKUP STRUCTURES
# -----------------------------------------------------------------------
sch_by_ref   = {c['ref']: c for c in placed_components}
bom_by_ref   = {}
for b in bom_components:
    if b['ref'] not in bom_by_ref:
        bom_by_ref[b['ref']] = b
    else:
        # duplicate BOM entry
        bom_by_ref[b['ref'] + "_DUP"] = b

# Filter schematic for "real" components (skip power flags #PWR, #FLG etc.)
def is_real(comp):
    r = comp['ref']
    if r.startswith('#') or r in ('??', ''):
        return False
    if comp['lib_id'].startswith('power:') or comp['lib_id'] == 'power':
        return False
    # skip "U" without number (lib symbol template refs)
    if not re.search(r'\d', r):
        return False
    return True

sch_real = [c for c in placed_components if is_real(c)]
sch_refs = set(c['ref'] for c in sch_real)
bom_refs = set(bom_by_ref.keys())

# -----------------------------------------------------------------------
# REPORT
# -----------------------------------------------------------------------
sep = "=" * 72

print(sep)
print("  BLE_MIOTY MODULE — SCHEMATIC QA REPORT")
print(sep)
print(f"  Schematic real components : {len(sch_real)}")
print(f"  BOM entries               : {len(bom_components)}")
print()

# ── 1. DUPLICATE REFERENCES IN SCHEMATIC ────────────────────────────────
print(sep)
print("  1. DUPLICATE REFERENCE DESIGNATORS IN SCHEMATIC")
print(sep)

ref_count = Counter(c['ref'] for c in placed_components if is_real(c))
dupes = {r: n for r, n in ref_count.items() if n > 1}

if dupes:
    for ref, n in sorted(dupes.items()):
        entries = [c for c in sch_real if c['ref'] == ref]
        print(f"  ⚠  {ref}  (×{n})")
        for e in entries:
            print(f"      Value='{e['value']}'   LibID={e['lib_id']}")
else:
    print("  ✓  No duplicate references found.")
print()

# ── 2. DUPLICATE REFERENCES IN BOM ──────────────────────────────────────
print(sep)
print("  2. DUPLICATE REFERENCE DESIGNATORS IN BOM")
print(sep)

bom_ref_count = Counter(b['ref'] for b in bom_components)
bom_dupes = {r: n for r, n in bom_ref_count.items() if n > 1}
if bom_dupes:
    for ref, n in sorted(bom_dupes.items()):
        entries = [b for b in bom_components if b['ref'] == ref]
        print(f"  ⚠  {ref}  (×{n})")
        for e in entries:
            print(f"      Section='{e['section']}'  MPN='{e['mpn']}'  Value='{e['value']}'")
else:
    print("  ✓  No duplicate references in BOM.")
print()

# ── 3. IN SCHEMATIC BUT NOT IN BOM ──────────────────────────────────────
print(sep)
print("  3. COMPONENTS IN SCHEMATIC BUT MISSING FROM BOM")
print(sep)

missing_from_bom = sch_refs - bom_refs
if missing_from_bom:
    for r in sorted(missing_from_bom, key=lambda x: (re.sub(r'\d','',x), int(re.search(r'\d+',x).group()) if re.search(r'\d+',x) else 0)):
        c = sch_by_ref[r]
        print(f"  ⚠  {r:<8}  Value='{c['value']}'   LibID={c['lib_id']}")
else:
    print("  ✓  All schematic components are in BOM.")
print()

# ── 4. IN BOM BUT NOT IN SCHEMATIC ──────────────────────────────────────
print(sep)
print("  4. COMPONENTS IN BOM BUT MISSING FROM SCHEMATIC")
print(sep)

extra_in_bom = bom_refs - sch_refs
if extra_in_bom:
    for r in sorted(extra_in_bom, key=lambda x: (re.sub(r'\d','',x), int(re.search(r'\d+',x).group()) if re.search(r'\d+',x) else 0)):
        b = bom_by_ref[r]
        print(f"  ⚠  {r:<8}  Section='{b['section']}'  MPN='{b['mpn']}'  Value='{b['value']}'")
else:
    print("  ✓  All BOM components are in schematic.")
print()

# ── 5. VALUE/MPN MISMATCHES ──────────────────────────────────────────────
print(sep)
print("  5. VALUE / MPN MISMATCHES (in both SCH and BOM)")
print(sep)

common = sch_refs & bom_refs
mismatches = []
for ref in sorted(common):
    sc = sch_by_ref.get(ref)
    bm = bom_by_ref.get(ref)
    if not sc or not bm:
        continue

    sch_val  = sc['value'].strip()
    bom_val  = bm['value'].strip()
    sch_mpn  = sc['mpn'].strip()
    bom_mpn  = bm['mpn'].strip()

    # Normalize values for comparison (e.g. "0.1u" vs "0.1u", "4.7K" vs "4.7K")
    val_mismatch = (bom_val not in ('', '0', 'None') and sch_val not in ('', '0', 'None')
                    and bom_val.lower() != sch_val.lower())
    mpn_mismatch = (bom_mpn not in ('', 'None') and sch_mpn not in ('', 'None')
                    and bom_mpn.lower() != sch_mpn.lower())

    if val_mismatch or mpn_mismatch:
        mismatches.append((ref, sc, bm, val_mismatch, mpn_mismatch))

if mismatches:
    for ref, sc, bm, vm, mm in mismatches:
        print(f"\n  ⚠  {ref}:")
        if vm:
            print(f"      Value   SCH: '{sc['value']}'  ←→  BOM: '{bm['value']}'")
        if mm:
            print(f"      MPN     SCH: '{sc['mpn']}'  ←→  BOM: '{bm['mpn']}'")
else:
    print("  ✓  No value/MPN mismatches found (for fields where both are populated).")
print()

# ── 6. SPECIAL ISSUES ───────────────────────────────────────────────────
print(sep)
print("  6. OTHER SCHEMATIC ISSUES")
print(sep)

# Components with no footprint
no_fp = [c for c in sch_real if not c['footprint']]
if no_fp:
    print(f"\n  Missing Footprint ({len(no_fp)} components):")
    for c in no_fp:
        print(f"    {c['ref']:<8} Value='{c['value']}'")

# Components with generic/template reference (no number - already filtered)
# Unresolved references (still have '?')
unresolved = [c for c in placed_components if '?' in c['ref']]
if unresolved:
    print(f"\n  Unresolved references ({len(unresolved)}):")
    for c in unresolved:
        print(f"    LibID={c['lib_id']}   Value='{c['value']}'")

# BOM value issues — placeholder/zero values with no MPN
bom_incomplete = [b for b in bom_components if b['value'] in ('0', '0.0', '') and b['mpn'] in ('', 'None')]
if bom_incomplete:
    print(f"\n  BOM rows with no value AND no MPN ({len(bom_incomplete)}):")
    for b in bom_incomplete:
        print(f"    {b['ref']:<8} Section='{b['section']}'")

if not no_fp and not unresolved and not bom_incomplete:
    print("  ✓  No additional issues found.")

print()
print(sep)
print("  END OF REPORT")
print(sep)
