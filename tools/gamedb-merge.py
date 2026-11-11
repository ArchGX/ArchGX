#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of ArchGX (https://github.com/ArchGX/ArchGX)
"""gamedb-merge.py - merge community submissions into the ArchGX game database.

Usage:
    python3 gamedb-merge.py gamedb.csv submission1.csv [submission2.csv ...] [-o merged.csv] [--accept-conflicts]

Files are CSV with the columns crc32,header,title,region,notes - exactly what
a console writes to gamedb-local.csv, or what a spreadsheet saves. Accepted:
UTF-8 with or without BOM, "," or ";" separators, codes with or without "0x",
codes that lost leading zeros in a spreadsheet. Older tab-separated .tsv
submissions are read too.

Rules:
  * Entries are keyed by crc32.
  * New crc32 values are appended to the database.
  * A crc32 already in the database with the same title is skipped.
  * A crc32 already in the database with a different title is a CONFLICT:
    listed for review and only applied with --accept-conflicts.
  * Within one submission the last row for a crc32 wins (the console adds a
    row each time a game is renamed).
  * Malformed rows are reported and ignored.
The output keeps the base file's rows in order and writes codes as 0x...
so spreadsheets keep them as text.
"""
import csv
import re
import sys

HEX = re.compile(r"^[0-9a-f]+$")


def code(value, width):
    v = value.strip().lower()
    if v.startswith("0x"):
        v = v[2:]
    if not v:
        return ""
    if not HEX.match(v) or len(v) > width:
        return None
    return v.rjust(width, "0")


def read_rows(path):
    rows, problems = [], []
    with open(path, encoding="utf-8-sig", errors="replace", newline="") as f:
        text = f.read()
    first = text.splitlines()[0] if text else ""
    if "\t" in first and "," not in first:
        delim = "\t"
    elif ";" in first and "," not in first:
        delim = ";"
    else:
        delim = ","
    for n, rec in enumerate(csv.reader(text.splitlines(), delimiter=delim), 1):
        if not rec or not "".join(rec).strip() or rec[0].strip().startswith("#"):
            continue
        if rec[0].strip().strip('"').lower() == "crc32":
            continue
        rec = (rec + [""] * 5)[:5]
        crc, hdr, title = code(rec[0], 8), code(rec[1], 16), rec[2].strip()
        if not crc or hdr is None or not title:
            problems.append(f"{path}:{n}: ignored malformed row: {rec}")
            continue
        rows.append([crc, hdr, title, rec[3].strip(), rec[4].strip()])
    return rows, problems


def write_rows(path, rows):
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\r\n")
        w.writerow(["crc32", "header", "title", "region", "notes"])
        for crc, hdr, title, region, notes in rows:
            w.writerow(["0x" + crc, "0x" + hdr if hdr else "", title, region, notes])


def main(argv):
    out = argv[argv.index("-o") + 1] if "-o" in argv else None
    args = [a for a in argv if not a.startswith("-") and a != out]
    accept = "--accept-conflicts" in argv
    if len(args) < 2:
        print(__doc__)
        return 2
    base_rows, problems = read_rows(args[0])
    known = {}
    for r in base_rows:
        known.setdefault(r[0], set()).add(r[2])

    added, conflicts, skipped = [], [], 0
    for sub in args[1:]:
        rows, p = read_rows(sub)
        problems += p
        latest = {}
        for r in rows:
            latest[r[0]] = r
        for crc, r in latest.items():
            if crc not in known:
                added.append(r)
                known[crc] = {r[2]}
            elif r[2] in known[crc]:
                skipped += 1
            else:
                conflicts.append((sub, r, sorted(known[crc])))
                if accept:
                    added.append(r)
                    known[crc].add(r[2])

    if out:
        write_rows(out, base_rows + added)
    print(f"{len(added)} added, {skipped} already known, {len(conflicts)} conflicts, {len(problems)} malformed")
    for sub, r, titles in conflicts:
        state = "applied" if accept else "NOT applied"
        print(f"CONFLICT ({state}) 0x{r[0]}: {r[2]!r} from {sub}; database has {', '.join(map(repr, titles))}")
    for p in problems:
        print(p)
    if not out:
        print("(dry run: use -o merged.csv to write the result)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
