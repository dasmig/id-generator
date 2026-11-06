# Usage Guide

This guide covers every feature of the id-generator library: national numbers, and passports and identity cards with their machine-readable zones.

Generated numbers and documents are test data. Realistic numbers are structurally valid and may coincide with a real person's number, because many national number spaces are densely assigned. Never use them to identify anyone, to open accounts or to pass identity checks. The library looks nothing up. Documents are data, not images: it draws no document likeness.

For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating">Generating</a></li>
<li><a href="#safe-numbers">Safe Numbers</a></li>
<li><a href="#documents">Documents</a></li>
<li><a href="#checking-text">Checking Text</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#batch-generation">Batch Generation</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/idgen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::idg::instance();
    std::cout << gen.get_identity(dasmig::identity_request{}) << '\n';
}
```

## Installation

Copy `dasmig/idgen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them together. Supply `resources/` next to the program, or define `DASMIG_ID_EMBED` to compile the `lite` tier in.

## Loading Resources

`instance()` loads the `lite` tier on first use (or `full` if there is no `lite`), from the compiled-in copy or from the first of `resources/`, `../resources`, `id-generator/resources` and `../id-generator/resources` that has it.

```cpp
dasmig::idg gen;                       // an empty, independent instance
bool ok = gen.load(dasmig::dataset::lite); // false when the tier is not found
gen.load(std::filesystem::path{"path/to/resources/lite"}); // a directory
gen.has_data();
gen.countries();        // all 199
gen.number_countries(); // the 63 with a primary national number
gen.schemes("AR");      // {"ar-dni", "ar-cuil"}, primary first
```

Each `load` replaces the data and every layer loaded before.

## Generating

```cpp
dasmig::identity_request request;
request.country = "IT";
request.surname = "Rossi";
request.given_names = "Mario";
request.birth_date = std::chrono::year{1980} / 1 / 1;
request.sex = dasmig::sex::male;
request.birthplace = dasmig::id_place{.country_code = "IT", .geonameid = 3169070U}; // Rome

auto v = gen.get_identity(request);       // draws a seed from the instance engine
auto w = gen.get_identity(request, 1234); // deterministic; does not touch the engine
const auto& n = w.numbers.front();
n.value;      // "RSSMRA80A01H501U"
n.formatted;  // the display form
n.assigned;   // when the person got it
n.place_code; // "H501"
```

Everything in the request is optional, and `identity_request{}` draws a whole person. These pass the results of the other generators as plain values:

| Field | Unset | Notes |
| --- | --- | --- |
| `country` | Drawn by population, or taken from `schemes` when it names keys | ISO alpha-2, any case |
| `birth_date` | Drawn uniformly for ages 18 to 80 | 1900-01-01 up to the reference date |
| `sex` | Drawn 50/50 | |
| `given_names`, `surname`, `second_surname` | Letters drawn in the scheme's shape | UTF-8 as written |
| `birthplace` | The residence if it is in the same country, else drawn | A city (`geonameid`), a region (`admin1_code`) or only a country |
| `residence` | The birthplace if it is in the same country, else drawn | Where the person lived when a number was issued |
| `schemes` | The primary scheme | `{}` asks for none, and `{"*"}` for every scheme of the country |
| `documents` | None | Any of `id_document_kind::passport` and `id_card`, in the order wanted |
| `reference_date` | 2023-07-01 | The date the result is true on, from 1900-01-01 to 2090-12-31 |
| `safe` | `false` | See below |
| `weighted` | `true` | `false` draws countries and places uniformly |

A number the person cannot hold yet is left out, not an error: a child without a National Insurance number, or a date before the scheme began. So is the primary number of a country without one, so batches over ages and countries work.

## Safe Numbers

`request.safe = true` gives numbers nobody holds, by the method `doc/data.md` lists for each scheme, and sets `reserved` on each number:

| Method | Schemes | The number | `is_valid` |
| --- | --- | --- | --- |
| `wrong_check` | Most | The check digit is off by a fixed amount | False |
| `impossible_date` | `dk-cpr`, `kr-rrn`, `vn-cccd` | The encoded birth date is after the reference date | False, given the reference date |
| `reserved_range` | `de-idnr`, `fi-hetu`, `gb-nino`, `no-fodselsnummer`, `us-ssn` | A range real numbers do not use | True |
| `test_numbers` | `is-kennitala`, `se-personnummer` | From the authority's list | True |
| `unissued_series` | `ar-dni` | Above the series at the reference date | True |
| `unassigned_place` | `id-nik`, `my-mykad`, `pk-cnic` | A place code nobody is given | True |

Safe documents are described under [Documents](#documents).

## Documents

```cpp
dasmig::identity_request request;
request.country = "DE";
request.surname = "Müller";
request.given_names = "Jürgen Karl";
request.documents = {dasmig::id_document_kind::passport, dasmig::id_document_kind::id_card};

const auto v = gen.get_identity(request, 11);
const auto& passport = v.documents.front();
passport.number;        // "HL9KM58HC"
passport.format;        // "de-passport-2007", the row in documents.tsv
passport.issuing_state; // "D", the ICAO code
passport.issued;        // on or before the reference date
passport.expires;       // after it; empty when the document does not expire
passport.mrz;           // {"P<D<<MUELLER<<JUERGEN<KARL<<<...", "HL9KM58HC1D<<8911252M2404026<<<...00"}
```

Each document is the one the person holds on the reference date, valid that day. A person who renews on time holds a document issued some time in its validity, so the issue date is drawn from the dates whose document still runs. The format is the one in use on the issue date, and the expiry follows the rule for the age at issue.

- **Not everyone holds one.** A document is left out, not an error, for a country without that document (Denmark and Ireland have no identity card) and for a person under its minimum age. Asking for one document never moves another, or a number.
- **`evidence` and `generic_validity`.** `evidence` says how the number's format is known: `issuer`, `reference`, `specimen`, `placeholder` or `generic`, a nine-digit stand-in for a country whose format was not found. `generic_validity` says the validity is a general rule, not the country's own.
- **The national number on a card.** In 18 countries the card's number is the person's national number (`cn-ric`, `in-aadhaar`, `kr-rrn` and others; `number_scheme` in `doc/data.md`). The card then shows the same number as `numbers`, whether or not you asked for that scheme.

**The zone.** `mrz` holds 2 lines of 44 characters for a passport and 3 lines of 30 for a card, or nothing for a document without a zone. The zone is the only part of a document that needs a name, and it follows ICAO Doc 9303:

- It needs a `surname`. With only a `surname` the name field has one identifier, as for a person with a single name. With only `given_names`, or no names, there is no zone.
- A `second_surname` joins the surname.
- Letters are written by Part 3's tables for Latin and Cyrillic, with the national rules of Belarus, Bulgaria, North Macedonia, Serbia and Ukraine. A name in another script needs a Latin form from you: if any name you give cannot be written, there is no zone rather than a zone with part of the name missing.
- Long names are shortened as Part 4 shows.

**Safe documents.** With `request.safe`, a document's number is changed to fall outside every format of that country and document, and `reserved` is set. Its check digits are still right, so the zone stays valid. A card that shows the national number shows the safe number made by the scheme's method instead. The other fields are as usual. For a `generic` format the real format is unknown, so a number outside the stand-in is no guarantee; see `doc/data.md`.

## Checking Text

```cpp
gen.is_valid("br-cpf", "111.444.777-35"); // true: the electronic or the display form
gen.is_valid("br-cpf", "111.444.777-36"); // false
gen.is_valid("se-personnummer", "196408233234", std::chrono::year{1960} / 1 / 1); // false: not born yet
```

`is_valid` checks structure, check digit and encoded date. It cannot tell whether a number is in use.

## Seeding and Deterministic Generation

- `get_identity(request, seed)` gives the same result for the same request, seed and data, on every platform, without advancing the instance engine.
- `get_identity(request)` draws a fresh seed from the instance engine, initially seeded from system entropy. There are no generator-level `seed()` or `unseed()` controls; for deterministic sequences, keep a `dasmig::core::rng` in the caller and pass its next value as the seed.
- `v.seed()` is the seed a result was generated from: `get_identity(request, v.seed())` replays it with the same request and data.
- A given field skips its draw and moves no other stream. Asking for a second scheme adds a number and leaves the first unchanged.

## Extending Data

The shipped data is a base layer. Files with the same columns as the tables in `resources/` can be loaded on top of it to add rows, change them or remove them, without editing the shipped files. `doc/data.md` lists each table's columns and key.

```cpp
// A directory may hold any of the tables; they are applied together, or none is.
gen.extend("mods/");

// One file, named after its table. Override mode (the default): a row replaces
// the loaded row with its key, and new keys are added.
gen.extend("mods/places.tsv");

// From text, for example supplied by a WebAssembly host.
gen.extend_from_string(dasmig::id_table::places,
                       "scheme\tcode\tfrom\tlabel\tweight\tadmin1\nbr-cpf\t5\t1900-01-02\tNew\t1000\tXY\n",
                       "scenario");

// Remove rows: a layer needs only the key and a disabled column.
gen.extend_from_string(dasmig::id_table::test_numbers, "scheme\tvalue\tdisabled\nis-kennitala\t2811913049\t1\n", "cleanup");

// Add mode refuses to touch keys that are already loaded.
gen.extend("mods/countries.tsv", dasmig::core::layer_mode::add);
```

A new scheme uses the builder `generic` (a pattern and a check rule), `generic_dated` (with tokens such as `{YYMMDD}` and `{S:odd_male}`), or a built-in one. A new country and its primary scheme name each other, so they go in one directory layer.

- Every value in a layer is checked before anything changes. A bad row throws `dasmig::core::tsv_error` with the file, line and column. Tables that no longer fit together throw `std::invalid_argument`. Either way the data stays as it was.
- `layers(table)` lists what is loaded: source, mode, rows added, replaced and removed, and warnings such as unknown columns.
- `load(...)` replaces the base and drops every layer.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto v = gen.get_identity(dasmig::identity_request{});
v.signature().seed; // the call seed
v.signature().data; // gen.data_hash() when it was generated
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume. Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`, for composing larger documents.

## Compiled-in Data

Define `DASMIG_ID_EMBED` before including the header to compile the `lite` tier into the program. `load(dataset::lite)` and `instance()` then use it and need no files. The compiled-in data has the same `data_hash()` as the file, so seeds replay the same either way.

`dasmig/idgen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Thread Safety

Each `idg` instance is independent. Concurrent calls to `get_identity(request)` on the **same** instance require external synchronisation (e.g. a `std::mutex`).

## Batch Generation

Every seeded getter has batch forms: `get_identity_batch` return a `std::vector`, and `for_each_identity` streams the items to a sink in index order without holding the batch.

```cpp
auto& gen = dasmig::idg::instance();
const auto request = dasmig::identity_request{};

// 1,000 results from one batch seed, on four threads.
auto items = gen.get_identity_batch(request, {.count = 1000, .threads = 4}, 42);

// Item 1 replays on its own: the batch seed and the index give its seed.
auto again = gen.get_identity(request, dasmig::core::item_seed(42, 1));

// A million results to a file as JSON Lines, never all in memory.
std::ofstream out{"items.jsonl"};
dasmig::core::jsonl_writer lines{out};
gen.for_each_identity(request, {.count = 1'000'000, .threads = 0}, 42, lines);
```

- **Item seeds.** Item `i` is exactly `get_identity(request, core::item_seed(batch_seed, first + i))`. A range can be generated directly (`first = 1000` starts at item 1000), two batches that follow each other equal one, and the output is the same for any `threads` (0 means the hardware count). Without a `batch_seed` one is drawn like the single getter draws its own.
- **Options.** `core::batch_options` has `count`, `first`, `threads` and `unique`. The request describes one item; the options describe the batch.
- **Uniqueness.** `.unique = core::unique_rule::by<identity>(key)` regenerates an item whose key already appeared in the batch, with a deterministic retry seed, and throws `std::runtime_error` naming the item when the attempts run out.
- **Sinks.** `core::jsonl_writer` writes one `to_json()` per line, `core::csv_writer` flattens each result's JSON into dotted columns. Any callable `(std::uint64_t index, identity&)` works.
- Batch calls use the same const getters from several threads, which the thread-safety rules above allow; the CI runs the batch tests under ThreadSanitizer.

## Error Reference

| Exception | When |
|-----------|------|
| `std::runtime_error` | Any generation call before data is loaded |
| `std::invalid_argument` | An unknown country or scheme; a scheme of another country; a birth date after the reference date or before 1900; an invalid reference date; a birthplace abroad that the scheme has no code for; a layer that leaves the tables inconsistent |
| `dasmig::core::tsv_error` | A malformed table or layer (message gives file, line and column); the data is unchanged |
