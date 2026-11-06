"""Shared helpers for id-generator's data build (stdlib only)."""
import csv
from pathlib import Path

csv.field_size_limit(10**7)

ROOT = Path(__file__).resolve().parent.parent
CURATED = ROOT / "scripts" / "curated"
RAW = ROOT / "raw_data"
RESOURCES = ROOT / "resources"
SIBLINGS = ROOT.parent
TIERS = ("lite", "full")


def read_tsv(path):
    """Rows of a TSV as dicts; `#` lines are comments, short rows are padded."""
    with open(path, encoding="utf-8", newline="") as f:
        lines = [line for line in f.read().split("\n") if line and not line.startswith("#")]
    header = lines[0].rstrip("\r").split("\t")
    rows = []
    for line in lines[1:]:
        cells = line.rstrip("\r").split("\t")
        cells += [""] * (len(header) - len(cells))
        rows.append(dict(zip(header, cells)))
    return rows


def write_table(tier, name, comment, columns, rows):
    """Write resources/<tier>/<name> with the source in its first line."""
    path = RESOURCES / tier / name
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# {comment}\n")
        f.write("\t".join(columns) + "\n")
        for row in rows:
            cells = [str(row.get(c, "")) for c in columns]
            for cell in cells:
                if "\t" in cell or "\n" in cell:
                    raise ValueError(f"{name}: a control character in {cell!r}")
            f.write("\t".join(cells) + "\n")
    return path


def write_all_tiers(name, comment, columns, rows):
    for tier in TIERS:
        write_table(tier, name, comment, columns, rows)
