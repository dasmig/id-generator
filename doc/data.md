# Data and Method

The data in `resources/` and how id-generator uses it (rule 6 of the dasmig standard).

## Tiers

| Table | `lite` rows | `full` rows |
| --- | --- | --- |
| `countries.tsv` | 199 | 199 |
| `schemes.tsv` | 65 | 65 |
| `places.tsv` | 3,861 | 61,054 |
| `place_cities.tsv` | 3,948 | 52,380 |
| `series.tsv` | 5,631 | 5,631 |
| `documents.tsv` | 536 | 536 |
| `validity.tsv` | 524 | 524 |
| `name_rules.tsv` | 113 | 113 |
| `test_numbers.tsv` | 2,153 | 42,945 |

The tables total 0.55 MB in `lite` and 5.44 MB in `full`. The tiers differ in three tables:

- `places.tsv`: `full` has every commune-level code in France, Italy, China and Indonesia, and past French and Italian communes. `lite` has the codes its own cities use, each region's largest code, and all the smaller lists.
- `place_cities.tsv`: each tier covers city-generator's cities of the same tier.
- `test_numbers.tsv`: `full` has all of Sweden's test numbers, `lite` every 20th.

## Columns

Every table is TSV, with a first-line `#` comment naming its source.

| Table | Key | Columns |
| --- | --- | --- |
| `countries.tsv` | `country_code` | `population`, `icao_code`, `primary_scheme`, `translit` (national MRZ rules: `Г=H;^Є=YE`, where `^` means only as a word's first letter; a rule's letter must be outside ASCII) |
| `schemes.tsv` | `key` | `country`, `label`, `native_label`, `builder`, `display`, `assigned`, `safe_method`, `pattern`, `check`, `place_source`, `from`, `until`, `safe_pattern`, `params`, `source` |
| `places.tsv` | `scheme`, `code`, `from` | `label`, `weight`, `admin1` (GeoNames codes, `;`-separated), `country` (alpha-2, or `*` for any foreign country, on codes for births abroad), `until`, `reserved` (`1`: unassigned, for safe mode) |
| `place_cities.tsv` | `scheme`, `geonameid` | `code` |
| `series.tsv` | `scheme`, `segment`, `date` | `value`, `note` |
| `documents.tsv` | `key` | `country`, `kind`, `pattern`, `from`, `until`, `mrz`, `evidence`, `prefix_places`, `mrz_code`, `mrz_optional`, `min_age`, `weight`, `year_at` (where the year of issue sits in the number), `number_scheme` (the scheme whose number the document shows) |
| `validity.tsv` | `document`, `from_age`, `from` | `years` (`5\|10`: the holder's choice), `until_age`, `until`, `expiry_rule` |
| `name_rules.tsv` | `scheme`, `kind`, `word` | `replacement` |
| `test_numbers.tsv` | `scheme`, `value` | |

`assigned` is `birth` or `age:a-b`, with `;lag:N` days and `;birth_from:DATE`. `check` is a core check scheme or `weights:<w...>:<rule>`, with the rules `r`, `r,10=0`, `r,10=1`, `r%10`, `10-r`, `11-r`, `11-r,10=0` and `11-r,10=X`. `params` holds date switches, such as `random_from=2020-10-05`.

Key columns cannot be empty. In `places.tsv` and `validity.tsv`, a `from` of `1900-01-01` means "always". In `series.tsv`, a `segment` of `-` means the scheme has one series.

## Sources

**Place codes:**

| Scheme | Source | Reliability |
| --- | --- | --- |
| `fr-nir` | INSEE's Code officiel géographique 2025: communes, the arrondissements of Paris, Lyon and Marseille, communes since 1943, countries | Official |
| `it-cf` | ANPR's Archivio comuni and table 2 (Ministero dell'Interno). Checked once against ISTAT's list: of the 7,893 codes in both, the ISTAT numbers all agree. One code is only in ANPR and three only in ISTAT's copy | Official. Table 2 was last updated in January 2024 |
| `us-ssn` | SSA, area numbers by state | Official, archived |
| `cn-ric`, `id-nik` | Wikidata P13971 and P2588, the codes these numbers use | Good: community data for official codes |
| `th-pin` | Wikidata P300 (ISO 3166-2 equals the number's province digits) | Good |
| `ba-jmb`, `rs-jmbg`, `mk-embg` | Each country's law on the number | Official; the mapping to GeoNames is by hand |
| `my-mykad` | JPN's state and country code lists | Official |
| `ro-cnp`, `bg-egn`, `vn-cccd`, `tw-nid`, `mx-curp`, `ec-cedula`, `br-cpf`, `ca-sin`, `pk-cnic`, `ru-inn` | The sources in the number survey | Official or reference, per scheme below |
| `mx-curp`'s word list | django-localflavor's `CURP_INCONVENIENT_WORDS` (BSD), plus `VACA` | Reference (see the limits) |
| `me-jmbg` | en.wikipedia and sr.wikipedia, citing a Pravilnik that could not be retrieved | Reference |
| `kr-rrn` | ko.wikipedia, citing a government press release | Reference, and its ranges overlap (see the limits) |

**Sequential numbers:**

- **US SSN:** the SSA's High Group Lists on 13 dates from 1999-01-04 to 2011-06-24, from the Wayback Machine, since ssa.gov refuses scripts. A group is stored as its place in the SSA's issuing order (odd 01-09, even 10-98, even 02-08, odd 11-99). No older list was found, so the years from 1936-11-01 to 1999 are a straight line. Areas opened later start at a date extrapolated from their own rate.
- **Argentina's DNI:** only the 2023 jump to 70 million is official. The rest is a press table by birth period (Infonegocios, 2025).
- **Chile's RUN:** derived from 1930 (the first number), about 12 million in 1984 and an undated "28 million range" from the Registro Civil.
- **Peru's DNI:** es.wikipedia's series by birth period, uncited. Numbers for people born before September 1978 follow geographic series that are not mapped.

**Everything else:** the two surveys in `scripts/curated/`, `survey_numbers.tsv` and `survey_documents.tsv`, which give a source per row. `LICENSE_DATA.txt` has the licences.

## Generation Model

The design is `designs/id-generator.md` in the meta repository.

- **The person.** A country not given is drawn by population among the countries that can serve the request. A birth date not given is drawn uniformly for ages 18 to 80, and a sex not given is drawn 50/50.
- **Assignment.** A number is assigned at birth, or at an age drawn from the scheme's band, then no earlier than the scheme's start, then up to `lag` days later. A number assigned after the reference date, or on or after the scheme's `until`, is left out.
- **Places.** The scheme's `place_source` says which place it encodes. Without the one it names, it uses the other if that is in the same country. A number takes the code of the person's city from `place_cities.tsv`. A city that is not there falls back to a code drawn by weight from its `admin1`, and with no city the code is drawn by weight from the whole list. A birthplace in another country takes that country's row, then the `*` row; a scheme with neither rejects the request. A residence in another country says nothing about the code, so the code is drawn.
- **Weights.** A code's weight is the population of the GeoNames cities in it, and codes sharing a region split it equally. China is the exception: a county's weight is its Wikidata population, or the median county's where none is given.
- **Codes with no city.** A current commune with no city has weight 1 in France and Italy. An Indonesian regency with no city gives its districts 1,000 each. Past communes, codes for births abroad, reserved codes and curated codes with no region have weight 0: they are valid, and they are not drawn.
- **Sequential numbers** interpolate between a series' anchors in integer arithmetic, with up to 7 days of noise either way, and carry the last two anchors' rate past the end. Anchors one day apart are a jump. The US keys its series on the assignment date, by area. Argentina, Chile and Peru key theirs on the birth date, because that is how their anchors are known.
- **Names.** Names are folded from the Latin-1 Supplement and Latin Extended-A to ASCII. A name in another script counts as missing, and missing letters are drawn in the scheme's shape.
- **Portugal's first digit.** No source gives the switch from 1 to 2. Before 3 starts in 2019, the library draws 1 for a quarter of people and 2 for the rest.
- **Documents** are described under [Documents](#documents).
- **Safe mode** uses each scheme's method below, and for documents the method under [Documents](#documents).
- **Checking.** `is_valid` tests structure, check digit and encoded date, and looks nothing up. Place codes are not checked, because the tiers hold different lists. Published test numbers and reserved ranges count as valid. Wrong-check numbers do not, nor do impossible-date numbers when a reference date is given.

## Number Schemes

65 schemes in 63 countries. Evidence is the survey's: `issuer`, `reference`, or `single` when one source backs a rule.

| Scheme | Number | Builder | Evidence | Safe method |
| --- | --- | --- | --- | --- |
| `ae-eid` | Emirates ID number | `generic_dated` | single | `wrong_check` |
| `ar-cuil` | Unique Labour Identification Code | `ar_cuil` | reference | `wrong_check` |
| `ar-dni` | National Identity Document number | `ar_dni` | issuer | `unissued_series` |
| `at-svnr` | Social insurance number | `at_svnr` | issuer | `wrong_check` |
| `au-tfn` | Tax file number | `generic` | reference | `wrong_check` |
| `ba-jmb` | unique master number | `jmbg` | issuer | `wrong_check` |
| `be-nn` | National Register number (identification number) | `be_nn` | issuer | `wrong_check` |
| `bg-egn` | uniform civil number | `bg_egn` | reference | `wrong_check` |
| `br-cpf` | Individual Taxpayer Registry number | `br_cpf` | reference | `wrong_check` |
| `ca-sin` | Social Insurance Number | `ca_sin` | reference | `wrong_check` |
| `ch-ahv` | AHV/AVS number (social security number, AHVN13) | `generic` | reference | `wrong_check` |
| `cl-run` | Unique National Role | `cl_run` | reference | `wrong_check` |
| `cn-ric` | Resident identity number | `cn_ric` | issuer | `wrong_check` |
| `cz-rc` | birth number | `rc` | issuer | `wrong_check` |
| `de-idnr` | Tax identification number | `de_idnr` | issuer | `reserved_range` |
| `dk-cpr` | CPR number (personal identification number) | `dk_cpr` | reference | `impossible_date` |
| `ec-cedula` | Identity card number | `ec_cedula` | reference | `wrong_check` |
| `ee-isikukood` | Personal identification code | `baltic_code` | reference | `wrong_check` |
| `es-dni` | National identity document number (NIF of Spanish nationals) | `es_dni` | reference | `wrong_check` |
| `fi-hetu` | Personal identity code | `fi_hetu` | issuer | `reserved_range` |
| `fr-nir` | Social security number (NIR, INSEE number) | `fr_nir` | issuer | `wrong_check` |
| `gb-nino` | National Insurance number | `gb_nino` | issuer | `reserved_range` |
| `gr-amka` | social security registration number | `generic_dated` | reference | `wrong_check` |
| `hk-hkid` | Hong Kong identity card number | `hk_hkid` | reference | `wrong_check` |
| `hr-oib` | personal identification number | `generic` | issuer | `wrong_check` |
| `id-nik` | Population identity number (NIK) | `id_nik` | issuer | `unassigned_place` |
| `ie-ppsn` | Personal Public Service Number | `ie_ppsn` | reference | `wrong_check` |
| `il-idnr` | Identity number (Teudat Zehut number) | `generic` | reference | `wrong_check` |
| `in-aadhaar` | Aadhaar number | `generic` | reference | `wrong_check` |
| `ir-codemeli` | National code | `ir_code` | reference | `wrong_check` |
| `is-kennitala` | Icelandic identification number | `is_kennitala` | issuer | `test_numbers` |
| `it-cf` | Tax code | `it_cf` | reference | `wrong_check` |
| `jp-mynumber` | Individual Number (My Number) | `generic` | issuer | `wrong_check` |
| `kr-rrn` | Resident registration number | `kr_rrn` | reference | `impossible_date` |
| `kz-iin` | individual identification number | `kz_iin` | issuer | `wrong_check` |
| `lt-asmens-kodas` | Personal code | `baltic_code` | reference | `wrong_check` |
| `lv-personas-kods` | Personal code | `lv_code` | single | `wrong_check` |
| `me-jmbg` | unique master citizen number | `jmbg` | reference | `wrong_check` |
| `mk-embg` | unique master citizen number | `jmbg` | issuer | `wrong_check` |
| `mx-curp` | Unique Population Registry Code | `mx_curp` | issuer | `wrong_check` |
| `my-mykad` | MyKad identity card number (NRIC) | `my_mykad` | issuer | `unassigned_place` |
| `nl-bsn` | Citizen service number | `generic` | issuer | `wrong_check` |
| `no-fodselsnummer` | National identity number (birth number) | `no_fnr` | issuer | `reserved_range` |
| `nz-ird` | IRD number | `nz_ird` | reference | `wrong_check` |
| `pe-dni` | National Identity Document number (Unique Identification Code) | `pe_dni` | single | `wrong_check` |
| `pk-cnic` | Computerised National Identity Card number | `pk_cnic` | reference | `unassigned_place` |
| `pl-pesel` | PESEL number | `pl_pesel` | issuer | `wrong_check` |
| `pt-nif` | Tax identification number | `pt_nif` | reference | `wrong_check` |
| `ro-cnp` | personal numeric code | `ro_cnp` | reference | `wrong_check` |
| `rs-jmbg` | unique master citizen number | `jmbg` | issuer | `wrong_check` |
| `ru-inn` | taxpayer identification number (individual) | `ru_inn` | reference | `wrong_check` |
| `ru-snils` | individual insurance account number | `ru_snils` | reference | `wrong_check` |
| `sa-nid` | National ID number (MOI computer number) | `generic` | issuer | `wrong_check` |
| `se-personnummer` | Personal identity number | `se_personnummer` | issuer | `test_numbers` |
| `sg-nric` | NRIC number (UIN) | `sg_nric` | reference | `wrong_check` |
| `si-emso` | unique master citizen number | `jmbg` | issuer | `wrong_check` |
| `sk-rc` | birth number | `rc` | issuer | `wrong_check` |
| `th-pin` | Personal identification number | `th_pin` | reference | `wrong_check` |
| `tr-tckn` | Turkish identification number | `tr_tckn` | reference | `wrong_check` |
| `tw-nid` | National identification number | `tw_nid` | reference | `wrong_check` |
| `ua-rnokpp` | taxpayer registration card number | `ua_rnokpp` | reference | `wrong_check` |
| `us-ssn` | Social Security number | `us_ssn` | issuer | `reserved_range` |
| `uy-cedula` | Identity card number | `generic` | reference | `wrong_check` |
| `vn-cccd` | Personal identification number (citizen ID card number) | `vn_cccd` | reference | `impossible_date` |
| `za-idnr` | Identity number | `za_idnr` | reference | `wrong_check` |

**Safe methods** (51 schemes use the fallback, `wrong_check`):

| Method | Meaning | Passes format validators? |
| --- | --- | --- |
| `test_numbers` | A published test number from `test_numbers.tsv` | Yes |
| `reserved_range` | A range the authority sets aside (`safe_pattern`) | Yes, unless the validator knows the reservation |
| `unissued_series` | A series past those issued | Yes |
| `unassigned_place` | A place code no area uses (`reserved` rows) | Yes, unless the validator checks the place |
| `wrong_check` | The right structure and fields, with a wrong check digit | No: any validator rejects it |
| `impossible_date` | A birth date after the reference date | No, if the validator checks dates |

**Weak checks.** Saudi Arabia publishes no check formula, the UAE's Luhn is disputed, and Latvian codes from July 2017 have no check digit in law. For `sa-nid`, `ae-eid` and `lv-personas-kods`, a wrong-check number is not guaranteed to be unheld.

**Unserved for numbers.** These countries still get documents:

- **Colombia:** the structure is published, but there is no check digit.
- **Nigeria (NIN) and Ethiopia (Fayda FIN):** each has only an unverified lead on a Verhoeff check digit.
- **Egypt:** the check digit is unpublished, and which digit carries sex is unconfirmed.
- **Kenya:** no structure is published.
- **Bangladesh:** the 13- and 17-digit forms have no check digit, and the smart card's checksum is unpublished.
- **The Philippines:** 12 random digits with no published check.

## Documents

**Issue date.** A person renews when a document runs out, so the document in force on the reference date was issued uniformly over the days whose document still runs that day. The library draws a day, works out the format and expiry for it, and draws again if the document has expired by the reference date or the person was under the format's minimum age. If 1,024 draws find nothing, it tries the reference date itself. If that fails too, the person has no document. Someone whose rule runs to an age and whose reference date is the last day before that age is one such case.

**Format and validity.** A country's formats cover all dates, so the format comes from the issue date, with `weight` deciding between formats that overlap. The validity comes from the age at issue, and `years` of `5|10` draws the holder's choice. A `from_age` band with `years` of 0 has no expiry. Each `expiry_rule` works as follows: `day_before` is the day before the anniversary, `same_day` the anniversary, `birthday` the holder's birthday after that many years, and `application` counts from a day up to 30 days before issue.

**Numbers.** A number is drawn from the format's pattern, with three exceptions:

- `prefix_places` puts a code of that scheme's places in front.
- `year_at` writes the two-digit year of issue at that position. Only Russia's internal passport uses it.
- `number_scheme` shows the person's own number in that scheme, in the card's form. It covers the 18 cards whose survey row says the card's number is the national number: AR BR CN HK ID IL IN IR JP KR MY PE PK SA SG TH TW VN. The issue date is then kept on or after the day the number was assigned. Peru's card leaves the check digit apart, so it shows the first eight digits. Older numbers that the card's pattern does not foresee, such as a 7-digit DNI, stay as they are.

**The machine-readable zone** follows ICAO Doc 9303, read from the current PDFs:

- **Layout.** Part 4 for TD3 passports and Part 5 for TD1 cards, with every check digit by Part 3, including the composite.
- **Letters.** Part 3, section 6's tables for Latin and Cyrillic, taking the first alternative where the table gives several (`Ä` is `AE`, `Ñ` is `N`).
- **National rules.** Part 3 gives exceptions for Belarus, Bulgaria, North Macedonia, Serbia and Ukraine, and these are in `countries.tsv`.
- **Shortening.** By Part 4's examples.
- **The name is the caller's.** The library draws no names, so a document without them has no zone. A name it cannot write leaves the zone out.
- **`mrz_optional`** carries the person's number in that scheme, when they held it on the issue date.

**Safe documents.** The number is changed to fall outside every format of that country and document: its first character becomes `X`, or `0` if that still fits, and otherwise it is shortened until no format matches. This takes no draws, and the zone's check digits are computed on the changed number, so the zone is valid. The result has `reserved` set. A card that shows a national number shows the safe number of that scheme, made by the scheme's own method. The design's "unissued passport series" is not used, because there is no passport series data.

**How much a safe document promises.** Outside the known formats is the best that can be shown without a list of what was issued. It says nothing where the format is `generic`, since the real format of those documents is unknown. It also says less where the `evidence` is `specimen` or `placeholder`, since one sample shows the shape of a number and not all its forms.

## Generic Documents

A document with no surveyed format has a nine-digit number (`#{9}`) and `evidence = "generic"`. Today, that is:

- **113 passports:** AD AE AF AG AL BB BD BF BI BJ BN BS BT BW BZ CF CG CI CM CR CU CV DJ DM DZ EC ER ET FJ FM GA GD GE GM GN GQ GW GY HN HT JM JO KE KH KI KM KN KP KW LA LB LC LK LR LS LY MA MC ME MG MH ML MN MU MV MW MX MZ NA NE NG NI PA PE PG PK PW PY QA RW SA SB SD SL SM SN SO SR SS ST SV SY SZ TD TG TJ TL TM TN TO TR TT TV TZ UG VA VC VE VU WS YE ZM ZW.
- **93 identity cards:** AE AG AZ BB BD BF BI BJ BN BO BT BW BZ CF CG CI CM CU CV DJ DM DZ ER FM GA GD GE GH GM GN GQ GT GW GY HN JM JO KE KG KH KI KM KN KP LA LB LC LR LS LU LY ME MG MK ML MM MN MR MV MW MZ NE NG NI NP OM PA PG PY RW SC SD SL SN SO SR SS ST SY SZ TD TG TJ TL TO TT TZ UG UY VA VC VU YE.

Generic rows also fill the dates before a country's earliest surveyed format, so the counts of rows are higher than these. A document with no surveyed validity uses the generic rule: passports 5 years under 16 and 10 from 16, identity cards 10 years.

## Rebuilding the Data

Raw downloads stay out of the repository, in `raw_data/`. From `scripts/`, with `-X utf8` on Windows:

```
python fetch_raw.py          # keeps files that exist; slow, the Wayback Machine limits its rate
python prepare_schemes.py    # schemes.tsv: run first, the others read it
python prepare_countries.py  # countries.tsv
python prepare_documents.py  # documents.tsv, validity.tsv
python prepare_series.py     # series.tsv
python prepare_lists.py      # name_rules.tsv, test_numbers.tsv
python prepare_places.py     # places.tsv, place_cities.tsv; needs ../city-generator
python check_schemes.py      # checks every table of both tiers
```

Hand-kept inputs are in `scripts/curated/`. `check_schemes.py` checks that:

- patterns parse with `core::pattern`'s grammar, and builders, check rules and safe methods exist;
- worked examples fit their schemes, with check digits recomputed for generic schemes;
- series anchors rise, and place codes fit their schemes and exist in GeoNames;
- each document's formats cover all dates without overlap, and agree with the survey on what is generic;
- optional MRZ data fits its field, and cross-references between tables hold.

It cannot show that a hand mapping is right, so these were also checked once, by script: every populated GeoNames region has a code in each scheme, and each hand-mapped code's label names its region.

`scripts/validate_numbers.py` checks the library's output against implementations it does not share. It draws 2,000 identities per served country at three dates, half of them at any age, plus a safe sample. It checks each number with python-stdnum, or with code written in the script from the national rules where stdnum has no such scheme. python-stdnum is LGPL and used only by this script, from a virtual environment; it is not shipped.

For documents it draws 500 people per country at the same dates, in all 199 countries, and reads every zone back with ICAO 9303 code written in the script: line lengths, each check digit, each field against the result, and the name against forms written by hand from Part 3. It also checks the number against its format, and the dates against the reference date. If the `mrz` package is installed, its check digits are compared too. That package does not read the overflow of a long number, so there the comparison leaves out the number's own digit; the composite digit still covers that field.

The script corrects stdnum in four schemes, and each correction is explained in the script:

- `cn-ric`: stdnum compares a county's first listing with the birth year. Its table starts around 1980, so it rejects most people born earlier, although the code follows the registration, not the birth.
- `id-nik` and `pk-cnic`: its lists leave out North Kalimantan and the Papuan provinces, and Azad Kashmir.
- `kr-rrn`: it requires a check digit on numbers from October 2020, which have none, and it does not reject future dates.

## Known Limitations

**Sources not read.** These pages refuse automated reading, so the rows rest on the other sources the survey gives: dvv.fi (Finland), interior.gob.es (Spain), Riigi Teataja (Estonia), e-seimas (Lithuania) and cpr.dk's PDF on the number. cpr.dk's web pages were read, and give the dates in `dk-cpr`'s `params`; it says its list of 1 January birth dates without a valid check may be incomplete.

**Not available:**

- **RENAPO's own CURP word list.** Anexo 01 of its 2021 rules is omitted from the public PDF, so the 81 words in `name_rules.tsv` are not RENAPO's copy, and the current list may differ. They are django-localflavor's 80 and `VACA`, the one word python-stdnum's list adds, whose other spellings `BACA`, `BAKA` and `VAKA` are on both. A CURP whose first four letters form a listed word takes an `X` as its second letter, as RENAPO's rule says, and `is_valid` rejects the word itself, as both of those libraries do. A layer can add or remove words.
- **Vietnam's Appendix II**, the codes for births abroad. `vn-cccd` has no abroad codes, and safe mode uses an impossible date.
- **Iranian office codes:** no list exists, so `ir-code` has no place mapping.
- **Authority examples.** Spain, the Netherlands, Switzerland, Ireland, Portugal and the JMBG countries have no worked example from their authority. Their examples are from reference works or checked by arithmetic.

**Approximate:**

- **China's county codes** are the current ones, valid at all dates. A code keeps its digits after a county is renamed or merged, so an older person should often hold a code that no longer exists, and may instead get one created after their number was issued. No open table of the codes' dates has been found.
- **Aadhaar's rollout** is not modelled. Everyone alive when it started in 2010 holds a number within 8 days of the start.
- **Slovenia's registers 51 to 59**, used when register 50 runs out for a date, are not drawn. `is_valid` accepts them.
- **Korea's region digits** come from one reference table whose ranges overlap. Each overlap went to one region (41 Daejeon, 44 Sejong, 55-56 and 65-66 Gwangju, 90 Ulsan).
- **Series anchors** other than the SSA's are approximate, as listed under Sources. The AR, CL and PE series are good to a few years at best.
- **City links:** in Bosnia only 34% of the population maps to a city-level code, since GeoNames gives only some cantons. Other people draw from the entity's codes. In the other schemes with city links (`cn-ric`, `fr-nir`, `id-nik`, `it-cf`, `rs-jmbg`, `tw-nid`), at least 99% maps. In Indonesia GeoNames' regency numbers differ from the NIK's in places, so the join uses the city's name first, and small towns may land in a neighbouring regency. Jakarta's cities have no regency in GeoNames and split across its five cities.
- **Weights** count GeoNames' populated places, so a city with many listed neighbourhoods counts them all (Turin exceeds Milan). A commune-level weight is a rough share within its region.
- **Historical codes that share a region** carry the region's full weight in their own era, so a person born before a merger draws evenly between the old and new codes.
- **Overseas France:** INSEE's overseas communes have no French `admin1`, because GeoNames files those territories as separate countries.
- **Romanian sectors 7 and 8** are valid but have no end date, since no source gives one. They have weight 0.
- **The Swedish list** has 43,895 rows holding 42,939 different numbers. Duplicates are dropped.
- **Russian INN:** the codes 91 and 92 are left out.
- **Peru's geographic series** (above) are unmapped.
- **MRZ codes** for document models after the reference date are not all modelled.

**Documents:**

- **Scripts.** Doc 9303 Part 3 has Latin, Cyrillic and Arabic tables and no Greek one. The Arabic table is not implemented. Names in Greek, Arabic and other scripts need a Latin form from the caller.
- **Letters beyond the tables.** The library's own choice for `Ș`, `Ț`, `Ơ`, `Ư` and the Vietnamese letters from U+1EA0 to U+1EF9 is the base letter, and for `Ћ` it is `C` and for `Ѓ` it is `G`. Part 3 does not settle these, so a country's documents may write them differently.
- **Serbian `Г`.** Part 3's table says `H` for Serbian. That looks like an error in the table, since Serbian `Г` is `G` in Serbian's own Latin alphabet, so the library leaves that one rule out. The other Serbian rules are in.
- **A long number in a passport.** Part 4 gives TD3 no overflow for a number over nine characters, so the library uses Part 5's form in the optional field, as Part 5 does for TD1.
- **Very old dates.** A zone is written for any reference date, including years before machine-readable documents existed.
- **Generic formats** are stand-ins, as above.
