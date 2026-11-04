#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Core-1 rules check on the LINKED firmware: everything reachable from the audio core's entry points must not
reach the RTOS, stdio, the heap, blocking SDK primitives or the panic path (docs/adr/0011-bare-metal-audio-core.md).

The token scan in cmake/check_core1_rules.cmake only sees our own sources; vendored decoders (dr_flac, dr_mp3, ALAC)
and SDK inlines make a call-graph check on the final ELF the real test. It disassembles the ELF, follows direct
branches and every function address found in literal pools (function pointers: callbacks, interrupt handlers),
starting from the roots, and fails if a forbidden function is reachable.

LIMITS (read before trusting a pass): a function whose address is only ever computed (not stored as a literal-pool
word or named by a branch) is not seen; calls through a pointer are followed only to functions whose address appears as
a pool word somewhere in reachable code, which over-approximates (safe) for callbacks but cannot know what a pointer
variable holds at run time. Direct tail calls are branches and are followed. It checks symbols, not behaviour: the
IGNORED_EDGES table lists the few SDK paths accepted by argument.

usage: check_core1_closure.py <objdump> <elf> [--verbose] [--root=function]   (--root only to prove the check can fail)
       check_core1_closure.py --text <disassembly.txt> [--root=function]      (test fixtures: tests/assets/closure/)
"""
import re, subprocess, sys
from collections import deque

ROOTS = ["audio_core_main", "dma_isr", "on_block"]
FORBIDDEN = re.compile(
    r"^(malloc|calloc|realloc|free|_malloc_r|_free_r|_realloc_r|_calloc_r|aligned_alloc|memalign|"
    r"printf|fprintf|vprintf|vfprintf|iprintf|puts|putchar|putc|fputs|fputc|fwrite|fread|fopen|fclose|fflush|"
    r"sprintf|snprintf|vsnprintf|_printf_r|_vfprintf_r|"
    r"abort|exit|_exit|__assert_func|__assert|panic|hard_assertion_failure|hal_panic|neupod_panic.*|"
    r"os_.*|x[A-Z][A-Za-z]+|v[A-Z][A-Za-z]+|pv[A-Z][A-Za-z]+|"
    r"mutex_.*|sem_.*|critical_section_.*|queue_.*|lock_core.*|recursive_mutex_.*|"
    r"sleep_.*|busy_wait_.*|best_effort_wfe_or_timeout|"
    r"multicore_lockout.*|multicore_fifo.*|alarm_pool_.*|add_alarm_.*|add_repeating_timer.*|"
    r"hal_delay_ms|hal_status_led|uart_.*|stdio_.*|cyw43_.*|"
    r"__wrap_malloc|__wrap_calloc|__wrap_realloc|__wrap_free)$")

# Edges that exist in the object code but are not taken on our paths, each with the reason. (caller -> callee regex)
# Matching is by function NAME in the linked ELF: a static helper elsewhere with the same name would be merged with the
# SDK one and covered by the same entry.
IGNORED_EDGES = [
    (r"^(hw_claim_unused_from_range|hw_claim_or_assert|hw_claim_lock)$", r"^panic$",
     "SDK claim helpers panic only when called with required=true; hal_audio.c claims with required=false and checks the result"),
    (r"^(irq_set_exclusive_handler|irq_add_shared_handler|irq_remove_handler)$", r"^panic$",
     "SDK interrupt handler registration panics only on a double registration, which hal_audio.c rules out"),
    (r"^(irq_set_exclusive_handler|irq_add_shared_handler|irq_remove_handler|hw_claim_unused_from_range|hw_claim_or_assert|"
     r"pio_sm_init|pio_add_program|pio_remove_program|dma_channel_configure|gpio_init|gpio_set_function)$", r"^hard_assertion_failure$",
     "SDK hard_assert on programmer errors (bad arguments, double registration), not reachable with the arguments hal_audio.c passes"),
    (r"^(irq_set_exclusive_handler|irq_remove_handler|pio_remove_program|channel_config_set_chain_to|channel_config_set_dreq|"
     r"hw_claim_clear|insert_bl_32|make_j_16|resolve_j_16|find_offset_for_program|irq_add_tail_to_free_list)$", r"^__assert_func$",
     "SDK plain assert() on programmer errors (argument range, 'not found' on teardown), compiled in only when NDEBUG is unset "
     "(Debug builds; Release has none). Same class as the hard_assert entry above; the callers are listed one by one so an "
     "assert() in our own or vendored code (hal_audio.c, decoders) is still a violation"),
]

def main():
    verbose = "--verbose" in sys.argv
    if sys.argv[1] == "--text":
        text = open(sys.argv[2]).read()
    else:
        objdump, elf = sys.argv[1], sys.argv[2]
        text = subprocess.run([objdump, "-d", "--no-show-raw-insn", elf], capture_output=True, text=True, check=True).stdout
    funcs = {}      # name -> address
    body = {}       # name -> list of lines
    cur = None
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
            if m:  # literal pool word: a function address (thumb bit set) is a function pointer
                v = int(m.group(1), 16)
                t = by_addr.get(v & ~1)
                if t is not None and t != n:
                    out.add(t)
        edges[n] = out
    extra = [a[7:] for a in sys.argv if a.startswith("--root=")] # for testing the checker itself
    roots = [r for r in (extra or ROOTS) if r in funcs]
    if not roots:
        print("check_core1_closure: root function not found in the ELF"); return 2
    parent = {r: None for r in roots}
    q = deque(roots)
    bad = []
    while q:
        f = q.popleft()
        for g in sorted(edges.get(f, ())):
            if g not in funcs:
                continue
            ignored = any(re.match(a, f) and re.match(b, g) for a, b, _ in IGNORED_EDGES)
            if FORBIDDEN.match(g) and not ignored:
                bad.append((f, g))
                continue
            if ignored or g in parent:
                continue
            parent[g] = f
            q.append(g)
    print("core-1 closure: %d functions reachable from %s" % (len(parent), ", ".join(roots)))
    if verbose:
        for n in sorted(parent):
            print("   ", n)
    if bad:
        print("CORE-1 RULE VIOLATION: forbidden function reachable from the audio core")
        for f, g in bad:
            chain, x = [], f
            while x is not None:
                chain.append(x); x = parent[x]
            print("   %s  <-  %s" % (g, "  <-  ".join(chain)))
        return 1
    print("core-1 closure: ok (no RTOS, stdio, heap, blocking SDK or panic path reachable)")
    return 0

sys.exit(main())
