#!/usr/bin/env python3
"""Write name_rules.tsv and test_numbers.tsv for both tiers.

test_numbers.tsv holds the published test numbers that are lists rather than
ranges: Skatteverket's test personnummer (open data; 43,895 rows that hold 42,939
different numbers, so duplicates are dropped) and Thjodskra's six test kennitolur.
`lite` keeps every 20th Swedish number, enough to find one near any birth date.
"""
from idgen_data import CURATED, RAW, read_tsv, write_all_tiers, write_table

KENNITALA = ["2811913049", "0209804299", "0404044400", "1005148220", "2412198560", "3003228340"]
TEST_COLUMNS = ["scheme", "value"]
LITE_STEP = 20


def main():
    rules = read_tsv(CURATED / "name_rules.tsv")
    write_all_tiers("name_rules.tsv",
                    "RENAPO, Reglas para la asignacion de la CURP (2021), section 5; the words: django-localflavor (BSD, see LICENSE_DATA.txt) and VACA, not RENAPO's Anexo 01",
                    ["scheme", "kind", "word", "replacement"], rules)

    swedish = sorted({line.strip() for line in (RAW / "se_test_numbers.txt").read_text().splitlines() if line.strip()})
    full = [{"scheme": "is-kennitala", "value": v} for v in KENNITALA]
    lite = list(full)
    full += [{"scheme": "se-personnummer", "value": v} for v in swedish]
    lite += [{"scheme": "se-personnummer", "value": v} for v in swedish[::LITE_STEP]]
    comment = "Skatteverket, test personnummer (published as open data); Thjodskra, kennitolur an vartolu"
    write_table("full", "test_numbers.tsv", comment, TEST_COLUMNS, full)
    write_table("lite", "test_numbers.tsv", comment, TEST_COLUMNS, lite)
    print(f"name_rules.tsv: {len(rules)} rows; test_numbers.tsv: full {len(full)}, lite {len(lite)}")


if __name__ == "__main__":
    main()
