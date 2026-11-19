#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Failure-tolerant allocators must not reach the panicking malloc, checked on the LINKED firmware.

Album art (art_decode) and FatFs scratch (ff_memalloc / ff_memfree) allocate memory they can do without, so they must get
NULL back on exhaustion, never a panic. On the RP2350 the SDK links malloc() through --wrap and the wrapper panics, and the
newlib heap is ~2 KB. Both once called malloc() because a source-level `#ifdef PICO_ON_DEVICE` guard was dead in the app
library (the macro is only defined for SDK targets), and the device panicked with "Out of memory" on a 114 KB cover.

This walks the call graph of the ELF from those functions and fails if malloc, calloc, realloc, free (or the SDK's
__wrap_* / newlib _r twins) is reachable. The walk stops at os_alloc / os_free (the sanctioned allocator: it ends in the
kernel heap's pvPortMalloc, whose failure hook os_alloc silences) and does not enter the kernel's own allocator.

usage: check_app_heap.py <objdump> <elf> [--verbose] [--root=function ...]
       check_app_heap.py --text <disassembly.txt> [--root=function ...]      (test fixtures: tests/assets/closure/)
Exit 0 = ok, 1 = violation, 2 = a root function is missing (the check would be vacuous: fail rather than pass).
"""
import re, subprocess, sys
from collections import deque

ROOTS = ["art_decode", "ff_memalloc", "ff_memfree"]
STOP = re.compile(r"^(os_alloc|os_free|pvPortMalloc|vPortFree)$")
FORBIDDEN = re.compile(r"^(malloc|calloc|realloc|free|_malloc_r|_calloc_r|_realloc_r|_free_r|aligned_alloc|memalign|"
                       r"__wrap_malloc|__wrap_calloc|__wrap_realloc|__wrap_free|__real_malloc|__real_calloc|__real_realloc|__real_free)$")

def main():
    verbose = "--verbose" in sys.argv
    if sys.argv[1] == "--text":
        text = open(sys.argv[2]).read()
    else:
        text = subprocess.run([sys.argv[1], "-d", "--no-show-raw-insn", sys.argv[2]], capture_output=True, text=True, check=True).stdout
    funcs, body, cur = {}, {}, None
    head = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
    for line in text.splitlines():
        m = head.match(line)
        if m:
            cur = m.group(2)
            funcs[cur] = int(m.group(1), 16)
            body[cur] = []
        elif cur is not None and re.match(r"^\s*[0-9a-f]+:", line):
            body[cur].append(line)
    by_addr = {}
    for n, a in funcs.items():
        by_addr.setdefault(a, n)
    ref = re.compile(r"<([A-Za-z_.$][\w.$]*)(?:\+0x[0-9a-f]+)?>")
    word = re.compile(r"\.word\s+0x([0-9a-f]+)")
    edges = {}
    for n, lines in body.items():
        out = set()
        for ln in lines:
            for m in ref.finditer(ln):
                if m.group(1) != n:
                    out.add(m.group(1))
            m = word.search(ln)
            if m:
                t = by_addr.get(int(m.group(1), 16) & ~1)
                if t is not None and t != n:
                    out.add(t)
        edges[n] = out
    extra = [a[7:] for a in sys.argv if a.startswith("--root=")]
    wanted = extra or ROOTS
    missing = [r for r in wanted if r not in funcs]
    if missing:
        print("check_app_heap: root function(s) not found in the image: %s (renamed or optimised out? update ROOTS)" % ", ".join(missing))
        return 2
    parent = {r: None for r in wanted}
    q = deque(wanted)
    bad = []
    while q:
        f = q.popleft()
        for g in sorted(edges.get(f, ())):
            if g not in funcs:
                continue
            if FORBIDDEN.match(g):
                bad.append((f, g))
                continue
            if STOP.match(g) or g in parent:
                continue
            parent[g] = f
            q.append(g)
    print("app heap: %d functions reachable from %s" % (len(parent), ", ".join(wanted)))
    if verbose:
        for n in sorted(parent):
            print("   ", n)
    if bad:
        print("APP HEAP RULE VIOLATION: a failure-tolerant allocator reaches the panicking malloc (use os_alloc/os_free)")
        for f, g in bad:
            chain, x = [], f
            while x is not None:
                chain.append(x)
                x = parent[x]
            print("   %s  <-  %s" % (g, "  <-  ".join(chain)))
        return 1
    print("app heap: ok (art and FatFs scratch never reach malloc)")
    return 0

sys.exit(main())
