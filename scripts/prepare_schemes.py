#!/usr/bin/env python3
"""Write schemes.tsv for both tiers.

curated/schemes.tsv says how each scheme is built. The country, the labels and
the source come from curated/survey_numbers.tsv, the number survey of
2026-11-04, so they are not written twice.
"""
from idgen_data import CURATED, read_tsv, write_all_tiers

COLUMNS = ["key", "country", "label", "native_label", "builder", "display", "assigned",
           "safe_method", "pattern", "check", "place_source", "from", "until",
           "safe_pattern", "params", "source"]


def first_url(sources):
    for part in sources.split(" ; "):
        part = part.strip()
        if part.startswith("http"):
            return part.split(" ")[0]
    return sources.split(" ; ")[0].strip()


def main():
    survey = {r["scheme"]: r for r in read_tsv(CURATED / "survey_numbers.tsv")}
    served = {k for k, r in survey.items() if r["served"] == "yes"}
    rows = []
    for row in read_tsv(CURATED / "schemes.tsv"):
        s = survey[row["key"]]
        rows.append({**row, "country": s["cc"], "label": s["label"],
                     "native_label": s["native_label"], "source": first_url(s["sources"])})
    missing = served - {r["key"] for r in rows}
    extra = {r["key"] for r in rows} - served
    if missing or extra:
        raise SystemExit(f"schemes.tsv differs from the survey: missing {sorted(missing)}, extra {sorted(extra)}")
    rows.sort(key=lambda r: r["key"])
    write_all_tiers("schemes.tsv",
                    "curated from each authority's published rules (number survey 2026-11-04); source column per row",
                    COLUMNS, rows)
    print(f"schemes.tsv: {len(rows)} schemes in {len({r['country'] for r in rows})} countries")


if __name__ == "__main__":
    main()
