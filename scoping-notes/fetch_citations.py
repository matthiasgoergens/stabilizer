#!/usr/bin/env python3
"""Fetch all citing works for a given OpenAlex work ID, paginating via cursor.
Writes one JSON object per line to the given output file: id, title, year, doi,
cited_by_count of the citing paper, and a short abstract reconstructed from
abstract_inverted_index if present.
"""
import sys
import json
import urllib.request
import time

work_id = sys.argv[1]
out_path = sys.argv[2]

def reconstruct_abstract(inv_index):
    if not inv_index:
        return ""
    positions = {}
    for word, idxs in inv_index.items():
        for i in idxs:
            positions[i] = word
    if not positions:
        return ""
    maxpos = max(positions.keys())
    words = [positions.get(i, "") for i in range(maxpos + 1)]
    return " ".join(words)

cursor = "*"
count_total = None
records = []
while cursor:
    url = ("https://api.openalex.org/works?filter=cites:%s&per-page=200&cursor=%s"
           % (work_id, cursor))
    req = urllib.request.Request(url, headers={"User-Agent": "stabilizer-scoping-research/1.0 (mailto:matthias.goergens@gmail.com)"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            break
        except Exception as e:
            sys.stderr.write("attempt %d failed: %s\n" % (attempt, e))
            time.sleep(3)
    else:
        sys.stderr.write("giving up on cursor %s\n" % cursor)
        break

    if count_total is None:
        count_total = data["meta"]["count"]
        sys.stderr.write("total citing works: %d\n" % count_total)

    for r in data["results"]:
        rec = {
            "id": r["id"],
            "title": r.get("title"),
            "year": r.get("publication_year"),
            "doi": r.get("doi"),
            "cited_by_count": r.get("cited_by_count"),
            "abstract": reconstruct_abstract(r.get("abstract_inverted_index")),
            "type": r.get("type"),
            "primary_topic": (r.get("primary_topic") or {}).get("display_name"),
        }
        records.append(rec)

    cursor = data["meta"].get("next_cursor")
    sys.stderr.write("fetched %d / %d\n" % (len(records), count_total))
    time.sleep(0.2)

with open(out_path, "w") as f:
    for rec in records:
        f.write(json.dumps(rec) + "\n")

sys.stderr.write("DONE: wrote %d records to %s\n" % (len(records), out_path))
