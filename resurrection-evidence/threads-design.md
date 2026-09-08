# Stabilizer runtime: a concurrency design for thread-safety

Status: design proposal (Phase 2 groundwork, see `/home/matthias/prog/stabilizers/stabilizer/ROADMAP.md`).
No runtime code changes are made by this document. Source grounding: the runtime
at `/home/matthias/prog/stabilizers/stabilizer-parsa-fix/stabilizer/runtime/` (files read in
full: `libstabilizer.cpp`, `Function.h`, `Function.cpp`, `FunctionLocation.h`,
`Heap.h`, `Heap.cpp`, `Context.h`, `Trap.h`, `Jump.h`, `Util.h`, `MemRange.h`,
`MMapSource.h`, `CodeWindow.cpp`, `TextRelocations.h`) and
`/home/matthias/prog/stabilizers/stabilizer/scoping-notes/runtime-analysis.md`.

Prior-art sources actually fetched and read (PDFs saved under `sources/`):

- **Shuffler** — David Williams-King et al., "Shuffler: Fast and Deployable
  Continuous Code Re-Randomization", OSDI 2016, pp. 367–381.
  `sources/shuffler-osdi16.pdf`.
- **TASR** — Bigelow, Hobson, Rudd, Streilein, Okhravi, "Timely
  Rerandomization for Mitigating Memory Disclosures", CCS 2015.
  `sources/tasr-ccs15.pdf`.
- **Morpheus** — Gallagher et al., "Morpheus: A Vulnerability-Tolerant Secure
  Architecture Based on Ensembles of Moving Target Defenses with Churn",
  **ASPLOS 2019** (not ISCA — the roadmap's citation is corrected below),
  pp. 469–484.
- **Linux kernel livepatch** — `docs.kernel.org/livepatch/{livepatch,
  system-state,reliable-stacktrace}.html`.
- **JIT safepoints** — general knowledge of HotSpot/V8 polling-page
  safepoints, plus Aleksey Shipilëv, "JVM Anatomy Quark #22: Safepoint
  Polls" (https://shipilev.net/jvm/anatomy-quarks/22-safepoint-polls/).
- **DieHard's own scalable allocator** (`stabilizer-parsa-fix/stabilizer/
  DieHard/src/include/{heapregistry,globalfreepool,util/atomicbitmap}.h`) —
  found in the same repository family (same author, Emery Berger, same Heap
  Layers lineage as Stabilizer's `Heap.h`), a directly-available second
  source for the registry/heap sub-problem, separate from the three
  re-randomization papers.

All three re-randomization papers were fetched and read directly (not
summarized from memory); section/figure/page citations below are from the
actual PDF text. Anything not tied to such a citation is marked **[inference]**.

**Correction to the roadmap text**: Morpheus is ASPLOS 2019, not ISCA 2019,
and Torrellas is not an author (verified by reading the paper's title page).
The roadmap should be corrected when this is folded back in.

---

## 1. Concurrency hazards, mapped to exact code

The runtime is single-threaded by construction: one process-wide `ITIMER_REAL`,
unlocked global `std::set`s, one global stack-walk anchor. Below, each hazard
is named, pinned to the code that is unsafe, and rated by what actually goes
wrong (crash vs. corruption vs. silent weakening).

### H1 — Unlocked global registries mutated from arbitrary signal handlers

- `std::set<Function*> functions`, `std::set<Function*> live_functions`,
  `std::set<uint8_t*> stack_pads` — `libstabilizer.cpp:30-33`.
- `live_functions.insert(f)` — `libstabilizer.cpp:227`, inside `onTrap`,
  which runs on **whichever thread took the trap**.
- `FunctionLocation::getRegistry()` (a function-local static
  `std::set<FunctionLocation*>`) — inserted at `FunctionLocation.h:52` (every
  relocation, i.e. every trap hit on every thread), iterated/erased at
  `FunctionLocation.h:94-108` (`sweep()`, called from `onTrap`,
  `libstabilizer.cpp:219`).

Two threads trapping "simultaneously" (a common case: two threads both
calling the same hot instrumented function) call `std::set::insert` on the
same set concurrently with no lock. This is a plain data race on
red-black-tree rebalancing — undefined behaviour, not just a logic bug;
in practice, heap/tree corruption and eventual crash.

### H2 — One global `topFrame`, one stack walked, for however many threads exist

- `void** topFrame` — `libstabilizer.cpp:38`, set once from the **registering
  thread's** frame address at `libstabilizer.cpp:54`.
- Mark phase: `Stack s = c.stack(); while(s.fp() != topFrame) { ...; s++; }`
  — `libstabilizer.cpp:206-210`, `Context.h:102-104` (`c.stack()` starts from
  `_c->uc_mcontext...RBP` of **whichever thread's signal context this is**).

If the trap (and hence the mark phase) fires on a sibling thread, that
thread's frame-pointer chain lives on a completely different stack region
and will **never** reach the value stored in `topFrame`. The `while`
condition never becomes true; the loop walks off the top of that thread's
real call stack into whatever memory follows (thread stacks are typically
adjacent to guard pages from `pthread_create`), producing either an
immediate SIGSEGV *inside a signal handler* (fatal — no handler for that) or,
if it doesn't immediately fault, treating garbage stack contents as return
addresses to mark, which can spuriously "save" or fail to save arbitrary
`FunctionLocation`s.

### H3 — `rerandomizing` is a racy plain bool, and can trigger concurrent mark/sweep passes

- `bool rerandomizing = false;` — `libstabilizer.cpp:35`; write in `onTimer`
  (`:264`), read-and-clear in `onTrap` (`:202`, `:221`).

Not `std::atomic`, no barrier. Under threads this is doubly bad: (a) it is a
textbook data race (UB under the C++ memory model, whether or not it
"happens to work" on x86's strong memory ordering), and (b) if two sibling
threads both observe `rerandomizing == true` before either clears it, **both**
independently run the H2-broken mark phase and then both call
`FunctionLocation::sweep()` (H1) concurrently on the same shared registry —
concurrent iterator use plus concurrent `delete`, i.e. a second, independent
route to heap corruption on top of H1.

### H4 — The deep one: freeing code a sibling thread may be executing right now

- `FunctionLocation::sweep()` — `FunctionLocation.h:94-108` — for every
  location that is `_defunct && !_marked`, calls `delete l`, whose destructor
  (`:55-57`) calls `getCodeHeap()->free(_memory.base())`, returning that
  memory to the shuffled code heap for reuse by a subsequent `malloc()`
  **from any thread**.
- Mark only ever inspects *one* stack (H2), so a `FunctionLocation` a sibling
  thread's program counter or saved return address currently points into can
  be `!_marked` purely because nobody ever walked that thread's stack — not
  because it is actually unused.

Concretely: thread A jumps into function F's current location and is
mid-execution (or has a return address on its stack pointing back into
F's current location) when the maintenance-of-the-moment thread B runs
`sweep()`. Because B only walked its own stack, F's location looks unmarked,
gets `delete`d, and its memory becomes available to `getCodeHeap()->malloc()`
for an unrelated allocation — while thread A's instruction pointer or a
future `ret` may land in memory that has since been overwritten with
something else entirely. This is exactly the hazard Shuffler's OSDI'16
abstract names as its core contribution to solve (see §2).

### H5 — Non-atomic, torn in-place rewriting of code a sibling thread may be fetching from

- `Function::forward()` — `Function.h:70-73` — placement-`new`s a `Jump`
  (`X86Jump32` = 5 bytes; `X86Jump64` = 21 bytes, `Jump.h:10-42`) directly
  over the function's **original entry bytes** at `_code.base()`, the address
  every caller in the program still calls.
- `Function::setTrap()` — `Function.h:133-135` — placement-`new`s a 1-byte
  `int3` (`Trap.h:8-18`) over the same live entry bytes, called from
  `onTimer` for every function in `live_functions` (`libstabilizer.cpp:258`)
  and once at startup for every registered function (`:131`).

Unlike a JIT that redirects through a table, Stabilizer's forwarding
mechanism overwrites bytes **inside the instruction stream at the address
every caller already calls**. `X86Jump64` (`Jump.h:21-42`) is a 21-byte,
5-field, non-atomic sequence written with five separate stores. If a sibling
thread executes a `call` into this function while another thread's
`forward()`/`setTrap()` is mid-write, the fetching thread can observe a torn,
semantically meaningless instruction sequence — not merely stale data, but
an invalid instruction fetch, since x86 gives no atomicity guarantee across
a multi-instruction, multi-store code patch this large. (The 1-byte `int3`
write in `setTrap()` is at least byte-atomic; the multi-byte jump write in
`forward()` is not.)

### H6 — `FunctionLocation::mark()`/`_marked` field races

`FunctionLocation.h:87-92`, `:104` — `_marked` is a plain `bool` field
written by every thread's mark phase and read/reset by `sweep()`, with the
same unsynchronized-access problem as H3, compounding once H2/H3 allow
concurrent passes.

### H7 — `getRandomByte()` global static state races

`Util.h:37-54` — function-local statics `_randCount`, and a `_rands`/`_bigRand`
union, mutated without synchronization. Called from `onTimer`'s stack-pad
refresh loop (`libstabilizer.cpp:245`) and `Function::relocate()`
(`Function.cpp:83`), both reachable from arbitrary trapping threads.
`runtime-analysis.md`'s addendum already documents this generator as
producing mostly-non-random output even single-threaded (a refill-counter
bug); under threads it additionally becomes a genuine data race on the
union's bytes (torn reads/writes), which is undefined behaviour on top of
already-bad randomness quality.

### H8 — Signal handlers allocate memory; a lock added for thread-safety creates a self-deadlock risk that doesn't exist today

- `onTrap` → `Function::relocate()` → `new FunctionLocation(...)`
  (`FunctionLocation.h:36`) → `getCodeHeap()->malloc(...)`.
- `onTrap` on first sight of a function → `new Function(...)`
  (`libstabilizer.cpp:154`) → `getDataHeap()->malloc(...)`.
- `Heap.h:66-67` composes `ShuffleHeap`/`KingsleyHeap`/`BumpAlloc` from Heap
  Layers with **no locking layer** — this is consistent with a
  single-threaded design, and is not documented as thread-safe.

Today (single-threaded) this is merely async-signal-unsafe in the abstract
(malloc inside a signal handler is always fragile) but doesn't self-deadlock,
because there's only one thread and it can't be interrupted by its own
signal while already inside that same call frame in a way that re-enters the
same lock (there is no lock). The moment H1/H4's fix adds a real lock around
the code/data heaps for thread-safety, this becomes a live self-deadlock
hazard: if a thread is itself inside `stabilizer_malloc` (holding the heap
lock) when the same thread receives `SIGTRAP` (e.g. because it just called
into another instrumented function) or `SIGALRM`, and the handler tries to
allocate on the same now-locked heap, it deadlocks against itself.

### H9 — Timer delivered to an arbitrary thread, conflated with "the" maintenance thread

`ITIMER_REAL`/`SIGALRM` is deliverable to any thread that hasn't blocked it
(POSIX process-wide semantics; `setTimer`, `libstabilizer.cpp:272-281`).
`onTimer`'s body (`:236-265`) unconditionally treats whichever thread it runs
on as authoritative for placing traps on every `live_functions` entry
(`:258`) and clearing/forwarding based on **that thread's own** `c.ip()`
(`:254-257`) — which conflates "the thread the OS happened to deliver
SIGALRM to" with "the thread whose call is currently at a trapped entry",
two unrelated things. This is a distinct hazard from H2/H5 (which are about
*what* is unsafe to do); H9 is about *which thread ends up doing it*, with no
guarantee of consistency epoch to epoch.

### H10 — Registration-time races if functions are registered after threads exist (`dlopen` from a live multithreaded program)

`stabilizer_register_function`/`_constructor`/`_stack_pad`
(`libstabilizer.cpp:153-164`) mutate `functions`/`constructors`/`stack_pads`
from module constructors. In the common case (static linking, all module
constructors run before `stabilizer_main`) this precedes any thread creation
and is safe today. If a target program `dlopen()`s a Stabilizer-instrumented
shared object *after* spawning threads (plausible for a Rust/C++ plugin
architecture), the `dlopen`-triggered constructors run on whatever thread
called `dlopen`, concurrently with other threads already inside `onTrap`
mutating the same sets (H1). Flagged as a secondary/deferred hazard — real,
but not the Phase 2 blocker; the fix (a mutex around registration + a check
for "not mid-epoch") is a small addition once H1's locking exists.

---

## 2. Prior-art mechanisms, mapped to each hazard

### H1 (registries) and H6 (mark bits)

**Shuffler's answer is architectural, not "add a lock": single-writer.**
Shuffler's code pointer table is mutated only by one dedicated maintenance
thread ("the Shuffler thread"); ordinary program threads never write it, only
read through it via `%gs`-relative indirection (Shuffler §3.2/4.1, Fig. 2–3,
pp. 370–372). The paper states table updates happen "asynchronously" and only
the stack-fixup step is synchronous (§3.2, p.370) — i.e. Shuffler doesn't
lock the table against concurrent writers because there is only ever one
writer. **[inference]**: the paper never states the table write's atomicity
explicitly; a single 8-byte aligned pointer write is atomic on x86-64 by ISA
guarantee, and single-writer removes the need to reason about write-write
races at all — this combination is almost certainly why the paper doesn't
discuss locking for it.

**Directly-available second source, already in this repository's file
tree**: DieHard's own scalable multithreaded allocator (`DieHard/src/include/
heapregistry.h`, `globalfreepool.h`, `util/atomicbitmap.h`, same Heap Layers
lineage as Stabilizer's `Heap.h`) solves the adjacent problem of *many
threads needing to allocate/free from shared heap state* with: per-thread
heap instances (no cross-thread contention on the hot path), a
`HeapRegistry` purely for cross-thread *ownership lookup*, and a
`GlobalFreePool` of batched cross-thread frees drained periodically by the
owning thread rather than freed synchronously by whichever thread called
`free()`. This is not one of the three re-randomization papers, but it is
directly on-point prior art for H8 (heap locking) specifically, from the
same codebase family.

**Kernel precedent** [general knowledge, not from a specific fetched
citation]: Linux livepatch's own bookkeeping structures (the list of
registered patches/objects) are protected by an ordinary `klp_mutex` plus
RCU for read-mostly traversal — i.e. real locks for the *management*
structure, with the interesting, novel part of livepatch being the per-task
transition protocol (H2/H4 below), not the registry locking. This suggests
registry locking itself is not where prior art needs to be stretched: an
ordinary mutex around `functions`/`live_functions`/`FunctionLocation`'s
registry is standard and sufficient, *if* it's taken by very few threads
doing very little work under it (which is what the single-writer
maintenance-thread model gives you for free).

### H2 and H4 (per-thread stack walk; freeing code a sibling thread may be executing) — the hard sub-problem

This is Shuffler's namesake contribution, and it is the single most load-bearing
citation for this design.

**Mechanism (Shuffler §3.2, §3.3, §4, Fig. 1–3, pp. 370–372, confirmed again
at §5.1.1 p.375):**

1. Code addresses are never taken directly; every static function pointer is
   rewritten at load time into an index into a **code pointer table**
   addressed via `%gs` (§3.2/4.1). Table entries — not the callers — change
   on each shuffle.
2. **Return addresses on the stack are real addresses, not indices** (ret/call
   fusion made indices too costly there), so they are instead **XOR-encrypted
   with a per-thread key at function entry/exit** (§3.3/4.1, Fig. 3b) so a
   disclosed stack can't be read as a usable code pointer.
3. When a shuffle happens, Shuffler "sends a signal to all threads (including
   itself); each thread unwinds and fixes up its stack. Shuffler waits on a
   barrier until all threads have finished unwinding, then erases the
   previous code copy." (§4, p.371). Each thread's own signal handler
   decrypts, translates old→new address, re-encrypts, and rewrites each of
   *its own* saved return addresses, using a **custom DWARF unwinder** written
   from scratch (§4.4, p.374 — `libunwind` was rejected as too unwieldy to
   extend for address translation).
4. Only after every thread has reported completion at the barrier does
   Shuffler perform a **bulk region erase** — the code sandbox is split in
   half; the stale half is reclaimed with one `mprotect` call (§4.4, p.374).
   Not per-object reference counting; a copying/semi-space-style bulk
   reclaim.
5. Overhead of the safety mechanism specifically: **3,247 stack frames
   unwound per millisecond** including barrier sync (§5.1.1, p.375); mean
   0.53ms per shuffle (gcc, worst normal SPEC case), worst case 6ms
   (`xalancbmk`, up to 45,000-deep stacks); headline claim: "program threads
   continue to execute unhindered 99.7% of the time" (Abstract, p.367;
   Fig. 6, p.375 — the synchronous barrier component is "barely visible"
   against asynchronous code-copy cost).
6. Explicitly stated limitations (§4.3.1 p.373-374, §4/§5.3, p.371/377):
   requires frame pointers *despite* being DWARF-based ("some SPEC CPU
   programs required `-fno-omit-frame-pointer`, due to a limitation in our
   DWARF unwind implementation"); no `dlopen`, no C++ exceptions; requires
   preserved relocations/symbols (`-Wl,-q`); JIT'd code must be disabled
   entirely (out of scope, "could in future handle JIT code if informed when
   new code chunks were generated," §5.3, p.377). **[inference, flagged by
   the paper's silence rather than a stated claim]**: the paper never
   discusses what happens if a target thread is blocked in a slow syscall or
   is otherwise slow to receive/handle the signal — an open gap, not an
   addressed limitation.
7. Explicitly **rejected** alternative, directly on point for the "per-thread
   copies" axis of this design's own tradeoff table: "a thread-local
   (`%fs`-based) per-thread [code layout] variant would be fairly
   straightforward" but was not implemented, trading parallel-disclosure
   resistance for lower memory/CPU cost (§6.2, p.379). Shuffler shares one
   layout across all threads by design.

**How this maps onto Stabilizer, concretely (§3):**
- H2 (single `topFrame`) → give every thread its own `topFrame`, captured at
  thread start exactly as `main()` already captures it for the primary
  thread (`libstabilizer.cpp:54`) — the direct analogue of Shuffler's
  "each thread unwinds ... using its own stack" (§3.2).
- H4 (freeing code a sibling thread is using) → gate `sweep()`'s `delete`
  (and hence `getCodeHeap()->free()`) on a barrier that only completes once
  every live thread has independently walked its own stack and reported in
  — the direct analogue of "waits on a barrier ... then erases" (§4, p.371).

**TASR's safe-point strategy — a different axis, weaker fit for this
specific hazard.** TASR's safe point is not "any syscall": specifically a
`read()`-family call **following** a `write()`-family call (§4.3, Table 1;
exact syscall list: `write, pwrite64, writev, sendto, sendmsg,
mq_timedsend, pwritev, sendmmsg` as outputs, `read, pread64, readv, recvfrom,
recvmsg, mq_timedreceive, preadv, recvmmsg` plus `fork`/`vfork` as inputs).
The safety argument is about *attacker capability*, not memory-model safety:
"the minimum interval to carry out an attack is the time between the most
recent output and the following input... rerandomizing between them makes a
leak stale before it's usable" (§4.1). Multi-threading is addressed only in
one paragraph (§4.3): rerandomization is triggered by the aggregate I/O
sequence across the whole process (indeed process group, for correlated
processes), not per-thread. Critically, **the paper never describes what
happens to a compute-bound sibling thread holding live code pointers in
registers or on its stack while a different thread's syscall triggers the
kernel-injected pointer-updater** — this silence is itself informative: TASR
sidesteps exactly the hazard Stabilizer must solve (H4) by construction,
because its threat model is single-thread-does-I/O, not
concurrent-compute-while-relocating. TASR's fixup mechanism (§4.2/4.4) does
supply useful adjacent ideas though: a **level of indirection through the
GOT** for cross-compilation-unit references (so only one pointer-sized GOT
entry needs updating per rerandomization, not every call site) — this is a
second, independent source converging on the same "add indirection, update
one word" idea used for H5 below, which strengthens confidence that's the
right general mechanism rather than a Shuffler-specific quirk. Overhead
figure for context: 2.1% mean / 10.1% max CPU on SPEC (§5.1, Fig. 3), framed
as cheap because the userspace/kernel context-switch cost was "already paid"
by the triggering syscall.

**Livepatch's per-task lazy consistency — the lock-free/incremental
alternative to Shuffler's stop-the-world barrier.** Read from
`docs.kernel.org/livepatch/livepatch.html`: livepatch is "a hybrid of kGraft
and kpatch: it uses kGraft's per-task consistency and syscall barrier
switching combined with kpatch's stack trace switching." Concretely:

- **Stack checking (kpatch-derived)**: for a sleeping task, walk its kernel
  stack; if no affected function is on the stack, the task can transition
  immediately.
- **Kernel-exit switching (kGraft-derived)**: otherwise, a task transitions
  lazily "when it returns to user space from a system call, a user space
  IRQ, or a signal" — tracked with a per-task `TIF_PATCH_PENDING` flag,
  checked at a natural quiescent point rather than forced.
- **Idle-task patching**: idle tasks get switched in the idle loop.
- Per `docs.kernel.org/livepatch/reliable-stacktrace.html`: this whole model
  depends on a stack unwinder that is *provably* complete or explicitly
  reports failure — "the kernel livepatch consistency model relies on
  accurately identifying which functions may have live state" — an
  ordinary best-effort backtrace is explicitly called out as unsound for
  this purpose. This is the same property Stabilizer's frame-pointer-based
  walk already leans on (per `runtime-analysis.md`: the stack-pad
  instrumentation forces frame pointers structurally), which is a good sign
  for reusing this style of protocol here.

The transferable idea: instead of one synchronous barrier that every thread
must reach *simultaneously* (Shuffler), let each thread graduate
**independently and lazily**, and only free a `FunctionLocation` once *every*
thread that could reference it has individually proven (via its own stack
check, whenever it next reaches a safe point) that it no longer does. No
thread stalls another; the cost is bookkeeping ("who still needs to graduate
for this particular retired location") instead of a rendezvous.

### H5 (torn in-place header rewrite)

Shuffler's architecture is itself the fix here, not a separate mechanism:
because it *never* rewrites bytes at an address a caller directly calls
(everything is one level removed through the code pointer table, §3.2), the
torn-multi-byte-write-into-a-live-instruction-stream hazard Stabilizer has
(H5) doesn't arise in Shuffler's design at all — it's architected away, not
synchronized away. TASR's GOT-style indirection for cross-unit references
(§4.2, discussed above) is a second, independent instance of the same
"indirect, then update one pointer-sized word" pattern. Recommendation:
adopt the same architectural move for Stabilizer's forwarding mechanism (see
§3) — this is the one hazard where "add a lock" cannot work at all, since the
racing party is a concurrent **instruction fetch**, not another
lock-obeying software thread.

### H7 (RNG races)

Not really a "prior art" question — ordinary thread-safety hygiene (either a
per-maintenance-thread RNG instance with no cross-thread sharing, since
random-byte generation is proposed to move onto the single maintenance
thread anyway per the H1 recommendation, or a lock). Noted for completeness,
not a citation-worthy design point.

### H8 (signal-handler allocation / potential self-deadlock once locks exist)

Shuffler's own split between synchronous and asynchronous signal-handler work
is the applicable precedent here, even though the paper doesn't frame it as
being "about" malloc-in-signal-handler safety: "the only synchronous work...
is the short time when the program thread is interrupted via a signal to
perform stack unwinding" (§5.1.1, p.375) — no allocation happens on that
synchronous path; code copying and table updates are explicitly asynchronous
background work (§3.2, p.370; §4.4, p.374). This directly informs §3: move
`new FunctionLocation`/heap allocation **out of** the signal handler
entirely.

### H9 (arbitrary-thread timer delivery)

Both Shuffler (a dedicated Shuffler thread drives all shuffling decisions,
§4) and livepatch (a dedicated kernel worker thread — `klp_transition_work`
— drives the transition, not whichever task happens to be running) converge
on the same shape: **one dedicated background thread owns the
epoch/transition decision**; per-target-thread signal handlers only ever
react to *that* thread's decision, never make their own. **[inference from
convergence, not a single explicit statement in either source]**: neither
paper argues for this pattern explicitly as "why", but both independently
arrived at it, which is reasonably strong evidence it's the natural shape
for this problem rather than an artefact of either codebase.

### Morpheus — considered, weak fit, noted why

Read via `shibo-chen.github.io` mirror (ASPLOS 2019, §4.4, Fig. 6). Its
safety story ("threshold register": a monotonic address cursor separating
already-churned memory below it from stale memory above/at it, plus a full
register-file reload via context switch to invalidate the one pipeline's
register state) is architecturally elegant but **[my own assessment,
following from what the source describes]** doesn't transfer: it's a
single-core RISC-V prototype, so "safe to churn" reduces to "the one
pipeline has been flushed" — it never has to prove that *no core anywhere*
holds a stale reference, which is exactly Stabilizer's multithreaded,
multi-core problem. The one genuinely transferable idea is the
threshold/progress-cursor pattern for letting a background updater and live
readers agree cheaply on old-vs-new representation without a global barrier
for the *whole* update — but it doesn't touch the in-flight-PC/return-address
problem (H4) at all, so it's not usable as this design's primary mechanism.
Noted as considered-and-rejected, not silently dropped.

### JIT safepoints — the alternative *signaling* mechanism, not the alternative *protocol*

HotSpot/V8-style safepoints are conceptually the same shape as Shuffler's
barrier (global stop-the-world until every thread reaches a designated poll
point) but differ in *how* threads are made to stop: rather than sending an
OS signal to each thread, the runtime unmaps/`mprotect`s a "polling page";
each thread's own poll instruction (an implicit read at a loop back-edge or
call return) then SIGSEGVs into a handler that parks it — cheaper than an
explicit per-thread signal or a checked flag on every iteration (Shipilëv,
"JVM Anatomy Quark #22"). Stabilizer already has a comparable idea built in
for free: **every instrumented function entry already carries a trap-capable
header** (`Trap.h`, `Function.h:133-135`). A thread about to call into any
live-region function already takes a controllable trap there. This means
Stabilizer doesn't need JIT-style separate polling-page instrumentation to
get *most* of a safepoint poll — but see §3 for why this alone is
insufficient (a thread that never makes another instrumented call, e.g.
blocked in a syscall or spinning in uninstrumented code, never revisits its
trap and so never reaches this "poll" — exactly the gap Shuffler's own paper
leaves unaddressed, per §2 above).

---

## 3. Recommended design

### 3.1 Locking model for the registries (H1, H6, H10)

Adopt **single-writer**, mirroring Shuffler §4 and livepatch's dedicated
transition worker (§2, H9): spawn one dedicated **maintenance thread** at
`main()` startup (alongside, not instead of, the existing signal handlers).
It alone mutates `functions`, `live_functions`, `stack_pads`,
`FunctionLocation::getRegistry()`, and owns the RNG state (fixes H7 as a
byproduct). Ordinary program threads' `onTrap` handlers become read-mostly:
look up which `Function*` trapped (already just a pointer read next to the
trap, `FunctionHeader::getFunction()`), and either take a fast, lock-free
path for "just relocate me, no epoch boundary" or post a note to the
maintenance thread and block/retry when an epoch is in progress. A short
mutex around `functions`/`stack_pads` covers the H10 `dlopen`-after-threads
edge case; it is rarely contended (registration is rare) so a plain mutex is
fine there, unlike the hot registries.

### 3.2 Code-migration safety protocol (H2, H4, H5, H9 — the hard sub-problem)

This is Shuffler's protocol, adapted to Stabilizer's existing skeleton rather
than replacing it wholesale:

1. **Per-thread `topFrame`.** Interpose `pthread_create` (Stabilizer already
   interposes `malloc`/`free`/etc. at the pass level, so interposing thread
   creation is the same kind of hook) to capture each new thread's initial
   frame address into thread-local storage, exactly as `main()` does today
   for the primary thread (`libstabilizer.cpp:54`). Fixes H2 directly: the
   mark phase always compares against *this thread's own* recorded top, never
   a foreign one.

2. **One maintenance thread drives every epoch**, still triggered by
   `ITIMER_REAL` (keep the existing 500ms trigger; TASR's syscall-boundary
   opportunism, §2, is a plausible later refinement to *shrink* the pause
   window, not a replacement for the trigger). The itimer handler on
   whichever thread the OS delivers `SIGALRM` to does the absolute minimum:
   wake the maintenance thread (a semaphore/eventfd post) and return. Fixes
   H9.

3. **Signal every live thread explicitly** (a dedicated real-time signal,
   distinct from the trap `SIGTRAP` and from `SIGALRM`, so the three
   purposes — relocate-on-trap, epoch-trigger, epoch-participate — don't
   collide) rather than relying on the entry-trap-as-implicit-poll alone.
   As §2 notes, a thread that never re-enters instrumented code between
   epochs (blocked in a syscall, spinning in uninstrumented code) would
   never take another entry trap and so would never participate — an
   explicit signal is needed to reach it. The entry-trap remains useful as a
   **fast path**: a thread already about to take a trap for an unrelated
   reason (a fresh call) can fold its epoch participation into that same
   trap instead of taking a second, separate signal.

4. **Each thread's handler walks its own stack** (`Context.h`'s existing
   `Stack` iterator, from its own trapped/interrupted frame to its own
   per-thread `topFrame`) and marks `FunctionLocation`s exactly as today's
   mark phase does (`libstabilizer.cpp:206-216`) — but scoped correctly now.

5. **Barrier before free.** The maintenance thread waits (atomic counter +
   futex, not `pthread_barrier_t`, since some "participants" are reached via
   asynchronous signal rather than a synchronous rendezvous call) until every
   live thread has reported its mark phase complete. Only then does it run
   `sweep()`'s `delete`/`getCodeHeap()->free()` path. This is what actually
   fixes H4: no `FunctionLocation` is freed until every thread has proven, by
   its own unwind, that it holds no live reference to it.

6. **Header rewrites move off the hot instruction stream (H5).** Replace the
   in-place `Jump`/`Trap` header at `_code.base()` (`Function.h:70-73,
   133-135`) with one level of indirection: a per-function, pointer-sized
   "current location" slot that all forwarding goes through, updated with a
   single aligned 8-byte store (atomic on x86-64 by ISA guarantee) instead of
   the current multi-byte in-place jump/trap injection. This is the one
   hazard a barrier or a lock cannot fix on its own — the racing party is a
   concurrent instruction *fetch*, not a lock-obeying thread — so it needs
   the architectural fix Shuffler and TASR both independently converged on
   (§2), not a synchronization primitive. This is the largest single change
   to the pass/runtime contract in this design; see §4 for how to de-risk it
   early.

### 3.3 Per-thread stack walking

Covered by 3.2.1 above — no separate design needed beyond "give every thread
its own recorded top-of-stack and never compare across threads."

### 3.4 Timer/signal strategy

Covered by 3.2.2/3.2.3: `SIGALRM` triggers, a dedicated maintenance thread
decides and drives, a dedicated real-time signal makes each live thread
participate, `SIGTRAP` continues to mean "relocate me" as it does today but
its handler no longer allocates memory synchronously (H8 fix: pre-allocate
`FunctionLocation` slots from a small free-list maintained asynchronously by
the maintenance thread — mirroring Shuffler's "only unwind is synchronous"
split, §2/H8 — rather than calling `getDataHeap()->malloc()` from inside the
signal handler).

### 3.5 Tradeoffs, explicit

| Axis | Stop-the-world barrier (Shuffler, recommended) | Per-task lazy graduation (livepatch-style) | Per-thread private code copies |
|---|---|---|---|
| Complexity | Moderate: one barrier primitive, one dedicated thread, per-thread topFrame | Higher: per-`FunctionLocation` "who still needs to graduate" bookkeeping, no single rendezvous point to reason about | Lowest conceptually (no cross-thread free hazard at all — each thread only ever frees its own copies) |
| Worst-case pause | Bounded by the slowest thread's stack depth: Shuffler measured 6ms worst case (45k-deep stack), 99.7% idle overall (§2) | No synchronous pause; a slow/blocked thread only delays freeing *the objects it references*, not global progress | None — no barrier needed at all |
| Handles a thread blocked in a long syscall | **Open gap** — Shuffler's own paper never addresses this (§2, H2 limitations); needs an explicit design decision here (exempt idle/blocked threads from the barrier by checking their saved state, at the cost of proving it correctly) | Naturally handles it — that thread just graduates later, whenever it next reaches a safe point | N/A, no barrier |
| Memory cost | Normal — one shared code layout | Normal | High — Shuffler's own authors explicitly considered and **rejected** this for cost (§6.2, p.379): "a thread-local variant would be fairly straightforward" but wasn't implemented, trading disclosure-resistance for memory/CPU |
| Fits Stabilizer's existing skeleton | Best fit — `mark()`/`sweep()`/`_marked`/`_defunct` already exist; this mostly scopes them correctly and adds a barrier | Requires reworking `FunctionLocation` lifecycle into per-referrer reference tracking, a bigger rewrite | Requires reworking the entire code-heap allocation and `Function::copyTo` model |
| Recommendation | **Primary**, because it is the smallest diff from the current mark/sweep design that is actually correct, and Shuffler's measured overhead (99.7% unhindered) suggests the pause cost is not the risk here | Worth prototyping as a *fallback* if the barrier's blocked-thread gap (above) proves unworkable in practice | Not recommended; same conclusion Shuffler's own authors reached, for the same reason |

The header-indirection fix (3.2.6) is not really optional under any of the
three rows above — it's needed regardless of which barrier/graduation model
is chosen, because it fixes a hazard (torn instruction-stream writes) none
of the three concurrency models address.

---

## 4. What to prototype first, and the cheapest falsifying experiment

### Prototype first

**Per-thread `topFrame` + explicit-signal barrier mark phase (3.2.1–3.2.5),
without yet touching header indirection (3.2.6).** Concretely: a minimal
2-thread synthetic C program, `-Rcode`-instrumented, where two `pthread`s
both call the same hot function in a tight loop while the existing 500ms
`ITIMER_REAL` fires. This isolates H2+H4 — the deep, novel sub-problem — from
H5, which is a separate (if related) architectural change. Today, this exact
program is expected to crash or corrupt memory quickly (H2's stack-walk
running off a sibling thread's stack is close to guaranteed on the very
first cross-thread epoch); that crash is the **baseline** the fix must
resolve. Running it in podman per the ROADMAP's standing constraint, never on
the host kernel.

### Cheapest experiment that would falsify the recommended approach

Construct a third thread that is either (a) blocked in a long/uninterruptible
syscall, or (b) spinning in a tight loop that never calls into any
instrumented function, precisely during an epoch — i.e. a thread that cannot
"participate" via the entry-trap fast path and may not promptly receive/act
on the explicit real-time signal either. Then check which of two ways the
barrier-based design (3.2.5) breaks:

- **Hangs**: if the barrier naively waits for *every* thread's mark-complete
  report with no exemption for a thread that can't currently respond, the
  maintenance thread waits forever (or until the blocking syscall returns) —
  starving reclamation for the whole process on one stuck thread.
- **Corrupts**: if instead an "idle/blocked thread exemption" is added
  without correctly proving that thread holds no stale reference (e.g.
  exempting it based on "hasn't taken a trap recently" without actually
  checking its saved stack contents), the barrier completes early and
  `sweep()` can free a `FunctionLocation` the blocked thread's saved return
  address still points to — reproducing H4 under a different trigger.

This is the single cheapest test to build (one extra thread, one syscall
call like `read()` on a pipe with no writer, or one `while(1);`) and it
targets precisely the gap the Shuffler paper itself never addresses (§2, H2
limitations: "what happens if a target thread is blocked in a syscall... is
not discussed"). If this scenario breaks the recommended design, the
fallback in the tradeoffs table — livepatch-style per-task lazy graduation,
which has an explicit answer for exactly this case (a task graduates
whenever it *next* reaches a safe point, with no global stall) — is the
next design to prototype, not stop-the-world with an ad hoc patch.
