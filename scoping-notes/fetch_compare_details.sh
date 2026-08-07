#!/usr/bin/env bash
set -uo pipefail
outdir="/home/matthias/prog/stabilizer/scoping-notes/compare-details"
mkdir -p "$outdir"
# format: owner branch
while IFS=' ' read -r owner branch; do
  [ -z "$owner" ] && continue
  safe="${owner}_${branch}"
  gh api "repos/ccurtsinger/stabilizer/compare/master...${owner}:${branch}" > "${outdir}/${safe}.json" 2> "${outdir}/${safe}.err"
done <<'EOF'
fusiled master
dendibakh master
jgall master
atw1020 master
magras master
magras fix-tls
timadye master
parsa master
parsa upgrade_llvm_19
thinkmoore master
schrummy14 master
nickhutchinson master
plasma-umass master
tristan-potter master
yqzhang master
EOF
