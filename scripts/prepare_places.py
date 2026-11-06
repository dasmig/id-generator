#!/usr/bin/env python3
"""Write places.tsv and place_cities.tsv for both tiers.

Sources, each described in doc/data.md:
- curated/places.tsv: statute and authority lists mapped to GeoNames admin1/admin2.
- curated/ssn_areas.tsv: the SSA's area numbers by state.
- raw_data/insee_*: France's communes, arrondissements, past communes and countries.
- raw_data/anpr_*: Italy's comuni (current and past) and foreign states.
- raw_data/wd_*: Wikidata (CC0) for Chinese counties, Indonesian districts, Thai provinces.
- ../city-generator/resources: the cities, their populations and admin codes.

Weights are the population of the cities in a code; several codes in one region
share it equally. China uses each county's Wikidata population instead. A current
French or Italian commune with no city has weight 1, an Indonesian regency's
districts 1,000. Past communes, codes for births abroad and reserved codes have
weight 0: they stay valid but are not drawn.

`full` holds every code. `lite` holds the curated lists and only the commune-level
codes (FR, IT, CN, ID) that a lite city uses, plus each region's largest code.
"""
import csv
import re
import statistics
import zipfile
from collections import defaultdict

from idgen_data import CURATED, RAW, SIBLINGS, read_tsv, write_table

COLUMNS = ["scheme", "code", "label", "weight", "admin1", "country", "from", "until", "reserved"]
ALWAYS = "1900-01-01"  # the earliest birth date a request can give
CITY_COLUMNS = ["scheme", "geonameid", "code"]
PLM = {"75056", "13055", "69123"}  # Paris, Marseille, Lyon: births use the arrondissement's code
CN_PROVINCE = {"11": "22", "12": "28", "13": "10", "14": "24", "15": "20", "21": "19", "22": "05", "23": "08", "31": "23",
               "32": "04", "33": "02", "34": "01", "35": "07", "36": "03", "37": "25", "41": "09", "42": "12", "43": "11",
               "44": "30", "45": "16", "46": "31", "50": "33", "51": "32", "52": "18", "53": "29", "54": "14", "61": "26",
               "62": "15", "63": "06", "64": "21", "65": "13"}
ID_PROVINCE = {"11": "01", "12": "26", "13": "24", "14": "37", "15": "05", "16": "32", "17": "03", "18": "15", "19": "35",
               "21": "40", "31": "04", "32": "30", "33": "07", "34": "10", "35": "08", "36": "33", "51": "02", "52": "17",
               "53": "18", "61": "11", "62": "13", "63": "12", "64": "14", "65": "42", "71": "31", "72": "21", "73": "38",
               "74": "22", "75": "34", "76": "41", "81": "28", "82": "29", "91": "36", "92": "39", "93": "PS", "94": "PT",
               "95": "PE", "96": "PD"}


def load_cities(tier, wanted):
    path = SIBLINGS / f"city-generator/resources/{tier}/cities.tsv"
    cities = []
    with open(path, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE)
        for r in reader:
            if r["country_code"] in wanted:
                cities.append({"id": r["geonameid"], "cc": r["country_code"], "a1": r["admin1_code"], "a2": r["admin2_code"],
                               "a3": r["admin3_code"], "a4": r["admin4_code"], "pop": int(r["population"] or 0)})
    return cities


def read_csv(path, encoding="utf-8", delimiter=","):
    with open(path, encoding=encoding, newline="") as f:
        return list(csv.DictReader(f, delimiter=delimiter))


class Builder:
    def __init__(self):
        self.places = []          # dicts; "tier_all": kept in lite regardless of the cities
        self.city_codes = defaultdict(dict)  # scheme -> geonameid -> code
        self.full = load_cities("full", {r["country"] for r in read_tsv(_schemes_file())})

    def admin_pop(self, cc):
        a1, a2 = defaultdict(int), defaultdict(int)
        for c in self.full:
            if c["cc"] == cc:
                a1[c["a1"]] += c["pop"]
                a2[c["a2"]] += c["pop"]
        return a1, a2


def _schemes_file():
    return CURATED.parent.parent / "resources/full/schemes.tsv"


def add_curated(b, scheme_country):
    rows = read_tsv(CURATED / "places.tsv")
    by_scheme = defaultdict(list)
    for r in rows:
        by_scheme[r["scheme"]].append(r)
    for scheme, group in by_scheme.items():
        cc = scheme_country[scheme]
        _, a2_pop = b.admin_pop(cc)
        # Regions are shared among the codes of one era (the same from and until).
        eras = defaultdict(list)
        for r in group:
            eras[(r["from"], r["until"])].append(r)
        for era_rows in eras.values():
            use_a1, use_a2 = defaultdict(int), defaultdict(int)
            for r in era_rows:
                for a in filter(None, r["admin2"].split(";")):
                    use_a2[a] += 1
                if not r["admin2"]:
                    for a in filter(None, r["admin1"].split(";")):
                        use_a1[a] += 1
            # An admin1-only code gets the cities no admin2 code of its era claims.
            rest = defaultdict(int)
            for c in b.full:
                if c["cc"] == cc and c["a2"] not in use_a2:
                    rest[c["a1"]] += c["pop"]
            for r in era_rows:
                weight = 0.0
                a2s = [a for a in r["admin2"].split(";") if a]
                a1s = [a for a in r["admin1"].split(";") if a]
                if a2s:
                    weight = sum(a2_pop.get(a, 0) / use_a2[a] for a in a2s)
                    found = {c["a1"] for c in b.full if c["cc"] == cc and c["a2"] in a2s}
                    a1s = sorted(set(a1s) | found)
                else:
                    weight = sum(rest.get(a, 0) / use_a1[a] for a in a1s)
                b.places.append({"scheme": scheme, "code": r["code"], "label": r["label"], "weight": round(weight),
                                 "admin1": ";".join(a1s), "country": r["country"], "from": r["from"],
                                 "until": r["until"], "reserved": r["reserved"], "keep": True})
                if not r["until"]:
                    for c in b.full:
                        if c["cc"] == cc and c["a2"] in a2s:
                            b.city_codes[scheme].setdefault(c["id"], r["code"])


def add_ssn(b):
    a1_pop, _ = b.admin_pop("US")
    rows = read_tsv(CURATED / "ssn_areas.tsv")
    states = defaultdict(list)
    for r in rows:
        states[r["area"]].append(r["state"])
    counts = defaultdict(int)
    for r in rows:
        counts[r["state"]] += 1
    labels = {"PR": "Puerto Rico", "VI": "Virgin Islands", "GU": "Guam, American Samoa, Philippines", "RR": "Railroad Board", "EE": "Enumeration at Entry"}
    for area, sts in sorted(states.items()):
        real = [s for s in sts if s in a1_pop]
        weight = sum(a1_pop[s] / counts[s] for s in real)
        b.places.append({"scheme": "us-ssn", "code": area, "label": " ".join(labels.get(s, s) for s in sts), "weight": round(weight),
                         "admin1": ";".join(real), "country": "", "from": "", "until": "1963-07-01" if "RR" in sts else "",
                         "reserved": "", "keep": True})


def add_france(b):
    cc_rows = read_csv(RAW / "insee_v_commune_2025.csv")
    for r in cc_rows:
        # INSEE's overseas regions (01-06) are separate countries in GeoNames, so they have no French admin1.
        if len(r["REG"]) == 2 and r["REG"] < "10":
            r["REG"] = ""
    city_pop = defaultdict(int)
    cities_of = defaultdict(list)
    for c in b.full:
        if c["cc"] == "FR" and c["a4"]:
            city_pop[c["a4"]] += c["pop"]
            cities_of[c["a4"]].append(c["id"])
    arrondissements = defaultdict(list)
    for r in cc_rows:
        if r["TYPECOM"] == "ARM":
            arrondissements[r["COMPARENT"]].append(r)
    current = set()
    for r in cc_rows:
        if r["TYPECOM"] == "ARM":
            parent = r["COMPARENT"]
            weight = round(city_pop[parent] / len(arrondissements[parent]))
            b.places.append(place("fr-nir", r["COM"], r["LIBELLE"], max(weight, 1), r["REG"]))
            current.add(r["COM"])
            for gid in cities_of.get(r["COM"], []):
                b.city_codes["fr-nir"].setdefault(gid, r["COM"])
        elif r["TYPECOM"] == "COM" and r["COM"] not in PLM:
            b.places.append(place("fr-nir", r["COM"], r["LIBELLE"], max(city_pop[r["COM"]], 1), r["REG"]))
            current.add(r["COM"])
            for gid in cities_of.get(r["COM"], []):
                b.city_codes["fr-nir"].setdefault(gid, r["COM"])
    for parent, rows in arrondissements.items():
        first = min(x["COM"] for x in rows)
        for gid in cities_of.get(parent, []):
            b.city_codes["fr-nir"].setdefault(gid, first)
    # Communes merged since 1943, kept for older people's birthplaces (never drawn).
    history = {}
    for r in read_csv(RAW / "insee_v_commune_depuis_1943.csv"):
        if r["TYPECOM"] == "COM" and r["DATE_FIN"] and r["COM"] not in current and r["COM"] not in PLM:
            if r["COM"] not in history or r["DATE_FIN"] > history[r["COM"]]["DATE_FIN"]:
                history[r["COM"]] = r
    for code, r in history.items():
        b.places.append({**place("fr-nir", code, r["LIBELLE"], 0, ""), "until": r["DATE_FIN"]})
        for gid in cities_of.get(code, []):
            b.city_codes["fr-nir"].setdefault(gid, code)
    # Births abroad: 99 + INSEE's country code.
    seen = set()
    for r in read_csv(RAW / "insee_v_pays_territoire_2025.csv"):
        iso, code = r["CODEISO2"], r["COG"]
        if iso and iso != "FR" and code not in seen and code.startswith("99"):
            seen.add(code)
            b.places.append({**place("fr-nir", code, r["LIBCOG"], 0, ""), "country": iso, "keep": True})


def read_xlsx(path):
    """The first sheet of an xlsx as dicts (stdlib only; shared strings and plain values)."""
    z = zipfile.ZipFile(path)
    strings = [re.sub(r"<[^>]+>", "", s) for s in re.findall(r"<si>(.*?)</si>", z.read("xl/sharedStrings.xml").decode("utf-8"), re.S)]
    rows = []
    for x in re.findall(r"<row[^>]*>(.*?)</row>", z.read("xl/worksheets/sheet1.xml").decode("utf-8"), re.S):
        cells = {}
        for col, attrs, body in re.findall(r'<c r="([A-Z]+)\d+"([^>]*?)(?:/>|>(.*?)</c>)', x, re.S):
            v = re.search(r"<v>(.*?)</v>", body or "")
            if v:
                cells[col] = strings[int(v.group(1))] if 't="s"' in attrs else v.group(1)
        rows.append(cells)
    head = rows[0]
    return [{head[k]: r.get(k, "") for k in head} for r in rows[1:]]


def add_italy(b):
    rows = read_csv(RAW / "anpr_comuni.csv")
    sigla_admin1 = {}
    city_pop = defaultdict(int)
    cities_of = defaultdict(list)
    for c in b.full:
        if c["cc"] == "IT":
            sigla_admin1.setdefault(c["a2"], c["a1"])
            if c["a3"]:
                city_pop[c["a3"]] += c["pop"]
                cities_of[c["a3"]].append(c["id"])
    current = {}
    past = {}
    istat_to_code = {}
    for r in rows:
        code, istat = r["CODCATASTALE"], r["CODISTAT"]
        if r["DATACESSAZIONE"] == "9999-12-31" and r["STATO"] == "A":
            current[code] = r
            istat_to_code[istat] = code
    for r in rows:
        code = r["CODCATASTALE"]
        # ANPR writes ND where a ceased comune's code is unknown.
        if re.fullmatch(r"[A-Z]\d{3}", code) and code not in current and (code not in past or r["DATACESSAZIONE"] > past[code]["DATACESSAZIONE"]):
            past[code] = r
        istat_to_code.setdefault(r["CODISTAT"], code)
    pop_by_code = defaultdict(int)
    for istat, pop in city_pop.items():
        if istat in istat_to_code:
            pop_by_code[istat_to_code[istat]] += pop
            for gid in cities_of[istat]:
                b.city_codes["it-cf"].setdefault(gid, istat_to_code[istat])
    for code, r in current.items():
        b.places.append(place("it-cf", code, r["DENOMINAZIONE_IT"].title(), max(pop_by_code[code], 1), sigla_admin1.get(r["SIGLAPROVINCIA"], "")))
    for code, r in past.items():
        b.places.append({**place("it-cf", code, r["DENOMINAZIONE_IT"].title(), 0, ""), "until": r["DATACESSAZIONE"]})
    # Births abroad: the Z code of the state.
    cca3 = {r["cca3"]: r["cca2"] for r in read_tsv(SIBLINGS / "country-generator/resources/full/countries.tsv")}
    seen = set()
    for r in read_xlsx(RAW / "anpr_stati.xlsx"):
        code, iso3 = r.get("CODAT", ""), r.get("CODISO3166_1_ALPHA3", "")
        if re.fullmatch(r"Z\d{3}", code) and iso3 in cca3 and code not in seen and r.get("DATAFINEVALIDITA", "").startswith("31/12/9999"):
            seen.add(code)
            b.places.append({**place("it-cf", code, r.get("DENOMINAZIONEISTAT_EN") or r.get("DENOMINAZIONE", ""), 0, ""), "country": cca3[iso3], "keep": True})


def place(scheme, code, label, weight, admin1):
    return {"scheme": scheme, "code": code, "label": label, "weight": weight, "admin1": admin1,
            "country": "", "from": "", "until": "", "reserved": "", "keep": False}


def wikidata(name):
    rows = {}
    gid_to_code = {}
    # Query results come in any order; sort so a rebuild picks the same label and link.
    for r in sorted(read_csv(RAW / name), key=lambda r: (r["code"], r["gn"], r["label"], r["pop"])):
        code = r["code"].replace(".", "")
        pop = int(float(r["pop"])) if r.get("pop") else 0
        e = rows.setdefault(code, {"label": "", "pop": 0})
        e["label"] = e["label"] or r["label"]
        e["pop"] = max(e["pop"], pop)
        if r["gn"]:
            gid_to_code.setdefault(r["gn"], code)
    return rows, gid_to_code


def add_china(b):
    rows, gid_to_code = wikidata("wd_p13971.csv")
    counties = {c: e for c, e in rows.items() if re.fullmatch(r"\d{6}", c) and not c.endswith("00") and c[:2] in CN_PROVINCE}
    # Dongguan, Zhongshan and a few others have no counties: their numbers use the prefecture's own code.
    prefixes = {c[:4] for c in counties}
    childless = {c: e for c, e in rows.items() if re.fullmatch(r"\d{4}00", c) and not c.endswith("0000")
                 and c[:2] in CN_PROVINCE and c[:4] not in prefixes}
    counties.update(childless)
    print("cn-ric prefectures without counties:", ", ".join(f"{c} {e['label']}" for c, e in sorted(childless.items())))
    province_of = {a1: p for p, a1 in CN_PROVINCE.items()}
    known = [e["pop"] for e in counties.values() if e["pop"] > 0]
    default = int(statistics.median(known)) if known else 100000
    for c, e in counties.items():
        e["weight"] = e["pop"] or default
        b.places.append(place("cn-ric", c, e["label"] or c, e["weight"], CN_PROVINCE[c[:2]]))

    def top(prefix):
        match = [c for c in counties if c.startswith(prefix)]
        return max(match, key=lambda c: (counties[c]["weight"], c)) if match else None

    for city in b.full:
        if city["cc"] != "CN":
            continue
        code = gid_to_code.get(city["id"], "")
        target = None
        if code in counties:
            target = code
        elif code.endswith("0000"):
            target = top(code[:2])
        elif code.endswith("00"):
            target = top(code[:4])
        if not target and re.fullmatch(r"\d{4}", city["a2"]):
            target = top(city["a2"])
        # Beijing, Tianjin, Shanghai and Chongqing have no prefecture level, so GeoNames has no usable admin2.
        if not target and province_of.get(city["a1"]) in ("11", "12", "31", "50"):
            target = top(province_of[city["a1"]])
        if target:
            b.city_codes["cn-ric"][city["id"]] = target


def city_names(cc):
    with open(SIBLINGS / "city-generator/resources/full/cities.tsv", encoding="utf-8", newline="") as f:
        return {r["geonameid"]: r["asciiname"] for r in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE) if r["country_code"] == cc}


def add_indonesia(b):
    rows, gid_to_code = wikidata("wd_p2588.csv")
    districts = {c: e for c, e in rows.items() if re.fullmatch(r"\d{6}", c) and c[:2] in ID_PROVINCE}
    regency = defaultdict(list)
    for c in districts:
        regency[c[:4]].append(c)
    # GeoNames' admin2 follows BPS, whose numbers differ from the NIK's Kemendagri numbers in places
    # (its 1275 is Medan, Kemendagri's is Binjai), and Jakarta has none. So a city's regency comes
    # from its name, then Wikidata's link, then a GeoNames code corrected by its largest city.
    by_name = defaultdict(list)
    for code, e in rows.items():
        if len(code) == 4 and code in regency:
            by_name[(code[:2], re.sub(r" (City|Regency)$", "", e["label"]).lower())].append(code)
    province_of = {a1: p for p, a1 in ID_PROVINCE.items()}
    names = city_names("ID")

    def named(city):
        found = by_name.get((province_of.get(city["a1"]), names[city["id"]].lower()), [])
        if city["a2"] in found:
            return city["a2"]
        kota = [c for c in found if c[2] == "7"]
        return min(kota or found) if found else None

    cities = [c for c in b.full if c["cc"] == "ID"]
    largest, votes = {}, defaultdict(lambda: defaultdict(int))
    for city in cities:
        if city["a2"] and (city["a2"] not in largest or city["pop"] > largest[city["a2"]]["pop"]):
            largest[city["a2"]] = city
        link = gid_to_code.get(city["id"], "")[:4]
        if city["a2"] and link in regency:
            votes[city["a2"]][link] += 1
    corrected = {}
    for a2, top in largest.items():
        corrected[a2] = (named(top) or (max(votes[a2], key=votes[a2].get) if votes[a2] else None)
                         or (a2 if a2 in regency else None))

    pop = defaultdict(int)
    jakarta = sorted(r for r in regency if r[:2] == "31" and r[2] == "7")
    target_of = {}
    for city in cities:
        link = gid_to_code.get(city["id"], "")
        target = named(city) or (link[:4] if link[:4] in regency else None) or corrected.get(city["a2"])
        if not target and province_of.get(city["a1"]) == "31":
            for r in jakarta:
                pop[r] += city["pop"] / len(jakarta)
            target = jakarta[0]
        elif target:
            pop[target] += city["pop"]
        if target:
            target_of[city["id"]] = link if link in districts and link[:4] == target else min(regency[target])
    for c, e in districts.items():
        weight = round(pop[c[:4]] / len(regency[c[:4]])) if pop[c[:4]] else 1000
        e["weight"] = max(weight, 1)
        b.places.append(place("id-nik", c, e["label"] or c, e["weight"], ID_PROVINCE[c[:2]]))
    b.city_codes["id-nik"].update(target_of)
    # Safe mode: a regency number no real regency uses, in a real province.
    for prov in ID_PROVINCE:
        unused = f"{prov}9901"
        if unused[:4] in regency:
            raise SystemExit(f"regency 99 is in use in province {prov}")
        b.places.append({**place("id-nik", unused, "Unused regency (safe mode)", 0, ""), "reserved": "1", "keep": True})


def add_thailand(b):
    gid_admin1 = {}
    with open(SIBLINGS / "city-generator/resources/admin1CodesASCII.txt", encoding="utf-8") as f:
        for line in f:
            key, _, _, gid = line.rstrip("\n").split("\t")
            gid_admin1[gid] = key.split(".")[1]
    a1_pop, _ = b.admin_pop("TH")
    seen = set()
    for r in sorted(read_csv(RAW / "wd_th.csv"), key=lambda r: (r["iso"], r["gn"], r["label"])):
        m = re.fullmatch(r"TH-(\d\d)", r["iso"])
        if not m or m.group(1) in seen:
            continue
        seen.add(m.group(1))
        adm = gid_admin1.get(r["gn"], "")
        b.places.append({**place("th-pin", m.group(1), r["label"], a1_pop.get(adm, 0), adm), "keep": True})


def main():
    schemes = read_tsv(_schemes_file())
    scheme_country = {r["key"]: r["country"] for r in schemes}
    b = Builder()
    add_curated(b, scheme_country)
    add_ssn(b)
    add_france(b)
    add_italy(b)
    add_china(b)
    add_indonesia(b)
    add_thailand(b)

    comment = ("national lists from the authorities (INSEE Licence Ouverte; ANPR, Ministero dell'Interno; SSA, US public domain; laws), "
               "Wikidata CC0 (CN, ID, TH); weights from city-generator")
    city_comment = "GeoNames cities joined to the national code through admin codes and Wikidata P1566 (CC0)"
    # `from` is part of the table key, and core::layered_table rejects an empty key cell.
    for p in b.places:
        p["from"] = p["from"] or ALWAYS
    b.places.sort(key=lambda p: (p["scheme"], p["code"], p["from"]))
    # The same code twice in one era would be a bug in the sources' joins.
    seen = set()
    unique = []
    for p in b.places:
        key = (p["scheme"], p["code"], p["from"])
        if key not in seen:
            seen.add(key)
            unique.append(p)
    dropped = len(b.places) - len(unique)

    lite_ids = {c["id"] for c in load_cities("lite", set(scheme_country.values()))}
    full_ids = {c["id"] for c in b.full}
    full_cities = [{"scheme": s, "geonameid": g, "code": c}
                   for s, m in sorted(b.city_codes.items()) for g, c in sorted(m.items(), key=lambda kv: int(kv[0])) if g in full_ids]
    lite_cities = [r for r in full_cities if r["geonameid"] in lite_ids]

    used = {(r["scheme"], r["code"]) for r in lite_cities}
    # Each region keeps its largest code, so a draw by admin1 finds something.
    largest = {}
    for p in unique:
        if p["weight"] and not p["keep"]:
            k = (p["scheme"], p["admin1"])
            if k not in largest or p["weight"] > largest[k]["weight"]:
                largest[k] = p
    used |= {(p["scheme"], p["code"]) for p in largest.values()}
    lite_places = [p for p in unique if p["keep"] or (p["scheme"], p["code"]) in used]

    write_table("full", "places.tsv", comment, COLUMNS, unique)
    write_table("lite", "places.tsv", comment, COLUMNS, lite_places)
    write_table("full", "place_cities.tsv", city_comment, CITY_COLUMNS, full_cities)
    write_table("lite", "place_cities.tsv", city_comment, CITY_COLUMNS, lite_cities)

    counts = defaultdict(lambda: [0, 0])
    for p in unique:
        counts[p["scheme"]][0] += 1
    for p in lite_places:
        counts[p["scheme"]][1] += 1
    print(f"places.tsv: full {len(unique)} ({dropped} duplicate keys dropped), lite {len(lite_places)}")
    print(f"place_cities.tsv: full {len(full_cities)}, lite {len(lite_cities)}")
    population = defaultdict(int)
    city_pop = {}
    for c in b.full:
        population[c["cc"]] += c["pop"]
        city_pop[c["id"]] = c["pop"]
    for s, (full, lite) in sorted(counts.items()):
        mapped = b.city_codes.get(s, {})
        share = sum(city_pop.get(g, 0) for g in mapped) / (population[scheme_country[s]] or 1)
        print(f"  {s}: full {full}, lite {lite}, cities {len(mapped)}" + (f" ({share:.1%} of the population)" if mapped else ""))


if __name__ == "__main__":
    main()
