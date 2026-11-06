#!/usr/bin/env python3
"""Write series.tsv for both tiers: when sequential numbers were issued.

US SSN groups come from the SSA's High Group Lists as archived by the Wayback
Machine (raw_data/ssa_*). A group is stored as its place in the SSA's issuing order
(odd 01-09, even 10-98, even 02-08, odd 11-99), so anchors rise with the date. The
oldest list found is from January 1999, so everything before it is a straight line
from the first group on 1936-11-01 (or, for areas opened later, from a date
extrapolated from the area's own rate). The other schemes' anchors are in
curated/series_anchors.tsv.
"""
import html
import re
from collections import defaultdict
from datetime import date, timedelta

from idgen_data import CURATED, RAW, read_tsv, write_all_tiers

COLUMNS = ["scheme", "segment", "date", "value", "note"]
START = date(1936, 11, 1)
RAILROAD_END = date(1963, 7, 1)
FIRST_LATE_AREA = 587  # areas below this were allocated with the first numbers


def group_order():
    order = list(range(1, 10, 2)) + list(range(10, 99, 2)) + list(range(2, 9, 2)) + list(range(11, 100, 2))
    return {f"{g:02d}": i + 1 for i, g in enumerate(order)}


def read_lists():
    lists = {}
    for path in sorted(RAW.glob("ssa_*highgroup*")):
        text = html.unescape(re.sub(r"<[^>]+>", " ", path.read_text(encoding="latin-1")))
        # The list's date is the first date on the page; the pairs start at area 001.
        m = re.search(r"\b(\d{1,2})/(\d{1,2})/(\d{2,4})\b", text)
        if not m:
            raise SystemExit(f"{path.name}: no date")
        month, day, year = int(m[1]), int(m[2]), int(m[3])
        if year < 100:
            year += 2000 if year < 50 else 1900
        start = re.search(r"\b001\s+\d{2}\*?\s", text)
        pairs = dict(re.findall(r"\b(\d{3})\s+(\d{2})\*?(?=\s|$)", text[start.start():])) if start else {}
        if len(pairs) < 600:
            raise SystemExit(f"{path.name}: only {len(pairs)} areas")
        lists[date(year, month, day)] = pairs
    return dict(sorted(lists.items()))


def main():
    order = group_order()
    lists = read_lists()
    if not lists:
        raise SystemExit("no SSA High Group Lists in raw_data: run fetch_raw.py")
    print("SSA lists:", ", ".join(f"{d} ({len(p)} areas)" for d, p in lists.items()))

    areas = defaultdict(list)
    for when, pairs in lists.items():
        for area, group in pairs.items():
            if group in order:
                areas[area].append((when, order[group]))

    rows = []
    for area, points in sorted(areas.items()):
        points.sort()
        # Keep the first date each value is seen, and a list's value never falls.
        clean = []
        for when, value in points:
            if clean and value < clean[-1][1]:
                raise SystemExit(f"area {area}: group order falls at {when}")
            if not clean or value > clean[-1][1]:
                clean.append((when, value))
        first_when, first_value = clean[0]
        if int(area) >= 700 and int(area) <= 728:
            # Railroad numbers stopped in 1963, whatever later lists repeat.
            anchors = [(START, 1), (RAILROAD_END, max(first_value, 2))]
        else:
            if first_value == 1:
                anchors = clean
            else:
                if int(area) < FIRST_LATE_AREA or len(clean) < 2:
                    opened = START
                else:
                    second_when, second_value = clean[1]
                    days_per_group = (second_when - first_when).days / (second_value - first_value)
                    opened = max(START, first_when - timedelta(days=days_per_group * (first_value - 1)))
                anchors = [(opened, 1)] + clean
        for when, value in anchors:
            rows.append({"scheme": "us-ssn", "segment": area, "date": when.isoformat(), "value": value, "note": ""})

    # `segment` is part of the table key: "-" for a scheme with a single series.
    for r in read_tsv(CURATED / "series_anchors.tsv"):
        rows.append({**r, "segment": r["segment"] or "-"})
    rows.sort(key=lambda r: (r["scheme"], r["segment"], r["date"]))
    write_all_tiers("series.tsv",
                    "SSA High Group Lists 1999-2011 (Wayback Machine; US public domain; values are the place in the issuing order); AR, CL, PE from curated/series_anchors.tsv (see note column)",
                    COLUMNS, rows)
    print(f"series.tsv: {len(rows)} rows, {len(areas)} SSN areas")


if __name__ == "__main__":
    main()
