#!/usr/bin/env bash
set -uo pipefail
outdir="/home/matthias/prog/stabilizer/scoping-notes/branches"
resultfile="/home/matthias/prog/stabilizer/scoping-notes/compare-results.tsv"
: > "$resultfile"
while IFS= read -r repo; do
  safe="${repo//\//_}"
  branchfile="${outdir}/${safe}.txt"
  [ -f "$branchfile" ] || continue
  while IFS= read -r branch; do
    [ -z "$branch" ] && continue
    owner="${repo%%/*}"
    res=$(gh api "repos/ccurtsinger/stabilizer/compare/master...${owner}:${branch}" --jq '[.status, (.ahead_by|tostring), (.behind_by|tostring), (.total_commits|tostring)] | @tsv' 2>&1)
    printf '%s\t%s\t%s\n' "$repo" "$branch" "$res" >> "$resultfile"
  done < "$branchfile"
done < /home/matthias/prog/stabilizer/scoping-notes/all-forks.txt
