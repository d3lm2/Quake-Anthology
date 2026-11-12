#!/usr/bin/env python3
"""Per-file depth comparison of the C port against the TypeScript source.

Reads docs/manifest.tsv and flags, for every TS file: missing C files, thin
ports (C plus header tokens well below TS tokens), and whether a C counterpart is reachable
from main. Comments are excluded. Tokens, not lines: this code base is dense and
line counts mislead. TS type annotations inflate the TS side somewhat, while C
is normally larger than TS for the same logic, so below 0.5 means logic is absent.
Writes docs/port_gaps.tsv when --write is given. A manifest c_source may be a glob,
or several paths or globs separated by spaces (for a TS file split across several C
files). A thin or missing file listed in tools/depth_audit_exceptions.tsv
(`ts_source <TAB> reason`) is reported as justified; the gate is zero unjustified.

Usage: tools/depth_audit.py [--ts ../quake-anthology-ts] [--ratio 0.5] [--write]
"""
import os, re, subprocess, sys, csv
ts_root = sys.argv[sys.argv.index('--ts') + 1] if '--ts' in sys.argv else '../quake-anthology-ts'
ratio = float(sys.argv[sys.argv.index('--ratio') + 1]) if '--ratio' in sys.argv else 0.5
strip = re.compile(r'/\*.*?\*/', re.S)
token = re.compile(r'[A-Za-z_][A-Za-z_0-9]*|\d+\.?\d*|[^\sA-Za-z_0-9]')
import glob as _glob
def code_tokens(path):
    if len(path.split()) > 1:
        counts = [code_tokens(p) for p in path.split()]
        return None if all(c is None for c in counts) else sum(c or 0 for c in counts)
    if any(ch in path for ch in '*?['):
        files = sorted(_glob.glob(path))
        if not files:
            return None
        return sum(code_tokens(f) or 0 for f in files)
    try:
        text = open(path, errors='replace').read()
    except OSError:
        return None
    text = re.sub(r'//[^\n]*', '', strip.sub('', text))
    return len(token.findall(text))
reach = subprocess.run([sys.executable, 'tools/reachability.py', '--list'], capture_output=True, text=True).stdout.splitlines()
unreach = set(l for l in reach if '/' in l and not re.match(r'^\s*\d', l))
rows = list(csv.DictReader(open('docs/manifest.tsv'), delimiter='\t'))
EXCEPTIONS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'depth_audit_exceptions.tsv')
justified = {}
if os.path.exists(EXCEPTIONS):
    for line in open(EXCEPTIONS):
        if line.startswith('#') or not line.strip():
            continue
        parts = line.rstrip('\n').split('\t')
        if len(parts) < 2 or not parts[1].strip():
            sys.exit(f'{EXCEPTIONS}: exception without a reason: {line.strip()}')
        justified[parts[0]] = parts[1]
seen_ts = set()
thin, missing = [], []
for r in rows:
    ts = r['ts_source']; c = r['c_source']; seen_ts.add(ts)
    t = code_tokens(os.path.join(ts_root, ts)) or 0
    header = r.get('header') or '-'
    if c in ('-', ''):
        if t > 100 and ts.endswith('.ts') and not os.path.exists(header): missing.append((t, ts, r['status']))
        continue
    n = code_tokens(c)
    if n is not None:
        n += code_tokens(r['header']) or 0 if r.get('header') not in (None, '-', '') else 0
    if n is None:
        n = code_tokens(header)
        if n is None:
            missing.append((t, ts, 'neither c nor header present: ' + c)); continue
    if t >= 300 and n < t * ratio:
        thin.append((t - n, t, n, ts, c.split()[0][4:-2] in unreach))
actual = set()
for d, _, fs in os.walk(os.path.join(ts_root, 'src')):
    for f in fs:
        if f.endswith('.ts'):
            actual.add(os.path.relpath(os.path.join(d, f), ts_root))
absent = sorted(actual - seen_ts)
print(f'ts files={len(actual)} manifest rows={len(rows)} not-in-manifest={len(absent)}')
for a in absent[:40]: print('  not in manifest:', a)
print(f'missing C ({len(missing)}, unjustified {sum(ts not in justified for _, ts, _ in missing)}):')
for t, ts, s in sorted(missing, reverse=True):
    print(f'  {t:6d} {ts} [{s}]' + (f'  justified: {justified[ts]}' if ts in justified else ''))
print(f'thin ports, C < {ratio:.0%} of TS tokens ({len(thin)}, unjustified {sum(x[3] not in justified for x in thin)}), '
      f'total deficit {sum(x[0] for x in thin)} tokens:')
for d, t, n, ts, un in sorted(thin, reverse=True):
    print(f'  ts={t:6d} c={n:6d} {"UNREACHED" if un else "reached  "} {ts}' + (f'  justified: {justified[ts]}' if ts in justified else ''))
flagged = {x[3] for x in thin} | {x[1] for x in missing}
unjustified = len(flagged - set(justified))
if '--write' in sys.argv:
    with open('docs/port_gaps.tsv', 'w') as out:
        out.write('ts_tokens\tc_tokens\treachable\tts_source\n')
        for d, t, n, ts, un in sorted(thin, reverse=True):
            out.write(f'{t}\t{n}\t{"no" if un else "yes"}\t{ts}\n')
sys.exit(1 if unjustified else 0)
