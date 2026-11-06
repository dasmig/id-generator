#!/usr/bin/env python3
"""Write countries.tsv for both tiers: the 199 countries the document survey covers.

Population comes from country-generator's full tier. An ICAO 9303 code is the
ISO alpha-3 code unless curated/icao_codes.tsv says otherwise. The primary scheme
is the only one a country has, or the one in PRIMARY when it has two.
"""
from idgen_data import CURATED, SIBLINGS, read_tsv, write_all_tiers

PRIMARY = {"AR": "ar-dni", "RU": "ru-snils"}
COLUMNS = ["country_code", "population", "icao_code", "primary_scheme", "translit"]


def main():
    source = {r["cca2"]: r for r in read_tsv(SIBLINGS / "country-generator/resources/full/countries.tsv")}
    survey = sorted({r["cc"] for r in read_tsv(CURATED / "survey_documents.tsv")})
    icao = {r["country_code"]: r["icao_code"] for r in read_tsv(CURATED / "icao_codes.tsv")}
    by_country = {}
    for row in read_tsv(CURATED / "survey_numbers.tsv"):
        if row["served"] == "yes":
            by_country.setdefault(row["cc"], []).append(row["scheme"])
    rows = []
    for cc in survey:
        schemes = by_country.get(cc, [])
        if len(schemes) > 1 and cc not in PRIMARY:
            raise SystemExit(f"{cc} has several schemes and no primary: {schemes}")
        rows.append({"country_code": cc, "population": source[cc]["population"],
                     "icao_code": icao.get(cc, source[cc]["cca3"]),
                     "primary_scheme": PRIMARY.get(cc, schemes[0] if schemes else ""),
                     "translit": ""})
    write_all_tiers("countries.tsv",
                    "country-generator (population); ICAO Doc 9303 Part 3 and the survey's specimens (codes); number survey (primary scheme)",
                    COLUMNS, rows)
    print(f"countries.tsv: {len(rows)} countries, {sum(1 for r in rows if r['primary_scheme'])} with a primary scheme")


if __name__ == "__main__":
    main()
