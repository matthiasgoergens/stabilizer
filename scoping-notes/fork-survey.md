# Survey of GitHub forks of ccurtsinger/stabilizer

Date: 2026-08-07. All data below is grounded in `gh api` calls made during this
session; raw outputs are kept alongside this file in
`/home/matthias/prog/stabilizer/scoping-notes/` (see file list at the end).

## Headline answer

**Yes, and the most substantial finding is not in the fork graph at all.**
[`Dead2/stabilizer`](https://github.com/Dead2/stabilizer) — maintained by
Hans Kristian Rosbach, a real named systems engineer — is a **detached**
copy of the repository (GitHub's `fork` flag is `false`; it was pushed to a
fresh repo rather than created via GitHub's Fork button) that nonetheless
shares real git ancestry with upstream: its earliest commit,
`0acddda3` "Initial check-in" (2012-04-28), is byte-identical to the same
commit in `ccurtsinger/stabilizer` — confirmed by fetching that SHA from
both repos. A pure fork-graph walk (steps 1–2 below) structurally cannot
find this repo, exactly as flagged. Its `develop` branch (HEAD `6c081b19`,
2023-05-14) is a serious, sustained, one-year, multi-contributor effort:
LLVM 12 support, a completely rewritten `szcc` compiler wrapper, env-var
based feature flags, a full **CMake** build-system port, **Docker support**,
and a **GitHub Actions CI workflow** (`.github/workflows/cmake.yml`) — none
of which exist anywhere else in the survey. Its README is unusually candid
engineering documentation: it states plainly that `SZ_HEAP`/`SZ_LINK` work
but `SZ_STACK`/`SZ_CODE` still crash, explains that the project's original
build-system rigidity ("would not pass even the most basic compiler tests
in CMake or GNU Autoconf") is *why* it never attracted contributors, and
asks for testing help. It has its own 3-fork sub-network
(`magras/stabilizer-fork`, `mkp-rh/stabilizer`, `Ghost-LZW/stabilizer`),
themselves invisible to any survey that only walks forks of `ccurtsinger`.
One of those, `magras/stabilizer-fork:github-workflow` (7 commits ahead of
Dead2's `develop`), is the apparent source of the CMake+Docker+CI work
before Dead2 re-applied it into `develop` under new commit SHAs — i.e. this
was genuinely collaborative, cross-account engineering, not one person's
solo branch. Full detail in its own section below.

Separately, and independently, **[`parsa/stabilizer`](https://github.com/parsa/stabilizer)**
also carries a real, currently-active modernisation effort, maintained by
Parsa Amini (STE||AR Group), a fork-of-a-fork of `emeryberger/stabilizer`.
Its `master` branch is 26 commits ahead of upstream `master` with **zero**
divergence (clean linear history), running from April 2023 ("managed to
compile with llvm 16.0.1") through a concentrated burst on 2026-02-13/14
("patch PC-relative refs", "restore heap shuffle", "near map PIE for
RIP-relative refs (no more -no-pie requirement)", "support flang, tighten
instrinsic rewrites"). Its README explicitly states: *"This project was
originally built for LLVM 3.1. This repository has been updated to work with
modern LLVM toolchains (tested with LLVM 21)."* It touches the pass-manager
API transition (old→new PassManager), adds two new runtime source files
(`runtime/CodeWindow.{cpp,h}`, `runtime/TextRelocations.{cpp,h}`), rewrites
`runtime/MMapSource.h` and `runtime/libstabilizer.cpp` substantially, and
drops legacy GCC/Dragonegg support in favour of Clang + LLVM Flang for
Fortran. No PR was ever opened against upstream or against `emeryberger`
(checked, see below) — the work lives only in this fork.

A third lineage — **`magras/stabilizer`** (in the fork graph, distinct from
the detached `magras/stabilizer-fork` discussed above — same person, two
different repos), specifically its `fix-tls` branch — is a genuine,
substantial (42-commit) LLVM 6→9→10→12→14 porting effort (commit messages
reference CERN's LCG/CVMFS software stack), including real runtime bug
fixes: *"fixed crashes caused by accessing TLS variables via relocation
table"* and *"fixed crashes caused by incorrect placement of a dummy"*
(Nov 2022). This is more advanced work than `fusiled/stabilizer` (the only
fork the earlier five-minute pass credited with non-trivial activity) —
`fusiled` is in fact the *ancestor* of this lineage (`fusiled` → `dendibakh`
→ `jgall` → `magras`), and `magras` carries far more work than `fusiled`
itself. **This lineage and the Dead2 cluster are the same underlying work at
a shared point**: the commit `115c2f78` "fix an 'f' missing in szcc"
(2022-10-08) has the **identical SHA** in both `magras/stabilizer:fix-tls`
and `Dead2/stabilizer:develop`, proving Dead2 imported this exact commit
object from the `fusiled`→`dendibakh`→`jgall`→`magras` lineage (git commit
SHAs are content-addressed; an identical SHA across unrelated repos is only
possible if the commit object was literally copied). After that shared
point the two lines diverge: `magras/stabilizer:fix-tls` continues to
"added support for llvm 14" (Nov 2022) and stops there, while
`Dead2/stabilizer:develop` re-derives its own TLS fixes under different SHAs
and continues seven more months into the CMake/Docker/CI work described
above. So the true frontier of this whole multi-generation lineage
(`fusiled`→`dendibakh`→`jgall`→`magras`→**Dead2**) is the detached
`Dead2/stabilizer` repo, not `magras/stabilizer` itself.

The earlier pass's judgement of `emeryberger/stabilizer` as trivial is
**correct for the `emeryberger` repo itself** (its own `master` is
byte-identical to upstream; its one extra branch, `patch-1`, is 0 commits
ahead — i.e. carries no unique work). But that repo has a fork
(`parsa/stabilizer`, not visible without recursing) that is the most
substantive modernisation attempt found anywhere in the network — proving
the brief's premise that real work can hide under an old/unrelated parent's
timestamp and on a non-default branch.

## Method and coverage

1. Enumerated forks via `gh api repos/ccurtsinger/stabilizer/forks --paginate`
   → **39 direct forks**.
2. Recursed into every direct fork's own `forks_count`; three direct forks
   had `forks_count>0` (`emeryberger`:1, `fusiled`:1, `plasma-umass`:2).
   Recursed repeatedly until every leaf reported `forks_count:0`:
   - `emeryberger` → `parsa` (0 further forks)
   - `fusiled` → `dendibakh` (1) → `jgall` (2) → `atw1020` (0), `magras` (1) → `timadye` (0)
   - `plasma-umass` → `tristan-potter` (0), `yqzhang` (0)
   - This adds **8 nested forks-of-forks**, none of which appear in the
     top-level forks list and all of which were missed by the earlier
     five-minute pass.
   - **Total: 47 repositories** in the `ccurtsinger` fork network (39 + 8).
     Coincidentally (or not — unclear) this equals the `forks_count: 47`
     field reported on `ccurtsinger/stabilizer` itself, even though that
     field nominally counts only direct forks; not investigated further,
     noted as an anomaly rather than relied upon.
   - **Plus a second, detached network found separately** (step 9 below):
     `Dead2/stabilizer` + its 3 forks = 4 more repositories, not part of the
     47 above and unreachable by this fork-graph walk at all. **Grand total
     across both networks: 51 repositories.**
3. Listed **all branches** on all 47 repos (`gh api repos/OWNER/stabilizer/branches`)
   → **163 branches total**. Upstream itself has 4 branches (`cleanup`,
   `jit`, `master`, `results`); most forks carry exactly this set unchanged.
   Diffed each fork's branch-name set against the upstream 4 to find
   anomalies: this surfaced three forks with an extra, non-standard branch —
   `emeryberger:patch-1`, `magras:fix-tls`, and **`parsa:upgrade_llvm_19`**
   (a strong prior signal on its own, confirmed substantive below).
4. Ran `gh api repos/ccurtsinger/stabilizer/compare/master...OWNER:BRANCH`
   for **all 163 branches** (script:
   `scoping-notes/compare_all.sh`, raw output: `scoping-notes/compare-results.tsv`).
   This is a real commit-graph comparison (ahead_by/behind_by), not a
   push-date proxy, and it worked correctly across nested forks (e.g.
   comparing `ccurtsinger/stabilizer` directly against `parsa:master` even
   though `parsa` is two hops removed) because all 47 repos share one GitHub
   fork network.
   - `jit` and `results` branches returned HTTP 404 "No common ancestor" on
     every fork that has them — these are orphan/rootless branches unrelated
     to `master`'s history (upstream's own `jit`/`results` branches predate
     or diverge from a shared root with `master`); not further pursued since
     no fork's `jit`/`results` differs from upstream's own `jit`/`results`
     (same branch, untouched).
   - `cleanup` is `behind 20` (0 ahead) on every fork that has it — upstream
     is ahead there, forks never touched it.
5. For every branch with `ahead_by > 0` (15 branches, see table), fetched
   full commit list + diffstat via the same compare endpoint
   (`scoping-notes/compare-details/*.json`, human-readable summary in
   `scoping-notes/compare-details-summary.txt`) and read commit messages and
   changed-file lists to classify trivial vs. substantive.
6. For the two most substantive branches (`parsa:master`, `magras:fix-tls`),
   pulled individual commit patches
   (`scoping-notes/parsa-pie-commit.json`, `scoping-notes/magras-tls-commit.json`)
   to verify the described mechanism (PIE/RIP-relative code mapping; TLS
   relocation-table crash fix) against actual added/removed lines, not just
   commit-message text.
7. Checked for open/merged PRs against `ccurtsinger/stabilizer` (found only
   `#8 "Update README.md"` by `emeryberger`, closed — the same trivial PR
   the repo's own git log already shows) and against `emeryberger/stabilizer`
   (none) to confirm none of the substantive fork work was ever proposed
   upstream.
8. Read `parsa/stabilizer`'s live README in full (`scoping-notes/parsa-readme.md`)
   and its owner's GitHub profile (`Parsa Amini`, STE||AR Group — a real HPC
   research group, not an anonymous/throwaway account) to corroborate the
   modernisation claim.
9. **Extended coverage to detached (non-fork-graph) copies**, per a
   cross-lead surfaced mid-survey pointing at `magras/stabilizer-fork`
   (a repo name suggesting a fork relationship not present in the actual
   GitHub fork graph) and `Dead2/stabilizer`:
   - `gh api repos/magras/stabilizer-fork` showed it *is* a GitHub fork —
     but of `Dead2/stabilizer`, not of anything in the `ccurtsinger`
     network. `gh api repos/Dead2/stabilizer` showed `"fork": false`: a
     genuinely detached repo, invisible to any `.../forks` recursion no
     matter how deep, confirming the cross-lead's premise.
   - Verified shared ancestry despite the missing fork edge: fetched commit
     `0acddda3` from both `ccurtsinger/stabilizer` and `Dead2/stabilizer`
     directly by SHA — identical commit object, dated 2012-04-28, message
     "Initial check-in". This proves `Dead2/stabilizer` is a real detached
     copy of the same repository (pushed into a fresh, non-forked GitHub
     repo, most likely via `git push` to an empty repo rather than the Fork
     button), not a same-named unrelated project.
   - Recursed into `Dead2/stabilizer`'s own fork network the same way as
     step 2 (`forks_count: 3` → `magras/stabilizer-fork`, `mkp-rh/stabilizer`,
     `Ghost-LZW/stabilizer`, each individually re-checked for
     `forks_count:0`, confirmed no further nesting).
   - Compared every branch of `Dead2/stabilizer` and its 3 forks against
     `Dead2:develop` (their effective "upstream"), the same ahead/behind
     method used for the main network, since `Dead2/stabilizer` is not in
     `ccurtsinger`'s network and a cross-network compare from
     `ccurtsinger/stabilizer` 404s (tested directly: `gh api
     repos/ccurtsinger/stabilizer/compare/master...Dead2:stabilizer:develop`
     → `404 Not Found`, confirming GitHub's compare API cannot bridge
     genuinely detached repos even when they share real git history).
   - Read the actual diffs/diffstats for the two commits/branches most
     load-bearing for the "does it build, how far did it get" question:
     the `github-workflow` branch (CMake+Docker+CI diffstat, 14 files) and
     the full current README (which itself functions as an honest status
     report: what works, what crashes, why the project historically
     struggled to gain contributors).
   - **Searched further for additional detached copies** beyond the one
     cross-lead, since the whole point is that fork-graph recursion cannot
     find these on its own:
     - `gh api search/repositories` for the exact tagline `"Rigorous
       Performance Evaluation"` → only `ccurtsinger/stabilizer` and
       `Dead2/stabilizer` are genuine matches (other hits are unrelated ML
       repos whose descriptions happen to contain the generic phrase
       "rigorous performance evaluation").
     - `gh api search/code` for the literal filename `libstabilizer.cpp` →
       (with `fork:true`) surfaces 11 repos, all already known fork-graph
       members; Dead2 and its 3 forks did **not** appear in this
       particular query (GitHub code search has known indexing gaps for
       forks and doesn't claim completeness), so this check is
       corroborating, not exhaustive.
     - `gh api search/code` for the combination `SZ_STACK SZ_CODE SZ_HEAP`
       (Stabilizer's distinctive env-var/flag names) → returned
       `Dead2/stabilizer` (4 file hits) plus unrelated noise (a
       virtualization project, an unrelated conference-talks repo); no new
       Stabilizer-derived repo.
     - `gh api search/code` for the literal flag string `"-Rcode -Rstack
       -Rheap"` → only `ccurtsinger/stabilizer` plus one clearly unrelated
       hit (a slide-deck repo mentioning the flags in passing, not a fork).
     - A broader `gh api search/repositories` for `stabilizer in:name
       language:C++` returns 162 repositories — but this term is heavily
       overloaded (PID/drone/image "stabilizer" projects unrelated to this
       tool) and was judged too noisy to triage exhaustively within this
       survey's scope; see coverage caveat below.
   - **Conclusion of the detached-copy search**: one genuine additional
     cluster found (`Dead2/stabilizer` + its 3 forks, all fully
     characterised above); multiple independent search strategies
     converged on the same two canonical repos (`ccurtsinger`, `Dead2`) and
     found nothing further. Confidence this is the *only* detached copy is
     moderate, not absolute — see coverage statement.

## Table: every branch with ahead_by > 0

All 163 branches were compared; 148 had `ahead_by == 0` (pure mirrors or
strictly behind) and are omitted. The 15 branches below are every branch in
the entire 47-repo network with any unique commit.

| Fork | Branch | ahead_by | behind_by | Classification | Notes |
|---|---|---|---|---|---|
| `parsa/stabilizer` | `master` | 26 | 0 | **Substantive — active** | LLVM 16→21 port; new PassManager; PIE/RIP-relative relocation fixes; new `CodeWindow`/`TextRelocations` runtime files; drops GCC/Dragonegg, adds LLVM Flang. Latest commit 2026-02-14. |
| `parsa/stabilizer` | `upgrade_llvm_19` | 1 | 26 (vs its own master) | Substantive but superseded | Single squashed commit "Upgrade Stabilizer pass/runtime for LLVM 19" (2025-12-06), touches pass/ and runtime/ broadly. Diverged from an earlier point; 26 commits behind current `master`, i.e. an earlier/parallel attempt subsumed by the `master` line. |
| `magras/stabilizer` | `fix-tls` | 42 | 4 | **Substantive — real bugfixes** | LLVM 9.0.1→10.0.0→12→14 port over 2020–2022; "fixed crashes caused by accessing TLS variables via relocation table"; "fixed crashes caused by incorrect placement of a dummy"; rewritten `szcc` wrapper; CVMFS/LCG (CERN) build integration. |
| `magras/stabilizer` | `master` | 39 | 4 | Substantive (subset of `fix-tls`, minus the 3 final TLS-crash commits) | Same lineage as `fix-tls`; branch diverges 3 commits before it. |
| `timadye/stabilizer` | `master` | 25 | 4 | Substantive (inherited) | Same commit lineage as `magras`/`jgall` up to "stop timer alarm before exit" (2021-07-01); no independent new work beyond what it inherited. |
| `atw1020/stabilizer` | `master` | 14 | 4 | Partial/abandoned | Inherits `fusiled`→`jgall` LLVM 9 port + submodule additions, then adds Apple Silicon build scaffolding and an explicitly **"(non-working) CMakeLists.txt"** (commit title's own words) as the last commit (2021-06-20). Incomplete. |
| `jgall/stabilizer` | `master` | 11 | 4 | Substantive (infra) | Adds `DieHard`/`Heap-Layers` as git submodules and refactors the submodule setup; carries `fusiled`+`dendibakh`'s LLVM 6→9 work. No LLVM-version work of its own. |
| `dendibakh/stabilizer` | `master` | 7 | 4 | Substantive (LLVM 9 port) | "Updated Stabilizer to LLVM 9.0" (2019-09-18), building on `fusiled`'s LLVM 6 fixes. |
| `fusiled/stabilizer` | `master` | 5 | 4 | Substantive (LLVM 6 port) | "pass: Now compilation should work for llvm6.0"; switches default compiler to clang; removes debug output. This is the root of the entire `dendibakh`/`jgall`/`atw1020`/`magras`/`timadye` lineage — i.e. all that later work descends from here. Matches the earlier five-minute pass's one positive finding, but that pass didn't know it had four more generations of descendants. |
| `thinkmoore/stabilizer` | `master` | 3 | 5 | Minor feature (not modernisation) | Adds an environment variable and run/config options to disable rerandomization (2016). Runtime feature addition, not a port to newer LLVM. |
| `schrummy14/stabilizer` | `master` | 1 | 4 | Trivial | Single commit, README text only: "There needs to be an updated guide on how to install LLVM 3.1." No code change. |
| `nickhutchinson/stabilizer` | `master` | 1 | 5 | Minor but real | Single commit "Add support for LLVM 3.6" (2015), touches `pass/Stabilizer.cpp` and `pass/LowerIntrinsics.cpp` (16+8 line changes). Real but tiny, single-shot, not pursued further; no lineage. |
| `plasma-umass/stabilizer` | `master` | 1 | 5 | Trivial | Sole extra commit is `"Merge pull request #1 from ccurtsinger/master"` — a sync merge, zero changed files. |
| `tristan-potter/stabilizer` | `master` | 1 | 5 | Trivial | Same merge-only commit as `plasma-umass` (its parent), inherited. |
| `yqzhang/stabilizer` | `master` | 1 | 5 | Trivial | Same merge-only commit as `plasma-umass` (its parent), inherited. |

Not shown in the table: `emeryberger/stabilizer:patch-1`, the only other
non-standard branch found — `ahead_by: 0, behind_by: 3` (status "behind").
It carries **no** unique commits, so it is not "ahead" of anything and was
excluded; confirms the earlier pass's call that `emeryberger`'s own repo
content is trivial.

## Detailed notes on the substantive lineages

### `Dead2/stabilizer` and its cluster (standout finding — detached, not in the fork graph)

- **Discovered via a cross-lead, not the fork-graph walk** — flagged mid-survey
  and verified here with direct API calls (`gh api repos/Dead2/stabilizer`,
  `gh api repos/Dead2/stabilizer/commits/0acddda3`,
  `gh api repos/ccurtsinger/stabilizer/commits/0acddda3`, branch/compare
  calls against `Dead2/stabilizer` and its 3 forks). Confirms the premise
  that a pure `forks` API walk from `ccurtsinger` cannot reach this
  material: `Dead2/stabilizer` has `"fork": false` on its repo object, so it
  never appears in anyone's `.../forks` listing no matter how deep the
  recursion goes.
- Owner: **Hans Kristian Rosbach** ("Dead2"), bio: *"Systems developer with a
  hobby interest in open-source software and a passion for optimization."*
  Real name, real contact email in the README (`stabilizer àt circlestorm
  dót org`) — not an anonymous or bot account.
- Repo created 2022-09-14 (same day as `magras/stabilizer`'s "Remove SPEC
  CPU2006 related files" / "Add rewritten szcc compiler wrapper" commits —
  consistent with Dead2 branching off the `magras` lineage at that point,
  see cross-link above). `develop` (default branch) HEAD is `6c081b19`,
  dated 2023-05-14; repo `pushed_at` 2023-08-15 (a later push landed on a
  non-default branch or was a metadata-only update — HEAD of `develop`
  itself is confirmed at May 2023 via `gh api repos/Dead2/stabilizer/branches/develop`).
  152 commits total on `develop` (`gh api ... --paginate`), starting from
  the same 2012 "Initial check-in" as upstream.
- **What it changed, read from the actual README and commit list** (full
  README saved verbatim at `scoping-notes/dead2-readme.md`):
  - LLVM 12 is the primary supported/tested version. `SZ_HEAP` and
    `SZ_LINK` randomizations work; `SZ_STACK` and `SZ_CODE` still crash —
    stated explicitly, not glossed over ("Help is wanted for testing and
    fixing the remaining crashes").
  - Compiler wrapper (`szcc`) completely rewritten for closer clang
    compatibility (inherited from the `magras` lineage, commit `a13f046d`
    "Add rewritten szcc compiler wrapper", same SHA as in `magras/stabilizer`).
  - Feature flags switched from CLI parameters to environment variables
    (`SZ_CODE`, `SZ_HEAP`, `SZ_STACK`, `SZ_LINK`, `SZ_VERBOSE`, `SZ_CLEAN`).
  - GCC/Gfortran (DragonEgg) support explicitly dropped — DragonEgg was
    never ported past old LLVM, so it's dead weight; matches the same
    engineering call `parsa/stabilizer` made independently, years later.
  - SPEC CPU2006-specific scripts/configs removed.
  - **Build-system modernisation** (commits `f0e34b62`/`b384bf35` "port
    project to cmake", 2023-03-10; `09a51306` "add docker support",
    2023-03-16): adds `CMakeLists.txt` at top level plus per-directory
    (`pass/`, `runtime/`, `tests/`, `tests/HelloWorld/`, `tests/bzip2/`,
    `tests/libquantum/`), a `Dockerfile`, `.dockerignore`, and
    `StabilizerToolchain.cmake.in`/`CTestTestfile.cmake.in` for CTest
    integration.
  - **CI**: `.github/workflows/cmake.yml` (verified present via the
    `github-workflow` branch diffstat, 47 lines) — i.e. commits were being
    verified by GitHub Actions, not just "works on my machine."
  - Later refinements: clang/clang++ auto-detection and symlink handling
    (`8231ab04`, `6c081b19`, May 2023), removing hard-coded
    `STABILIZER_HOME`-relative paths (`50be16b5`), moving CMake helpers to
    a dedicated folder (`c88ab745`).
- **Collaborative, not solo**: `Dead2/stabilizer` has its own 3 forks —
  `magras/stabilizer-fork` (a *separate* repo from `magras/stabilizer` in
  the main fork graph, same person), `mkp-rh/stabilizer`, and
  `Ghost-LZW/stabilizer`. Branch-by-branch comparison against `develop`
  (`gh api repos/Dead2/stabilizer/compare/develop...OWNER:REPO:BRANCH`):

  | Repo | Branch | ahead_by | behind_by | Notes |
  |---|---|---|---|---|
  | `magras/stabilizer-fork` | `develop` | 0 | 17 | mirror, no unique work |
  | `magras/stabilizer-fork` | `github-workflow` | 7 | 6 | Source of the CMake+Docker+CI work: commits "port project to cmake", "add docker support", "remove paths relative to `STABILIZER_HOME`", plus a merge of 4 feature branches (`debug`, `cmake`, `remove-stabilizer-home`, `docker`). Content matches what later landed in `Dead2:develop` (different SHAs — re-applied/cherry-picked rather than merged as-is), i.e. this branch is the real origin of that work. |
  | `magras/stabilizer-fork` | `remove-symlinks` | 1 | 17 | single commit "removed clang symlinks" (2022-11-24) |
  | `mkp-rh/stabilizer` | `develop` | 0 | 12 | mirror, no unique work |
  | `Ghost-LZW/stabilizer` | `develop` | 0 | 18 | mirror, no unique work |
  | `Ghost-LZW/stabilizer` | `fix_f_miss_in_szcc` | 1 | 18 | single commit "fix an 'f' missing in szcc" (2022-10-08) — this is the **PR-source branch** for the `115c2f78`/`773b2479` commit that appears (different SHA, same message/date, cherry-picked) in both `Dead2:develop` and, further back, in the `magras/stabilizer` (fork-graph) lineage. Confirms `Ghost-LZW` submitted this fix and it was accepted into `Dead2/develop`. |

  Net picture: `Dead2/stabilizer` functioned as a small but real
  multi-contributor upstream — `magras` and `Ghost-LZW` both submitted
  branches with focused, named fixes that were incorporated into `develop`.
- **Status as of this survey**: last activity across the whole cluster is
  `Dead2/stabilizer`'s `pushed_at` of 2023-08-15 — over two years stale as
  of 2026-08-07, i.e. not currently active (unlike `parsa/stabilizer`,
  which pushed as recently as 2026-02-14). But it reached a materially more
  complete and better-engineered state (CMake, Docker, CI, documented
  known-crash inventory) than any repo actually in the `ccurtsinger` fork
  graph.

### `parsa/stabilizer` (second standout finding, currently active)

- Fork chain: `ccurtsinger/stabilizer` → `emeryberger/stabilizer` →
  `parsa/stabilizer`. Owner: Parsa Amini, STE||AR Group (real named
  individual at a real HPC research group — HPX runtime, C++ parallelism).
- `master` is cleanly 26 commits ahead of upstream `master` with 0 commits
  behind (linear history, no divergence/rebasing artefacts).
- Timeline: April 2023 burst (`1ba581b1`…`a806c2bf`, 16 commits) gets it
  compiling on LLVM 16.0.1, migrates `LowerIntrinsics` to the new
  PassManager API, fixes an `ilist` misuse and `ConstantExpr::getGetElementPtr`
  misuse, and is explicitly marked "still broken" partway through (commit
  `371d13fa`) — an honest WIP trail, not a claimed-done-when-it-isn't commit.
  Then a second burst on 2026-02-13/14 (`6f6fa674`…`2bffc191`, 9 commits)
  does deeper runtime work:
  - `55380af5` "near map PIE for RIP-relative refs (no more -no-pie
    requirement)" — verified via the actual patch: adds
    `runtime/CodeWindow.{cpp,h}` (new files, 39 lines), rewrites
    `runtime/MMapSource.h` (+91/-1) to map the code window near the
    executable's own address space so RIP-relative addressing works without
    forcing non-PIE linking, and adds 45 lines to `libstabilizer.cpp`. This
    is real, substantial runtime engineering addressing a genuine bitrot
    problem: Stabilizer's original code-randomization technique predates
    universal PIE and needed `-no-pie`, which modern distros/toolchains
    increasingly refuse or discourage.
  - `dce75acd` "broader x86_64 relocation coverage", `27536905` "support
    flang, tighten instrinsic rewrites", `03720f3b` "tighten reloc checking
    if linux" — further relocation/ABI correctness work.
  - Also touches `runtime/TextRelocations.{cpp,h}` (new, 275+26 lines).
- README (read in full) explicitly claims: *"This repository has been
  updated to work with modern LLVM toolchains (tested with LLVM 21)"*, drops
  the legacy GCC+Dragonegg frontend in favour of Clang, and adds LLVM Flang
  support for Fortran inputs — none of which exist in upstream or in any
  other fork surveyed.
- The `upgrade_llvm_19` branch (the one extra branch name that first flagged
  this fork) turned out to be an **earlier, now-superseded** single-commit
  attempt (2025-12-06, 26 commits behind current `master`) — the real,
  larger effort continued on `master` past that point up to 2026-02-14. Not
  a case of two active branches; `master` is the live line.
- No PR against `ccurtsinger/stabilizer` or `emeryberger/stabilizer` exists
  from this account (checked both repos' PR lists, `--paginate`, `state=all`).
  This work is undiscoverable by anyone browsing pull requests upstream.

### `magras/stabilizer:fix-tls` (second-most substantive)

- Fork chain: `ccurtsinger` → `fusiled` → `dendibakh` → `jgall` →
  `magras/stabilizer`. Owner: GitHub user `magras`, no public name/bio;
  commit messages reference CERN's LCG/CVMFS software distribution
  ("setup.sh to use Clang from LCG CVMFS", "get clang 10.0.0 from CVMFS
  LCG_99"), suggesting a CERN-affiliated author doing this for real use, not
  a toy exercise.
- 42 commits ahead of upstream master, spanning 2020-10-26 to 2022-11-24.
  Ports through LLVM 9.0.1 → 10.0.0 → 12 → 14 sequentially, each with a
  "port to llvm N" or "update to LLVM N.0.0" commit plus multiple follow-up
  fix commits.
- Two commits are genuine runtime bug fixes rather than mechanical porting:
  `4e154b8f` "fixed crashes caused by accessing TLS variables via relocation
  table" (verified patch: 1-line but functionally load-bearing change in
  `pass/Stabilizer.cpp`) and `d08d6ae1` "fixed crashes caused by incorrect
  placement of a dummy". Also rewrites the `szcc` compiler-driver wrapper
  from scratch (`a13f046d` "Add rewritten szcc compiler wrapper") and splits
  `SZ_LOWER` out of `SZ_CODE` to avoid `SZ_CODE`-related crashes
  (`cc2acf76`).
- `master` on the same repo is a near-identical but 3-commits-shorter branch
  (diverges before the final two TLS-crash fixes) — `fix-tls` is the more
  complete/final state.
- Descendant `timadye/stabilizer` inherits this lineage up to a mid-2021
  point but adds nothing of its own (25 ahead, all inherited).

### Why the earlier pass's picture was incomplete

- It credited `fusiled/stabilizer` (push date 2018-11-21) as the one
  non-trivial post-2016 fork. That's correct as far as it goes — `fusiled`
  really did do an LLVM 6 port — but `fusiled` is the **root**, not the
  frontier, of a five-generation lineage (`fusiled` → `dendibakh` → `jgall`
  → `magras`/`atw1020` → `timadye`) that continued through 2022. Sorting by
  push date alone put `fusiled` at the top of that lineage and hid four
  later, more advanced generations sitting on other accounts with unrelated
  (often older-looking, because they only touched non-code branches later,
  or the account's *default* branch push date doesn't reflect its most
  advanced *non-default* branch) push timestamps.
- It judged `emeryberger/stabilizer` trivial by content, which is right for
  that repo — but that repo has a fork (`parsa/stabilizer`) that never shows
  up in any list sorted by push date of *direct* forks of `ccurtsinger`,
  because `parsa` is two hops away and the recursion was never done.

## Explicit statement of coverage — what was NOT checked, and why

- **Branches only, not tags or PR-only commits.** `gh api .../branches`
  lists branches; any work that exists only as an unmerged pull-request
  head with no corresponding branch on the fork (rare on GitHub, since PR
  heads are branches by definition, but a PR from a *third* fork into one of
  these 47 repos would not be picked up) was not searched for. Not checked:
  whether any of these 47 repos has open PRs *into* it from yet another,
  unlisted fork. Given none of these repos showed multiple contributors or
  any PR activity in the branch/commit data, this is judged low-risk but is
  not proven.
- **Gists, GitLab/other-host mirrors, and local/offline clones** are
  entirely out of scope — this survey only covers what GitHub's fork graph
  *and* GitHub's search indexes expose. A detached copy pushed to a
  non-GitHub host, or to GitHub under a repo name/description containing
  none of the search terms tried, would not be found.
- **The detached-copy search is not exhaustive, and this is the biggest
  open coverage gap in the whole survey.** Multiple targeted `search/code`
  and `search/repositories` queries (exact tagline, exact filename,
  distinctive env-var names, distinctive CLI flag string) all converged on
  the same two canonical repos (`ccurtsinger`, `Dead2`) and found nothing
  else — that convergence is real evidence, not just an absence of effort.
  But: (a) GitHub's code-search index has known, undocumented gaps for
  forks and for repos below some popularity/recency threshold, so a
  negative result there is suggestive, not conclusive; (b) the one broad,
  untargeted query (`stabilizer in:name language:C++`, 162 hits) was
  **not** triaged — it's dominated by unrelated "stabilizer" projects
  (PID controllers, drones, image/video stabilization) and going through
  all 162 by hand was judged out of proportion to the marginal likelihood
  of finding a third detached copy, given the convergent negative result
  from the targeted content searches. If a third copy exists, it is most
  likely to be found by triaging that list of 162, specifically filtering
  for repos whose description or file tree mentions LLVM, `szc`, or
  Curtsinger/Berger.
- **Did not build or run any of the substantive branches**, in the
  `ccurtsinger` network or the `Dead2` network. Classification of
  "substantive" rests on commit messages, diffstats, and reading a sample
  of full patches (2 commits pulled in full from the `ccurtsinger` network:
  the PIE-mapping commit and the TLS-crash-fix commit; the `Dead2` network's
  CMake/Docker/CI claim was checked via its diffstat and its own README's
  self-report of what works vs. crashes, not independently re-verified by
  building), not by compiling and testing. `parsa`'s README claim of
  "tested with LLVM 21" and `Dead2`'s README claim of "LLVM 12 ...
  tested" are both taken as the authors' claims, not independently
  verified.
- **Did not read every commit's full diff** — for the 26-commit and
  42-commit branches, classification used the aggregate diffstat (from the
  three-dot compare) plus commit messages plus two individually-pulled
  commit patches as spot checks, not a line-by-line read of all ~90
  commits across the substantive branches combined. There is a real chance
  a specific commit among those is less impressive than its message
  suggests, though the two spot-checked commits (one from each lineage)
  both matched their commit messages when read in full.
- **`jit` and `results` branches were not diffed against upstream's own
  `jit`/`results`** (only against `master`, where they 404 as unrelated
  histories, expected since they're rootless w.r.t. master in upstream too).
  If a fork silently modified its own `jit`/`results` branch without that
  showing up as "ahead of master" it would be missed. Given zero forks
  showed any interest in these branches elsewhere (no fork's `cleanup`
  differs from upstream's either), judged unlikely, not verified.
- **The `forks_count: 47` vs `39` direct-fork discrepancy is unexplained.**
  It's flagged as an anomaly rather than resolved; total network size
  (39 direct + 8 nested = 47) matching that number exactly could be
  coincidence or could indicate `forks_count` on GitHub counts the whole
  network in some circumstances — not investigated, doesn't change any
  substantive conclusion since the 8 nested forks were found and checked
  independently of that number.
- **No sentiment/social check** (stars, issues, discussions, README badges
  claiming CI status) beyond what was needed to corroborate `parsa`'s and
  `magras`'s authorship (their GitHub user profiles) and to confirm no PRs
  were opened upstream.

## Files in `scoping-notes/` supporting this report

- `all-forks.txt` — the 47-repo list.
- `forks-level1.tsv` — raw 39 direct forks with push dates/fork counts.
- `branches/*.txt` — branch list per repo (47 files).
- `compare-results.tsv` — ahead/behind for all 163 branches (raw script output).
- `compare-details/*.json` — full compare payload (commits + files) for the
  15 candidate branches plus a few zero/near-zero ones checked as controls.
- `compare-details-summary.txt` — human-readable digest of the above.
- `parsa-readme.md`, `parsa-pie-commit.json`, `magras-tls-commit.json` —
  primary-source verification for the `parsa`/`magras` (fork-graph) findings.
- `dead2-readme.md` — full README of the detached `Dead2/stabilizer` repo
  (the project's own honest status report: what works, what crashes).
- `dead2-develop-commits-full.txt` — all 152 commits on `Dead2/stabilizer:develop`,
  paginated in full back to the shared 2012 root commit.
- `dead2-vs-upstream.err` — proof that `ccurtsinger/stabilizer` cannot
  cross-network-compare against the detached `Dead2/stabilizer` (404),
  supporting the coverage note that this repo is structurally invisible to
  a fork-graph walk.
- `search1.tsv`…`search9.err` — the nine `search/repositories` and
  `search/code` queries run while hunting for further detached copies.
- `fetch_branches.sh`, `compare_all.sh`, `fetch_compare_details.sh` — the
  scripts used against the `ccurtsinger` network, for reproducibility.
