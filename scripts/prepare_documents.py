#!/usr/bin/env python3
"""Write documents.tsv and validity.tsv for both tiers from the document survey.

Every country and document that exists gets rows covering all dates:
- the surveyed format, from the year the survey gives;
- the older formats in curated/document_formats_older.tsv;
- a generic format (nine digits) before those, or for all dates when the survey
  found no format (decision 13).

A document with no surveyed validity has no rows in validity.tsv; the library's
generic rule applies and the result says so (generic_validity).
"""
import re
from collections import Counter

from idgen_data import CURATED, read_tsv, write_all_tiers

DOC_COLUMNS = ["key", "country", "kind", "pattern", "from", "until", "mrz", "evidence",
               "prefix_places", "mrz_code", "mrz_optional", "min_age", "weight"]
ALWAYS = "1900-01-01"  # `from` is part of the validity key, which cannot be empty
VALIDITY_COLUMNS = ["document", "from_age", "years", "until_age", "from", "until", "expiry_rule"]

EARLIEST = "1900-01-01"
GENERIC_PATTERN = "#{9}"
STANDARD_CODE = {"passport": "P<", "id_card": "ID"}

# Exact start dates where the survey's notes give more than a year.
FROM_DATE = {
    ("BG", "id_card"): "2024-06-17", ("CH", "passport"): "2022-10-31", ("DE", "passport"): "2007-11-01",
    ("HU", "id_card"): "2000-01-01", ("IN", "passport"): "2024-04-01", ("KR", "passport"): "2021-12-21",
    ("LI", "id_card"): "2009-01-01", ("NL", "id_card"): "2019-12-01", ("NL", "passport"): "2019-12-01",
    ("RO", "id_card"): "2025-03-20", ("SI", "id_card"): "2022-03-28",
}

# The notes say the pattern the survey recorded should be narrowed for real output.
PATTERN_FIX = {("TW", "passport"): "3#{8}"}

# MRZ document codes from specimens, when not P< or ID (survey notes).
MRZ_CODE = {
    ("AT", "passport"): "PP", ("AZ", "passport"): "PC", ("BH", "passport"): "PB", ("BS", "passport"): "PA",
    ("CH", "passport"): "PM", ("DO", "passport"): "PP", ("IS", "passport"): "PA", ("LI", "passport"): "PP",
    ("MD", "passport"): "PA", ("RO", "passport"): "PE", ("SK", "passport"): "PP", ("SY", "passport"): "PN",
    ("VA", "passport"): "PP", ("YE", "passport"): "PR",
    ("AL", "id_card"): "I<", ("AM", "id_card"): "I<", ("CL", "id_card"): "IN", ("HR", "id_card"): "IO",
    ("KG", "id_card"): "I<", ("LT", "id_card"): "I<", ("LV", "id_card"): "I<", ("MA", "id_card"): "I<",
    ("MC", "id_card"): "I<", ("NO", "id_card"): "CA", ("PT", "id_card"): "I<", ("SE", "id_card"): "I<",
    ("TR", "id_card"): "I<", ("US", "id_card"): "IP",
}
# Codes a newer model brought in: older rows use the standard code, or the one given here.
MRZ_CODE_OLDER = {("AT", "passport"): "", ("DO", "passport"): "", ("LI", "passport"): "", ("RO", "passport"): "",
                  ("SK", "passport"): "", ("VA", "passport"): "PV"}

# The MRZ optional field carries this scheme where the survey states it without
# hedging ("probably" and "unknown" rows are left out).
MRZ_OPTIONAL = {
    ("AE", "id_card"): "ae-eid", ("AR", "passport"): "ar-dni", ("BA", "passport"): "ba-jmb",
    ("BE", "id_card"): "be-nn", ("BG", "id_card"): "bg-egn", ("BG", "passport"): "bg-egn",
    ("CL", "id_card"): "cl-run", ("CL", "passport"): "cl-run", ("CZ", "passport"): "cz-rc",
    ("DK", "passport"): "dk-cpr", ("EC", "passport"): "ec-cedula", ("EE", "id_card"): "ee-isikukood",
    ("EE", "passport"): "ee-isikukood", ("FI", "passport"): "fi-hetu", ("HK", "passport"): "hk-hkid",
    ("HR", "id_card"): "hr-oib", ("IS", "id_card"): "is-kennitala", ("IS", "passport"): "is-kennitala",
    ("KZ", "id_card"): "kz-iin", ("LT", "id_card"): "lt-asmens-kodas", ("LT", "passport"): "lt-asmens-kodas",
    ("LV", "id_card"): "lv-personas-kods", ("LV", "passport"): "lv-personas-kods", ("ME", "passport"): "me-jmbg",
    ("MK", "passport"): "mk-embg", ("MY", "passport"): "my-mykad", ("NO", "id_card"): "no-fodselsnummer",
    ("PL", "id_card"): "pl-pesel", ("RO", "passport"): "ro-cnp", ("RS", "id_card"): "rs-jmbg",
    ("RS", "passport"): "rs-jmbg", ("SE", "id_card"): "se-personnummer", ("SE", "passport"): "se-personnummer",
    ("SI", "passport"): "si-emso", ("SK", "passport"): "sk-rc", ("TR", "id_card"): "tr-tckn",
    ("TW", "passport"): "tw-nid", ("UY", "passport"): "uy-cedula", ("VN", "passport"): "vn-cccd",
}

# Survey values overridden by what its own notes say. Russia's internal passport
# has only a non-ICAO national zone (design 5.1), so it gets no MRZ.
MRZ_FIX = {("RU", "id_card"): "none"}
VALIDITY_FIX = {
    ("AR", "id_card"): "0-4:until_age_8;5-13:until_age_14;14-:15",
    ("DO", "id_card"): "16-17:until_age_18;18-35:12;36-60:16;61-:20",
    ("JP", "id_card"): "0-17:5;18-:10",
    ("MN", "id_card"): "16-24:until_age_25;25-44:until_age_45;45-:0",
    ("MT", "id_card"): "14-15:until_age_16;16-17:until_age_18;18-:10",
    ("VN", "id_card"): "0-13:until_age_14;14-24:until_age_25;25-39:until_age_40;40-59:until_age_60;60-:0",
}
EXPIRY_FIX = {
    ("HU", "id_card"): "birthday", ("IS", "id_card"): "application", ("IS", "passport"): "application",
    ("IT", "id_card"): "birthday", ("JP", "id_card"): "birthday", ("TH", "id_card"): "birthday",
}
# (country, kind): (date the current rule began, the earlier rule's validity string).
VALIDITY_CHANGES = {
    ("GD", "passport"): ("2024-07-01", "5"),
    ("GH", "passport"): ("2019-03-31", "5"),
    ("GR", "passport"): ("2022-09-01", "0-13:3;14-:5"),
    ("NA", "passport"): ("2020-01-01", "5"),
    ("SG", "passport"): ("2021-10-01", "5"),
}


def kind_slug(kind):
    return kind.replace("_", "-")


def parse_validity(text):
    """Bands of (from_age, years, until_age) from the survey's validity string."""
    bands = []
    for part in text.split(";"):
        if ":" not in part:
            bands.append((0, part.strip(), ""))
            continue
        ages, value = part.split(":")
        start = int(ages.split("-")[0])
        if value.startswith("until_age_"):
            bands.append((start, "", value.removeprefix("until_age_")))
        else:
            bands.append((start, value, ""))
    return bands


def validity_rows(row, cc, kind):
    text = VALIDITY_FIX.get((cc, kind), row["validity"])
    if not text:
        return [], ""
    rule = EXPIRY_FIX.get((cc, kind), row["expiry_rule"])
    if rule not in ("day_before", "same_day", "birthday", "application"):
        rule = "same_day"  # design 5.2: an unknown rule uses same_day
    document = f"{cc.lower()}-{kind_slug(kind)}"
    rows = []
    change = VALIDITY_CHANGES.get((cc, kind))
    for start, years, until_age in parse_validity(text):
        rows.append({"document": document, "from_age": start, "years": years, "until_age": until_age,
                     "from": change[0] if change else ALWAYS, "until": "", "expiry_rule": rule})
    if change:
        for start, years, until_age in parse_validity(change[1]):
            rows.append({"document": document, "from_age": start, "years": years, "until_age": until_age,
                         "from": ALWAYS, "until": change[0], "expiry_rule": rule})
    first_age = min(int(r["from_age"]) for r in rows)
    return rows, first_age


def format_key(cc, kind, start):
    slug = "current" if start == EARLIEST else start[:4]
    return f"{cc.lower()}-{kind_slug(kind)}-{slug}"


def main():
    survey = read_tsv(CURATED / "survey_documents.tsv")
    older = {}
    for row in read_tsv(CURATED / "document_formats_older.tsv"):
        older.setdefault((row["country"], row["kind"]), []).append(row)

    documents, validity = [], []
    stats = Counter()
    for row in survey:
        cc, kind = row["cc"], row["doc"]
        if row["exists"] != "yes":
            stats[f"no {kind}"] += 1
            continue
        if kind == "passport":
            mrz = "td3"
        else:
            mrz = row["mrz"] if row["mrz"] in ("td1",) else "none"
        mrz = MRZ_FIX.get((cc, kind), mrz)

        v_rows, first_age = validity_rows(row, cc, kind)
        validity += v_rows
        stats[f"{kind} with validity" if v_rows else f"{kind} generic validity"] += 1
        min_age = row["min_age"]
        # A surveyed rule that starts above the stated minimum age: no card below it.
        if v_rows and first_age and (not min_age or int(min_age) < first_age):
            min_age = str(first_age)

        base = {"country": cc, "kind": kind, "mrz": mrz, "min_age": min_age, "weight": "",
                "prefix_places": "", "mrz_optional": MRZ_OPTIONAL.get((cc, kind), "")}
        code = MRZ_CODE.get((cc, kind), "")
        code_old = MRZ_CODE_OLDER.get((cc, kind), code)

        pattern = PATTERN_FIX.get((cc, kind), row["number_pattern"])
        surveyed = bool(pattern) and row["evidence"] != "none"
        formats = []
        if surveyed:
            start = FROM_DATE.get((cc, kind)) or (f"{row['format_since']}-01-01" if row["format_since"] else EARLIEST)
            formats.append({**base, "pattern": pattern, "from": start, "until": "",
                            "evidence": row["evidence"], "mrz_code": code})
            for old in older.get((cc, kind), []):
                formats.append({**base, "pattern": old["pattern"], "from": old["from"], "until": old["until"],
                                "evidence": old["evidence"], "mrz_code": code_old})
            formats.sort(key=lambda f: f["from"])
            # Close gaps: each format runs until the next begins, and a generic
            # format covers whatever comes before the first known one.
            filled = []
            cursor = EARLIEST
            for f in formats:
                if f["from"] > cursor:
                    filled.append({**base, "pattern": GENERIC_PATTERN, "from": cursor, "until": f["from"],
                                   "evidence": "generic", "mrz_code": code_old})
                filled.append(f)
                cursor = f["until"] or "9999-12-31"
            formats = filled
            stats[f"{kind} surveyed format"] += 1
        else:
            formats.append({**base, "pattern": GENERIC_PATTERN, "from": EARLIEST, "until": "",
                            "evidence": "generic", "mrz_code": code})
            stats[f"{kind} generic format"] += 1
        for f in formats:
            f["key"] = (f"{cc.lower()}-{kind_slug(kind)}-generic" if f["evidence"] == "generic"
                        else format_key(cc, kind, f["from"]))
        keys = [f["key"] for f in formats]
        if len(keys) != len(set(keys)):
            # A generic row in two gaps: number the later ones by their start year.
            seen = set()
            for f in formats:
                if f["key"] in seen:
                    f["key"] = f"{f['key']}-{f['from'][:4]}"
                seen.add(f["key"])
        documents += formats

    documents.sort(key=lambda f: f["key"])
    validity.sort(key=lambda v: (v["document"], v["from"], int(v["from_age"])))
    write_all_tiers("documents.tsv",
                    "document survey 2026-11-04 (issuers' pages, Purview CC BY 4.0, Wikipedia, Commons specimens read only); generic = no surveyed format",
                    DOC_COLUMNS, documents)
    write_all_tiers("validity.tsv",
                    "document survey 2026-11-04: validity by age at issue; a document without rows uses the generic rule",
                    VALIDITY_COLUMNS, validity)
    print(f"documents.tsv: {len(documents)} rows; validity.tsv: {len(validity)} rows")
    for k, v in sorted(stats.items()):
        print(f"  {k}: {v}")
    # Which format applies on the default reference date, by population-free count.
    on_date = Counter()
    for f in documents:
        if f["from"] <= "2023-07-01" and (not f["until"] or f["until"] > "2023-07-01"):
            on_date[(f["kind"], "generic" if f["evidence"] == "generic" else "surveyed")] += 1
    print(f"  in force on 2023-07-01: {dict(on_date)}")
    if re.search(r"\s", "".join(f["pattern"] for f in documents)):
        raise SystemExit("a pattern has white space")


if __name__ == "__main__":
    main()
