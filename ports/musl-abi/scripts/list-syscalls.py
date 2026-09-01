#!/usr/bin/env python3
import re, sys
from pathlib import Path

p=Path(sys.argv[1] if len(sys.argv)>1 else "src/kernel/main.c")
s=p.read_text(errors="replace")
a=s.find("void syscall_dispatch")
b=s.find("static int pipe_read_into_task", a)
if a < 0 or b < 0:
	raise SystemExit("syscall_dispatch not found")
d=s[a:b]
for n, expr in re.findall(r"case\s+(\d+):\s+ret=([^;]+);", d):
	print(f"{n:>3}  {expr.strip()}")
