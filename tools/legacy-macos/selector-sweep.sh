#!/bin/bash
# selector-sweep.sh — automated sweep for the 10.7+ selector abort class.
# Adapted from leopard-webkit-build/tools/selector-sweep.sh.
#
# The 10.6 SDK overlay makes every modern selector compile, so unguarded
# sends to post-10.6 selectors only fail at runtime (NSInvalidArgumentException
# unwinding through the paint/input path). This finds them statically:
#   1. Extract selector-shaped strings from our XUL binary.
#   2. Diff against the 10.6 runtime universe (strings of the stock system
#      frameworks, cached at tools/legacy-macos/10.6-runtime-universe.txt).
#   3. Intersect with ObjC(++) platform sources to find plausible send sites
#      (system-class sends; self/super/our-own-class receivers excluded).
#   4. Subtract the safe list (tools/legacy-macos/selector-sweep-safe.txt)
#      and report NEW candidates with file:line for manual classification:
#      fix with a respondsToSelector probe + fallback, or safe-list it.
#
# Usage:
#   tools/legacy-macos/selector-sweep.sh
#   SWEEP_TARGET=<ssh-destination> tools/legacy-macos/selector-sweep.sh --refresh-universe
#
# Exit 0 = report written; "NEW UNGUARDED CANDIDATES: 0" is the clean state.

set -u
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OBJDIR="${SWEEP_OBJDIR:-$PROJECT_ROOT/obj-x86_64-apple-darwin}"
XUL="$OBJDIR/dist/Nightly.app/Contents/MacOS/XUL"
UNIVERSE="${SWEEP_UNIVERSE:-$PROJECT_ROOT/tools/legacy-macos/10.6-runtime-universe.txt}"
SAFE_LIST="$PROJECT_ROOT/tools/legacy-macos/selector-sweep-safe.txt"
REPORT="$PROJECT_ROOT/tools/legacy-macos/selector-sweep-report.txt"
TARGET="${SWEEP_TARGET:-}"

# Stock 10.6 system locations — identical on every 10.6.8 install.
SYS_FRAMEWORKS=(
    /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
    /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
    /System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
    /System/Library/Frameworks/CoreData.framework/Versions/A/CoreData
    /System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreText.framework/CoreText
    /System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices
    /System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/HIServices.framework/Versions/A/HIServices
    /System/Library/Frameworks/SecurityInterface.framework/Versions/A/SecurityInterface
    /System/Library/Frameworks/Quartz.framework/Versions/A/Frameworks/PDFKit.framework/Versions/A/PDFKit
    /System/Library/Frameworks/Quartz.framework/Versions/A/Quartz
    /System/Library/Frameworks/ApplicationServices.framework/Frameworks/ImageIO.framework/ImageIO
    /System/Library/Frameworks/CoreWLAN.framework/Versions/A/CoreWLAN
)

if [ "${1:-}" = "--refresh-universe" ]; then
    if [ -z "$TARGET" ]; then
        echo "usage: SWEEP_TARGET=<ssh-destination> $0 --refresh-universe" >&2
        exit 2
    fi
    echo "[sweep] regenerating 10.6 runtime universe from $TARGET..."
    : > "$UNIVERSE.tmp"
    for fw in "${SYS_FRAMEWORKS[@]}"; do
        ssh "$TARGET" "strings '$fw' 2>/dev/null" >> "$UNIVERSE.tmp" \
            || echo "[sweep] WARN: could not read $fw on target" >&2
    done
    LC_ALL=C sort -u "$UNIVERSE.tmp" > "$UNIVERSE"; rm -f "$UNIVERSE.tmp"
    echo "[sweep] universe: $(wc -l < "$UNIVERSE" | tr -d ' ') unique strings -> $UNIVERSE"
fi

if [ ! -f "$UNIVERSE" ]; then
    cat >&2 <<EOF
[sweep] no universe at $UNIVERSE.
Regenerate it from any 10.6 machine:
    SWEEP_TARGET=<ssh-destination> $0 --refresh-universe
EOF
    exit 1
fi

if [ ! -f "$XUL" ]; then
    echo "[sweep] no XUL at $XUL (build first or set SWEEP_OBJDIR)." >&2
    exit 1
fi
echo "[sweep] binary: $XUL"

strings "$XUL" > /tmp/sweep-ours.$$

# Our ObjC(++) platform sources (skip vendored upstream trees).
find "$PROJECT_ROOT" \( -name '*.mm' -o -name '*.m' \) \
    -not -path '*/third_party/*' -not -path '*/gfx/skia/*' \
    -not -path '*/obj-*' -not -path '*/.git/*' \
    > /tmp/sweep-files.$$
echo "[sweep] sources: $(wc -l < /tmp/sweep-files.$$ | tr -d ' ') ObjC files"

/usr/bin/python3 - "$UNIVERSE" "$SAFE_LIST" /tmp/sweep-ours.$$ \
    "$PROJECT_ROOT" /tmp/sweep-files.$$ "$REPORT" <<'PYEOF'
import re, os, sys

universe_path, safe_path, ours_path, src_root, files_path, report_path = sys.argv[1:7]
universe = set(l.rstrip('\n') for l in open(universe_path, errors='ignore'))
universe_colon = set(u for u in universe if u.endswith(':'))

def present(sel):
    if sel in universe:
        return True
    return sel + ':' in universe_colon or (':' in sel and sel in universe)

selshape = re.compile(r'^[A-Za-z_][A-Za-z0-9_:]{2,64}$')
ours = set(l.strip() for l in open(ours_path, errors='ignore') if selshape.match(l.strip()))

safe = {}
for line in open(safe_path, errors='ignore'):
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    parts = line.split(None, 2)
    if len(parts) >= 2:
        safe[parts[0]] = parts[1]

cands = set(s for s in ours if not present(s))

class_send_re = re.compile(
    r'\[\s*(NS|CI|CA|CT|AV|SF|QL|IK|PDF|Sec|ATSU|MTL|WK|CB|GK|NE|AS)[A-Za-z0-9_]*'
    r'(?:\s+[A-Za-z_][A-Za-z0-9_.\]]+){0,2}\s+([A-Za-z_][A-Za-z0-9_]*)')
any_send_recv_re = re.compile(
    r'\[\s*([A-Za-z_][A-Za-z0-9_.\]]*)((?:\s+[A-Za-z_][A-Za-z0-9_.\]]+)*?)\s+([A-Za-z_][A-Za-z0-9_]*)')

hits = {}
unresolved = {}
scanned = 0

our_classes = set(['NSGraphicsContext'])  # SDKDeclarations overlay category
impl_re = re.compile(r'^\s*@(?:implementation|interface)\s+([A-Za-z_][A-Za-z0-9_]*)')

for fp in open(files_path):
    p = fp.strip()
    rel = os.path.relpath(p, src_root)
    try:
        lines = open(p, errors='ignore').readlines()
    except OSError:
        continue
    scanned += 1
    for line in lines:
        m = impl_re.match(line)
        if m:
            our_classes.add(m.group(1))
    for i, line in enumerate(lines, 1):
        if '[' not in line and '@selector' not in line:
            continue
        stripped = line.strip()
        if 'respondsToSelector' in line or '@selector(' in line or stripped.startswith('//'):
            continue
        sys_sels = set()
        for m in class_send_re.finditer(line):
            sys_sels.add(m.group(2))
        for sel in sys_sels & cands:
            hits.setdefault(sel, []).append(f"{rel}:{i}: {stripped[:90]}")
        for m in any_send_recv_re.finditer(line):
            recv, sel = m.group(1), m.group(3)
            if sel not in cands or sel in sys_sels:
                continue
            if recv in ('self', 'super') or recv in our_classes or recv.startswith(
                    ('ns', 'moz', 'MOZ', 'Gecko', 'Child', 'Toolbar', 'Base', 'Pixel', 'Native', 'Web')):
                continue
            unresolved.setdefault(sel, []).append(f"{rel}:{i}: {stripped[:90]}")

new_hits = {s: v for s, v in hits.items() if s.split(':')[0] not in safe and s not in safe}
known_hits = {s: v for s, v in hits.items() if s in safe or s.split(':')[0] in safe}

with open(report_path, 'w') as r:
    r.write(f"selector-sweep report — sources scanned: {scanned} ObjC files\n")
    r.write(f"candidates absent from 10.6 runtime: {len(cands)}; "
            f"system-class send-sites: {len(hits)}; "
            f"already in safe list: {len(known_hits)}; "
            f"NEW needing classification: {len(new_hits)}; "
            f"UNRESOLVED variable-receiver sends: {len(unresolved)}\n\n")
    r.write("== NEW UNGUARDED CANDIDATES (system-class sends) ==\n")
    for sel in sorted(new_hits):
        r.write(f"### {sel}\n")
        for site in new_hits[sel][:5]:
            r.write(f"    {site}\n")
        if len(new_hits[sel]) > 5:
            r.write(f"    (+{len(new_hits[sel])-5} more)\n")
        r.write("\n")
    r.write("\n== UNRESOLVED (variable receivers — classify by receiver type) ==\n")
    for sel in sorted(unresolved):
        r.write(f"### {sel}\n")
        for site in unresolved[sel][:3]:
            r.write(f"    {site}\n")
        if len(unresolved[sel]) > 3:
            r.write(f"    (+{len(unresolved[sel])-3} more)\n")
    r.write("\n-- classified-as-safe this run (in selector-sweep-safe.txt) --\n")
    for sel in sorted(known_hits):
        r.write(f"    {sel}  [{safe.get(sel) or safe.get(sel.split(':')[0])}]\n")

print(open(report_path).read()[:2400])
print(f"[sweep] full report: {report_path}")
print(f"[sweep] NEW UNGUARDED CANDIDATES: {len(new_hits)}")
PYEOF
rm -f /tmp/sweep-ours.$$ /tmp/sweep-files.$$
