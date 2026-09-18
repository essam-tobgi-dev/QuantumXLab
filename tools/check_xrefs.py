#!/usr/bin/env python3
"""Cross-reference checker for specs/ and docs/theory/.
Finds references of the form `NN §X`, `NN §X.Y`, `TNN §X`, `NN §X–Y`, `NN §X, §Y` and verifies
that the target document exists and contains a numbered heading X (## X. …) or X.Y (### X.Y …).
Descriptive references (`T05 §transmon Hamiltonian`) are listed separately."""
import re, sys, glob, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
files = {}
for p in glob.glob(f'{root}/specs/*.md') + glob.glob(f'{root}/docs/theory/*.md'):
    b = os.path.basename(p)
    m = re.match(r'(T?\d\d)-', b)
    if m: files[m.group(1)] = p
heads = {}
for k, p in files.items():
    hs = set()
    for line in open(p, encoding='utf8'):
        m = re.match(r'^(#{2,4})\s+(?:§\s*)?(\d+(?:\.\d+)*)[\.\s—-]', line)
        if m: hs.add(m.group(2))
    heads[k] = hs
ref_re = re.compile(r'`?(T?\d\d)\s*§\s*([0-9][0-9.]*(?:\s*(?:[–-]|,\s*§?)\s*[0-9][0-9.]*)*)`?')
desc_re = re.compile(r'\b(T\d\d)\s*§\s*([A-Za-z][A-Za-z0-9 /\-]{2,40})')
bad, desc, missing_doc = [], [], []
for k, p in files.items():
    for n, line in enumerate(open(p, encoding='utf8'), 1):
        for m in ref_re.finditer(line):
            doc, secs = m.group(1), m.group(2)
            if doc not in files:
                missing_doc.append((k, n, doc)); continue
            for s in re.split(r'\s*(?:[–-]|,\s*§?)\s*', secs):
                s = s.rstrip('.')
                if not s: continue
                if s not in heads[doc] and s.split('.')[0] not in heads[doc]:
                    bad.append((k, n, doc, s))
                elif s not in heads[doc]:
                    bad.append((k, n, doc, s + ' (top-level exists, subsection missing)'))
        for m in desc_re.finditer(line):
            desc.append((k, n, m.group(1), m.group(2)))
print(f'documents: {len(files)}')
print(f'\n== missing target documents ({len(missing_doc)})')
for x in missing_doc: print(f'  {x[0]}:{x[1]} -> {x[2]}')
print(f'\n== unresolved numbered references ({len(bad)})')
for x in bad: print(f'  {x[0]}:{x[1]} -> {x[2]} §{x[3]}')
print(f'\n== descriptive references to convert ({len(desc)})')
for x in desc: print(f'  {x[0]}:{x[1]} -> {x[2]} §{x[3]}')
sys.exit(1 if bad or missing_doc else 0)
