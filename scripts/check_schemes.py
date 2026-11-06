#!/usr/bin/env python3
"""Check every table of both tiers. Writes nothing; exits non-zero on the first run with failures.

Checks that do not need the C++ library: patterns parse with core::pattern's
grammar, keys are unique, dates are ordered, anchors rise, place codes fit their
scheme and exist in GeoNames, each document's formats cover all dates without
overlap, the survey's generic rows are exactly the generic formats, and the worked
examples fit their schemes, with the check digit recomputed for generic schemes.
"""
import re
import re._parser as re_parser
import sys
from collections import Counter, defaultdict
from datetime import date

from idgen_data import CURATED, RESOURCES, SIBLINGS, TIERS, read_tsv

BUILDERS = {
    "generic", "generic_dated", "ar_cuil", "ar_dni", "at_svnr", "baltic_code", "be_nn", "bg_egn", "br_cpf", "ca_sin",
    "cl_run", "cn_ric", "de_idnr", "dk_cpr", "ec_cedula", "es_dni", "fi_hetu", "fr_nir", "gb_nino", "hk_hkid", "id_nik",
    "ie_ppsn", "ir_code", "is_kennitala", "it_cf", "jmbg", "kr_rrn", "kz_iin", "lv_code", "mx_curp", "my_mykad", "no_fnr",
    "nz_ird", "pe_dni", "pk_cnic", "pl_pesel", "pt_nif", "rc", "ro_cnp", "ru_inn", "ru_snils", "se_personnummer", "sg_nric",
    "th_pin", "tr_tckn", "tw_nid", "ua_rnokpp", "us_ssn", "vn_cccd", "za_idnr",
}
SAFE_METHODS = {"test_numbers", "reserved_range", "unissued_series", "unassigned_place", "wrong_check", "impossible_date"}
CHECKS = {"luhn", "mod97_10", "mod11_2", "mod11_10", "verhoeff", "gtin"}
RULES = {"11-r", "11-r,10=0", "11-r,10=X", "10-r", "r", "r,10=0", "r,10=1", "r%10"}
PLACE_CODE = {
    "ba-jmb": r"\d\d", "bg-egn": r"\d{3}-\d{3}", "br-cpf": r"\d", "ca-sin": r"\d", "cn-ric": r"\d{6}", "ec-cedula": r"\d\d",
    "fr-nir": r"[0-9][0-9AB]\d{3}", "id-nik": r"\d{6}", "it-cf": r"[A-Z]\d{3}", "kr-rrn": r"\d\d", "me-jmbg": r"\d\d",
    "mk-embg": r"\d\d", "mx-curp": r"[A-Z]{2}", "my-mykad": r"\d\d", "pk-cnic": r"\d", "ro-cnp": r"\d\d", "rs-jmbg": r"\d\d",
    "ru-inn": r"\d\d", "th-pin": r"\d\d", "tw-nid": r"[A-Z]", "us-ssn": r"\d{3}", "vn-cccd": r"\d{3}",
}
TABLES = ["countries", "schemes", "places", "place_cities", "series", "documents", "validity", "name_rules", "test_numbers"]
TD_OPTIONAL = {"td3": 14, "td1": 15}
MISSING = "9999-12-31"

errors = []


def fail(message):
    errors.append(message)


class PatternError(ValueError):
    pass


def pattern_to_regex(text):
    """core::pattern's grammar as a regular expression; raises on what core rejects."""
    out = []
    i = 0
    depth = 0
    can_repeat = False
    while i < len(text):
        c = text[i]
        if c == "#":
            out.append("[0-9]")
            can_repeat = True
        elif c == "@":
            out.append("[A-Z]")
            can_repeat = True
        elif c == "*":
            out.append("[0-9A-Z]")
            can_repeat = True
        elif c == "[":
            j = text.find("]", i + 1)
            if j < 0 or j == i + 1:
                raise PatternError(f"bad class in {text}")
            body = text[i + 1:j]
            for a, b in re.findall(r"(.)-(.)", body[1:-1] if len(body) > 2 else ""):
                if a > b:
                    raise PatternError(f"bad range in {text}")
            first, last = body[0], body[-1]
            inner = body
            if "-" in (first, last):
                inner = inner.strip("-") + "-"
            out.append("[" + inner.replace("\\", "\\\\") + "]")
            i = j
            can_repeat = True
        elif c == "(":
            depth += 1
            out.append("(?:")
            can_repeat = False
        elif c == "|":
            if not depth:
                raise PatternError(f"| outside a group in {text}")
            out.append("|")
            can_repeat = False
        elif c == ")":
            if not depth:
                raise PatternError(f"unbalanced ) in {text}")
            depth -= 1
            out.append(")")
            can_repeat = True
        elif c == "{":
            j = text.find("}", i)
            if j < 0 or not can_repeat or not re.fullmatch(r"\d+(,\d+)?", text[i + 1:j]):
                raise PatternError(f"bad repeat in {text}")
            out.append(text[i:j + 1])
            i = j
            can_repeat = False
        elif c == "\\":
            i += 1
            if i >= len(text):
                raise PatternError(f"trailing backslash in {text}")
            out.append(re.escape(text[i]))
            can_repeat = True
        else:
            if c in "]}":
                raise PatternError(f"stray {c} in {text}")
            out.append(re.escape(c))
            can_repeat = True
        i += 1
    if depth:
        raise PatternError(f"unbalanced ( in {text}")
    return "".join(out)


def dated_to_regex(text):
    """generic_dated's tokens are replaced by their shapes, then the rest is a core pattern."""
    expanded = re.sub(r"\{(YYMMDD|DDMMYY)\}", "#{6}", text)
    expanded = expanded.replace("{YYYY}", "#{4}")
    expanded = re.sub(r"\{S:(odd_male|even_male)\}", "#", expanded)
    expanded = re.sub(r"\{C:[^}]*\}", "#", expanded)
    return pattern_to_regex(expanded)


def max_length(scheme):
    """The longest value a scheme can produce; a generic pattern omits its check digit."""
    dated = scheme["builder"] == "generic_dated"
    regex = dated_to_regex(scheme["pattern"]) if dated else pattern_to_regex(scheme["pattern"])
    width = re_parser.parse(regex).getwidth()[1]
    return width + (1 if scheme["builder"] in ("generic", "generic_dated") else 0)


def is_date(text):
    try:
        date.fromisoformat(text)
        return True
    except ValueError:
        return False


# Check digits, written from the survey's rules, not from the library.
def luhn(body):
    total = 0
    for i, ch in enumerate(reversed(body)):
        d = int(ch)
        if i % 2 == 0:
            d *= 2
            d -= 9 if d > 9 else 0
        total += d
    return str((10 - total % 10) % 10)


def gtin(body):
    total = sum(int(ch) * (3 if i % 2 == 0 else 1) for i, ch in enumerate(reversed(body)))
    return str((10 - total % 10) % 10)


def mod11_10(body):
    product = 10
    for ch in body:
        s = (int(ch) + product) % 10 or 10
        product = 2 * s % 11
    return str((11 - product) % 10)


VERHOEFF_D = [[0, 1, 2, 3, 4, 5, 6, 7, 8, 9], [1, 2, 3, 4, 0, 6, 7, 8, 9, 5], [2, 3, 4, 0, 1, 7, 8, 9, 5, 6],
              [3, 4, 0, 1, 2, 8, 9, 5, 6, 7], [4, 0, 1, 2, 3, 9, 5, 6, 7, 8], [5, 9, 8, 7, 6, 0, 4, 3, 2, 1],
              [6, 5, 9, 8, 7, 1, 0, 4, 3, 2], [7, 6, 5, 9, 8, 2, 1, 0, 4, 3], [8, 7, 6, 5, 9, 3, 2, 1, 0, 4],
              [9, 8, 7, 6, 5, 4, 3, 2, 1, 0]]
VERHOEFF_P = [[0, 1, 2, 3, 4, 5, 6, 7, 8, 9], [1, 5, 7, 6, 2, 8, 3, 0, 9, 4], [5, 8, 0, 3, 7, 9, 6, 1, 4, 2],
              [8, 9, 1, 6, 0, 4, 3, 5, 2, 7], [9, 4, 5, 3, 1, 2, 6, 8, 7, 0], [4, 2, 8, 6, 5, 7, 3, 9, 0, 1],
              [2, 7, 9, 3, 8, 0, 6, 4, 1, 5], [7, 0, 4, 6, 9, 1, 3, 2, 5, 8]]


def verhoeff(body):
    c = 0
    for i, ch in enumerate(reversed(body)):
        c = VERHOEFF_D[c][VERHOEFF_P[(i + 1) % 8][int(ch)]]
    return str([0, 4, 3, 2, 1, 5, 6, 7, 8, 9][c])


def weighted(body, weights, rule):
    total = sum(int(ch) * w for ch, w in zip(body, weights))
    if rule.startswith("10-r"):
        return str((10 - total % 10) % 10)
    r = total % 11
    if rule == "r%10":
        return str(r % 10)
    if rule.startswith("11-r"):
        c = 11 - r
        if c == 11:
            return "0"
        if c == 10:
            return {"11-r,10=0": "0", "11-r,10=X": "X"}.get(rule)
        return str(c)
    if r == 10:
        return {"r,10=0": "0", "r,10=1": "1"}.get(rule)
    return str(r)


def compute_check(check, body):
    if check.startswith("weights:"):
        _, weights, rule = check.split(":")
        return weighted(body, [int(w) for w in weights.split()], rule)
    return {"luhn": luhn, "gtin": gtin, "mod11_10": mod11_10, "verhoeff": verhoeff}[check](body)


def check_schemes(tier, schemes):
    seen = Counter(r["key"] for r in schemes)
    for key, n in seen.items():
        if n > 1:
            fail(f"{tier}/schemes: {key} twice")
    regexes = {}
    for r in schemes:
        key = r["key"]
        if not re.fullmatch(r"[a-z]{2}-[a-z0-9-]+", key) or key[:2].upper() != r["country"]:
            fail(f"schemes: bad key or country: {key} {r['country']}")
        if r["builder"] not in BUILDERS:
            fail(f"schemes: {key}: no builder {r['builder']}")
        if r["safe_method"] not in SAFE_METHODS:
            fail(f"schemes: {key}: no safe method {r['safe_method']}")
        for col in ("from", "until"):
            if r[col] and not is_date(r[col]):
                fail(f"schemes: {key}: {col} is not a date: {r[col]}")
        if r["from"] and r["until"] and r["from"] >= r["until"]:
            fail(f"schemes: {key}: from is not before until")
        for token in r["assigned"].split(";"):
            if not re.fullmatch(r"birth|age:\d+-\d+|lag:\d+|birth_from:\d{4}-\d\d-\d\d", token):
                fail(f"schemes: {key}: bad assigned token {token!r}")
        if r["place_source"] not in ("", "birthplace", "residence"):
            fail(f"schemes: {key}: bad place_source {r['place_source']}")
        if not r["source"] or not r["label"]:
            fail(f"schemes: {key}: no source or label")
        for part in filter(None, r["params"].split(";")):
            if not re.fullmatch(r"[a-z0-9_]+=[^;=]+", part):
                fail(f"schemes: {key}: bad param {part!r}")
            name, value = part.split("=")
            if name.endswith(("_from", "_until", "_from_birth", "_until_birth")) and not is_date(value):
                fail(f"schemes: {key}: {name} is not a date: {value}")
        try:
            regexes[key] = dated_to_regex(r["pattern"]) if r["builder"] == "generic_dated" else pattern_to_regex(r["pattern"])
            if r["safe_pattern"]:
                pattern_to_regex(r["safe_pattern"])
        except PatternError as e:
            fail(f"schemes: {key}: {e}")
            continue
        if r["builder"] in ("generic", "generic_dated"):
            check = r["check"]
            if check.startswith("weights:"):
                parts = check.split(":")
                if len(parts) != 3 or parts[2] not in RULES or not re.fullmatch(r"-?\d+( -?\d+)*", parts[1]):
                    fail(f"schemes: {key}: bad weights {check}")
            elif check not in CHECKS:
                fail(f"schemes: {key}: generic needs a check, got {check!r}")
        elif r["check"]:
            fail(f"schemes: {key}: a built-in builder owns its check, got {r['check']!r}")
    return regexes


def check_examples(schemes, regexes):
    by_key = {r["key"]: r for r in schemes}
    served_without = set(by_key)
    for ex in read_tsv(CURATED / "scheme_examples.tsv"):
        key, value, kind = ex["scheme"], ex["value"], ex["kind"]
        scheme = by_key.get(key)
        if not scheme:
            fail(f"examples: no scheme {key}")
            continue
        served_without.discard(key)
        if kind == "algorithm":
            continue
        generic = scheme["builder"] in ("generic", "generic_dated")
        if generic:
            body, digit = value[:-1], value[-1]
            if not re.fullmatch(regexes[key], body):
                fail(f"examples: {key}: {value}: the body does not fit {scheme['pattern']}")
            if compute_check(scheme["check"], body) != digit:
                fail(f"examples: {key}: {value}: check digit should be {compute_check(scheme['check'], body)}")
        elif kind != "safe" and not re.fullmatch(regexes[key], value):
            fail(f"examples: {key}: {value} does not fit {scheme['pattern']}")
    # The US has no check digit and no worked examples (the survey says so).
    for key in sorted(served_without - {"us-ssn"}):
        fail(f"examples: {key} has none")


def check_countries(tier, countries, schemes):
    keys = {r["key"]: r for r in schemes}
    if len(countries) != 199:
        fail(f"{tier}/countries: {len(countries)} rows, not 199")
    if sum(1 for r in countries if r["primary_scheme"]) != 63:
        fail(f"{tier}/countries: not 63 primary schemes")
    icao = re.compile(r"[A-Z]{1,3}")
    for r in countries:
        cc = r["country_code"]
        if not re.fullmatch(r"[A-Z]{2}", cc):
            fail(f"countries: bad code {cc}")
        if not r["population"].isdigit() or int(r["population"]) <= 0:
            fail(f"countries: {cc}: population {r['population']!r}")
        if not icao.fullmatch(r["icao_code"]):
            fail(f"countries: {cc}: icao {r['icao_code']}")
        p = r["primary_scheme"]
        if p and (p not in keys or keys[p]["country"] != cc):
            fail(f"countries: {cc}: primary {p}")


def load_admin1():
    codes = defaultdict(set)
    with open(SIBLINGS / "city-generator/resources/admin1CodesASCII.txt", encoding="utf-8") as f:
        for line in f:
            cc, code = line.split("\t")[0].split(".")
            codes[cc].add(code)
    return codes


def check_places(tier, places, schemes, countries, admin1):
    by_key = {r["key"]: r for r in schemes}
    known = {r["country_code"] for r in countries}
    seen = Counter()
    positive = defaultdict(int)
    placed = set()
    for r in places:
        scheme = by_key.get(r["scheme"])
        if not scheme:
            fail(f"{tier}/places: no scheme {r['scheme']}")
            continue
        placed.add(r["scheme"])
        seen[(r["scheme"], r["code"], r["from"])] += 1
        shape = PLACE_CODE.get(r["scheme"])
        if not shape or not re.fullmatch(shape, r["code"]):
            fail(f"{tier}/places: {r['scheme']} {r['code']!r} does not fit {shape}")
        if not r["weight"].isdigit():
            fail(f"{tier}/places: {r['scheme']} {r['code']}: weight {r['weight']!r}")
        elif int(r["weight"]):
            positive[r["scheme"]] += 1
        for a in filter(None, r["admin1"].split(";")):
            if a not in admin1[scheme["country"]]:
                fail(f"{tier}/places: {r['scheme']} {r['code']}: no admin1 {a} in {scheme['country']}")
        for col in ("from", "until"):
            if r[col] and not is_date(r[col]):
                fail(f"{tier}/places: {r['scheme']} {r['code']}: {col} {r[col]!r}")
        if r["country"] and r["country"] != "*" and r["country"] not in known and not re.fullmatch(r"[A-Z]{2}", r["country"]):
            fail(f"{tier}/places: {r['scheme']} {r['code']}: country {r['country']}")
        if r["reserved"] not in ("", "1"):
            fail(f"{tier}/places: {r['scheme']} {r['code']}: reserved {r['reserved']!r}")
    for key, n in seen.items():
        if n > 1:
            fail(f"{tier}/places: {key} twice")
    reserved = {r["scheme"] for r in places if r["reserved"]}
    for key, scheme in by_key.items():
        if scheme["safe_method"] == "unassigned_place" and key not in reserved:
            fail(f"{tier}/places: {key}: safe mode draws unassigned places and has none")
    need = {r["key"] for r in schemes if r["place_source"]}
    for key in sorted(need - placed):
        fail(f"{tier}/places: {key} has a place_source and no places")
    for key in sorted(placed):
        if not positive[key]:
            fail(f"{tier}/places: {key} has nothing to draw")


def check_place_cities(tier, rows, places, schemes):
    country = {r["key"]: r["country"] for r in schemes}
    codes = {(r["scheme"], r["code"]) for r in places}
    cities = {}
    with open(SIBLINGS / f"city-generator/resources/{tier}/cities.tsv", encoding="utf-8", newline="") as f:
        next(f)
        for line in f:
            cells = line.split("\t", 7)
            cities[cells[0]] = cells[6]
    seen = Counter()
    for r in rows:
        seen[(r["scheme"], r["geonameid"])] += 1
        if (r["scheme"], r["code"]) not in codes:
            fail(f"{tier}/place_cities: {r['scheme']} {r['code']} is not in places.tsv")
        where = cities.get(r["geonameid"])
        if where is None:
            fail(f"{tier}/place_cities: {r['geonameid']} is not a {tier} city")
        elif where != country[r["scheme"]]:
            fail(f"{tier}/place_cities: {r['geonameid']} is in {where}, not {country[r['scheme']]}")
    for key, n in seen.items():
        if n > 1:
            fail(f"{tier}/place_cities: {key} twice")


def check_series(tier, rows, schemes):
    keys = {r["key"] for r in schemes}
    groups = defaultdict(list)
    for r in rows:
        if r["scheme"] not in keys:
            fail(f"{tier}/series: no scheme {r['scheme']}")
        if not is_date(r["date"]) or not r["value"].isdigit():
            fail(f"{tier}/series: bad row {r}")
            continue
        groups[(r["scheme"], r["segment"])].append((r["date"], int(r["value"])))
    for key, points in groups.items():
        points.sort()
        for a, b in zip(points, points[1:]):
            if a[0] == b[0] or b[1] <= a[1]:
                fail(f"{tier}/series: {key}: anchors do not rise: {a} {b}")
    for need in ("ar-dni", "cl-run", "pe-dni", "us-ssn"):
        if need not in {k[0] for k in groups}:
            fail(f"{tier}/series: nothing for {need}")


def check_documents(tier, rows, schemes, survey):
    keys = {r["key"]: r for r in schemes}
    seen = Counter(r["key"] for r in rows)
    for key, n in seen.items():
        if n > 1:
            fail(f"{tier}/documents: {key} twice")
    timeline = defaultdict(list)
    unused = []
    for r in rows:
        k = r["key"]
        if r["kind"] not in ("passport", "id_card"):
            fail(f"documents: {k}: kind {r['kind']}")
        if r["mrz"] not in ("td3", "td1", "none"):
            fail(f"documents: {k}: mrz {r['mrz']}")
        if r["evidence"] not in ("issuer", "reference", "specimen", "placeholder", "generic"):
            fail(f"documents: {k}: evidence {r['evidence']}")
        if r["kind"] == "passport" and r["mrz"] != "td3":
            fail(f"documents: {k}: a passport without TD3")
        if r["mrz_code"] and not re.fullmatch(r"[A-Z][A-Z<]", r["mrz_code"]):
            fail(f"documents: {k}: mrz_code {r['mrz_code']!r}")
        try:
            regex = pattern_to_regex(r["pattern"])
        except PatternError as e:
            fail(f"documents: {k}: {e}")
            regex = ""
        if r["evidence"] == "generic" and r["pattern"] != "#{9}":
            fail(f"documents: {k}: generic with {r['pattern']}")
        if regex and not re.search(r"[0-9A-Z]", regex):
            fail(f"documents: {k}: pattern has no number")
        if not is_date(r["from"]) or (r["until"] and not is_date(r["until"])):
            fail(f"documents: {k}: bad dates")
        elif r["until"] and r["from"] >= r["until"]:
            fail(f"documents: {k}: from {r['from']} not before until {r['until']}")
        own = r["number_scheme"]
        if own and (own not in keys or keys[own]["country"] != r["country"]):
            fail(f"documents: {k}: number_scheme {own}")
        if r["year_at"] and not r["year_at"].isdigit():
            fail(f"documents: {k}: year_at {r['year_at']!r}")
        if r["min_age"] and not r["min_age"].isdigit():
            fail(f"documents: {k}: min_age {r['min_age']!r}")
        opt = r["mrz_optional"]
        if opt:
            scheme = keys.get(opt)
            if not scheme or scheme["country"] != r["country"]:
                fail(f"documents: {k}: mrz_optional {opt}")
            elif r["mrz"] == "none":
                unused.append(k)
            elif r["mrz"] in TD_OPTIONAL and max_length(scheme) > TD_OPTIONAL[r["mrz"]]:
                fail(f"documents: {k}: {opt} can take {max_length(scheme)} characters, the {r['mrz']} optional field holds {TD_OPTIONAL[r['mrz']]}")
        timeline[(r["country"], r["kind"])].append(r)
    if unused:
        print(f"note: {tier}/documents: mrz_optional has no effect without an MRZ: {', '.join(unused)}")
    # Every document that exists covers all dates once; those that do not exist have no rows.
    for s in survey:
        key = (s["cc"], s["doc"])
        if s["exists"] != "yes":
            if key in timeline:
                fail(f"documents: {key} does not exist and has rows")
            continue
        formats = sorted(timeline.get(key, []), key=lambda r: r["from"])
        if not formats:
            fail(f"documents: {key} has no rows")
            continue
        cursor = "1900-01-01"
        for f in formats:
            if f["from"] != cursor:
                fail(f"documents: {key}: {f['key']} starts {f['from']}, expected {cursor}")
            cursor = f["until"] or MISSING
        if cursor != MISSING:
            fail(f"documents: {key}: ends at {cursor}")
        # The survey's evidence level none (or no pattern) is exactly the always-generic document.
        none = not s["number_pattern"] or s["evidence"] == "none"
        always_generic = all(f["evidence"] == "generic" for f in formats)
        if none != always_generic:
            fail(f"documents: {key}: survey none={none}, generic={always_generic}")
    return timeline


def check_validity(tier, rows, documents):
    docs = {f"{r['country'].lower()}-{r['kind'].replace('_', '-')}" for r in documents}
    seen = Counter()
    for r in rows:
        k = (r["document"], r["from_age"], r["from"])
        seen[k] += 1
        if r["document"] not in docs:
            fail(f"{tier}/validity: no document {r['document']}")
        if not r["from_age"].isdigit():
            fail(f"{tier}/validity: {k}: from_age")
        if bool(r["years"]) == bool(r["until_age"]):
            fail(f"{tier}/validity: {k}: needs years or until_age, not both")
        if r["years"] and not re.fullmatch(r"\d+(\|\d+)?", r["years"]):
            fail(f"{tier}/validity: {k}: years {r['years']!r}")
        if r["until_age"] and int(r["until_age"]) <= int(r["from_age"]):
            fail(f"{tier}/validity: {k}: until_age below from_age")
        if r["expiry_rule"] not in ("day_before", "same_day", "birthday", "application"):
            fail(f"{tier}/validity: {k}: expiry_rule {r['expiry_rule']!r}")
        for col in ("from", "until"):
            if r[col] and not is_date(r[col]):
                fail(f"{tier}/validity: {k}: {col}")
    for k, n in seen.items():
        if n > 1:
            fail(f"{tier}/validity: {k} twice")


def check_lists(tier, rules, tests, schemes, regexes):
    keys = {r["key"]: r for r in schemes}
    for r in rules:
        if r["scheme"] not in keys or r["kind"] not in ("particle", "given_skip", "char", "word"):
            fail(f"{tier}/name_rules: bad row {r}")
    counted = Counter()
    for r in tests:
        key = r["scheme"]
        counted[key] += 1
        if key not in keys or keys[key]["safe_method"] != "test_numbers":
            fail(f"{tier}/test_numbers: {key} is not a test_numbers scheme")
        elif not re.fullmatch(regexes[key], r["value"]):
            fail(f"{tier}/test_numbers: {key}: {r['value']} does not fit")
    for key, row in keys.items():
        if row["safe_method"] == "test_numbers" and not counted[key]:
            fail(f"{tier}/test_numbers: nothing for {key}")


def main():
    admin1 = load_admin1()
    survey = read_tsv(CURATED / "survey_documents.tsv")
    data = {}
    for tier in TIERS:
        data[tier] = {}
        for name in TABLES:
            path = RESOURCES / tier / f"{name}.tsv"
            if not path.exists():
                fail(f"{tier}: {name}.tsv is missing")
                data[tier][name] = []
                continue
            first = path.read_text(encoding="utf-8").split("\n", 1)[0]
            if not first.startswith("# "):
                fail(f"{tier}/{name}.tsv: no source comment on the first line")
            data[tier][name] = read_tsv(path)
    for tier in TIERS:
        d = data[tier]
        regexes = check_schemes(tier, d["schemes"])
        if tier == TIERS[0]:
            check_examples(d["schemes"], regexes)
        check_countries(tier, d["countries"], d["schemes"])
        check_places(tier, d["places"], d["schemes"], d["countries"], admin1)
        check_place_cities(tier, d["place_cities"], d["places"], d["schemes"])
        check_series(tier, d["series"], d["schemes"])
        check_documents(tier, d["documents"], d["schemes"], survey)
        check_validity(tier, d["validity"], d["documents"])
        check_lists(tier, d["name_rules"], d["test_numbers"], d["schemes"], regexes)
    lite_codes = {(r["scheme"], r["code"], r["from"]) for r in data["lite"]["places"]}
    full_codes = {(r["scheme"], r["code"], r["from"]) for r in data["full"]["places"]}
    for key in sorted(lite_codes - full_codes):
        fail(f"lite has a place full lacks: {key}")
    if errors:
        for e in errors[:60]:
            print("FAIL", e)
        print(f"{len(errors)} failures")
        return 1
    for tier in TIERS:
        sizes = {name: (RESOURCES / tier / f"{name}.tsv").stat().st_size for name in TABLES}
        print(f"{tier}: {sum(sizes.values()) / 1e6:.2f} MB; " + ", ".join(f"{k} {len(data[tier][k])}" for k in TABLES))
    print("check_schemes: all tables pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
