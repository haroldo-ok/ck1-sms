# perf/lines.py prof.txt func [top]: attribute one function's profile buckets to C source lines
import re, sys, bisect
prof, fn = sys.argv[1], sys.argv[2]; top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
rows = []; cur = None; start = end = None
for line in open('build/main.rst'):
    if ';src/main.c:' in line:
        cur = line[line.index(';src/main.c:') + 12:].strip(); continue
    m = re.match(r'\s+([0-9A-F]{6})\s+([0-9A-F]{2}\s)', line)
    if m and cur: rows.append((int(m.group(1), 16), cur))
    lab = re.match(r'\s+([0-9A-F]{6})\s+\d+\s+_(\w+):', line)
    if lab:
        a = int(lab.group(1), 16)
        if lab.group(2) == fn: start = a
        elif start is not None and end is None and a > start and not lab.group(2).startswith(fn): end = a
rows.sort(); keys = [r[0] for r in rows]; acc = {}
for line in open(prof):
    a, c = line.split(); a = int(a, 16); c = int(c)
    if start is None or not (start - 32 < a < end): continue
    for sub in range(a, a + 32, 4):
        if not (start <= sub < end): continue
        j = bisect.bisect_right(keys, sub) - 1
        if j >= 0: acc[rows[j][1]] = acc.get(rows[j][1], 0) + c // 8
print('range %04X-%04X' % (start, end))
for k, v in sorted(acc.items(), key=lambda kv: -kv[1])[:top]: print('%9d  %s' % (v, k[:120]))
