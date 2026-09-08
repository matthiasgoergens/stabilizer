# Preserved resurrection evidence

These documents are canonical, version-controlled copies of reports that were
previously stored only in untracked experiment directories on this machine.
They preserve the investigation narrative and its references to raw artefacts;
large logs, build trees, containers, and benchmark outputs remain in the source
directories named below.

| document | original location | subject |
|---|---|---|
| `bug5-notes.md` | `../stabilizer-bug5/NOTES.md` | deterministic `-Rcode` corruption and defects 5a–5c |
| `parsa-fix-notes.md` | `../stabilizer-parsa-fix/NOTES.md` | diagnosis and validation of the LLVM 21 runtime fixes |
| `parsa-verify-notes.md` | `../stabilizer-parsa-verify/NOTES.md` | initial verification of Parsa Amini's LLVM 21 port |
| `period-container-notes.md` | `../stabilizer-period/NOTES.md` | reproduction of original Stabilizer under its period toolchain |
| `threads-design.md` | `../stabilizer-threads-design/DESIGN.md` | proposed concurrency design and falsifying experiment |

The copies were imported on 2026-09-08 without rewriting their technical
content. Relative paths in them therefore describe the original experiment
directories. When a conclusion depends on an external artefact, consult
`scoping-notes/evidence-manifest.md` and verify that artefact before publishing
the claim.
