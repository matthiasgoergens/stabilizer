# Adjudication: 4 cross-model reviews of the Parsa PR (2026-08-08)

codex + DeepSeek, each on the code and the description, per Matthias's
request. Raw verdicts: `codex-pr-code-review.txt` (clean),
`codex-pr-desc-verdict.txt` (POST-WITH-EDITS + split),
`deepseek-pr-code-verdict.txt` (4 findings), `deepseek-pr-desc-verdict.txt`
(POST-WITH-EDITS + split, one unverifiable claim).

## Code: one family disagreement, resolved by measurement

- **Classification mismatch (DeepSeek Finding 1, CONFIRMED) vs codex
  (REFUTED).** DeepSeek: `getSize(ptr)` could exceed MaxSize for a
  request ≤ MaxSize (header/rounding), so malloc shuffles but the guard
  bypasses free → stale shuffle-slot → double alloc/free. **Resolved
  FALSE by measurement**, not by preferring a family: the stress agent's
  exhaustive 1..300 size sweep found 0 malloc/free-decision disagreements
  (`stabilizer-stress/NOTES.md:61`), and the analytical reason is clean —
  MaxSize=256 is a power-of-two Kingsley class boundary, so
  `class2Size(size2Class(r)) ≤ 256 ⟺ r ≤ 256`, and ANSIWrapper's
  16-rounding of any r≤256 reaches at most 256. DeepSeek assumed Kingsley
  getSize returns request+header (272 for 256); it returns the class size
  (256). Codex correct.
- **Signal reentrancy (Finding 2) and per-TU RNG state (Finding 3):**
  both TRUE, both **pre-existing in the 2013 original**, neither
  introduced by these five commits. Not PR blockers; roadmap/threads
  items. The per-TU RNG point (each translation unit gets its own
  `getRandomByte` static `_rng`) is a genuine quality issue worth a
  ROADMAP note. The PR must not claim to fix either.
- **Code-heap starvation (Finding 4): REFUTED by DeepSeek itself**, agrees
  with codex.
- codex holistic code review: clean, no findings; independently
  re-confirmed the cursor reset and free-side routing.

Net: the five commits are sound as written. No code change needed for the
PR; the two pre-existing limitations are disclosed as roadmap, not fixed
here.

## Description: POST-WITH-EDITS (both families), and SPLIT (unanimous)

Edits accepted (mostly codex, all fair against evidence):
1. "three bugs" → "three crash manifestations, two root defects".
2. Don't say sustained runs crash in all three modes — `-Rheap` crashes
   before the first timer tick; only `-Rcode`/`-Rstack` are epoch-driven.
3. gdb `reqSz=512` is the freed OLD block during a realloc to 1024 — say
   "freeing a 512-byte old block during the first large realloc".
4. Attribute the heap bug as "a missing free-side bypass in the modern
   API composition", not "a bug in modern DieHard"; drop the
   dependency-pinning argument from the PR body.
5. RNG: drop "roughly" (exact per the harness); ".bss contents" →
   "out-of-bounds reads into adjacent storage, potentially unmapped".
6. Drop "never crashed in 2013 because always-mapped .bss" — unsupported.
7. Drop the stack-randomness-degradation / paper-ablation paragraph — the
   exclusivity is verified (only two stack-pad call sites, grep of the
   original runtime) but "substantially degraded / affects the paper" is
   stronger than measured and is our research finding, not PR material.
8. State the port and original results independently — no cross-era
   "matches the original" implication (no cross-era diff was run).
9. `-Rheap` is not epoch-gated (169 timer ticks, continuous shuffling) —
   don't call them heap epochs.
10. "each commit carries gdb evidence" is false — the two RNG micro-fixes
    are harness-verified, not gdb.
11. Split the debugger caveat: gdb breakpoints (0xCC/trap collision) vs
    strace (ptrace/SIGTRAP) — two mechanisms.
12. Tone: open in the author's voice with the concrete action
    ("I've been testing the LLVM 21 port on libquantum…"), then thank
    Parsa; cut the investigation-report detail; offer logs separately;
    strip the internal preamble/posting-command lines.

**Split: two PRs (disjoint files, independent modes, distinct
provenance).**
- PR 1 (Heap.h): f9ed534 + 19137a3 + 6b263a4 — ShuffleFreeGuard, fixes
  `-Rheap` + `-Rcode`. Port/adaptation bug.
- PR 2 (Util.h): 29afeef + 24df701 — getRandomByte OOB + first-call
  refill, fixes `-Rstack`. Latent since 2013 (inherited from
  ccurtsinger); note that, and that ccurtsinger is itself unmaintained.
Each PR states which mode(s) it fixes so the maintainer isn't surprised
neither alone makes all three modes work.
