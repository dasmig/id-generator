#!/usr/bin/env python3
"""Check generated numbers with implementations the library does not share.

Builds scripts/sample_identities.cpp, draws 2,000 identities per served country at
three dates, and checks every number with python-stdnum. Schemes stdnum lacks are
checked by code written here from the national rules, not from schemes.tsv.

python-stdnum is LGPL and is for this script only; it is never shipped or vendored:

    python -m venv .venv && .venv/Scripts/pip install python-stdnum
    .venv/Scripts/python scripts/validate_numbers.py [--tier lite] [--count 2000] [--cxx clang++]

Target: every realistic number valid, and each safe number rejected or accepted as
its scheme's method declares (SAFE_REJECTED below). A hit on CURP's word list is
reported, not failed, while the library ships no list (doc/data.md).
"""
import argparse
import csv
import importlib
import re
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

STDNUM = {
    "ar-cuil": "ar.cuit", "ar-dni": "ar.dni", "at-svnr": "at.vnr", "au-tfn": "au.tfn", "be-nn": "be.nn", "bg-egn": "bg.egn",
    "br-cpf": "br.cpf", "ca-sin": "ca.sin", "ch-ahv": "ch.ssn", "cl-run": "cl.rut", "cn-ric": "cn.ric", "cz-rc": "cz.rc",
    "de-idnr": "de.idnr", "dk-cpr": "dk.cpr", "ec-cedula": "ec.ci", "ee-isikukood": "ee.ik", "es-dni": "es.dni",
    "fi-hetu": "fi.hetu", "fr-nir": "fr.nir", "gr-amka": "gr.amka", "hr-oib": "hr.oib", "id-nik": "id.nik", "ie-ppsn": "ie.pps",
    "il-idnr": "il.idnr", "in-aadhaar": "in_.aadhaar", "is-kennitala": "is_.kennitala", "it-cf": "it.codicefiscale",
    "jp-mynumber": "jp.in_", "kr-rrn": "kr.rrn", "lt-asmens-kodas": "lt.asmens", "lv-personas-kods": "lv.pvn", "mx-curp": "mx.curp",
    "my-mykad": "my.nric", "nl-bsn": "nl.bsn", "no-fodselsnummer": "no.fodselsnummer", "nz-ird": "nz.ird", "pe-dni": "pe.cui",
    "pk-cnic": "pk.cnic", "pl-pesel": "pl.pesel", "pt-nif": "pt.nif", "ro-cnp": "ro.cnp", "ru-inn": "ru.inn",
    "se-personnummer": "se.personnummer", "si-emso": "si.emso", "sk-rc": "sk.rc", "th-pin": "th.pin", "tr-tckn": "tr.tckimlik",
    "ua-rnokpp": "ua.rntrc", "us-ssn": "us.ssn", "za-idnr": "za.idnr",
}

# Safe numbers the independent check must reject. wrong_check and impossible_date
# are not valid numbers. The reserved ranges are outside what real numbers use, and
# DE, FI, NO, US and GB's validators know that. The other methods make numbers that
# are well-formed by design, so their validators accept them (AR's series is unissued,
# SE's list holds real formats). An unassigned place is rejected where the validator
# knows the place codes, Malaysia's and Pakistan's, and accepted by Indonesia's, which
# only knows the region above the unassigned digits.
ACCEPTED_WHEN_SAFE = {"ar-dni", "id-nik", "se-personnummer"}
# Iceland's list is of numbers without a check digit, which stdnum cannot judge.
EITHER_WHEN_SAFE = {"is-kennitala"}

# Provinces newer than stdnum's table: North Kalimantan (2012) and the Papuan ones (2022).
ID_NEW_PROVINCES = {"65", "92", "93", "94", "95", "96", "97"}
KR_RANDOM_FROM = date(2020, 10, 5)


def luhn_ok(t):
    total = 0
    for i, c in enumerate(reversed(t)):
        d = int(c) * (2 if i % 2 else 1)
        total += d - 9 if d > 9 else d
    return total % 10 == 0


def real_date(y, m, d):
    try:
        date(y, m, d)
    except ValueError:
        return False
    return True


def ae_eid(t):
    return re.fullmatch(r"784(19|20)\d{10}", t) is not None and luhn_ok(t)


def jmbg(t):
    if re.fullmatch(r"\d{13}", t) is None:
        return False
    g = int(t[4:7])
    if not real_date((1000 if g >= 800 else 2000) + g, int(t[2:4]), int(t[:2])):
        return False
    r = sum(int(c) * w for c, w in zip(t[:12], [7, 6, 5, 4, 3, 2] * 2)) % 11
    return r != 1 and int(t[12]) == (0 if r == 0 else 11 - r)


def gb_nino(t):
    pattern = r"[A-CEGHJ-PR-TW-Z][A-CEGHJ-NPR-TW-Z]\d{6}[A-D]"
    return re.fullmatch(pattern, t) is not None and t[:2] not in {"BG", "GB", "KN", "NK", "NT", "TN", "ZZ"}


def hk_hkid(t):
    m = re.fullmatch(r"([A-Z]{1,2})(\d{6})([0-9A])", t)
    if m is None:
        return False
    letters = m[1].rjust(2)
    values = [36 if c == " " else ord(c) - 55 for c in letters] + [int(c) for c in m[2]]
    total = sum(v * w for v, w in zip(values, range(9, 1, -1))) + (10 if m[3] == "A" else int(m[3]))
    return total % 11 == 0


def ir_code(t):
    if re.fullmatch(r"\d{10}", t) is None or len(set(t)) == 1:
        return False
    r = sum(int(c) * w for c, w in zip(t[:9], range(10, 1, -1))) % 11
    return int(t[9]) == (r if r < 2 else 11 - r)


def kz_iin(t):
    if re.fullmatch(r"\d{12}", t) is None or t[6] not in "123456":
        return False
    if not real_date(1800 + (int(t[6]) - 1) // 2 * 100 + int(t[:2]), int(t[2:4]), int(t[4:6])):
        return False
    r = sum(int(c) * w for c, w in zip(t[:11], range(1, 12))) % 11
    if r == 10:
        r = sum(int(c) * w for c, w in zip(t[:11], [3, 4, 5, 6, 7, 8, 9, 10, 11, 1, 2])) % 11
    return r != 10 and int(t[11]) == r


def ru_snils(t):
    if re.fullmatch(r"\d{11}", t) is None:
        return False
    if int(t[:9]) <= 1001998:
        return True
    s = sum(int(c) * w for c, w in zip(t[:9], range(9, 0, -1)))
    while s > 101:
        s %= 101
    return int(t[9:]) == (0 if s >= 100 else s)


def sa_nid(t):
    return re.fullmatch(r"1\d{9}", t) is not None and luhn_ok(t)


def sg_nric(t):
    if re.fullmatch(r"[ST]\d{7}[A-Z]", t) is None:
        return False
    s = sum(int(c) * w for c, w in zip(t[1:8], [2, 7, 6, 5, 4, 3, 2])) + (4 if t[0] == "T" else 0)
    return t[8] == "JZIHGFEDCBA"[s % 11]


def tw_nid(t):
    if re.fullmatch(r"[A-Z][12]\d{8}", t) is None:
        return False
    code = "ABCDEFGHJKLMNPQRSTUVXYWZIO".index(t[0]) + 10
    total = code // 10 + code % 10 * 9 + sum(int(c) * w for c, w in zip(t[1:9], range(8, 0, -1))) + int(t[9])
    return total % 10 == 0


def uy_cedula(t):
    if re.fullmatch(r"\d{8}", t) is None:
        return False
    s = sum(int(c) * w for c, w in zip(t[:7], [2, 9, 8, 7, 6, 3, 4]))
    return int(t[7]) == (10 - s % 10) % 10


def vn_cccd(t, reference):
    if re.fullmatch(r"\d{12}", t) is None or t[3] not in "0123":
        return False
    return 1900 + int(t[3]) // 2 * 100 + int(t[4:6]) <= reference.year


def cn_ric(t):
    """stdnum's check without its comparison of the county's first listing with the birth year.

    Its table starts about 1980, so it rejects most people born earlier. GB 11643 ties the
    code to the registration, not the birth, and a county's code outlives later renames.
    """
    from stdnum import numdb
    from stdnum.cn import ric

    if re.fullmatch(r"\d{17}[0-9X]", t) is None or t[-1] != ric.calc_check_digit(t):
        return False
    if not real_date(int(t[6:10]), int(t[10:12]), int(t[12:14])):
        return False
    return any(props for _, props in numdb.get("cn/loc").info(t[:6]))


def id_nik(t):
    from stdnum.id import nik

    if t[:2] not in ID_NEW_PROVINCES:
        return nik.is_valid(t)
    day = int(t[6:8]) % 40
    return re.fullmatch(r"\d{16}", t) is not None and t[12:] != "0000" and (real_date(1900 + int(t[10:12]), int(t[8:10]), day) or real_date(2000 + int(t[10:12]), int(t[8:10]), day))


def pk_cnic(t):
    """stdnum's check; its province list leaves out Azad Kashmir (8)."""
    from stdnum.pk import cnic

    return cnic.is_valid("1" + t[1:] if t[0] == "8" else t)


def kr_rrn(t, assigned, reference):
    """stdnum's check, except that numbers since October 2020 have no check digit, and
    that it does not compare the date with anything: a date after the reference is rejected here."""
    from stdnum.kr import rrn

    if re.fullmatch(r"\d{13}", t) is None or t[6] not in "012349":
        return False
    year = {"9": 1800, "0": 1800, "1": 1900, "2": 1900, "3": 2000, "4": 2000}[t[6]] + int(t[:2])
    if not real_date(year, int(t[2:4]), int(t[4:6])) or year > reference.year:
        return False
    return assigned >= KR_RANDOM_FROM or rrn.is_valid(t)


OWN = {
    "ae-eid": ae_eid, "ba-jmb": jmbg, "gb-nino": gb_nino, "hk-hkid": hk_hkid, "ir-codemeli": ir_code, "kz-iin": kz_iin,
    "me-jmbg": jmbg, "mk-embg": jmbg, "rs-jmbg": jmbg, "ru-snils": ru_snils, "sa-nid": sa_nid, "sg-nric": sg_nric,
    "tw-nid": tw_nid, "uy-cedula": uy_cedula, "cn-ric": cn_ric, "id-nik": id_nik, "pk-cnic": pk_cnic,
}


def load_checker(scheme):
    if scheme == "vn-cccd" or scheme == "kr-rrn":
        return None
    if scheme in OWN:
        return OWN[scheme]
    return importlib.import_module("stdnum." + STDNUM[scheme]).is_valid


def reason(scheme, value):
    if scheme in OWN or scheme not in STDNUM:
        return ""
    try:
        importlib.import_module("stdnum." + STDNUM[scheme]).validate(value)
    except Exception as e:  # noqa: BLE001, for the message only
        return f" [{type(e).__name__}]"
    return ""


def curp_word_hit(value):
    from stdnum.mx import curp

    return value[:4] in curp._name_blacklist  # noqa: SLF001, the module has no public list


def build(args, build_dir):
    binary = build_dir / "sample_identities.exe"
    command = [args.cxx, "-std=c++23", "-O2", "-I.", "scripts/sample_identities.cpp", "-o", str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tier", default="lite")
    parser.add_argument("--count", type=int, default=2000)
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()

    build_dir = ROOT / "build"
    build_dir.mkdir(exist_ok=True)
    binary = build(args, build_dir)
    sample = build_dir / "sample_identities.tsv"
    subprocess.run([str(binary), str(ROOT / "resources" / args.tier), str(args.count), str(sample)], check=True)

    counts = defaultdict(Counter)
    shown = Counter()
    curp_words = set()
    failures = 0
    checkers = {}
    with open(sample, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
            scheme, value = row["scheme"], row["value"]
            safe = row["safe"] == "1"
            if scheme not in checkers:
                checkers[scheme] = load_checker(scheme)
            if scheme == "vn-cccd":
                ok = vn_cccd(value, date.fromisoformat(row["reference"]))
            elif scheme == "kr-rrn":
                ok = kr_rrn(value, date.fromisoformat(row["assigned"]), date.fromisoformat(row["reference"]))
            else:
                ok = checkers[scheme](value)
            kind = "safe" if safe else "realistic"
            counts[scheme][f"{kind} total"] += 1
            counts[scheme][f"{kind} valid"] += ok
            if scheme == "mx-curp" and not safe and not ok and curp_word_hit(value):
                curp_words.add(value[:4])
                counts[scheme]["realistic word"] += 1
                continue
            expected = True
            if safe:
                if scheme in EITHER_WHEN_SAFE:
                    continue
                expected = scheme in ACCEPTED_WHEN_SAFE
            if ok != expected:
                failures += 1
                shown[scheme, kind] += 1
                if shown[scheme, kind] <= 5:
                    print(f"{kind} {scheme} {value} ({row['formatted']}) born {row['birth']} {row['sex']} assigned {row['assigned']} "
                          f"at {row['reference']} place '{row['place']}': the check says {'valid' if ok else 'invalid'}{reason(scheme, value)}")

    print(f"\n{'scheme':<20}{'checker':<10}{'realistic':>12}{'valid':>12}{'safe':>9}{'valid':>9}")
    for scheme in sorted(counts):
        c = counts[scheme]
        who = "own" if scheme in OWN or scheme == "vn-cccd" else "stdnum"
        who = "stdnum*" if scheme in ("cn-ric", "id-nik", "kr-rrn", "pk-cnic") else who
        print(f"{scheme:<20}{who:<10}{c['realistic total']:>12}{c['realistic valid']:>12}{c['safe total']:>9}{c['safe valid']:>9}")
    served = {r["key"] for r in csv.DictReader((l for l in open(ROOT / "resources" / args.tier / "schemes.tsv", encoding="utf-8") if not l.startswith("#")), delimiter="\t", quoting=csv.QUOTE_NONE)}
    missing = served - set(counts)
    if missing:
        print("schemes with no sample:", sorted(missing))
        failures += len(missing)
    if curp_words:
        print(f"\nCURP word-list hits, reported only: {counts['mx-curp']['realistic word']} numbers, {len(curp_words)} words: {sorted(curp_words)[:20]}")
    print("\nstdnum*: stdnum with the corrections described at cn_ric, id_nik, kr_rrn and pk_cnic.")
    print(f"\n{failures} unexpected results")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
