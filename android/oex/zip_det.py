#!/usr/bin/env python3
"""Deterministic APK zip. Usage: zip_det.py OUT.apk BASE_FROM_AAPT2.apk STAGE_DIR
Copies every entry of the aapt2 output plus every file under STAGE_DIR (classes.dex, lib/, assets/)
into OUT with fixed timestamps and fixed order; resources.arsc stored uncompressed (API 30+)."""
import sys, os, zipfile

out, base, stage = sys.argv[1:4]
DATE = (1981, 1, 1, 1, 1, 2)
entries = {}
with zipfile.ZipFile(base) as z:
    for n in z.namelist():
        if not n.endswith('/'):
            entries[n] = z.read(n)
for root, dirs, files in os.walk(stage):
    for f in files:
        p = os.path.join(root, f)
        n = os.path.relpath(p, stage).replace(os.sep, '/')
        if n in entries:
            sys.exit(f"duplicate entry {n}")
        with open(p, 'rb') as fh:
            entries[n] = fh.read()
order = sorted(entries, key=lambda n: (n != 'AndroidManifest.xml', n))
with zipfile.ZipFile(out, 'w') as z:
    for n in order:
        zi = zipfile.ZipInfo(n, DATE)
        zi.create_system = 3
        zi.external_attr = 0o100644 << 16
        if n == 'resources.arsc':
            zi.compress_type = zipfile.ZIP_STORED
            z.writestr(zi, entries[n])
        else:
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, entries[n], compresslevel=9)
print(f"{out}: {len(order)} entries")
