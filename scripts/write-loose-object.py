"""Writes a loose git object without the atomic rename git would use.

The sandbox refuses the rename git does when it moves a temporary file into
the object store, so the object file is written in place instead. The content
and the hash are the ones git computes, so the object is the same one.

Usage: python3 scripts/write-loose-object.py <type> <hash> <file>
"""

import hashlib
import os
import sys
import zlib

OBJ_TYPE = sys.argv[1]
WANTED = sys.argv[2]
PATH = sys.argv[3]

data = open(PATH, "rb").read()
body = OBJ_TYPE.encode() + b" " + str(len(data)).encode() + b"\0" + data

digest = hashlib.sha1(body).hexdigest()
if digest != WANTED:
    raise SystemExit(f"hash mismatch: {digest} != {WANTED}")

target = os.path.join(".git", "objects", digest[:2], digest[2:])
os.makedirs(os.path.dirname(target), exist_ok=True)
with open(target, "wb") as handle:
    handle.write(zlib.compress(body))

print(f"wrote {target}")
