# Bug #5: -Rcode corruption on tiny / C++-static-heavy binaries — root-caused

Investigation 2026-08-09. Full writeup + evidence:
`~/prog/stabilizer-bug5/NOTES.md`, `~/prog/stabilizer-bug5/logs/`
(incl. `codex-verdict.txt`); reproducer `repro/teardown.cpp`.

**Deterministic reproducer:** a C++ file with a global object whose
destructor + an atexit handler call instrumented functions, built with
`szc -Rcode`, aborts 100%. Trigger characterised: **C++ static objects +
small, tightly-packed functions** — corruption is baked into the linked
layout, not a runtime race and not relocation size. libquantum escapes it
(large, well-spaced functions).

**Initial hypothesis REFUTED:** lld GOT/PLT relaxation desyncing
`--emit-relocs` from final bytes — objdump -dr and readelf --relocs agree
byte-for-byte. The bug is entirely in Stabilizer's own runtime, not the
linker. (Refuted by evidence, not assumed.)

**Three defects, each confirmed by runtime instrumentation + static
disasm + codex adversarial review (all CONFIRMED):**
- **5a — applyTextRelocs internal/external test keys on the wrong value.**
  Tests ELF symbol value `S = oldVal+oldP-addend` for containment, but a
  function's refs to its own adjacent relocation table are emitted vs the
  `.text` section symbol + large addend, so `S` is the section base
  (outside the function) while the true target `S+A = oldVal+oldP` is the
  adjacent table (inside). Internal refs misclassified external →
  rewritten → wild read. **FIX APPLIED + committed** (runtime/Function.cpp,
  test `oldVal+oldP`); codex flagged a ~4-byte x86 boundary caveat,
  documented in-code. Correct and safe, but **insufficient alone**.
- **5b — FunctionHeader (32 B) overwrites the next function.** The runtime
  writes a 32-byte header (Function* _f at +24) at every entry, but tiny
  CRT/C++ thunks are 16 B apart, so one header's _f clobbers the
  neighbour's first instruction. Proximate cause of the archived
  "Text relocation overflow off=7" abort. Structural.
- **5c — code.size() from a non-adjacent dummy.** codeLimit is the
  pass-inserted `stabilizer.dummy.<f>`, assumed physically adjacent, but
  codegen/linker reorder it; when the dummy lands before the function,
  code.size() underflows to a huge size_t → allocation abort. Structural.

**Fix status / disposition:**
- 5a: committed in the bug5 repo. Real correctness fix; does not by itself
  make the repro pass (5b/5c remain).
- 5b, 5c: **design-assumption violations, no small/safe fix.** 5b wants the
  Function* stored out-of-line (not a 32 B in-entry header), or guaranteed
  ≥header-size spacing; 5c wants code extent derived from actual symbol
  size / the relocation table, not an adjacent dummy. A 64-B alignment
  hack fixed 5b but fatally activated 5c → reverted (recorded so it is not
  re-tried blindly).

**Not a PR yet:** shipping 5a alone fixes no observable symptom (repro
still crashes on 5b/5c). Bug #5 is a characterised, still-open structural
limitation of the port's -Rcode on small/packed C++ code. Options: (a)
report to parsa as an ISSUE with this analysis + the 5a fix offered; (b)
do the 5b/5c redesign (out-of-line header + real code-extent) then a PR;
(c) hold. Decision pending (issue text would need approval before posting).
