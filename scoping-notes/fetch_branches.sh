#!/usr/bin/env bash
set -uo pipefail
outdir="/home/matthias/prog/stabilizer/scoping-notes/branches"
mkdir -p "$outdir"
while IFS= read -r repo; do
  owner="${repo%%/*}"
  safe="${repo//\//_}"
  gh api "repos/${repo}/branches" --paginate --jq '.[].name' > "${outdir}/${safe}.txt" 2> "${outdir}/${safe}.err"
done < /home/matthias/prog/stabilizer/scoping-notes/all-forks.txt
