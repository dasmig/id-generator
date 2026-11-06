#!/usr/bin/env python3
"""Write countries.tsv for both tiers: the 199 countries the document survey covers.

Population comes from country-generator's full tier. An ICAO 9303 code is the
ISO alpha-3 code unless curated/icao_codes.tsv says otherwise. The primary scheme
is the only one a country has, or the one in PRIMARY when it has two.
"""
from idgen_data import CURATED, SIBLINGS, read_tsv, write_all_tiers

PRIMARY = {"AR": "ar-dni", "RU": "ru-snils"}
COLUMNS = ["country_code", "population", "icao_code", "primary_scheme", "translit"]

# The national exceptions in ICAO Doc 9303 Part 3, section 6.B. `^` marks a rule for
# a name's first letter. The table also gives Serbian Г as H, which is left out:
# Serbian Г is G. Macedonian Ѓ takes the exception on the table's row U+0492, which
# can only mean that letter.
TRANSLIT = {
    "BY": "Ё=IO;Г=H",
    "BG": "Щ=SHT",
    "MK": "Ѓ=GJ;Ќ=KJ;Џ=DJ;Х=H;Ц=C",
    "RS": "Ж=Z;Х=H;Ц=C;Ч=C;Ш=S",
    "UA": "Г=H;И=Y;^Й=Y;^Є=YE;^Ї=YI;^Ю=YU;^Я=YA",
}


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
                     "translit": TRANSLIT.get(cc, "")})
    write_all_tiers("countries.tsv",
                    "country-generator (population); ICAO Doc 9303 Part 3 and the survey's specimens (codes); number survey (primary scheme)",
                    COLUMNS, rows)
    print(f"countries.tsv: {len(rows)} countries, {sum(1 for r in rows if r['primary_scheme'])} with a primary scheme")


if __name__ == "__main__":
    main()
