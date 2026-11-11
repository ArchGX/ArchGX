# Game database

ArchGX identifies a HuCard dump in two steps:
1. **CRC32** of the cleaned dump: an exact match.
2. If there's no CRC match, the **8-byte header at offset 0x200**, used only when it points to exactly one title. All-zero and all-0xFF headers are ignored.

Two CSV files are searched; your own entries win over the built-in ones:

| File | Where | Who edits it |
|---|---|---|
| `gamedb.csv` | `/usr/share/archgx/` on the console (from `system/share/gamedb.csv`) | ArchGX releases |
| `gamedb-local.csv` | the card's large partition | You. Naming an unknown cart on the console adds a line |

## Format

```
crc32,header,title,region,notes
0xe7f8375c,,"Strife Sisters",,named on console
```

| Column | Content |
|---|---|
| `crc32` | CRC32 of the cleaned dump, `0x` + 8 hex digits |
| `header` | Optional: the 8 header bytes at 0x200 as 16 hex digits, `0x`-prefixed |
| `title` | The name to show; quote it if it contains commas |
| `region` | Optional, for example `US` or `JP` |
| `notes` | Optional free text |

The file opens and saves in any spreadsheet program, so CSV with or without CRLF line endings is fine. Within one file, the newest line for a CRC wins.

## Contributing entries

Name your unknown carts on the console, then send us your `gamedb-local.csv` (or just the new lines), as a GitHub issue or a pull request that adds them to `system/share/gamedb.csv`.

Maintainers merge submissions with the merge tool:

```sh
tools/gamedb-merge.py system/share/gamedb.csv submission.csv -o system/share/gamedb.csv
```

The tool:
- reports conflicts (same CRC, different title) and applies them only with `--accept-conflicts`;
- rejects malformed rows;
- keeps the last row of a submission when it repeats a CRC.

Only checksums and titles go into the database, never game data.
