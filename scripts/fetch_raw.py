#!/usr/bin/env python3
"""Download the raw sources into raw_data/ (git-ignored). Existing files are kept.

ssa.gov refuses scripts, so the SSA's lists come from the Wayback Machine, slowly
because it rate-limits. Wikidata's query service can allow one request a minute,
so its queries are spaced by that. Exits non-zero if anything could not be fetched.
"""
import json
import sys
import time
import urllib.parse
import urllib.request

from idgen_data import RAW

HEADERS = {"User-Agent": "dasmig-data-build/1.0"}
SPARQL = "https://query.wikidata.org/sparql?"
QUERIES = {
    "wd_p13971.csv": 'SELECT ?code ?label ?gn ?pop WHERE { ?s wdt:P13971 ?code . FILTER(STRLEN(?code)=6) OPTIONAL { ?s wdt:P1566 ?gn } OPTIONAL { ?s wdt:P1082 ?pop } OPTIONAL { ?s rdfs:label ?label . FILTER(LANG(?label)="en") } }',
    "wd_p2588.csv": 'SELECT ?code ?label ?gn ?pop WHERE { ?s wdt:P2588 ?code . FILTER(STRLEN(?code)<=8) OPTIONAL { ?s wdt:P1566 ?gn } OPTIONAL { ?s wdt:P1082 ?pop } OPTIONAL { ?s rdfs:label ?label . FILTER(LANG(?label)="en") } }',
    "wd_th.csv": 'SELECT ?iso ?label ?gn WHERE { ?s wdt:P300 ?iso . FILTER(STRSTARTS(?iso,"TH-")) OPTIONAL { ?s wdt:P1566 ?gn } OPTIONAL { ?s rdfs:label ?label . FILTER(LANG(?label)="en") } }',
}
FILES = {
    "insee_v_commune_2025.csv": "https://www.insee.fr/fr/statistiques/fichier/8377162/v_commune_2025.csv",
    "insee_v_commune_depuis_1943.csv": "https://www.insee.fr/fr/statistiques/fichier/8377162/v_commune_depuis_1943.csv",
    "insee_v_pays_territoire_2025.csv": "https://www.insee.fr/fr/statistiques/fichier/8377162/v_pays_territoire_2025.csv",
    "anpr_comuni.csv": "https://www.anagrafenazionale.interno.it/wp-content/uploads/ANPR_archivio_comuni.csv",
    "anpr_stati.xlsx": "https://www.anagrafenazionale.interno.it/wp-content/uploads/tabella_2_statiesteri.xlsx",
}
FOIA = "www.ssa.gov/foia/highgroup.htm"
EMPLOYER = "www.ssa.gov/employer/highgroup.txt"
# Timestamps are the snapshots' own, so the Wayback Machine returns these exact lists.
SSA = [(f"ssa_foia_highgroup_{stamp}.htm", FOIA, stamp)
       for stamp in ("19990203", "19990507", "20000303", "20000815", "20010413", "20011214", "20020806", "20030621")]
SSA += [("ssa_highgroup_2003.htm", EMPLOYER, "20030701"),
        ("ssa_highgroup_2005.htm", EMPLOYER, "20050701"),
        ("ssa_highgroup_2007.htm", EMPLOYER, "20070701"),
        ("ssa_highgroup_2009.htm", EMPLOYER, "20090701"),
        ("ssa_highgroup_2011.txt", "ssa.gov/employer/ssns/highgroup.txt", "20110701"),
        ("ssa_stateweb.htm", "www.ssa.gov/employer/stateweb.htm", "20100701")]
WIKIDATA_WAIT = 65  # seconds
SKATTEVERKET = "https://skatteverket.entryscape.net/rowstore/dataset/b4de7df7-63c0-4e7e-bb59-1f156a591763"


failures = []


def get(url, headers=None, retries=4, wait=15):
    request = urllib.request.Request(url, headers={**HEADERS, **(headers or {})})
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(request, timeout=280) as response:
                return response.read()
        except OSError as e:
            print(f"  retry {attempt}: {url[:80]}: {e}")
            time.sleep(wait * (attempt + 1))
    return None


def save(name, data):
    if data:
        (RAW / name).write_bytes(data)
        print(f"{name}: {len(data)} bytes")
    else:
        print(f"FAILED: {name}")
        failures.append(name)


def main():
    RAW.mkdir(exist_ok=True)
    for name, url in FILES.items():
        if not (RAW / name).exists():
            save(name, get(url))
    for name, query in QUERIES.items():
        if not (RAW / name).exists():
            save(name, get(SPARQL + urllib.parse.urlencode({"query": query}), {"Accept": "text/csv"}, wait=WIKIDATA_WAIT))
            time.sleep(WIKIDATA_WAIT)
    if not (RAW / "se_test_numbers.txt").exists():
        numbers, offset = [], 0
        while True:
            page = json.loads(get(f"{SKATTEVERKET}?_limit=500&_offset={offset}"))
            numbers += [r["testpersonnummer"] for r in page["results"]]
            offset += 500
            if offset >= page["resultCount"]:
                break
        save("se_test_numbers.txt", ("\n".join(numbers) + "\n").encode())
    for name, target, stamp in SSA:
        if (RAW / name).exists():
            continue
        meta = get(f"http://archive.org/wayback/available?url={target}&timestamp={stamp}")
        snapshot = json.loads(meta).get("archived_snapshots", {}).get("closest") if meta else None
        if not snapshot:
            print(f"FAILED: no snapshot of {target} near {stamp}")
            failures.append(name)
            continue
        save(name, get(f"https://web.archive.org/web/{snapshot['timestamp']}id_/http://{target}"))
        time.sleep(12)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
