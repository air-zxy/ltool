import os
import sys

base = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
for sub in ("", os.path.join("d1"), os.path.join("d1", "d2"), os.path.join("d2")):
    os.makedirs(os.path.join(base, sub), exist_ok=True)
payload = b"x" * 512
for i in range(n):
    if i % 4 == 0:
        dir_ = os.path.join(base, "d1")
    elif i % 4 == 1:
        dir_ = os.path.join(base, "d1", "d2")
    elif i % 4 == 2:
        dir_ = os.path.join(base, "d2")
    else:
        dir_ = base
    with open(os.path.join(dir_, "f%05d.txt" % i), "wb") as f:
        f.write(payload)
print("made %d files under %s" % (n, base))
