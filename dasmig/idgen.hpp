// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Diego Dasso Migotto

#ifndef DASMIG_IDGEN_HPP
#define DASMIG_IDGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef DASMIG_ID_EMBED
#include "idgen_data.hpp"
#endif

/// @file idgen.hpp
/// @brief ID Generator library — identity numbers and documents that fit a person and a date for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.
///
/// Generated numbers are test data. Realistic numbers are structurally valid
/// and may coincide with a real person's number, because many national number
/// spaces are densely assigned. Never use them to identify anyone, to open
/// accounts or to pass identity checks. The library looks nothing up.

DASMIG_CORE_REQUIRE(1, 4);

namespace dasmig
{

// Members have {} initializers so designated initializers may leave them
// out without -Wmissing-designated-field-initializers warnings.
// NOLINTBEGIN(readability-redundant-member-init)

/// @brief The documents a request can ask for.
enum class id_document_kind : std::uint8_t
{
    passport, ///< A passport; every country has one.
    id_card   ///< A national identity card, where the country issues one.
};

/// @brief A place as city-generator gives it; every field is optional except
///        the country.
struct id_place
{
    /// ISO alpha-2 code, any case. A birthplace abroad gives the codes for
    /// births abroad (Italy's `Z...`, France's `99...`).
    std::string country_code{};
    /// GeoNames admin1 code, city-generator's `city::admin1_code`.
    std::string admin1_code{};
    /// GeoNames id, city-generator's `city::geonameid`, for city-level codes.
    std::optional<std::uint32_t> geonameid{};
};

/// @brief What an identity call asks for.
///
/// Every generation call takes one; `identity_request{}` asks for the
/// defaults. Choices are never stored on the generator, so one call cannot
/// change the next.
struct identity_request
{
    /// The reference date when none is given.
    static constexpr std::chrono::year_month_day default_reference_date{
        std::chrono::year{2023}, std::chrono::month{7}, std::chrono::day{1}};

    /// ISO alpha-2, any case. Unset: drawn by population among the countries
    /// that can serve the request, or taken from `schemes` when it names keys.
    std::optional<std::string> country{};
    /// From birth-generator, 1900-01-01 up to the reference date. Unset: drawn
    /// uniformly for ages 18 to 80.
    std::optional<std::chrono::year_month_day> birth_date{};
    /// Unset: drawn 50/50.
    std::optional<dasmig::sex> sex{};
    /// UTF-8 as written: "María José". Unset: name-encoded letters are drawn in
    /// the scheme's shape.
    std::optional<std::string> given_names{};
    /// UTF-8 as written: "García".
    std::optional<std::string> surname{};
    /// Mexico's CURP uses it.
    std::optional<std::string> second_surname{};
    /// Where the person was born. Unset: the residence when it is in the same
    /// country, else drawn by population.
    std::optional<id_place> birthplace{};
    /// Where the person lived when a number was issued. Unset: the birthplace.
    std::optional<id_place> residence{};
    /// Scheme keys ("br-cpf"). Unset: the primary scheme; empty: none;
    /// containing "*": every scheme of the country.
    std::optional<std::vector<std::string>> schemes{};
    /// The documents to generate. Empty: none. A document the person cannot
    /// hold is left out.
    std::vector<id_document_kind> documents{};
    /// The date the result is true on, from 1900-01-01 to 2090-12-31.
    std::chrono::year_month_day reference_date{default_reference_date};
    /// Numbers and documents nobody holds, by the methods in doc/data.md.
    bool safe{false};
    /// Draw countries and places by population; false draws them uniformly.
    bool weighted{true};
};

/// @brief One national number held by a person.
struct id_number
{
    std::string scheme{};       ///< Scheme key, "br-cpf".
    std::string label{};        ///< English name of the number.
    std::string native_label{}; ///< The number's name in its country's language.
    std::string value{};        ///< Electronic form, "11144477735".
    std::string formatted{};    ///< Display form, "111.444.777-35".
    std::chrono::year_month_day assigned{}; ///< When the person got it.
    std::string place_code{};   ///< The encoded place; empty when none.
    bool reserved{false};       ///< Made by the scheme's safe method.
};

/// @brief One document held by a person on the reference date.
struct id_document
{
    id_document_kind kind{id_document_kind::passport}; ///< Passport or identity card.
    std::string format{};        ///< `documents.tsv` key, "de-passport-2007".
    std::string number{};        ///< As printed, "C01X00T47".
    std::string issuing_state{}; ///< ICAO code, "D", "FRA", "GBR".
    std::chrono::year_month_day issued{}; ///< On or before the reference date.
    /// After the reference date. Unset: the document does not expire.
    std::optional<std::chrono::year_month_day> expires{};
    /// 2 lines of 44 (TD3) or 3 of 30 (TD1). Empty when the document has no
    /// machine-readable zone or the request gives no surname in a script the
    /// zone can write.
    std::vector<std::string> mrz{};
    /// The number format's source: "issuer", "reference", "specimen",
    /// "placeholder" or "generic" (nothing surveyed; the number is invented).
    std::string evidence{};
    bool generic_validity{false}; ///< No surveyed validity: the generic rule applied.
    bool reserved{false};         ///< Outside the country's formats, for safe mode.
};

// NOLINTEND(readability-redundant-member-init)

/// @brief The tables of the library; see doc/data.md.
enum class id_table : std::uint8_t
{
    countries,    ///< `countries.tsv`: population, ICAO code, primary scheme.
    schemes,      ///< `schemes.tsv`: how each national number is built.
    places,       ///< `places.tsv`: place codes of each scheme.
    place_cities, ///< `place_cities.tsv`: GeoNames cities to place codes.
    series,       ///< `series.tsv`: anchors of sequential numbers.
    documents,    ///< `documents.tsv`: document number formats.
    validity,     ///< `validity.tsv`: document validity by age at issue.
    name_rules,   ///< `name_rules.tsv`: word lists of name-encoding schemes.
    test_numbers  ///< `test_numbers.tsv`: published test numbers.
};

/// @cond INTERNAL
namespace idgen_detail
{

using day_t = std::int32_t;
inline constexpr day_t no_start = std::numeric_limits<day_t>::min();
inline constexpr day_t no_end = std::numeric_limits<day_t>::max();
inline constexpr std::int64_t seconds_per_day = 86400;

// -- Dates -----------------------------------------------------------------

struct civil
{
    int y{1900};
    int m{1};
    int d{1};
};

[[nodiscard]] inline bool real_date(int y, int m, int d)
{
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    return std::chrono::year_month_day{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)},
                                       std::chrono::day{static_cast<unsigned>(d)}}
        .ok();
}

[[nodiscard]] inline day_t to_day(const std::chrono::year_month_day& d)
{
    return static_cast<day_t>(std::chrono::sys_days{d}.time_since_epoch().count());
}

[[nodiscard]] inline day_t to_day(const civil& c)
{
    return to_day(std::chrono::year_month_day{std::chrono::year{c.y}, std::chrono::month{static_cast<unsigned>(c.m)},
                                              std::chrono::day{static_cast<unsigned>(c.d)}});
}

[[nodiscard]] inline std::chrono::year_month_day ymd_of(day_t d)
{
    return std::chrono::year_month_day{std::chrono::sys_days{std::chrono::days{d}}};
}

[[nodiscard]] inline civil civil_of(day_t d)
{
    const auto v = ymd_of(d);
    return {.y = static_cast<int>(v.year()), .m = static_cast<int>(static_cast<unsigned>(v.month())),
            .d = static_cast<int>(static_cast<unsigned>(v.day()))};
}

[[nodiscard]] inline std::optional<day_t> parse_date(std::string_view t)
{
    if (t.size() != 10 || t[4] != '-' || t[7] != '-') return std::nullopt;
    const auto number = [&](std::size_t at, std::size_t len) -> int {
        int v = 0;
        for (std::size_t i = at; i < at + len; ++i)
        {
            if (t[i] < '0' || t[i] > '9') return -1;
            v = (v * 10) + (t[i] - '0');
        }
        return v;
    };
    const int y = number(0, 4);
    const int m = number(5, 2);
    const int d = number(8, 2);
    if (y < 0 || !real_date(y, m, d)) return std::nullopt;
    return to_day(civil{.y = y, .m = m, .d = d});
}

// -- Text -------------------------------------------------------------------

inline constexpr std::string_view decimal = "0123456789";

[[nodiscard]] inline std::string num(std::uint64_t v, std::size_t width)
{
    std::string s(width, '0');
    for (std::size_t i = width; i > 0; --i)
    {
        s[i - 1] = static_cast<char>('0' + (v % 10U));
        v /= 10U;
    }
    return s;
}

[[nodiscard]] inline std::string iso_date(day_t day)
{
    const civil c = civil_of(day);
    return num(static_cast<std::uint64_t>(c.y), 4) + "-" + num(static_cast<std::uint64_t>(c.m), 2) + "-" +
           num(static_cast<std::uint64_t>(c.d), 2);
}

[[nodiscard]] inline bool only_digits(std::string_view t)
{
    return !t.empty() && std::ranges::all_of(t, [](char c) { return c >= '0' && c <= '9'; });
}

// The number written at t[at, at + len); the caller has checked the digits.
[[nodiscard]] inline int at(std::string_view t, std::size_t pos, std::size_t len)
{
    int v = 0;
    for (std::size_t i = pos; i < pos + len; ++i) v = (v * 10) + (t[i] - '0');
    return v;
}

[[nodiscard]] inline std::uint64_t at64(std::string_view t)
{
    std::uint64_t v = 0;
    for (const char c : t) v = (v * 10U) + static_cast<std::uint64_t>(c - '0');
    return v;
}

[[nodiscard]] inline bool all_same(std::string_view t)
{
    return std::ranges::all_of(t, [&](char c) { return c == t.front(); });
}

[[nodiscard]] inline bool is_palindrome(std::string_view t)
{
    return std::ranges::equal(t.substr(0, t.size() / 2), t.substr(t.size() - (t.size() / 2)) | std::views::reverse);
}

[[nodiscard]] inline std::string upper(std::string_view t)
{
    std::string s{t};
    for (char& c : s)
    {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

// -- Series -----------------------------------------------------------------

struct anchor
{
    std::int64_t t{0}; // seconds since the epoch
    std::int64_t value{0};
};

// The value at time t. Before the first anchor: its value. After the last: the
// last two anchors' rate carried on. Anchors one day apart are a jump, not a
// slope (the Argentine DNI went from 59,999,999 to 70,000,000 overnight).
[[nodiscard]] inline std::int64_t series_at(const std::vector<anchor>& a, std::int64_t t)
{
    if (t <= a.front().t || a.size() == 1) return a.front().value;
    auto hi = std::ranges::upper_bound(a, t, {}, &anchor::t);
    if (hi == a.end()) --hi;
    const auto lo = hi - 1;
    if (hi->t - lo->t <= seconds_per_day) return t < hi->t ? lo->value : hi->value;
    return lo->value + ((hi->value - lo->value) * (t - lo->t) / (hi->t - lo->t));
}

// -- Check rules --------------------------------------------------------------

struct check_rule
{
    enum class kind : std::uint8_t { none, luhn, mod97_10, mod11_2, mod11_10, verhoeff, gtin, weights };
    enum class reduce : std::uint8_t { r, r_10_0, r_10_1, r_mod_10, ten_minus_r, eleven_minus_r, eleven_minus_r_10_0, eleven_minus_r_10_x };
    kind type{kind::none};
    reduce rule{reduce::r};
    std::vector<int> weights;
};

// The check characters of a body; empty when this body has none (the number is
// never issued, so a generator redraws). Throws for a body the rule cannot read.
[[nodiscard]] inline std::string compute_check(const check_rule& c, std::string_view body)
{
    using k = check_rule::kind;
    using red = check_rule::reduce;
    switch (c.type)
    {
    case k::none: return {};
    case k::luhn: return {core::luhn_digit(body)};
    case k::mod11_2: return {core::mod11_2_digit(body)};
    case k::mod11_10: return {core::mod11_10_digit(body)};
    case k::verhoeff: return {core::verhoeff_digit(body)};
    case k::gtin: return {core::gtin_digit(body)};
    case k::mod97_10:
    {
        const auto d = core::mod97_10_digits(body);
        return {d[0], d[1]};
    }
    case k::weights: break;
    }
    std::int64_t sum = 0;
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        sum += static_cast<std::int64_t>(core::alnum_value(body[i])) * c.weights.at(i % c.weights.size());
    }
    const auto mod = [&](std::int64_t m) { return ((sum % m) + m) % m; };
    const auto digit = [](std::int64_t v) { return std::string(1, static_cast<char>('0' + v)); };
    const std::int64_t r = mod(11);
    switch (c.rule)
    {
    case red::r: return r == 10 ? std::string{} : digit(r);
    case red::r_10_0: return digit(r == 10 ? 0 : r);
    case red::r_10_1: return digit(r == 10 ? 1 : r);
    case red::r_mod_10: return digit(r % 10);
    case red::ten_minus_r: return digit((10 - mod(10)) % 10);
    case red::eleven_minus_r: return r == 1 ? std::string{} : digit(r == 0 ? 0 : 11 - r);
    case red::eleven_minus_r_10_0: return digit(r <= 1 ? 0 : 11 - r);
    case red::eleven_minus_r_10_x: return r == 1 ? std::string{"X"} : digit(r == 0 ? 0 : 11 - r);
    }
    return {};
}

// -- Scheme definitions -------------------------------------------------------

enum class safe_method : std::uint8_t { test_numbers, reserved_range, unissued_series, unassigned_place, wrong_check, impossible_date };
enum class place_from : std::uint8_t { none, birthplace, residence };

struct assigned_rule
{
    int age_lo{-1}; // -1: at birth
    int age_hi{-1};
    int lag{0}; // days, drawn from 0 to lag
    day_t birth_from{no_end}; // people born from this date get it at birth
};

struct place_row
{
    std::string code;
    double weight{0.0};
    day_t from{no_start};
    day_t until{no_end};
    std::string admin1; // ';'-separated
    std::string country; // alpha-2, or '*'
    bool reserved{false};

    [[nodiscard]] bool valid_on(day_t d) const { return from <= d && d < until; }
};

struct place_era
{
    day_t start{no_start};
    core::weighted_table<std::uint32_t> table;
};

// A scheme's place codes, with the indices draws and lookups need.
struct place_book
{
    std::vector<place_row> rows;
    std::vector<place_era> eras; // ascending start; rows with weight valid in the era
    core::weighted_table<std::uint32_t> any; // every row with weight
    std::vector<std::uint32_t> reserved;
    std::vector<std::uint32_t> by_code; // row indices sorted by code
    std::unordered_map<std::uint32_t, std::uint32_t> cities; // geonameid -> a row with the code
    std::unordered_map<std::string, std::vector<std::uint32_t>> by_admin1;

    [[nodiscard]] std::pair<std::size_t, std::size_t> code_range(std::string_view code) const
    {
        const auto r = std::ranges::equal_range(by_code, code, {}, [&](std::uint32_t i) { return std::string_view{rows[i].code}; });
        return {static_cast<std::size_t>(r.begin() - by_code.begin()), static_cast<std::size_t>(r.end() - by_code.begin())};
    }

    [[nodiscard]] const core::weighted_table<std::uint32_t>& table_on(day_t d) const
    {
        const auto hi = std::ranges::upper_bound(eras, d, {}, &place_era::start);
        if (hi != eras.begin())
        {
            const auto& era = *(hi - 1);
            if (era.table.total_weight() > 0.0) return era.table;
        }
        return any;
    }
};

struct name_rule_set
{
    std::vector<std::string> particles;
    std::vector<std::string> given_skip;
    std::vector<std::pair<std::string, std::string>> chars;
    std::vector<std::pair<std::string, std::string>> words;
};

struct param
{
    std::string name;
    std::string text;
    day_t day{no_end}; // the value as a date, when it is one
};

struct dated_segment
{
    enum class kind : std::uint8_t { body, yymmdd, ddmmyy, yyyy, sex_odd_male, sex_even_male, century };
    kind type{kind::body};
    core::pattern body;
    std::string table; // century: "19=1,20=2"

    [[nodiscard]] std::size_t width() const
    {
        switch (type)
        {
        case kind::body: return body.lengths().first;
        case kind::yymmdd:
        case kind::ddmmyy: return 6;
        case kind::yyyy: return 4;
        default: return 1;
        }
    }
};

struct build_ctx;
struct valid_ctx;
using build_fn = bool (*)(build_ctx&, std::string&);
using valid_fn = bool (*)(const valid_ctx&, std::string_view);

struct scheme
{
    std::string key;
    std::string country;
    std::string label;
    std::string native_label;
    std::string stream;
    std::string builder;
    build_fn build{nullptr};
    valid_fn valid{nullptr};
    std::string display;
    assigned_rule assigned;
    safe_method safe{safe_method::wrong_check};
    core::pattern pattern;
    std::optional<core::pattern> safe_pattern;
    check_rule check;
    std::vector<dated_segment> dated;
    place_from place_source{place_from::none};
    day_t from{no_start};
    day_t until{no_end};
    std::vector<param> params;
    place_book places;
    std::unordered_map<std::string, std::vector<anchor>> series;
    std::vector<std::string> test_numbers; // sorted
    name_rule_set names;
    const scheme* companion{nullptr}; // ar-cuil's ar-dni

    [[nodiscard]] const param* find_param(std::string_view name) const
    {
        for (const auto& p : params)
        {
            if (p.name == name) return &p;
        }
        return nullptr;
    }
    [[nodiscard]] day_t param_day(std::string_view name) const
    {
        const param* p = find_param(name);
        return p == nullptr ? no_end : p->day;
    }
    [[nodiscard]] std::string_view param_text(std::string_view name) const
    {
        const param* p = find_param(name);
        return p == nullptr ? std::string_view{} : std::string_view{p->text};
    }
    [[nodiscard]] std::uint64_t param_number(std::string_view name) const
    {
        return at64(param_text(name));
    }
    [[nodiscard]] const std::vector<anchor>* find_series(const std::string& segment) const
    {
        const auto it = series.find(segment);
        return it == series.end() ? nullptr : &it->second;
    }
};

// When a person got a number. Draws from the scheme's stream, first thing.
[[nodiscard]] inline day_t assigned_day(const scheme& s, day_t birth, core::rng& r)
{
    const auto& rule = s.assigned;
    day_t day = birth;
    if (rule.age_lo >= 0 && birth < rule.birth_from)
    {
        day += core::uniform_int(r, rule.age_lo * 1461 / 4, ((rule.age_hi + 1) * 1461 / 4) - 1);
    }
    day = std::max(day, s.from);
    if (rule.lag > 0) day += core::uniform_int(r, 0, rule.lag);
    return day;
}

// What a builder needs. `date` is the date the number carries: the birth date,
// or a later one under the impossible_date safe method.
struct build_ctx
{
    const scheme& s;
    civil date;
    day_t birth_day;
    sex bio_sex;
    day_t assigned;
    day_t reference;
    std::string place; // the code; a builder clears or changes it to what the number encodes
    const std::string* given;
    const std::string* surname;
    const std::string* second;
    core::rng& r;
    const core::rng& call;
    bool safe;
    bool wrong;
    bool reserved_range;
    bool impossible;

    [[nodiscard]] bool male() const { return bio_sex == sex::male; }
};

struct valid_ctx
{
    const scheme& s;
    std::optional<day_t> reference;

    // A real date, not after the reference date when one is given.
    [[nodiscard]] bool date(int y, int m, int d) const
    {
        if (!real_date(y, m, d)) return false;
        return !reference || to_day(civil{.y = y, .m = m, .d = d}) <= *reference;
    }
    // A year without a century: real in the 1900s or the 2000s.
    [[nodiscard]] bool date_2d(int yy, int m, int d) const { return date(1900 + yy, m, d) || date(2000 + yy, m, d); }
};

// -- Builder helpers -----------------------------------------------------------

// Two draws never share an expression: the order of operands is unspecified, so
// the same seed would give different numbers on different toolchains.
[[nodiscard]] inline std::string rand_digits(core::rng& r, std::size_t n)
{
    std::string s(n, '0');
    for (char& c : s) c = static_cast<char>('0' + core::bounded(r, 10));
    return s;
}

[[nodiscard]] inline std::string rand_range(core::rng& r, std::uint64_t lo, std::uint64_t hi, std::size_t width)
{
    return num(core::uniform_int(r, lo, hi), width);
}

[[nodiscard]] inline char parity_digit(core::rng& r, bool odd)
{
    return static_cast<char>('0' + (core::bounded(r, 5) * 2U) + (odd ? 1U : 0U));
}

[[nodiscard]] inline std::string two(int v) { return num(static_cast<std::uint64_t>(v % 100), 2); }

// The next character of an alphabet, wrapping: the fixed amount a wrong check is off by.
[[nodiscard]] inline char shifted(char c, std::string_view alphabet)
{
    return alphabet[(alphabet.find(c) + 1U) % alphabet.size()];
}

inline void put_check(const build_ctx& b, std::string& out, char c, std::string_view alphabet = decimal)
{
    out += b.wrong ? shifted(c, alphabet) : c;
}

template <std::size_t N>
[[nodiscard]] inline unsigned sum_mod(std::string_view body, const std::array<unsigned, N>& w, unsigned m)
{
    return static_cast<unsigned>(core::weighted_sum(body, w) % m);
}

[[nodiscard]] inline char dig(unsigned v) { return static_cast<char>('0' + v); }

// A century digit shared by the Baltic, Kazakh and Romanian numbers, odd for men.
[[nodiscard]] inline char century_sex_digit(const build_ctx& b, char base_1900)
{
    const int offset = (b.date.y - 1900) / 100; // -1, 0, 1, ...
    return static_cast<char>(base_1900 + (offset * 2) + (b.male() ? 0 : 1));
}

// -- Generic builders ----------------------------------------------------------

[[nodiscard]] inline std::string draw_dated(build_ctx& b)
{
    using kind = dated_segment::kind;
    std::string out;
    for (const auto& seg : b.s.dated)
    {
        switch (seg.type)
        {
        case kind::body: out += seg.body.draw(b.r); break;
        case kind::yymmdd: out += two(b.date.y) + two(b.date.m) + two(b.date.d); break;
        case kind::ddmmyy: out += two(b.date.d) + two(b.date.m) + two(b.date.y); break;
        case kind::yyyy: out += num(static_cast<std::uint64_t>(b.date.y), 4); break;
        case kind::sex_odd_male: out += parity_digit(b.r, b.male()); break;
        case kind::sex_even_male: out += parity_digit(b.r, !b.male()); break;
        case kind::century:
        {
            const std::string century = two(b.date.y / 100) + "=";
            const auto pos = seg.table.find(century);
            if (pos == std::string::npos) throw std::runtime_error("scheme " + b.s.key + ": no century code for " + century);
            out += seg.table.at(pos + century.size());
            break;
        }
        }
    }
    return out;
}

inline bool build_generic(build_ctx& b, std::string& out)
{
    std::string body;
    if (b.reserved_range && b.s.safe_pattern) body = b.s.safe_pattern->draw(b.r);
    else if (!b.s.dated.empty()) body = draw_dated(b);
    else body = b.s.pattern.draw(b.r);
    std::string check = compute_check(b.s.check, body);
    if (check.empty()) return false;
    if (b.wrong)
    {
        const std::string_view alphabet = check.back() == 'X' || b.s.check.type == check_rule::kind::mod11_2 ||
                                                  b.s.check.rule == check_rule::reduce::eleven_minus_r_10_x
                                              ? std::string_view{"0123456789X"}
                                              : decimal;
        check.back() = shifted(check.back(), alphabet);
    }
    out = body + check;
    return b.wrong || b.s.find_param("no_palindrome") == nullptr || !is_palindrome(out);
}

[[nodiscard]] inline bool dated_body_valid(const valid_ctx& v, std::string_view body)
{
    using kind = dated_segment::kind;
    std::size_t pos = 0;
    for (const auto& seg : v.s.dated)
    {
        const std::size_t w = seg.width();
        if (pos + w > body.size()) return false;
        const std::string_view part = body.substr(pos, w);
        pos += w;
        switch (seg.type)
        {
        case kind::body:
            if (!seg.body.matches(part)) return false;
            break;
        case kind::yymmdd:
            if (!only_digits(part) || !v.date_2d(at(part, 0, 2), at(part, 2, 2), at(part, 4, 2))) return false;
            break;
        case kind::ddmmyy:
            if (!only_digits(part) || !v.date_2d(at(part, 4, 2), at(part, 2, 2), at(part, 0, 2))) return false;
            break;
        case kind::yyyy:
            if (!only_digits(part) || at(part, 0, 4) < 1800) return false;
            if (v.reference && at(part, 0, 4) > civil_of(*v.reference).y) return false;
            break;
        case kind::sex_odd_male:
        case kind::sex_even_male:
            if (!only_digits(part)) return false;
            break;
        case kind::century:
            if (seg.table.find("=" + std::string{part}) == std::string::npos) return false;
            break;
        }
    }
    return pos == body.size();
}

inline bool valid_generic(const valid_ctx& v, std::string_view t)
{
    const std::size_t width = v.s.check.type == check_rule::kind::mod97_10 ? 2 : 1;
    if (t.size() <= width) return false;
    const std::string_view body = t.substr(0, t.size() - width);
    bool shaped = v.s.dated.empty() ? v.s.pattern.matches(body) : dated_body_valid(v, body);
    if (!shaped && v.s.safe_pattern) shaped = v.s.safe_pattern->matches(body);
    if (!shaped) return false;
    if (v.s.find_param("no_palindrome") != nullptr && is_palindrome(t)) return false;
    try
    {
        return compute_check(v.s.check, body) == t.substr(t.size() - width);
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
}

// -- Names ----------------------------------------------------------------------

// The code point at name[i], moving i past it; empty for malformed UTF-8.
[[nodiscard]] inline std::optional<std::uint32_t> next_code_point(std::string_view name, std::size_t& i)
{
    const auto b0 = static_cast<std::uint8_t>(name[i]);
    std::size_t len = 1;
    std::uint32_t cp = b0;
    if ((b0 & 0x80U) == 0U) { len = 1; }
    else if ((b0 & 0xE0U) == 0xC0U) { len = 2; cp = b0 & 0x1FU; }
    else if ((b0 & 0xF0U) == 0xE0U) { len = 3; cp = b0 & 0x0FU; }
    else if ((b0 & 0xF8U) == 0xF0U) { len = 4; cp = b0 & 0x07U; }
    else return std::nullopt;
    if (i + len > name.size()) return std::nullopt;
    for (std::size_t k = 1; k < len; ++k) cp = (cp << 6U) | (static_cast<std::uint8_t>(name[i + k]) & 0x3FU);
    i += len;
    return cp;
}

// One Latin letter from the Latin-1 Supplement or Latin Extended-A as its
// ASCII base, upper case; empty for anything else.
[[nodiscard]] inline std::string_view fold_latin(std::uint32_t cp)
{
    // U+00C0-U+00FF; '?' is not a letter, and the two-letter ones follow below.
    static constexpr std::string_view latin1 =
        "AAAAAA?CEEEEIIIIDNOOOOO?OUUUUY??"
        "AAAAAA?CEEEEIIIIDNOOOOO?OUUUUY?Y";
    // U+0100-U+017F, range by range.
    static constexpr std::string_view extended =
        "AAAAAACCCCCCCCDDDDEEEEEEEEEEGGGGGGGGHHHHIIIIIIIIIIIIJJKKKLLLLLLLLLLNNNNNNNNN"
        "OOOOOO??RRRRRRSSSSSSSSTTTTTTUUUUUUUUUUUUWWYYYZZZZZZS";
    static_assert(latin1.size() == 64 && extended.size() == 128);
    switch (cp)
    {
    case 0xC6U: case 0xE6U: return "AE";
    case 0xDEU: case 0xFEU: return "TH";
    case 0xDFU: return "SS";
    case 0x152U: case 0x153U: return "OE";
    default: break;
    }
    if (cp >= 0xC0U && cp <= 0xFFU)
    {
        const std::string_view c = latin1.substr(cp - 0xC0U, 1);
        return c == "?" ? std::string_view{} : c;
    }
    if (cp >= 0x100U && cp <= 0x17FU) return extended.substr(cp - 0x100U, 1);
    return {};
}

// The words of a name in upper-case ASCII: letters folded, other ASCII kept as
// written, spaces splitting. A non-ASCII character with a rule takes the rule's
// replacement. Empty when a character has no Latin form.
[[nodiscard]] inline std::vector<std::string> latin_words(std::string_view name,
                                                          const std::vector<std::pair<std::string, std::string>>& rules)
{
    std::vector<std::string> words(1);
    std::size_t i = 0;
    while (i < name.size())
    {
        const auto b0 = static_cast<std::uint8_t>(name[i]);
        if (b0 < 0x80U)
        {
            const char c = name[i++];
            if (c == ' ' || c == '\t')
            {
                if (!words.back().empty()) words.emplace_back();
            }
            else
            {
                words.back() += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
            }
            continue;
        }
        const auto decoded = next_code_point(name, i);
        if (!decoded) return {};
        const std::uint32_t cp = *decoded;
        // The upper-case Latin-1 form is 0x20 below the lower-case one, except U+00F7 and U+00FF.
        const std::uint32_t upper_cp = cp >= 0xE0U && cp <= 0xFEU && cp != 0xF7U ? cp - 0x20U : cp;
        const std::string as_written{static_cast<char>(0xC0U | (upper_cp >> 6U)), static_cast<char>(0x80U | (upper_cp & 0x3FU))};
        bool replaced = false;
        for (const auto& [from, to] : rules)
        {
            if (upper_cp < 0x800U && from == as_written)
            {
                words.back() += to;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            const std::string_view folded = fold_latin(cp);
            if (folded.empty()) return {};
            words.back() += folded;
        }
    }
    if (words.back().empty()) words.pop_back();
    return words;
}

[[nodiscard]] inline bool is_vowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }
[[nodiscard]] inline bool is_letter(char c) { return c >= 'A' && c <= 'Z'; }

inline constexpr std::string_view consonants = "BCDFGHJKLMNPQRSTVWXYZ";
inline constexpr std::string_view vowels = "AEIOU";
inline constexpr std::string_view letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

[[nodiscard]] inline char pick_char(core::rng& r, std::string_view from) { return from[core::bounded(r, from.size())]; }

[[nodiscard]] inline std::string it_name_code(const std::string* name, bool given, core::rng& r)
{
    std::string cons;
    std::string vow;
    if (name != nullptr)
    {
        for (const auto& w : latin_words(*name, {}))
        {
            for (const char c : w)
            {
                if (!is_letter(c)) continue;
                (is_vowel(c) ? vow : cons) += c;
            }
        }
    }
    if (cons.empty() && vow.empty())
    {
        // Missing or not Latin: three consonants, the design's drawn shape.
        return {pick_char(r, consonants), pick_char(r, consonants), pick_char(r, consonants)};
    }
    if (given && cons.size() >= 4) cons = {cons[0], cons[2], cons[3]};
    return (cons + vow + "XXX").substr(0, 3);
}

// -- Built-in builders and validators -------------------------------------------
//
// Each builder writes the electronic form and returns false to be called again
// (a body the check rule has no digit for is never issued). Each validator reads
// the electronic form written from the same rules. Algorithms are from the
// number survey (designs/id-generator-numbers.tsv), one row per scheme.

// ---- Argentina ----

[[nodiscard]] inline std::pair<std::uint64_t, std::uint64_t> ar_block(const scheme& s)
{
    const std::string_view text = s.param_text("reserved_block");
    const auto dash = text.find('-');
    return {at64(text.substr(0, dash)), at64(text.substr(dash + 1))};
}

[[nodiscard]] inline std::uint64_t ar_dni_number(const scheme& s, day_t birth_day, day_t reference, bool safe, core::rng& r)
{
    const auto* a = s.find_series("-");
    if (a == nullptr) throw std::runtime_error("scheme " + s.key + ": no series");
    if (safe)
    {
        // Above the series issued at the reference date, below the separate
        // numbering foreign residents get, outside the block reserved for CUILs.
        const auto [block_lo, block_hi] = ar_block(s);
        const auto top = static_cast<std::uint64_t>(series_at(*a, reference * seconds_per_day));
        const std::uint64_t hi = s.param_number("safe_below") - 1;
        // A far-future reference date can run the series past the safe band.
        const std::uint64_t lo = std::min((top >= block_lo && top <= block_hi ? block_hi : top) + 1, hi - 1000000U);
        for (;;)
        {
            const std::uint64_t v = core::uniform_int(r, lo, hi);
            if (v < block_lo || v > block_hi) return v;
        }
    }
    const std::int64_t noise = core::uniform_int<std::int64_t>(r, -7 * seconds_per_day, 7 * seconds_per_day);
    // Seven digits at least: the series' first anchor says nothing about the first numbers.
    return static_cast<std::uint64_t>(std::clamp<std::int64_t>(series_at(*a, (birth_day * seconds_per_day) + noise), 1000000, 99999999));
}

inline bool build_ar_dni(build_ctx& b, std::string& out)
{
    out = std::to_string(ar_dni_number(b.s, b.birth_day, b.reference, b.safe, b.r));
    return true;
}

inline bool valid_ar_dni(const valid_ctx& v, std::string_view t)
{
    if ((t.size() != 7 && t.size() != 8) || !only_digits(t) || t.front() == '0') return false;
    const auto [lo, hi] = ar_block(v.s);
    return at64(t) < lo || at64(t) > hi;
}

inline bool build_ar_cuil(build_ctx& b, std::string& out)
{
    static constexpr std::array<unsigned, 10> w{5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
    std::string dni;
    if (b.s.companion != nullptr)
    {
        // The stream the person's ar-dni draws from, so both show one number.
        core::rng dr = b.call.split(b.s.companion->stream);
        (void)assigned_day(*b.s.companion, b.birth_day, dr);
        dni = num(ar_dni_number(*b.s.companion, b.birth_day, b.reference, b.safe, dr), 8);
    }
    else
    {
        dni = rand_digits(b.r, 8);
    }
    const bool male_prefix = b.assigned >= b.s.param_day("random_prefix_from") ? core::bernoulli(b.r, 0.5) : b.male();
    std::string body = std::string{male_prefix ? "20" : "27"} + dni;
    unsigned rem = sum_mod(body, w, 11);
    if (rem == 1)
    {
        body[1] = '3'; // the prefix becomes 23 when the check would be 10
        rem = sum_mod(body, w, 11);
    }
    out = body;
    put_check(b, out, dig(rem == 0 ? 0 : 11 - rem));
    return true;
}

inline bool valid_ar_cuil(const valid_ctx& v, std::string_view t)
{
    static constexpr std::array<unsigned, 10> w{5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
    if (t.size() != 11 || !only_digits(t) || !v.s.pattern.matches(t)) return false;
    const unsigned rem = sum_mod(t.substr(0, 10), w, 11);
    return rem != 1 && t[10] == dig(rem == 0 ? 0 : 11 - rem);
}

// ---- Austria ----

inline bool build_at_svnr(build_ctx& b, std::string& out)
{
    static constexpr std::array<unsigned, 9> w{3, 7, 9, 5, 8, 4, 2, 1, 6};
    const std::string lll = rand_range(b.r, 100, 999, 3);
    const std::string date = two(b.date.d) + two(b.date.m) + two(b.date.y);
    const unsigned rem = sum_mod(lll + date, w, 11);
    if (rem == 10) return false; // the Laufnummer is skipped
    out = lll;
    put_check(b, out, dig(rem));
    out += date;
    return true;
}

inline bool valid_at_svnr(const valid_ctx& v, std::string_view t)
{
    static constexpr std::array<unsigned, 9> w{3, 7, 9, 5, 8, 4, 2, 1, 6};
    if (t.size() != 10 || !only_digits(t) || t[0] == '0') return false;
    const int m = at(t, 6, 2);
    // Months 13-15 are real: fictitious dates when a date is unknown or used up.
    if ((m < 13 || m > 15) && !v.date_2d(at(t, 8, 2), m, at(t, 4, 2))) return false;
    const unsigned rem = sum_mod(std::string{t.substr(0, 3)} + std::string{t.substr(4)}, w, 11);
    return rem != 10 && t[3] == dig(rem);
}

// ---- Estonia and Lithuania ----

[[nodiscard]] inline char baltic_check(std::string_view body)
{
    static constexpr std::array<unsigned, 10> first{1, 2, 3, 4, 5, 6, 7, 8, 9, 1};
    static constexpr std::array<unsigned, 10> second{3, 4, 5, 6, 7, 8, 9, 1, 2, 3};
    unsigned rem = sum_mod(body, first, 11);
    if (rem == 10) rem = sum_mod(body, second, 11);
    return dig(rem == 10 ? 0 : rem);
}

inline bool build_baltic_code(build_ctx& b, std::string& out)
{
    out = std::string{century_sex_digit(b, '3')} + two(b.date.y) + two(b.date.m) + two(b.date.d) + rand_digits(b.r, 3);
    const char c = baltic_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_baltic_code(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 11 || !only_digits(t) || !v.s.pattern.matches(t)) return false;
    const int g = t[0] - '0';
    const int century = 1800 + (((g - 1) / 2) * 100);
    return v.date(century + at(t, 1, 2), at(t, 3, 2), at(t, 5, 2)) && t[10] == baltic_check(t.substr(0, 10));
}

// ---- Belgium ----

[[nodiscard]] inline unsigned be_key(std::string_view nine, bool prefix2)
{
    const std::uint64_t n = at64(nine) + (prefix2 ? 2000000000ULL : 0ULL);
    return static_cast<unsigned>(97U - (n % 97U));
}

inline bool build_be_nn(build_ctx& b, std::string& out)
{
    const std::uint64_t serial = (core::bounded(b.r, 499) * 2U) + (b.male() ? 1U : 2U);
    out = two(b.date.y) + two(b.date.m) + two(b.date.d) + num(serial, 3);
    unsigned key = be_key(out, to_day(b.date) >= b.s.param_day("prefix2_from_birth"));
    if (b.wrong) key = (key % 97U) + 1U;
    out += num(key, 2);
    return true;
}

inline bool valid_be_nn(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 11 || !only_digits(t)) return false;
    const int m = at(t, 2, 2);
    const int d = at(t, 4, 2);
    // Month or day 00: the register's form for an unknown date.
    if (m != 0 && d != 0 && !v.date_2d(at(t, 0, 2), m, d)) return false;
    const auto key = static_cast<unsigned>(at(t, 9, 2));
    return key == be_key(t.substr(0, 9), false) || key == be_key(t.substr(0, 9), true);
}

// ---- Bulgaria ----

inline bool build_bg_egn(build_ctx& b, std::string& out)
{
    static constexpr std::array<unsigned, 9> w{2, 4, 8, 5, 10, 9, 7, 3, 6};
    const std::uint64_t lo = at64(std::string_view{b.place}.substr(0, 3));
    const std::uint64_t hi = at64(std::string_view{b.place}.substr(4, 3));
    std::uint64_t serial = core::uniform_int(b.r, lo, hi);
    // The ninth digit is even for men.
    if ((serial % 2U == 0U) != b.male()) serial = serial < hi ? serial + 1U : serial - 1U;
    const int month = b.date.m + (b.date.y >= 2000 ? 40 : (b.date.y < 1900 ? 20 : 0));
    out = two(b.date.y) + two(month) + two(b.date.d) + num(serial, 3);
    const unsigned rem = sum_mod(out, w, 11);
    put_check(b, out, dig(rem == 10 ? 0 : rem));
    return true;
}

inline bool valid_bg_egn(const valid_ctx& v, std::string_view t)
{
    static constexpr std::array<unsigned, 9> w{2, 4, 8, 5, 10, 9, 7, 3, 6};
    if (t.size() != 10 || !only_digits(t)) return false;
    int m = at(t, 2, 2);
    int century = 1900;
    if (m > 40) { m -= 40; century = 2000; }
    else if (m > 20) { m -= 20; century = 1800; }
    if (!v.date(century + at(t, 0, 2), m, at(t, 4, 2))) return false;
    const unsigned rem = sum_mod(t.substr(0, 9), w, 11);
    return t[9] == dig(rem == 10 ? 0 : rem);
}

// ---- Brazil ----

[[nodiscard]] inline char cpf_digit(std::string_view body, unsigned first_weight)
{
    std::array<unsigned, 10> w{};
    for (std::size_t i = 0; i < w.size(); ++i) w.at(i) = first_weight - static_cast<unsigned>(i);
    const auto rem = static_cast<unsigned>(core::weighted_sum(body, std::span<const unsigned>{w.data(), body.size()}) % 11U);
    return dig(rem < 2 ? 0 : 11 - rem);
}

inline bool build_br_cpf(build_ctx& b, std::string& out)
{
    out = rand_digits(b.r, 8) + b.place;
    out += cpf_digit(out, 10);
    if (all_same(out)) return false; // validators reject repeated digits
    const char c2 = cpf_digit(out, 11);
    put_check(b, out, c2);
    return true;
}

inline bool valid_br_cpf(const valid_ctx& /*v*/, std::string_view t)
{
    if (t.size() != 11 || !only_digits(t) || all_same(t)) return false;
    return t[9] == cpf_digit(t.substr(0, 9), 10) && t[10] == cpf_digit(t.substr(0, 10), 11);
}

// ---- Canada ----

inline bool build_ca_sin(build_ctx& b, std::string& out)
{
    out = b.place + rand_digits(b.r, 7);
    const char c = core::luhn_digit(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_luhn_pattern(const valid_ctx& v, std::string_view t)
{
    return v.s.pattern.matches(t) && core::luhn_valid(t);
}

// ---- Chile ----

[[nodiscard]] inline char cl_digit(std::string_view digits)
{
    unsigned sum = 0;
    unsigned weight = 2;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it)
    {
        sum += static_cast<unsigned>(*it - '0') * weight;
        weight = weight == 7 ? 2 : weight + 1;
    }
    const unsigned c = 11U - (sum % 11U);
    return c == 11U ? '0' : (c == 10U ? 'K' : dig(c));
}

inline bool build_cl_run(build_ctx& b, std::string& out)
{
    const auto* a = b.s.find_series("-");
    if (a == nullptr) throw std::runtime_error("scheme " + b.s.key + ": no series");
    const std::int64_t noise = core::uniform_int<std::int64_t>(b.r, -7 * seconds_per_day, 7 * seconds_per_day);
    out = std::to_string(std::max<std::int64_t>(1000000, series_at(*a, (b.birth_day * seconds_per_day) + noise)));
    const char c = cl_digit(out);
    put_check(b, out, c, "0123456789K");
    return true;
}

inline bool valid_cl_run(const valid_ctx& /*v*/, std::string_view t)
{
    if (t.size() < 2 || t.size() > 9 || !only_digits(t.substr(0, t.size() - 1))) return false;
    return t.back() == cl_digit(t.substr(0, t.size() - 1));
}

// ---- China ----

inline bool build_cn_ric(build_ctx& b, std::string& out)
{
    std::string seq = rand_digits(b.r, 2);
    seq += parity_digit(b.r, b.male());
    if (seq == "000") return false;
    out = b.place + num(static_cast<std::uint64_t>(b.date.y), 4) + two(b.date.m) + two(b.date.d) + seq;
    const char c = core::mod11_2_digit(out);
    put_check(b, out, c, "0123456789X");
    return true;
}

inline bool valid_cn_ric(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 18 || !only_digits(t.substr(0, 17))) return false;
    return v.date(at(t, 6, 4), at(t, 10, 2), at(t, 12, 2)) && core::mod11_2_valid(t);
}

// ---- Czech Republic and Slovakia ----

inline bool build_rc(build_ctx& b, std::string& out)
{
    const bool nine = to_day(b.date) <= b.s.param_day("nine_digit_until_birth");
    int month = b.date.m + (b.male() ? 0 : 50);
    int day = b.date.d;
    // A tiny share of the births since 2004 falls in the extra series (+20).
    if (to_day(b.date) >= b.s.param_day("extra_series_from") && core::bernoulli(b.r, 0.001)) month += 20;
    // Nine-digit numbers have no check digit to get wrong: an impossible date instead.
    if (nine && b.wrong)
    {
        month = 2 + (b.male() ? 0 : 50);
        day = 30;
    }
    out = two(b.date.y) + two(month) + two(day) + rand_digits(b.r, 3);
    if (nine) return true;
    const auto rem = static_cast<unsigned>(at64(out) % 11U);
    if (rem == 10) return false; // the remainder-10 form ended in 1985
    put_check(b, out, dig(rem));
    return true;
}

inline bool valid_rc(const valid_ctx& v, std::string_view t)
{
    if ((t.size() != 9 && t.size() != 10) || !only_digits(t)) return false;
    const int yy = at(t, 0, 2);
    int m = at(t, 2, 2);
    const int d = at(t, 4, 2);
    if (m > 50) m -= 50;
    const bool extra_allowed = v.s.find_param("extra_series_from") != nullptr;
    if (m > 20 && extra_allowed) m -= 20;
    if (t.size() == 9) return yy <= 53 && v.date(1900 + yy, m, d);
    const int year = yy >= 54 ? 1900 + yy : 2000 + yy;
    if (!v.date(year, m, d)) return false;
    if (at64(t) % 11U == 0U) return true;
    return year <= 1985 && at64(t.substr(0, 9)) % 11U == 10U && t[9] == '0';
}

[[nodiscard]] inline std::string rc_display(std::string_view value)
{
    return std::string{value.substr(0, 6)} + "/" + std::string{value.substr(6)};
}

// ---- Germany ----

[[nodiscard]] inline bool de_digits_ok(std::string_view ten)
{
    std::array<int, 10> count{};
    for (const char c : ten) ++count.at(static_cast<std::size_t>(c - '0'));
    int repeated = 0;
    for (const int n : count)
    {
        if (n > 3) return false;
        if (n >= 2) ++repeated;
    }
    if (repeated != 1) return false;
    for (std::size_t i = 2; i < ten.size(); ++i)
    {
        if (ten[i] == ten[i - 1] && ten[i] == ten[i - 2]) return false;
    }
    return true;
}

inline bool build_de_idnr(build_ctx& b, std::string& out)
{
    // Exactly one digit twice or three times. Of all such digit strings, 45 in 105
    // have it twice.
    std::array<char, 10> pool{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    core::shuffle(b.r, pool);
    const bool twice = core::bernoulli(b.r, 45.0 / 105.0);
    const std::size_t distinct = twice ? 9 : 8;
    std::string ten{pool.begin(), pool.begin() + static_cast<std::ptrdiff_t>(distinct)};
    const char repeated = ten[core::bounded(b.r, distinct)];
    ten.append(twice ? 1 : 2, repeated);
    core::shuffle(b.r, ten);
    if (!de_digits_ok(ten)) return false;
    if (b.reserved_range)
    {
        if (ten.front() != '0') return false;
    }
    else
    {
        // Until 2014 no number started with 0 to 3; since then only 0 is withheld.
        const char lowest = b.assigned >= b.s.param_day("low_prefix_from") ? '1' : '4';
        if (ten.front() < lowest) return false;
    }
    out = ten;
    out += core::mod11_10_digit(ten);
    return true;
}

inline bool valid_de_idnr(const valid_ctx& /*v*/, std::string_view t)
{
    return t.size() == 11 && only_digits(t) && de_digits_ok(t.substr(0, 10)) && core::mod11_10_valid(t);
}

// ---- Denmark ----

[[nodiscard]] inline unsigned dk_sum(std::string_view nine)
{
    static constexpr std::array<unsigned, 9> w{4, 3, 2, 7, 6, 5, 4, 3, 2};
    return sum_mod(nine, w, 11);
}

[[nodiscard]] inline bool dk_no_mod11(const scheme& s, int y, int m, int d)
{
    if (m != 1 || d != 1) return false;
    return s.param_text("no_mod11_jan1").find(num(static_cast<std::uint64_t>(y), 4)) != std::string_view::npos;
}

inline bool build_dk_cpr(build_ctx& b, std::string& out)
{
    const int y = b.date.y;
    int month = b.date.m;
    char century = '0';
    if (y <= 1899) century = pick_char(b.r, "5678");
    else if (y <= 1999) century = pick_char(b.r, "0123");
    else if (y <= 2036) century = pick_char(b.r, "456789");
    else if (y <= 2057) century = pick_char(b.r, "5678");
    else if (b.impossible) month = 13; // no later year can be written
    out = two(b.date.d) + two(month) + two(y) + century + rand_digits(b.r, 2);
    const bool mod11 = b.assigned <= b.s.param_day("mod11_until") || !dk_no_mod11(b.s, y, month, b.date.d);
    if (!mod11)
    {
        out += parity_digit(b.r, b.male());
        return true;
    }
    const unsigned last = (11U - dk_sum(out)) % 11U;
    if (last > 9 || (last % 2U == 1U) != b.male()) return false; // serial skipped
    out += dig(last);
    return true;
}

inline bool valid_dk_cpr(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 10 || !only_digits(t)) return false;
    const int yy = at(t, 4, 2);
    const int c = t[6] - '0';
    int year = 1900 + yy;
    if ((c == 4 || c == 9) && yy <= 36) year = 2000 + yy;
    else if (c >= 5 && c <= 8) year = yy <= 57 ? 2000 + yy : 1800 + yy;
    const int m = at(t, 2, 2);
    const int d = at(t, 0, 2);
    if (!v.date(year, m, d)) return false;
    if (dk_no_mod11(v.s, year, m, d)) return true;
    return (dk_sum(t.substr(0, 9)) + static_cast<unsigned>(t[9] - '0')) % 11U == 0U;
}

// ---- Ecuador ----

inline bool build_ec_cedula(build_ctx& b, std::string& out)
{
    out = b.place + rand_range(b.r, 0, 5, 1);
    out += rand_digits(b.r, 6);
    const char c = core::luhn_digit(out);
    put_check(b, out, c);
    return true;
}

// ---- Spain ----

inline constexpr std::string_view es_letters = "TRWAGMYFPDXBNJZSQVHLCKE";

inline bool build_es_dni(build_ctx& b, std::string& out)
{
    out = rand_digits(b.r, 8);
    put_check(b, out, es_letters[at64(out) % 23U], es_letters);
    return true;
}

inline bool valid_es_dni(const valid_ctx& /*v*/, std::string_view t)
{
    return t.size() == 9 && only_digits(t.substr(0, 8)) && t[8] == es_letters[at64(t.substr(0, 8)) % 23U];
}

// ---- Finland ----

inline constexpr std::string_view fi_marks = "0123456789ABCDEFHJKLMNPRSTUVWXY";

inline bool build_fi_hetu(build_ctx& b, std::string& out)
{
    const int y = b.date.y;
    char sign = '-';
    const bool may_be_new = b.assigned >= b.s.param_day("new_signs_from");
    if (y < 1900) sign = '+';
    else if (y < 2000) sign = may_be_new && core::bernoulli(b.r, 0.5) ? 'Y' : '-';
    else sign = may_be_new && core::bernoulli(b.r, 0.5) ? 'B' : 'A';
    const std::uint64_t serial = b.reserved_range ? 900U + (core::bounded(b.r, 50) * 2U) + (b.male() ? 1U : 0U)
                                                  : 2U + (core::bounded(b.r, 449) * 2U) + (b.male() ? 1U : 0U);
    const std::string nine = two(b.date.d) + two(b.date.m) + two(y) + num(serial, 3);
    out = nine.substr(0, 6) + sign + nine.substr(6);
    out += fi_marks[at64(nine) % 31U];
    return true;
}

inline bool valid_fi_hetu(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 11 || !only_digits(t.substr(0, 6)) || !only_digits(t.substr(7, 3))) return false;
    int century = 0;
    if (t[6] == '+') century = 1800;
    else if (std::string_view{"-YXWVU"}.find(t[6]) != std::string_view::npos) century = 1900;
    else if (std::string_view{"ABCDEF"}.find(t[6]) != std::string_view::npos) century = 2000;
    else return false;
    if (!v.date(century + at(t, 4, 2), at(t, 2, 2), at(t, 0, 2))) return false;
    if (at(t, 7, 3) < 2) return false;
    const std::string nine = std::string{t.substr(0, 6)} + std::string{t.substr(7, 3)};
    return t[10] == fi_marks[at64(nine) % 31U];
}

// ---- France ----

[[nodiscard]] inline unsigned fr_key(std::string_view thirteen)
{
    std::string digits{thirteen};
    if (digits[6] == 'A') { digits[5] = '1'; digits[6] = '9'; }
    else if (digits[6] == 'B') { digits[5] = '1'; digits[6] = '8'; }
    return static_cast<unsigned>(97U - (at64(digits) % 97U));
}

inline bool build_fr_nir(build_ctx& b, std::string& out)
{
    std::string place = b.place;
    // Corsican births before 1976 keep the department 20.
    if (to_day(b.date) < b.s.param_day("corsica_letters_from_birth") && place.size() == 5 && place[0] == '2' &&
        (place[1] == 'A' || place[1] == 'B'))
    {
        place[1] = '0';
    }
    out = std::string{b.male() ? "1" : "2"} + two(b.date.y) + two(b.date.m) + place + rand_range(b.r, 1, 999, 3);
    unsigned key = fr_key(out);
    if (b.wrong) key = (key % 97U) + 1U;
    out += num(key, 2);
    return true;
}

inline bool valid_fr_nir(const valid_ctx& v, std::string_view t)
{
    if (!v.s.pattern.matches(t)) return false;
    return at(t, 13, 2) == static_cast<int>(fr_key(t.substr(0, 13)));
}

// ---- United Kingdom ----

inline bool build_gb_nino(build_ctx& b, std::string& out)
{
    if (b.reserved_range && b.s.safe_pattern)
    {
        out = b.s.safe_pattern->draw(b.r);
        return true;
    }
    out = b.s.pattern.draw(b.r);
    return b.s.param_text("barred").find(std::string_view{out}.substr(0, 2)) == std::string_view::npos;
}

inline bool valid_gb_nino(const valid_ctx& v, std::string_view t)
{
    if (v.s.safe_pattern && v.s.safe_pattern->matches(t)) return true;
    return v.s.pattern.matches(t) && v.s.param_text("barred").find(t.substr(0, 2)) == std::string_view::npos;
}

// ---- Hong Kong ----

[[nodiscard]] inline char hk_check(std::string_view body)
{
    std::array<unsigned, 8> values{};
    const std::size_t pad = 8 - body.size();
    values[0] = 36; // a one-letter prefix is padded with a space
    for (std::size_t i = 0; i < body.size(); ++i) values.at(pad + i) = core::alnum_value(body[i]);
    unsigned sum = 0;
    for (std::size_t i = 0; i < values.size(); ++i) sum += values.at(i) * (9U - static_cast<unsigned>(i));
    const unsigned c = 11U - (sum % 11U);
    return c == 11U ? '0' : (c == 10U ? 'A' : dig(c));
}

inline bool build_hk_hkid(build_ctx& b, std::string& out)
{
    // Two-letter prefixes exist and are rare.
    out = core::bernoulli(b.r, 0.02) ? std::string{"X"} + pick_char(b.r, "ABCDEGHKMPRVYZ") : std::string{pick_char(b.r, "ACDEGHKPRVYZ")};
    out += rand_digits(b.r, 6);
    const char c = hk_check(out);
    put_check(b, out, c, "0123456789A");
    return true;
}

inline bool valid_hk_hkid(const valid_ctx& v, std::string_view t)
{
    if (!v.s.pattern.matches(t)) return false;
    return t.back() == hk_check(t.substr(0, t.size() - 1));
}

// ---- Indonesia ----

inline bool build_id_nik(build_ctx& b, std::string& out)
{
    out = b.place + two(b.date.d + (b.male() ? 0 : 40)) + two(b.date.m) + two(b.date.y) + rand_range(b.r, 1, 9999, 4);
    return true;
}

inline bool valid_id_nik(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 16 || !only_digits(t) || at(t, 12, 4) == 0) return false;
    int d = at(t, 6, 2);
    if (d > 40) d -= 40;
    return v.date_2d(at(t, 10, 2), at(t, 8, 2), d);
}

// ---- Ireland ----

inline constexpr std::string_view ie_marks = "WABCDEFGHIJKLMNOPQRSTUV";

[[nodiscard]] inline char ie_check(std::string_view seven, char suffix)
{
    static constexpr std::array<unsigned, 7> w{8, 7, 6, 5, 4, 3, 2};
    auto sum = static_cast<unsigned>(core::weighted_sum(seven, w));
    if (suffix >= 'A' && suffix <= 'H') sum += 9U * (static_cast<unsigned>(suffix - 'A') + 1U);
    return ie_marks[sum % 23U];
}

inline bool build_ie_ppsn(build_ctx& b, std::string& out)
{
    char suffix = '\0';
    if (b.assigned >= b.s.param_day("suffix_b_from")) suffix = 'B';
    else if (b.assigned >= b.s.param_day("suffix_a_from")) suffix = 'A';
    out = rand_digits(b.r, 7);
    put_check(b, out, ie_check(out, suffix), ie_marks);
    if (suffix != '\0') out += suffix;
    return true;
}

inline bool valid_ie_ppsn(const valid_ctx& v, std::string_view t)
{
    if (!v.s.pattern.matches(t)) return false;
    const char suffix = t.size() == 9 ? t[8] : '\0';
    return t[7] == ie_check(t.substr(0, 7), suffix);
}

// ---- Iran ----

[[nodiscard]] inline char ir_check(std::string_view nine)
{
    static constexpr std::array<unsigned, 9> w{10, 9, 8, 7, 6, 5, 4, 3, 2};
    const unsigned rem = sum_mod(nine, w, 11);
    return dig(rem < 2 ? rem : 11 - rem);
}

inline bool build_ir_code(build_ctx& b, std::string& out)
{
    out = rand_digits(b.r, 9);
    const char c = ir_check(out);
    if (all_same(out) && c == out.front()) return false;
    put_check(b, out, c);
    return true;
}

inline bool valid_ir_code(const valid_ctx& /*v*/, std::string_view t)
{
    return t.size() == 10 && only_digits(t) && !all_same(t) && t[9] == ir_check(t.substr(0, 9));
}

// ---- Iceland ----

[[nodiscard]] inline unsigned is_sum(std::string_view eight)
{
    static constexpr std::array<unsigned, 8> w{3, 2, 7, 6, 5, 4, 3, 2};
    return sum_mod(eight, w, 11);
}

inline bool build_is_kennitala(build_ctx& b, std::string& out)
{
    out = two(b.date.d) + two(b.date.m) + two(b.date.y) + rand_range(b.r, 20, 99, 2);
    if (b.assigned >= b.s.param_day("no_check_from"))
    {
        out += rand_digits(b.r, 1); // issued without a check digit since 2026-02-18
    }
    else
    {
        const unsigned rem = is_sum(out);
        if (rem == 1) return false; // the RR value is skipped
        out += dig(rem == 0 ? 0 : 11 - rem);
    }
    out += b.date.y >= 2000 ? '0' : (b.date.y >= 1900 ? '9' : '8');
    return true;
}

inline bool valid_is_kennitala(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 10 || !only_digits(t)) return false;
    const int c = t[9] - '0';
    if (c != 0 && c != 8 && c != 9) return false;
    const int year = (c == 0 ? 2000 : (c == 9 ? 1900 : 1800)) + at(t, 4, 2);
    if (!v.date(year, at(t, 2, 2), at(t, 0, 2))) return false;
    // Check digit: required when the number was surely issued before it was dropped.
    if (to_day(civil{.y = year, .m = at(t, 2, 2), .d = at(t, 0, 2)}) + v.s.assigned.lag >= v.s.param_day("no_check_from")) return true;
    const unsigned rem = is_sum(t.substr(0, 8));
    return rem != 1 && t[8] == dig(rem == 0 ? 0 : 11 - rem);
}

// ---- Italy ----

[[nodiscard]] inline char it_check(std::string_view fifteen)
{
    static constexpr std::array<std::uint8_t, 36> odd{1, 0, 5, 7, 9, 13, 15, 17, 19, 21,  // 0-9
                                                       1, 0, 5, 7, 9, 13, 15, 17, 19, 21, 2, 4, 18, 20, 11, 3, 6, 8, 12, 14, 16, 10, 22, 25, 24, 23}; // A-Z
    unsigned sum = 0;
    for (std::size_t i = 0; i < fifteen.size(); ++i)
    {
        const unsigned v = core::alnum_value(fifteen[i]);
        sum += i % 2U == 0U ? odd.at(v) : (v >= 10U ? v - 10U : v);
    }
    return letters[sum % 26U];
}

inline bool build_it_cf(build_ctx& b, std::string& out)
{
    static constexpr std::string_view months = "ABCDEHLMPRST";
    out = it_name_code(b.surname, false, b.r);
    out += it_name_code(b.given, true, b.r);
    out += two(b.date.y);
    out += months[static_cast<std::size_t>(b.date.m - 1)];
    out += two(b.date.d + (b.male() ? 0 : 40)) + b.place;
    const char c = it_check(out);
    put_check(b, out, c, letters);
    return true;
}

inline bool valid_it_cf(const valid_ctx& v, std::string_view t)
{
    static constexpr std::string_view months = "ABCDEHLMPRST";
    static constexpr std::string_view omocodes = "LMNPQRSTUV";
    if (t.size() != 16) return false;
    // Omocodia: to separate equal codes, digits become letters from the right.
    std::string plain{t};
    for (const std::size_t i : {6U, 7U, 9U, 10U, 12U, 13U, 14U})
    {
        const auto pos = omocodes.find(plain[i]);
        if (pos != std::string_view::npos) plain[i] = decimal[pos];
    }
    const std::string_view p = plain;
    for (const std::size_t i : {0U, 1U, 2U, 3U, 4U, 5U, 8U, 11U, 15U})
    {
        if (!is_letter(p[i])) return false;
    }
    if (!only_digits(p.substr(6, 2)) || !only_digits(p.substr(9, 2)) || !only_digits(p.substr(12, 3))) return false;
    const auto month = months.find(p[8]);
    if (month == std::string_view::npos) return false;
    int d = at(p, 9, 2);
    if (d > 40) d -= 40;
    if (!v.date_2d(at(p, 6, 2), static_cast<int>(month) + 1, d)) return false;
    return t[15] == it_check(t.substr(0, 15));
}

// ---- Former Yugoslavia ----

[[nodiscard]] inline unsigned jmbg_sum(std::string_view twelve)
{
    static constexpr std::array<unsigned, 12> w{7, 6, 5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
    return sum_mod(twelve, w, 11);
}

inline bool build_jmbg(build_ctx& b, std::string& out)
{
    const std::string area = b.s.place_source == place_from::none ? std::string{b.s.param_text("register")} : b.place;
    const std::string serial = b.male() ? rand_range(b.r, 0, 499, 3) : rand_range(b.r, 500, 999, 3);
    out = two(b.date.d) + two(b.date.m) + num(static_cast<std::uint64_t>(b.date.y % 1000), 3) + area + serial;
    const unsigned rem = jmbg_sum(out);
    if (rem == 1) return false; // never assigned: the serial is skipped
    put_check(b, out, dig(rem == 0 ? 0 : 11 - rem));
    return true;
}

inline bool valid_jmbg(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 13 || !only_digits(t)) return false;
    const int g = at(t, 4, 3);
    if (!v.date((g >= 800 ? 1000 : 2000) + g, at(t, 2, 2), at(t, 0, 2))) return false;
    const unsigned rem = jmbg_sum(t.substr(0, 12));
    return rem != 1 && t[12] == dig(rem == 0 ? 0 : 11 - rem);
}

// ---- South Korea ----

[[nodiscard]] inline char kr_check(std::string_view twelve)
{
    static constexpr std::array<unsigned, 12> w{2, 3, 4, 5, 6, 7, 8, 9, 2, 3, 4, 5};
    return dig((11U - sum_mod(twelve, w, 11)) % 10U);
}

inline bool build_kr_rrn(build_ctx& b, std::string& out)
{
    const char s = b.date.y >= 2000 ? (b.male() ? '3' : '4') : (b.date.y >= 1900 ? (b.male() ? '1' : '2') : (b.male() ? '9' : '0'));
    out = two(b.date.y) + two(b.date.m) + two(b.date.d) + s;
    if (b.assigned >= b.s.param_day("random_from"))
    {
        out += rand_digits(b.r, 6); // nothing encoded since 2020-10-05
        b.place.clear();
        return true;
    }
    out += b.place + rand_digits(b.r, 2);
    out += rand_range(b.r, 1, 9, 1);
    out += kr_check(out);
    return true;
}

inline bool valid_kr_rrn(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 13 || !only_digits(t)) return false;
    int century = 0;
    switch (t[6])
    {
    case '9': case '0': century = 1800; break;
    case '1': case '2': century = 1900; break;
    case '3': case '4': century = 2000; break;
    default: return false;
    }
    const int year = century + at(t, 0, 2);
    if (!v.date(year, at(t, 2, 2), at(t, 4, 2))) return false;
    if (to_day(civil{.y = year, .m = at(t, 2, 2), .d = at(t, 4, 2)}) + v.s.assigned.lag >= v.s.param_day("random_from")) return true;
    return t[12] == kr_check(t.substr(0, 12));
}

// ---- Kazakhstan ----

[[nodiscard]] inline std::optional<char> kz_check(std::string_view eleven)
{
    static constexpr std::array<unsigned, 11> first{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static constexpr std::array<unsigned, 11> second{3, 4, 5, 6, 7, 8, 9, 10, 11, 1, 2};
    unsigned rem = sum_mod(eleven, first, 11);
    if (rem == 10) rem = sum_mod(eleven, second, 11);
    if (rem == 10) return std::nullopt;
    return dig(rem);
}

inline bool build_kz_iin(build_ctx& b, std::string& out)
{
    out = two(b.date.y) + two(b.date.m) + two(b.date.d) + century_sex_digit(b, '3') + rand_digits(b.r, 4);
    const auto c = kz_check(out);
    if (!c) return false;
    put_check(b, out, *c);
    return true;
}

inline bool valid_kz_iin(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 12 || !only_digits(t)) return false;
    const int g = t[6] - '0';
    if (g < 1 || g > 6) return false;
    const int century = 1800 + (((g - 1) / 2) * 100);
    if (!v.date(century + at(t, 0, 2), at(t, 2, 2), at(t, 4, 2))) return false;
    const auto c = kz_check(t.substr(0, 11));
    return c && t[11] == *c;
}

// ---- Latvia ----

[[nodiscard]] inline char lv_check(std::string_view ten)
{
    static constexpr std::array<unsigned, 10> w{10, 5, 8, 4, 2, 1, 6, 3, 7, 9};
    return dig(static_cast<unsigned>((1U + core::weighted_sum(ten, w)) % 11U % 10U));
}

inline bool build_lv_code(build_ctx& b, std::string& out)
{
    if (b.assigned >= b.s.param_day("random_from")) out = "32" + rand_digits(b.r, 8);
    else out = two(b.date.d) + two(b.date.m) + two(b.date.y) + dig(static_cast<unsigned>((b.date.y - 1800) / 100)) + rand_digits(b.r, 3);
    const char c = lv_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_lv_code(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 11 || !only_digits(t)) return false;
    if (!t.starts_with("32"))
    {
        const int c = t[6] - '0';
        if (c > 2 || !v.date(1800 + (c * 100) + at(t, 4, 2), at(t, 2, 2), at(t, 0, 2))) return false;
    }
    return t[10] == lv_check(t.substr(0, 10));
}

// ---- Mexico ----

struct curp_name
{
    char initial{'X'};
    char vowel{'X'};
    char consonant{'X'};
};

// Punctuation left in a word becomes what the rules say; anything else is dropped.
[[nodiscard]] inline std::string curp_letters(std::string_view word, const name_rule_set& rules)
{
    std::string out;
    for (const char c : word)
    {
        if (is_letter(c))
        {
            out += c;
            continue;
        }
        for (const auto& [from, to] : rules.chars)
        {
            if (from.size() == 1 && from[0] == c) out += to;
        }
    }
    return out;
}

[[nodiscard]] inline bool curp_listed(const std::vector<std::string>& list, std::string_view word)
{
    return std::ranges::find(list, word) != list.end();
}

// One name's letters: after the leading particles (and, for a given name, a
// leading MARIA or JOSE when another name follows), the first word's initial,
// first internal vowel and first internal consonant.
[[nodiscard]] inline std::optional<curp_name> curp_parts(const std::string* name, bool given, const name_rule_set& rules)
{
    if (name == nullptr) return std::nullopt;
    const auto words = latin_words(*name, rules.chars);
    std::size_t i = 0;
    while (i + 1 < words.size() && curp_listed(rules.particles, words[i])) ++i;
    if (given && i + 1 < words.size() && curp_listed(rules.given_skip, words[i])) ++i;
    if (i >= words.size()) return std::nullopt;
    const std::string w = curp_letters(words[i], rules);
    if (w.empty()) return std::nullopt;
    curp_name out{.initial = w[0]};
    bool have_vowel = false;
    bool have_consonant = false;
    for (std::size_t k = 1; k < w.size(); ++k)
    {
        if (is_vowel(w[k]) && !have_vowel) { out.vowel = w[k]; have_vowel = true; }
        if (!is_vowel(w[k]) && !have_consonant) { out.consonant = w[k]; have_consonant = true; }
    }
    return out;
}

[[nodiscard]] inline curp_name curp_drawn(core::rng& r)
{
    return {.initial = pick_char(r, letters), .vowel = pick_char(r, vowels), .consonant = pick_char(r, consonants)};
}

[[nodiscard]] inline char curp_check(std::string_view seventeen)
{
    // 0-9, A-N, Ñ, O-Z: the letters from O on count one more because of Ñ.
    unsigned sum = 0;
    for (std::size_t i = 0; i < seventeen.size(); ++i)
    {
        const char c = seventeen[i];
        unsigned v = core::alnum_value(c);
        if (c >= 'O' && c <= 'Z') v += 1U;
        sum += v * (18U - static_cast<unsigned>(i));
    }
    return dig((10U - (sum % 10U)) % 10U);
}

inline bool build_mx_curp(build_ctx& b, std::string& out)
{
    const auto& rules = b.s.names;
    const curp_name first = curp_parts(b.surname, false, rules).value_or(curp_drawn(b.r));
    const auto second = b.second != nullptr ? curp_parts(b.second, false, rules) : std::optional<curp_name>{};
    const curp_name other = second ? *second : (b.surname != nullptr ? curp_name{} : curp_drawn(b.r));
    const curp_name given = curp_parts(b.given, true, rules).value_or(curp_drawn(b.r));
    const bool letter_century = to_day(b.date) >= b.s.param_day("century_letter_from_birth");
    std::string head{first.initial, first.vowel, other.initial, given.initial};
    for (const auto& [word, replacement] : rules.words)
    {
        if (word == head) head = replacement;
    }
    out = head + two(b.date.y) + two(b.date.m) + two(b.date.d);
    out += b.male() ? 'H' : 'M';
    out += b.place;
    out += first.consonant;
    out += other.consonant;
    out += given.consonant;
    out += letter_century ? pick_char(b.r, "ABCDEFGHIJ") : pick_char(b.r, decimal);
    const char c = curp_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_mx_curp(const valid_ctx& v, std::string_view t)
{
    if (!v.s.pattern.matches(t)) return false;
    for (const auto& [word, replacement] : v.s.names.words)
    {
        if (t.substr(0, 4) == word) return false;
    }
    const char homonymy = t[16];
    if (!((homonymy >= '0' && homonymy <= '9') || (homonymy >= 'A' && homonymy <= 'J'))) return false;
    const int century = homonymy <= '9' ? 1900 : 2000;
    if (!v.date(century + at(t, 4, 2), at(t, 6, 2), at(t, 8, 2))) return false;
    return t[17] == curp_check(t.substr(0, 17));
}

// ---- Malaysia ----

inline bool build_my_mykad(build_ctx& b, std::string& out)
{
    out = two(b.date.y) + two(b.date.m) + two(b.date.d) + b.place + rand_digits(b.r, 3);
    out += parity_digit(b.r, b.male());
    return true;
}

inline bool valid_my_mykad(const valid_ctx& v, std::string_view t)
{
    return t.size() == 12 && only_digits(t) && v.date_2d(at(t, 0, 2), at(t, 2, 2), at(t, 4, 2));
}

// ---- Norway ----

[[nodiscard]] inline unsigned no_sum1(std::string_view nine)
{
    static constexpr std::array<unsigned, 9> w{3, 7, 6, 1, 8, 9, 4, 5, 2};
    return sum_mod(nine, w, 11);
}

[[nodiscard]] inline unsigned no_sum2(std::string_view ten)
{
    static constexpr std::array<unsigned, 10> w{5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
    return sum_mod(ten, w, 11);
}

inline bool build_no_fnr(build_ctx& b, std::string& out)
{
    const bool new_rule = b.assigned >= b.s.param_day("new_rule_from");
    const int y = b.date.y;
    std::uint64_t individual = 0;
    if (new_rule)
    {
        individual = core::bounded(b.r, 1000); // neither sex nor century
    }
    else
    {
        std::uint64_t lo = 0;
        std::uint64_t hi = 499;
        if (y < 1900) { lo = 500; hi = 749; }
        else if (y >= 2000) { lo = 500; hi = 999; }
        else if (y >= 1940 && core::bernoulli(b.r, 0.1)) { lo = 900; hi = 999; }
        individual = core::uniform_int(b.r, lo, hi);
        if ((individual % 2U == 1U) != b.male()) individual ^= 1U;
    }
    const int month = b.date.m + (b.reserved_range ? static_cast<int>(b.s.param_number("synthetic_month_offset")) : 0);
    out = two(b.date.d) + two(month) + two(y) + num(individual, 3);
    const unsigned rem1 = no_sum1(out);
    unsigned k1 = 11U - rem1;
    if (new_rule) k1 += static_cast<unsigned>(core::bounded(b.r, 4));
    if (k1 > 10U) k1 -= 11U;
    if (k1 == 10U) return false;
    out += dig(k1);
    const unsigned k2 = (11U - no_sum2(out)) % 11U;
    if (k2 == 10U) return false;
    out += dig(k2);
    return true;
}

inline bool valid_no_fnr(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 11 || !only_digits(t)) return false;
    int m = at(t, 2, 2);
    if (m > 80) m -= 80; // synthetic numbers
    const int d = at(t, 0, 2);
    const int yy = at(t, 4, 2);
    const int individual = at(t, 6, 3);
    if ((no_sum2(t.substr(0, 10)) + static_cast<unsigned>(t[10] - '0')) % 11U != 0U) return false;
    const unsigned total1 = (no_sum1(t.substr(0, 9)) + static_cast<unsigned>(t[9] - '0')) % 11U;
    if (total1 > 3U) return false;
    if (total1 != 0U) return v.date_2d(yy, m, d); // the 2032 rule: no century
    // The individual number's series gives the century.
    if (individual <= 499) return v.date(1900 + yy, m, d);
    if (individual >= 900 && yy >= 40) return v.date(1900 + yy, m, d);
    if (individual <= 749 && yy >= 54) return v.date(1800 + yy, m, d);
    if (yy <= 39) return v.date(2000 + yy, m, d);
    return v.date_2d(yy, m, d);
}

// ---- New Zealand ----

[[nodiscard]] inline std::optional<unsigned> nz_check(std::string_view eight)
{
    static constexpr std::array<unsigned, 8> primary{3, 2, 7, 6, 5, 4, 3, 2};
    static constexpr std::array<unsigned, 8> secondary{7, 4, 3, 2, 5, 2, 7, 6};
    unsigned rem = sum_mod(eight, primary, 11);
    unsigned c = rem == 0 ? 0 : 11 - rem;
    if (c == 10)
    {
        rem = sum_mod(eight, secondary, 11);
        c = rem == 0 ? 0 : 11 - rem;
    }
    if (c == 10) return std::nullopt;
    return c;
}

inline bool build_nz_ird(build_ctx& b, std::string& out)
{
    // The whole number lies between the params `min` and `max`.
    const std::uint64_t base = core::uniform_int(b.r, b.s.param_number("min") / 10U, (b.s.param_number("max") / 10U) - 1U);
    const auto c = nz_check(num(base, 8));
    if (!c) return false;
    out = std::to_string(base);
    put_check(b, out, dig(*c));
    return true;
}

inline bool valid_nz_ird(const valid_ctx& v, std::string_view t)
{
    if ((t.size() != 8 && t.size() != 9) || !only_digits(t)) return false;
    const std::uint64_t n = at64(t);
    if (n < v.s.param_number("min") || n > v.s.param_number("max")) return false;
    const auto c = nz_check(num(n / 10U, 8));
    return c && t.back() == dig(*c);
}

// ---- Peru ----

[[nodiscard]] inline char pe_digit(std::string_view eight)
{
    static constexpr std::array<unsigned, 8> w{3, 2, 7, 6, 5, 4, 3, 2};
    static constexpr std::string_view table = "65432110987";
    return table[sum_mod(eight, w, 11)];
}

inline bool build_pe_dni(build_ctx& b, std::string& out)
{
    const auto* a = b.s.find_series("national");
    if (a == nullptr) throw std::runtime_error("scheme " + b.s.key + ": no series");
    const std::int64_t t = b.birth_day * seconds_per_day;
    std::uint64_t number = 0;
    if (t < a->front().t)
    {
        // Geographic series of 1984-1996, not mapped to places.
        number = core::uniform_int<std::uint64_t>(b.r, 1000000, 39999999);
    }
    else
    {
        const std::int64_t noise = core::uniform_int<std::int64_t>(b.r, -7 * seconds_per_day, 7 * seconds_per_day);
        number = static_cast<std::uint64_t>(series_at(*a, t + noise));
    }
    out = num(number, 8);
    const char c = pe_digit(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_pe_dni(const valid_ctx& /*v*/, std::string_view t)
{
    static constexpr std::array<unsigned, 8> w{3, 2, 7, 6, 5, 4, 3, 2};
    static constexpr std::string_view letter = "KJIHGFEDCBA";
    if (t.size() != 9 || !only_digits(t.substr(0, 8))) return false;
    return t[8] == pe_digit(t.substr(0, 8)) || t[8] == letter[sum_mod(t.substr(0, 8), w, 11)];
}

// ---- Pakistan ----

inline bool build_pk_cnic(build_ctx& b, std::string& out)
{
    // Odd for men, even for women; validators do not take 0 for either.
    out = b.place + rand_digits(b.r, 11);
    out += pick_char(b.r, b.male() ? "13579" : "2468");
    return true;
}

inline bool valid_pk_cnic(const valid_ctx& /*v*/, std::string_view t) { return t.size() == 13 && only_digits(t); }

// ---- Poland ----

inline bool build_pl_pesel(build_ctx& b, std::string& out)
{
    static constexpr std::array<unsigned, 10> w{1, 3, 7, 9, 1, 3, 7, 9, 1, 3};
    const int y = b.date.y;
    const int offset = y < 1900 ? 80 : ((y - 1900) / 100) * 20;
    out = two(y) + two(b.date.m + offset) + two(b.date.d) + rand_digits(b.r, 3);
    out += parity_digit(b.r, b.male());
    const unsigned rem = sum_mod(out, w, 10);
    put_check(b, out, dig((10U - rem) % 10U));
    return true;
}

inline bool valid_pl_pesel(const valid_ctx& v, std::string_view t)
{
    static constexpr std::array<unsigned, 10> w{1, 3, 7, 9, 1, 3, 7, 9, 1, 3};
    if (t.size() != 11 || !only_digits(t)) return false;
    const int coded = at(t, 2, 2);
    const int band = coded / 20;
    const int century = band == 4 ? 1800 : 1900 + (band * 100);
    if (!v.date(century + at(t, 0, 2), coded - (band * 20), at(t, 4, 2))) return false;
    return t[10] == dig((10U - sum_mod(t.substr(0, 10), w, 10)) % 10U);
}

// ---- Portugal ----

[[nodiscard]] inline char pt_check(std::string_view eight)
{
    static constexpr std::array<unsigned, 8> w{9, 8, 7, 6, 5, 4, 3, 2};
    const unsigned rem = sum_mod(eight, w, 11);
    return dig(rem < 2 ? 0 : 11 - rem);
}

inline bool build_pt_nif(build_ctx& b, std::string& out)
{
    char first = '3';
    // No source gives the switch from 1 to 2; a rough split, documented in doc/data.md.
    if (b.assigned < b.s.param_day("range3_from")) first = core::bernoulli(b.r, 0.25) ? '1' : '2';
    out = first + rand_digits(b.r, 7);
    const char c = pt_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_pt_nif(const valid_ctx& v, std::string_view t)
{
    return v.s.pattern.matches(t) && t[8] == pt_check(t.substr(0, 8));
}

// ---- Romania ----

[[nodiscard]] inline char ro_check(std::string_view twelve)
{
    static constexpr std::array<unsigned, 12> w{2, 7, 9, 1, 4, 6, 3, 5, 8, 2, 7, 9};
    const unsigned rem = sum_mod(twelve, w, 11);
    return dig(rem == 10 ? 1 : rem);
}

inline bool build_ro_cnp(build_ctx& b, std::string& out)
{
    const int y = b.date.y;
    char s = b.male() ? '1' : '2';
    if (y >= 2000) s = b.male() ? '5' : '6';
    else if (y < 1900) s = b.male() ? '3' : '4';
    // From 2024-05-08 numbers generated in the new system use code 70; the rollout was gradual.
    if (b.assigned >= b.s.param_day("any_county_from") && core::bernoulli(b.r, 0.5)) b.place = "70";
    out = std::string{s} + two(y) + two(b.date.m) + two(b.date.d) + b.place + rand_range(b.r, 1, 999, 3);
    const char c = ro_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_ro_cnp(const valid_ctx& v, std::string_view t)
{
    if (!v.s.pattern.matches(t)) return false;
    const int g = t[0] - '0';
    const int century = g <= 2 ? 1900 : (g <= 4 ? 1800 : 2000);
    return v.date(century + at(t, 1, 2), at(t, 3, 2), at(t, 5, 2)) && t[12] == ro_check(t.substr(0, 12));
}

// ---- Russia ----

[[nodiscard]] inline std::pair<char, char> ru_inn_checks(std::string_view ten)
{
    static constexpr std::array<unsigned, 10> w1{7, 2, 4, 10, 3, 5, 9, 4, 6, 8};
    static constexpr std::array<unsigned, 11> w2{3, 7, 2, 4, 10, 3, 5, 9, 4, 6, 8};
    const char c1 = dig(sum_mod(ten, w1, 11) % 10U);
    const std::string eleven = std::string{ten} + c1;
    return {c1, dig(sum_mod(eleven, w2, 11) % 10U)};
}

inline bool build_ru_inn(build_ctx& b, std::string& out)
{
    // The two office digits have no public list here; a plausible low number.
    out = b.place + rand_range(b.r, 1, 49, 2);
    out += rand_digits(b.r, 6);
    const auto [c1, c2] = ru_inn_checks(out);
    out += c1;
    put_check(b, out, c2);
    return true;
}

inline bool valid_ru_inn(const valid_ctx& /*v*/, std::string_view t)
{
    if (t.size() != 12 || !only_digits(t)) return false;
    const auto [c1, c2] = ru_inn_checks(t.substr(0, 10));
    return t[10] == c1 && t[11] == c2;
}

[[nodiscard]] inline unsigned ru_snils_control(std::string_view nine)
{
    static constexpr std::array<unsigned, 9> w{9, 8, 7, 6, 5, 4, 3, 2, 1};
    const auto s = static_cast<unsigned>(core::weighted_sum(nine, w));
    if (s < 100U) return s;
    if (s <= 101U) return 0;
    const unsigned rem = s % 101U;
    return rem == 100U ? 0 : rem;
}

inline bool build_ru_snils(build_ctx& b, std::string& out)
{
    out = rand_range(b.r, 1001999, 999999999, 9); // the check starts above 001-001-998
    unsigned control = ru_snils_control(out);
    if (b.wrong) control = (control + 1U) % 100U;
    out += num(control, 2);
    return true;
}

inline bool valid_ru_snils(const valid_ctx& /*v*/, std::string_view t)
{
    if (t.size() != 11 || !only_digits(t)) return false;
    const std::string_view nine = t.substr(0, 9);
    if (at64(nine) <= 1001998U) return true;
    return static_cast<unsigned>(at(t, 9, 2)) == ru_snils_control(nine);
}

// ---- Sweden ----

inline bool build_se_personnummer(build_ctx& b, std::string& out)
{
    std::string serial = rand_digits(b.r, 2);
    serial += parity_digit(b.r, b.male());
    if (serial == "000") return false;
    const std::string nine = two(b.date.y) + two(b.date.m) + two(b.date.d) + serial;
    out = two(b.date.y / 100) + nine;
    out += core::luhn_digit(nine);
    return true;
}

inline bool valid_se_personnummer(const valid_ctx& v, std::string_view t)
{
    if (!v.s.pattern.matches(t) || !only_digits(t)) return false;
    if (!v.date(at(t, 0, 4), at(t, 4, 2), at(t, 6, 2))) return false;
    return core::luhn_valid(t.substr(2));
}

// ---- Singapore ----

[[nodiscard]] inline char sg_letter(std::string_view eight)
{
    static constexpr std::array<unsigned, 7> w{2, 7, 6, 5, 4, 3, 2};
    static constexpr std::string_view table = "JZIHGFEDCBA";
    auto sum = static_cast<unsigned>(core::weighted_sum(eight.substr(1), w));
    if (eight[0] == 'T') sum += 4U;
    return table[sum % 11U];
}

inline bool build_sg_nric(build_ctx& b, std::string& out)
{
    const char prefix = to_day(b.date) >= b.s.param_day("t_prefix_from_birth") ? 'T' : 'S';
    out = std::string{prefix};
    if (to_day(b.date) >= b.s.param_day("year_digits_from_birth")) out += two(b.date.y) + rand_digits(b.r, 5);
    else
    {
        out += pick_char(b.r, "01");
        out += rand_digits(b.r, 6);
    }
    const char c = sg_letter(out);
    put_check(b, out, c, "JZIHGFEDCBA");
    return true;
}

inline bool valid_sg_nric(const valid_ctx& v, std::string_view t)
{
    return v.s.pattern.matches(t) && t[8] == sg_letter(t.substr(0, 8));
}

// ---- Thailand ----

[[nodiscard]] inline char th_check(std::string_view twelve)
{
    static constexpr std::array<unsigned, 12> w{13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};
    return dig((11U - sum_mod(twelve, w, 11)) % 10U);
}

inline bool build_th_pin(build_ctx& b, std::string& out)
{
    // Type 1 for births registered on time since 1984, 3 for people registered before.
    const char type = to_day(b.date) >= b.s.from ? '1' : '3';
    out = std::string{type} + b.place + rand_range(b.r, 1, 49, 2);
    out += rand_digits(b.r, 7);
    const char c = th_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_th_pin(const valid_ctx& v, std::string_view t)
{
    return v.s.pattern.matches(t) && t[12] == th_check(t.substr(0, 12));
}

// ---- Turkey ----

[[nodiscard]] inline std::pair<char, char> tr_checks(std::string_view nine)
{
    int odd = 0;
    int even = 0;
    for (std::size_t i = 0; i < 9; ++i) (i % 2U == 0U ? odd : even) += nine[i] - '0';
    const int c1 = (((7 * odd) - even) % 10 + 10) % 10;
    return {static_cast<char>('0' + c1), static_cast<char>('0' + ((odd + even + c1) % 10))};
}

inline bool build_tr_tckn(build_ctx& b, std::string& out)
{
    out = rand_range(b.r, 100000000, 999999999, 9);
    const auto [c1, c2] = tr_checks(out);
    out += c1;
    put_check(b, out, c2);
    return true;
}

inline bool valid_tr_tckn(const valid_ctx& v, std::string_view t)
{
    if (!v.s.pattern.matches(t)) return false;
    const auto [c1, c2] = tr_checks(t.substr(0, 9));
    return t[9] == c1 && t[10] == c2;
}

// ---- Taiwan ----

[[nodiscard]] inline char tw_check(std::string_view nine)
{
    static constexpr std::string_view order = "ABCDEFGHJKLMNPQRSTUVXYWZIO"; // from 10 up
    const auto code = static_cast<unsigned>(order.find(nine[0])) + 10U;
    unsigned sum = (code / 10U) + ((code % 10U) * 9U);
    for (std::size_t i = 1; i < 9; ++i) sum += static_cast<unsigned>(nine[i] - '0') * (9U - static_cast<unsigned>(i));
    return dig((10U - (sum % 10U)) % 10U);
}

inline bool build_tw_nid(build_ctx& b, std::string& out)
{
    out = b.place + (b.male() ? "1" : "2") + rand_range(b.r, 0, 5, 1);
    out += rand_digits(b.r, 6);
    const char c = tw_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_tw_nid(const valid_ctx& v, std::string_view t)
{
    return v.s.pattern.matches(t) && t[9] == tw_check(t.substr(0, 9));
}

// ---- Ukraine ----

[[nodiscard]] inline char ua_check(std::string_view nine)
{
    static constexpr std::array<int, 9> w{-1, 5, 7, 9, 4, 6, 10, 5, 7};
    int sum = 0;
    for (std::size_t i = 0; i < 9; ++i) sum += (nine[i] - '0') * w.at(i);
    return static_cast<char>('0' + ((((sum % 11) + 11) % 11) % 10));
}

inline bool build_ua_rnokpp(build_ctx& b, std::string& out)
{
    const int count = to_day(b.date) - to_day(civil{.y = 1899, .m = 12, .d = 31});
    out = num(static_cast<std::uint64_t>(count), 5) + rand_digits(b.r, 3);
    out += parity_digit(b.r, b.male());
    const char c = ua_check(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_ua_rnokpp(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 10 || !only_digits(t)) return false;
    const int count = at(t, 0, 5);
    if (count == 0) return false;
    if (v.reference && to_day(civil{.y = 1899, .m = 12, .d = 31}) + count > *v.reference) return false;
    return t[9] == ua_check(t.substr(0, 9));
}

// ---- United States ----

[[nodiscard]] inline unsigned us_group_at(int place)
{
    // The SSA's issuing order: odd 01-09, even 10-98, even 02-08, odd 11-99.
    if (place <= 5) return static_cast<unsigned>((place * 2) - 1);
    if (place <= 50) return static_cast<unsigned>(10 + ((place - 6) * 2));
    if (place <= 54) return static_cast<unsigned>(2 + ((place - 51) * 2));
    return static_cast<unsigned>(11 + ((place - 55) * 2));
}

inline bool build_us_ssn(build_ctx& b, std::string& out)
{
    if (b.reserved_range && b.s.safe_pattern)
    {
        out = b.s.safe_pattern->draw(b.r);
        b.place.clear();
        return true;
    }
    const std::string serial = rand_range(b.r, 1, 9999, 4);
    if (b.assigned >= b.s.param_day("random_from"))
    {
        // Random over all areas since 2011-06-25, encoding nothing.
        const std::uint64_t area = core::uniform_int<std::uint64_t>(b.r, 1, 899);
        if (area == 666) return false;
        out = num(area, 3) + rand_range(b.r, 1, 99, 2) + serial;
        b.place.clear();
        return true;
    }
    const std::int64_t when = (b.assigned * seconds_per_day) + core::uniform_int<std::int64_t>(b.r, -7 * seconds_per_day, 7 * seconds_per_day);
    const auto* a = b.s.find_series(b.place);
    if (a != nullptr && when < a->front().t)
    {
        // An area not open yet: another area of the same state.
        const auto [lo, hi] = b.s.places.code_range(b.place);
        if (lo != hi)
        {
            const auto& state = b.s.places.rows[b.s.places.by_code[lo]].admin1;
            const auto it = b.s.places.by_admin1.find(state);
            if (it != b.s.places.by_admin1.end())
            {
                std::vector<const place_row*> open;
                for (const auto i : it->second)
                {
                    const auto& row = b.s.places.rows[i];
                    const auto* other = b.s.find_series(row.code);
                    if (other != nullptr && other->front().t <= when && row.weight > 0.0) open.push_back(&row);
                }
                if (!open.empty())
                {
                    b.place = open[core::bounded(b.r, open.size())]->code;
                    a = b.s.find_series(b.place);
                }
            }
        }
    }
    const int place = a == nullptr ? 1 : static_cast<int>(std::clamp<std::int64_t>(series_at(*a, when), 1, 99));
    out = b.place + num(us_group_at(place), 2) + serial;
    return true;
}

inline bool valid_us_ssn(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 9 || !only_digits(t)) return false;
    if (v.s.safe_pattern && v.s.safe_pattern->matches(t)) return true;
    const int area = at(t, 0, 3);
    return area != 0 && area != 666 && area < 900 && at(t, 3, 2) != 0 && at(t, 5, 4) != 0;
}

// ---- Vietnam ----

inline bool build_vn_cccd(build_ctx& b, std::string& out)
{
    const int century = (b.date.y / 100) - 19; // 1900s: 0, 2000s: 1, ...
    out = b.place + dig(static_cast<unsigned>((century * 2) + (b.male() ? 0 : 1))) + two(b.date.y) + rand_digits(b.r, 6);
    return true;
}

inline bool valid_vn_cccd(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 12 || !only_digits(t)) return false;
    const int year = 1900 + (((t[3] - '0') / 2) * 100) + at(t, 4, 2);
    return !v.reference || year <= static_cast<int>(ymd_of(*v.reference).year());
}

// ---- South Africa ----

inline bool build_za_idnr(build_ctx& b, std::string& out)
{
    out = two(b.date.y) + two(b.date.m) + two(b.date.d) + (b.male() ? rand_range(b.r, 5000, 9999, 4) : rand_range(b.r, 0, 4999, 4)) + "08";
    const char c = core::luhn_digit(out);
    put_check(b, out, c);
    return true;
}

inline bool valid_za_idnr(const valid_ctx& v, std::string_view t)
{
    if (t.size() != 13 || !only_digits(t) || t[10] > '2') return false;
    return v.date_2d(at(t, 0, 2), at(t, 2, 2), at(t, 4, 2)) && core::luhn_valid(t);
}

// -- The builder table -----------------------------------------------------------

struct builder_entry
{
    std::string_view name;
    build_fn build;
    valid_fn valid;
};

inline constexpr std::array<builder_entry, 50> builders{{
    {"generic", build_generic, valid_generic},
    {"generic_dated", build_generic, valid_generic},
    {"ar_cuil", build_ar_cuil, valid_ar_cuil},
    {"ar_dni", build_ar_dni, valid_ar_dni},
    {"at_svnr", build_at_svnr, valid_at_svnr},
    {"baltic_code", build_baltic_code, valid_baltic_code},
    {"be_nn", build_be_nn, valid_be_nn},
    {"bg_egn", build_bg_egn, valid_bg_egn},
    {"br_cpf", build_br_cpf, valid_br_cpf},
    {"ca_sin", build_ca_sin, valid_luhn_pattern},
    {"cl_run", build_cl_run, valid_cl_run},
    {"cn_ric", build_cn_ric, valid_cn_ric},
    {"de_idnr", build_de_idnr, valid_de_idnr},
    {"dk_cpr", build_dk_cpr, valid_dk_cpr},
    {"ec_cedula", build_ec_cedula, valid_luhn_pattern},
    {"es_dni", build_es_dni, valid_es_dni},
    {"fi_hetu", build_fi_hetu, valid_fi_hetu},
    {"fr_nir", build_fr_nir, valid_fr_nir},
    {"gb_nino", build_gb_nino, valid_gb_nino},
    {"hk_hkid", build_hk_hkid, valid_hk_hkid},
    {"id_nik", build_id_nik, valid_id_nik},
    {"ie_ppsn", build_ie_ppsn, valid_ie_ppsn},
    {"ir_code", build_ir_code, valid_ir_code},
    {"is_kennitala", build_is_kennitala, valid_is_kennitala},
    {"it_cf", build_it_cf, valid_it_cf},
    {"jmbg", build_jmbg, valid_jmbg},
    {"kr_rrn", build_kr_rrn, valid_kr_rrn},
    {"kz_iin", build_kz_iin, valid_kz_iin},
    {"lv_code", build_lv_code, valid_lv_code},
    {"mx_curp", build_mx_curp, valid_mx_curp},
    {"my_mykad", build_my_mykad, valid_my_mykad},
    {"no_fnr", build_no_fnr, valid_no_fnr},
    {"nz_ird", build_nz_ird, valid_nz_ird},
    {"pe_dni", build_pe_dni, valid_pe_dni},
    {"pk_cnic", build_pk_cnic, valid_pk_cnic},
    {"pl_pesel", build_pl_pesel, valid_pl_pesel},
    {"pt_nif", build_pt_nif, valid_pt_nif},
    {"rc", build_rc, valid_rc},
    {"ro_cnp", build_ro_cnp, valid_ro_cnp},
    {"ru_inn", build_ru_inn, valid_ru_inn},
    {"ru_snils", build_ru_snils, valid_ru_snils},
    {"se_personnummer", build_se_personnummer, valid_se_personnummer},
    {"sg_nric", build_sg_nric, valid_sg_nric},
    {"th_pin", build_th_pin, valid_th_pin},
    {"tr_tckn", build_tr_tckn, valid_tr_tckn},
    {"tw_nid", build_tw_nid, valid_tw_nid},
    {"ua_rnokpp", build_ua_rnokpp, valid_ua_rnokpp},
    {"us_ssn", build_us_ssn, valid_us_ssn},
    {"vn_cccd", build_vn_cccd, valid_vn_cccd},
    {"za_idnr", build_za_idnr, valid_za_idnr},
}};

// The display form: the mask filled from the right, so a shorter number drops
// its leading groups ("##.###.###" shows 7 digits as "1.234.567").
[[nodiscard]] inline std::string apply_display(const scheme& s, std::string_view value)
{
    if (s.builder == "rc" && value.size() == 9) return rc_display(value);
    if (s.display.empty()) return std::string{value};
    std::string reversed;
    std::size_t left = value.size();
    for (auto it = s.display.rbegin(); it != s.display.rend() && left > 0; ++it)
    {
        reversed += *it == '#' ? value[--left] : *it;
    }
    while (left > 0) reversed += value[--left];
    std::ranges::reverse(reversed);
    return reversed;
}

// The electronic form of a number as text: the display mask's separators removed.
[[nodiscard]] inline std::string strip_display(const scheme& s, std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (c != '#' && s.display.find(c) != std::string::npos) continue;
        out += c;
    }
    return out;
}

// -- Documents ------------------------------------------------------------------

enum class expiry_rule : std::uint8_t { day_before, same_day, birthday, application };
enum class mrz_type : std::uint8_t { none, td1, td3 };

// One row of validity.tsv: how long a document issued at this age lasts.
struct validity_rule
{
    int from_age{0};
    std::vector<int> years{}; // the holder's choice when several; {0}: no expiry
    int until_age{-1};      // set instead of years
    day_t from{no_start};
    day_t until{no_end};
    expiry_rule rule{expiry_rule::same_day};
};

// One row of documents.tsv: a number format and the issue dates it covers.
struct document_format
{
    std::string key;
    core::pattern pattern;
    day_t from{no_start};
    day_t until{no_end};
    mrz_type mrz{mrz_type::none};
    std::string evidence;
    std::string prefix_scheme;  // the scheme whose place codes start the number
    std::string mrz_code;
    std::string optional_scheme; // the scheme whose number fills the MRZ's optional field
    int min_age{0};
    double weight{1.0};
    int year_at{-1};            // where the two-digit year of issue sits in the number
    std::string number_scheme;  // the scheme whose number is the document's number
};

// A country's document of one kind.
struct document_book
{
    std::vector<document_format> formats; // by `from`
    std::vector<validity_rule> rules;     // by `from_age`
    bool generic_validity{false};
    int min_age{0};
    int window_years{0};  // no document in force was issued longer ago than this
    bool permanent{false}; // some rule has no expiry, so the window has no start
};

// A country's variant of the ICAO tables: `first` applies to a name's first letter only.
struct translit_rule
{
    std::uint32_t cp{0};
    bool first{false};
    std::string to;
};

// The upper case of a Latin-1 or Cyrillic letter, as translit rules name it.
[[nodiscard]] inline std::uint32_t letter_upper(std::uint32_t cp)
{
    if (cp >= 0x430U && cp <= 0x44FU) return cp - 0x20U;
    if (cp >= 0x450U && cp <= 0x45FU) return cp - 0x50U;
    if (cp == 0x46BU || cp == 0x475U || cp == 0x491U || cp == 0x493U || cp == 0x4BBU) return cp - 1U;
    if (cp >= 0xE0U && cp <= 0xFEU && cp != 0xF7U) return cp - 0x20U;
    return cp;
}

// A letter as ICAO 9303 Part 3 section 6 writes it in the MRZ, for the upper or
// lower case. Where the table offers alternatives (Ä: AE or A) this is the first.
// Empty: the character has no form. The table omits the soft sign, which is
// written as nothing, hence the optional.
[[nodiscard]] inline std::optional<std::string_view> icao_letter(std::uint32_t cp)
{
    switch (cp)
    {
    case 0xC4U: case 0xE4U: case 0xC6U: case 0xE6U: return "AE";
    case 0xC5U: case 0xE5U: return "AA";
    case 0xD6U: case 0xF6U: case 0xD8U: case 0xF8U: case 0x152U: case 0x153U: return "OE";
    case 0xDCU: case 0xFCU: return "UE";
    case 0x132U: case 0x133U: return "IJ";
    case 0x1E9EU: return "SS";
    // Not in the table: Romanian's comma letters and Vietnamese's horned ones.
    case 0x218U: case 0x219U: return "S";
    case 0x21AU: case 0x21BU: return "T";
    case 0x1A0U: case 0x1A1U: return "O";
    case 0x1AFU: case 0x1B0U: return "U";
    default: break;
    }
    if (cp >= 0xC0U && cp <= 0x17FU)
    {
        const std::string_view base = fold_latin(cp);
        if (!base.empty()) return base;
        return std::nullopt;
    }
    // Vietnamese, by runs of U+1EA0-U+1EF9; not in the table either.
    if (cp >= 0x1EA0U && cp <= 0x1EF9U)
    {
        if (cp <= 0x1EB7U) return "A";
        if (cp <= 0x1EC7U) return "E";
        if (cp <= 0x1ECBU) return "I";
        if (cp <= 0x1EE3U) return "O";
        if (cp <= 0x1EF1U) return "U";
        return "Y";
    }
    cp = letter_upper(cp); // Cyrillic from here on
    static constexpr std::array<std::string_view, 32> basic{
        "A", "B", "V", "G", "D", "E", "ZH", "Z", "I", "I", "K", "L", "M", "N", "O", "P",
        "R", "S", "T", "U", "F", "KH", "TS", "CH", "SH", "SHCH", "IE", "Y", "", "E", "IU", "IA"};
    if (cp >= 0x410U && cp <= 0x42FU) return basic.at(cp - 0x410U);
    switch (cp)
    {
    case 0x401U: return "E";
    case 0x402U: return "D";
    case 0x403U: return "G"; // the table's row at U+0492, whose exception is Macedonian
    case 0x404U: return "IE";
    case 0x405U: return "DZ";
    case 0x406U: case 0x407U: return "I";
    case 0x408U: return "J";
    case 0x409U: return "LJ";
    case 0x40AU: return "NJ";
    case 0x40BU: return "C"; // not in the table; the same as Serbian Ч
    case 0x40CU: return "K";
    case 0x40EU: case 0x46AU: return "U";
    case 0x40FU: return "DZ";
    case 0x474U: return "Y";
    case 0x490U: case 0x492U: return "G";
    case 0x4BAU: return "C";
    default: return std::nullopt;
    }
}

// A name in the MRZ's alphabet: words joined by '<'. Spaces and hyphens separate;
// apostrophes, periods, numerals and other ASCII marks are left out (Part 3, 4.6
// and Part 4, 4.2.3.1). Empty when a character has no form or nothing is left.
[[nodiscard]] inline std::optional<std::string> mrz_words(std::string_view name, const std::vector<translit_rule>& rules)
{
    std::string out;
    bool first = true;
    std::size_t i = 0;
    while (i < name.size())
    {
        const auto cp_opt = next_code_point(name, i);
        if (!cp_opt) return std::nullopt;
        const std::uint32_t cp = *cp_opt;
        if (cp < 0x80U)
        {
            const char c = static_cast<char>(cp);
            if (c == ' ' || c == '\t' || c == '-')
            {
                if (!out.empty() && out.back() != '<') out += '<';
                first = true;
            }
            else if (c >= 'A' && c <= 'Z') { out += c; first = false; }
            else if (c >= 'a' && c <= 'z') { out += static_cast<char>(c - 'a' + 'A'); first = false; }
            continue;
        }
        if (cp == 0x2019U || cp == 0x2BBU || cp == 0x2BCU) continue; // typographic apostrophes
        const std::uint32_t up = letter_upper(cp);
        const auto rule = std::ranges::find_if(rules, [&](const translit_rule& t) { return t.cp == up && (!t.first || first); });
        if (rule != rules.end())
        {
            out += rule->to;
        }
        else
        {
            const auto letter = icao_letter(cp);
            if (!letter) return std::nullopt;
            out += *letter;
        }
        first = false;
    }
    while (!out.empty() && out.back() == '<') out.pop_back();
    if (out.empty()) return std::nullopt;
    return out;
}

// The last character of a shortened name must be a letter (Part 4, 4.2.3). When
// the cut lands on a filler, the next word's initial takes that place and the
// word before gives up a letter, so the words stay apart.
[[nodiscard]] inline std::string cut_words(std::string_view words, std::size_t width)
{
    std::string out{words.substr(0, width)};
    if (out.size() >= 2 && out.back() == '<' && width < words.size())
    {
        out[out.size() - 2] = '<';
        out.back() = words[width];
    }
    return out;
}

// The name field: primary<<secondary, shortened as Part 4, 4.2.3.2 and 4.2.3.3 show,
// and filled. The primary identifier gives way only as far as it must, and leaves
// two letters of the secondary one.
[[nodiscard]] inline std::string mrz_name(std::string_view primary, std::string_view secondary, std::size_t width)
{
    std::string out;
    if (secondary.empty())
    {
        out = cut_words(primary, width);
    }
    else if (primary.size() + 2U + secondary.size() <= width)
    {
        out = std::string{primary} + "<<" + std::string{secondary};
    }
    else
    {
        const std::size_t leave = 2U + std::min<std::size_t>(2U, secondary.size());
        std::string head = primary.size() + leave > width ? cut_words(primary, width - leave) : std::string{primary};
        const std::size_t room = width - head.size() - 2U;
        out = head + "<<" + cut_words(secondary, room);
    }
    out.resize(width, '<');
    return out;
}

// What goes into a machine-readable zone. Dates are YYMMDD; a missing expiry is
// six fillers. `name` is the fitted name field.
struct mrz_fields
{
    std::string code;
    std::string state;
    std::string name;
    std::string number; // letters and digits; over nine, the rest overflows
    std::string birth;
    char sex{'<'};
    std::string expiry;
    std::string optional;
};

[[nodiscard]] inline std::string filled(std::string_view text, std::size_t width)
{
    std::string out{text};
    out.resize(width, '<');
    return out;
}

// The number's field and check digit, and what overflows: the rest of the number,
// the digit over all of it, and a filler (Part 5, 4.2.2, note j).
struct mrz_number
{
    std::string field;
    char check{'<'};
    std::string overflow;
};

[[nodiscard]] inline mrz_number mrz_number_of(std::string_view number)
{
    if (number.size() <= 9U)
    {
        std::string field = filled(number, 9U);
        const char check = core::icao9303_digit(field);
        return {.field = std::move(field), .check = check, .overflow = {}};
    }
    std::string overflow{number.substr(9)};
    overflow += core::icao9303_digit(number);
    overflow += '<';
    return {.field = std::string{number.substr(0, 9)}, .check = '<', .overflow = std::move(overflow)};
}

// TD3, ICAO 9303 Part 4: 2 lines of 44. The optional field has no room for an
// overflow in the standard, so one takes the front of it, as in TD1.
[[nodiscard]] inline std::vector<std::string> mrz_td3(const mrz_fields& f)
{
    constexpr std::size_t optional_width = 14;
    const mrz_number n = mrz_number_of(f.number);
    std::string optional = n.overflow;
    if (optional.size() + f.optional.size() <= optional_width) optional += f.optional;
    optional = filled(optional, optional_width);

    std::string line1 = filled(f.code, 2U) + filled(f.state, 3U) + f.name;
    std::string line2 = n.field;
    line2 += n.check;
    line2 += filled(f.state, 3U);
    line2 += f.birth;
    line2 += core::icao9303_digit(f.birth);
    line2 += f.sex;
    line2 += f.expiry;
    line2 += core::icao9303_digit(f.expiry);
    line2 += optional;
    line2 += core::icao9303_digit(optional);
    const std::string composite = line2.substr(0, 10) + line2.substr(13, 7) + line2.substr(21, 22);
    line2 += core::icao9303_digit(composite);
    return {std::move(line1), std::move(line2)};
}

// TD1, ICAO 9303 Part 5: 3 lines of 30. The optional number takes the upper line's
// field after any overflow, and the middle line's when it does not fit there.
[[nodiscard]] inline std::vector<std::string> mrz_td1(const mrz_fields& f)
{
    constexpr std::size_t upper_width = 15;
    constexpr std::size_t middle_width = 11;
    const mrz_number n = mrz_number_of(f.number);
    std::string upper_optional = n.overflow;
    std::string middle_optional;
    if (upper_optional.size() + f.optional.size() <= upper_width) upper_optional += f.optional;
    else if (f.optional.size() <= middle_width) middle_optional = f.optional;

    std::string upper = filled(f.code, 2U) + filled(f.state, 3U) + n.field;
    upper += n.check;
    upper += filled(upper_optional, upper_width);
    std::string middle = f.birth;
    middle += core::icao9303_digit(f.birth);
    middle += f.sex;
    middle += f.expiry;
    middle += core::icao9303_digit(f.expiry);
    middle += filled(f.state, 3U);
    middle += filled(middle_optional, middle_width);
    const std::string composite = upper.substr(5) + middle.substr(0, 7) + middle.substr(8, 7) + middle.substr(18, 11);
    middle += core::icao9303_digit(composite);
    return {std::move(upper), std::move(middle), f.name};
}

// Letters and digits only, for the MRZ.
[[nodiscard]] inline std::string alphanumeric(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
        else if (c >= 'a' && c <= 'z') out += static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

// A national number as a document prints it: the form the format's pattern takes,
// or its first characters where the card leaves the check digit apart (Peru). A
// number the pattern does not foresee, such as an old 7-digit DNI, stays as it is.
[[nodiscard]] inline std::string printed_number(const core::pattern& pattern, const std::string& value, const std::string& formatted)
{
    if (pattern.matches(formatted)) return formatted;
    if (pattern.matches(value)) return value;
    const std::string head = value.substr(0, pattern.lengths().second);
    return pattern.matches(head) ? head : value;
}

// A number outside every format of the country's document, for safe mode: the
// first character changed, else the number shortened where the formats take any
// character there. Draws nothing.
[[nodiscard]] inline std::string outside_formats(const document_book& book, std::string number)
{
    const auto fits = [&](std::string_view text) {
        return std::ranges::any_of(book.formats, [&](const document_format& f) { return f.pattern.matches(text); });
    };
    for (const char c : {'X', '0'})
    {
        std::string changed = number;
        changed.front() = c;
        if (!fits(changed)) return changed;
    }
    while (number.size() > 1U && fits(number)) number.pop_back();
    return number;
}

} // namespace idgen_detail
/// @endcond

/// @brief Return type for identity generation.
///
/// Supports implicit conversion to std::string (the summary), streaming via
/// operator<< and JSON output via to_json().
class identity
{
  public:
    std::string country_code;              ///< ISO alpha-2 code.
    std::chrono::year_month_day birth_date{}; ///< Echoed, given or drawn.
    sex bio_sex{sex::male};                ///< Echoed, given or drawn.
    std::vector<id_number> numbers;        ///< The numbers held, primary first.
    std::vector<id_document> documents;    ///< The documents held, as requested.

    /// @brief Retrieve the random seed used to generate this identity.
    /// @return The call seed; pass it to get_identity(request, seed) with
    ///         the same request and data to replay this identity.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this identity was generated from.
    /// @return The replay signature; `data` is idg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const
    {
        return _signature;
    }

    /// @brief Human-readable summary string.
    /// @return For example `BR, Individual Taxpayer Registry number 111.444.777-35,
    ///         passport GA123456 (valid to 2031-05-03)`.
    [[nodiscard]] std::string to_string() const
    {
        std::string out = country_code;
        for (const auto& n : numbers) out += ", " + n.label + " " + n.formatted;
        for (const auto& d : documents)
        {
            out += d.kind == id_document_kind::passport ? ", passport " : ", identity card ";
            out += d.number;
            out += d.expires ? " (valid to " + idgen_detail::iso_date(idgen_detail::to_day(*d.expires)) + ")" : " (no expiry)";
        }
        return out;
    }

    /// @brief Write this identity as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("country_code", country_code)
            .field("birth_date", idgen_detail::iso_date(idgen_detail::to_day(birth_date)))
            .field("sex", core::sex_name(bio_sex))
            .key("numbers")
            .begin_array();
        for (const auto& n : numbers)
        {
            w.begin_object()
                .field("scheme", n.scheme)
                .field("label", n.label)
                .field("native_label", n.native_label)
                .field("value", n.value)
                .field("formatted", n.formatted)
                .field("assigned", idgen_detail::iso_date(idgen_detail::to_day(n.assigned)))
                .field("place_code", n.place_code)
                .field("reserved", n.reserved)
                .end_object();
        }
        w.end_array().key("documents").begin_array();
        for (const auto& d : documents)
        {
            w.begin_object()
                .field("kind", d.kind == id_document_kind::passport ? "passport" : "id_card")
                .field("format", d.format)
                .field("number", d.number)
                .field("issuing_state", d.issuing_state)
                .field("issued", idgen_detail::iso_date(idgen_detail::to_day(d.issued)))
                .key("expires");
            if (d.expires) w.value(idgen_detail::iso_date(idgen_detail::to_day(*d.expires)));
            else w.value(nullptr);
            w.key("mrz").begin_array();
            for (const auto& line : d.mrz) w.value(line);
            w.end_array()
                .field("evidence", d.evidence)
                .field("generic_validity", d.generic_validity)
                .field("reserved", d.reserved)
                .end_object();
        }
        w.end_array().key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This identity as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return Summary string.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return to_string();
    }

    /// @brief Stream the summary to an output stream.
    /// @param os Output stream.
    /// @param v The identity to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const identity& v)
    {
        os << v.to_string();
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class idg;
};

/// @brief ID Generator: national numbers that fit a person and a date.
///
/// The call seed splits into `"country"`, `"birth_date"`, `"sex"`,
/// `"birthplace"`, `"residence"` and one stream per scheme
/// (`"scheme:br-cpf"`). A given field skips its draw and leaves every other
/// stream unchanged, and asking for a second scheme adds output without moving
/// the first. Output is identical on every compiler and platform (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed independently.
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to get_identity(request) on
/// the **same** instance require external synchronisation; the seeded
/// overloads and the queries do not.
class idg
{
  public:
    /// @brief Default constructor — creates an empty generator with no data.
    idg() = default;

    idg(const idg&) = delete;            ///< Not copyable.
    idg& operator=(const idg&) = delete; ///< Not copyable.
    idg(idg&&) noexcept = default;       ///< Movable.
    idg& operator=(idg&&) noexcept = default; ///< Movable.
    ~idg() = default;                    ///< Destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return The singleton.
    static idg& instance()
    {
        static idg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random identity.
    ///        Use identity_request{} for the defaults.
    /// @param request What to generate.
    /// @return The identity.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request is invalid (see the
    ///         seeded overload).
    [[nodiscard]] identity get_identity(const identity_request& request)
    {
        return get_identity(request, draw_seed_());
    }

    /// @brief Generate a deterministic identity.
    ///
    /// A number the person cannot hold yet (assigned after the reference date)
    /// is left out, as is the primary number of a country without one, a card
    /// below its minimum age, and a card where none is issued.
    ///
    /// @param request What to generate.
    /// @param call_seed Seed; the same request, seed and data give the same
    ///        result. Does not touch the instance engine.
    /// @return The identity.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument For an unknown country; an unknown scheme
    ///         or a scheme of another country; a birth date after the reference
    ///         date or before 1900; an invalid reference date; a birthplace in
    ///         another country when the scheme has no code for it.
    [[nodiscard]] identity get_identity(const identity_request& request, std::uint64_t call_seed) const
    {
        using namespace idgen_detail;
        require_data_();
        const core::rng call{call_seed};
        const day_t reference = reference_of_(request);
        const auto selection = select_country_(request, call);
        const day_t birth = birth_of_(request, reference, call);
        const sex bio_sex = request.sex ? *request.sex : sex_draw_(call);

        identity v;
        v._signature = {.seed = call_seed, .data = data_hash()};
        v.country_code = _model.countries[selection.country].code;
        v.birth_date = ymd_of(birth);
        v.bio_sex = bio_sex;
        for (const auto index : selection.schemes)
        {
            auto number = make_number_(_model.schemes[index], request, birth, bio_sex, reference, call);
            if (number) v.numbers.push_back(std::move(*number));
        }
        for (auto kind = request.documents.begin(); kind != request.documents.end(); ++kind)
        {
            if (std::find(request.documents.begin(), kind, *kind) != kind) continue; // asked for twice
            auto document = make_document_(*kind, _model.countries[selection.country], request, birth, bio_sex, reference, call);
            if (document) v.documents.push_back(std::move(*document));
        }
        return v;
    }

    // -- Batch generation -------------------------------------------------

    /// @brief Generate a batch of identity results from a seed drawn like
    ///        get_identity() draws its own.
    ///
    /// Item `i` is `get_identity(request, core::item_seed(batch_seed, options.first + i))`
    /// for the drawn batch seed; see the seeded overload.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<identity> get_identity_batch(const identity_request& request,
                                                  const core::batch_options& options)
    {
        return get_identity_batch(request, options, draw_seed_());
    }

    /// @brief Generate a batch of identity results, deterministically.
    ///
    /// Item `i` is exactly `get_identity(request, core::item_seed(batch_seed,
    /// options.first + i))`, so an item replays on its own, a range can be
    /// generated directly, two batches that follow each other equal one, and
    /// the output does not depend on `options.threads`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (see
    ///        core::unique_rule; the rule must be made for `identity`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<identity> get_identity_batch(const identity_request& request,
                                                  const core::batch_options& options,
                                                  std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_identity(request, item_seed); });
    }

    /// @brief Stream a batch of identity results to a sink without holding the
    ///        whole batch.
    ///
    /// Generates the same items as get_identity_batch() and calls
    /// `sink(index, item)` in index order on the calling thread; `index`
    /// includes `options.first`.
    ///
    /// @tparam Sink Callable taking `(std::uint64_t, identity&)`; for example
    ///         core::jsonl_writer or core::csv_writer.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_identity(const identity_request& request, const core::batch_options& options,
                        std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_identity(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of identity results with a batch seed drawn like
    ///        get_identity() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, identity&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_identity(const identity_request& request, const core::batch_options& options,
                        Sink&& sink)
    {
        for_each_identity(request, options, draw_seed_(), std::forward<Sink>(sink));
    }

    // -- Queries ----------------------------------------------------------

    /// @brief Every country in the data.
    /// @return Alpha-2 codes in data order.
    [[nodiscard]] std::vector<std::string> countries() const
    {
        std::vector<std::string> out;
        out.reserve(_model.countries.size());
        for (const auto& c : _model.countries) out.push_back(c.code);
        return out;
    }

    /// @brief The countries with a primary number scheme.
    /// @return Alpha-2 codes in data order.
    [[nodiscard]] std::vector<std::string> number_countries() const
    {
        std::vector<std::string> out;
        for (const auto& c : _model.countries)
        {
            if (c.has_primary) out.push_back(c.code);
        }
        return out;
    }

    /// @brief The number schemes of a country.
    /// @param country Alpha-2 code, any case.
    /// @return Scheme keys, primary first; empty for a country without any.
    /// @throws std::invalid_argument If the country is unknown.
    [[nodiscard]] std::vector<std::string> schemes(std::string_view country) const
    {
        std::vector<std::string> out;
        for (const auto index : _model.countries[country_index_(country)].schemes)
        {
            out.push_back(_model.schemes[index].key);
        }
        return out;
    }

    /// @brief Whether a text is a well-formed number of a scheme: its structure,
    ///        check digit and encoded date.
    ///
    /// Takes the electronic or the display form. It looks nothing up, and cannot
    /// tell whether a number is in use. A scheme's published test numbers and
    /// reserved ranges count as well-formed; a safe number made with a wrong
    /// check digit does not, nor, when @p reference_date is given, does one
    /// whose encoded birth date is after it.
    ///
    /// @param scheme Scheme key.
    /// @param text The number.
    /// @param reference_date When given, a birth date after it is not valid.
    /// @return Whether the text is valid.
    /// @throws std::invalid_argument If the scheme is unknown.
    [[nodiscard]] bool is_valid(std::string_view scheme, std::string_view text,
                                std::optional<std::chrono::year_month_day> reference_date = std::nullopt) const
    {
        using namespace idgen_detail;
        const auto& s = scheme_or_throw_(scheme);
        const std::string plain = strip_display(s, text);
        if (std::ranges::binary_search(s.test_numbers, plain)) return true;
        std::optional<day_t> reference;
        if (reference_date) reference = to_day(*reference_date);
        return s.valid(valid_ctx{.s = s, .reference = reference}, plain);
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    /// @return True when countries and schemes are loaded.
    [[nodiscard]] bool has_data() const { return !_model.countries.empty() && !_model.schemes.empty(); }

    /// @brief Load data from a resource directory, replacing all data and
    ///        layers loaded before.
    ///
    /// The directory must contain `countries.tsv` and `schemes.tsv`; the other
    /// tables are read when present. A path without them is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) ||
            !std::filesystem::is_regular_file(dir / file_name_(id_table::countries), ec) ||
            !std::filesystem::is_regular_file(dir / file_name_(id_table::schemes), ec))
        {
            return;
        }
        apply_(read_directory_(dir), core::layer_mode::replace, true);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_ID_EMBED`, otherwise searches `resources/`, `../resources`,
    /// `id-generator/resources` and `../id-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        const auto marker = tier_dir + "/" + std::string{file_name_(id_table::countries)};
        const auto root = core::find_resources(library_name_, marker);
        documents docs;
        for (const auto t : all_tables_)
        {
            docs.at(index_(t)) = core::load_resource(tier_dir + "/" + std::string{file_name_(t)}, embedded_files_(), root);
        }
        if (!docs.at(index_(id_table::countries)) || !docs.at(index_(id_table::schemes))) return false;
        apply_(std::move(docs), core::layer_mode::replace, true);
        return true;
    }

    /// @brief Add data layers from a directory or a file.
    ///
    /// A directory may hold any of the table files; all are applied together
    /// or, if any fails, none. A file must have one of those names.
    ///
    /// @param path Directory or file.
    /// @param mode How the layers combine with the data loaded before.
    /// @throws core::tsv_error If a file cannot be read or a row is invalid;
    ///         the data is then unchanged.
    /// @throws std::invalid_argument If @p path is a file with another name,
    ///         or the tables do not fit together.
    void extend(const std::filesystem::path& path, core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        for (const auto t : all_tables_)
        {
            if (path.filename() == file_name_(t))
            {
                (void)extend(t, core::tsv_document::load(path), mode);
                return;
            }
        }
        throw std::invalid_argument("extend: expected one of the identity tables (countries.tsv, schemes.tsv, "
                                    "places.tsv, ...), got " + path.filename().string());
    }

    /// @brief Add a data layer to one table.
    ///
    /// Rows are keyed by the table's key columns (see doc/data.md). In the
    /// default override mode a row replaces the loaded row with the same key
    /// and new keys are added; a row with `disabled` set to 1 removes its key.
    ///
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did (rows added, replaced, removed, warnings).
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    /// @throws std::invalid_argument If the tables no longer fit together.
    const core::layer_info& extend(id_table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_(std::move(docs), mode, false);
        return _tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from TSV text (for example from
    ///        a WebAssembly host).
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(id_table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The layers in effect for one table, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(id_table which) const
    {
        return _tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded data, recorded in each result's signature().
    /// @return A hash that changes when any table does; 0 when nothing is
    ///         loaded.
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t h = 0;
        bool any = false;
        for (const auto& t : _tables)
        {
            any = any || !t.layers().empty();
            h = core::hash_combine(h, t.data_hash());
        }
        return any ? h : 0;
    }

  private:
    using table = id_table;

    // -- Tables -----------------------------------------------------------

    static constexpr std::string_view library_name_ = "id-generator";
    static constexpr std::size_t table_count_ = 9;
    static constexpr std::array<table, table_count_> all_tables_{
        table::countries, table::schemes,  table::places,     table::place_cities, table::series,
        table::documents, table::validity, table::name_rules, table::test_numbers};

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        constexpr std::array<std::string_view, table_count_> names{
            "countries.tsv", "schemes.tsv",  "places.tsv",     "place_cities.tsv", "series.tsv",
            "documents.tsv", "validity.tsv", "name_rules.tsv", "test_numbers.tsv"};
        return names.at(index_(t));
    }

    static core::table_schema schema_(table t)
    {
        switch (t)
        {
        case table::countries:
            return {.key = {"country_code"}, .required = {"population", "icao_code", "primary_scheme"}, .optional = {"translit"}};
        case table::schemes:
            return {.key = {"key"},
                    .required = {"country", "label", "builder", "display", "assigned", "safe_method"},
                    .optional = {"native_label", "pattern", "check", "place_source", "from", "until", "safe_pattern", "params", "source"}};
        case table::places:
            return {.key = {"scheme", "code", "from"},
                    .required = {"label", "weight"},
                    .optional = {"admin1", "country", "until", "reserved"}};
        case table::place_cities: return {.key = {"scheme", "geonameid"}, .required = {"code"}, .optional = {}};
        case table::series: return {.key = {"scheme", "segment", "date"}, .required = {"value"}, .optional = {"note"}};
        case table::documents:
            return {.key = {"key"},
                    .required = {"country", "kind", "pattern", "from", "mrz", "evidence"},
                    .optional = {"until", "prefix_places", "mrz_code", "mrz_optional", "min_age", "weight", "year_at", "number_scheme"}};
        case table::validity:
            return {.key = {"document", "from_age", "from"},
                    .required = {},
                    .optional = {"years", "until_age", "until", "expiry_rule"}};
        case table::name_rules: return {.key = {"scheme", "kind", "word"}, .required = {}, .optional = {"replacement"}};
        case table::test_numbers: return {.key = {"scheme", "value"}, .required = {}, .optional = {}};
        }
        return {};
    }

    static tables make_tables_()
    {
        return {core::layered_table{schema_(table::countries)},    core::layered_table{schema_(table::schemes)},
                core::layered_table{schema_(table::places)},       core::layered_table{schema_(table::place_cities)},
                core::layered_table{schema_(table::series)},       core::layered_table{schema_(table::documents)},
                core::layered_table{schema_(table::validity)},     core::layered_table{schema_(table::name_rules)},
                core::layered_table{schema_(table::test_numbers)}};
    }

    // -- The model built from the tables ------------------------------------

    struct country_entry
    {
        std::string code;
        std::string icao;
        double population{0.0};
        std::vector<std::uint32_t> schemes; // primary first
        bool has_primary{false};
        std::array<std::optional<idgen_detail::document_book>, 2> documents; // by id_document_kind
        std::vector<idgen_detail::translit_rule> translit;
    };

    struct model
    {
        std::vector<country_entry> countries;
        std::vector<idgen_detail::scheme> schemes;
        std::unordered_map<std::string, std::uint32_t> country_index;
        std::unordered_map<std::string, std::uint32_t> scheme_index;
        core::weighted_table<std::uint32_t> all_countries;    // by population
        core::weighted_table<std::uint32_t> served_countries; // those with a primary scheme
    };

    // Cells of optional columns, which a layer may leave out.
    static std::string_view cell_(const core::tsv_row& row, std::string_view column)
    {
        return row.has(column) ? row[column] : std::string_view{};
    }

    static idgen_detail::day_t date_cell_(const core::tsv_row& row, std::string_view column, idgen_detail::day_t missing)
    {
        const auto text = cell_(row, column);
        if (text.empty()) return missing;
        const auto day = idgen_detail::parse_date(text);
        if (!day) throw row.error(column, "expected a date, got '" + std::string{text} + "'");
        return *day;
    }

    static idgen_detail::assigned_rule parse_assigned_(const core::tsv_row& row)
    {
        idgen_detail::assigned_rule rule;
        std::string_view text = row["assigned"];
        for (;;)
        {
            const auto pos = text.find(';');
            const std::string_view token = text.substr(0, pos);
            if (token.starts_with("age:"))
            {
                const auto dash = token.find('-');
                if (dash == std::string_view::npos) throw row.error("assigned", "bad age band");
                rule.age_lo = row_int_(row, "assigned", token.substr(4, dash - 4));
                rule.age_hi = row_int_(row, "assigned", token.substr(dash + 1));
                if (rule.age_hi < rule.age_lo) throw row.error("assigned", "bad age band");
            }
            else if (token.starts_with("lag:"))
            {
                rule.lag = row_int_(row, "assigned", token.substr(4));
            }
            else if (token.starts_with("birth_from:"))
            {
                const auto day = idgen_detail::parse_date(token.substr(11));
                if (!day) throw row.error("assigned", "bad birth_from date");
                rule.birth_from = *day;
            }
            else if (token != "birth")
            {
                throw row.error("assigned", "unknown token '" + std::string{token} + "'");
            }
            if (pos == std::string_view::npos) break;
            text.remove_prefix(pos + 1);
        }
        return rule;
    }

    static int row_int_(const core::tsv_row& row, std::string_view column, std::string_view text)
    {
        int v = 0;
        const auto* end = text.data() + text.size();
        const auto res = std::from_chars(text.data(), end, v);
        if (res.ec != std::errc{} || res.ptr != end || v < 0)
        {
            throw row.error(column, "expected a whole number, got '" + std::string{text} + "'");
        }
        return v;
    }

    static idgen_detail::safe_method parse_safe_(const core::tsv_row& row)
    {
        using m = idgen_detail::safe_method;
        const std::string_view text = row["safe_method"];
        if (text == "test_numbers") return m::test_numbers;
        if (text == "reserved_range") return m::reserved_range;
        if (text == "unissued_series") return m::unissued_series;
        if (text == "unassigned_place") return m::unassigned_place;
        if (text == "wrong_check") return m::wrong_check;
        if (text == "impossible_date") return m::impossible_date;
        throw row.error("safe_method", "unknown method '" + std::string{text} + "'");
    }

    static idgen_detail::check_rule parse_check_(const core::tsv_row& row)
    {
        using k = idgen_detail::check_rule::kind;
        using red = idgen_detail::check_rule::reduce;
        idgen_detail::check_rule c;
        const std::string_view text = cell_(row, "check");
        if (text.empty()) return c;
        if (text == "luhn") c.type = k::luhn;
        else if (text == "mod97_10") c.type = k::mod97_10;
        else if (text == "mod11_2") c.type = k::mod11_2;
        else if (text == "mod11_10") c.type = k::mod11_10;
        else if (text == "verhoeff") c.type = k::verhoeff;
        else if (text == "gtin") c.type = k::gtin;
        else if (text.starts_with("weights:"))
        {
            const std::string_view rest = text.substr(8);
            const auto colon = rest.find(':');
            if (colon == std::string_view::npos) throw row.error("check", "weights need a rule");
            std::string_view list = rest.substr(0, colon);
            const std::string_view rule = rest.substr(colon + 1);
            c.type = k::weights;
            while (!list.empty())
            {
                const auto space = list.find(' ');
                const std::string_view item = list.substr(0, space);
                int v = 0;
                const auto* end = item.data() + item.size();
                const auto res = std::from_chars(item.data(), end, v);
                if (res.ec != std::errc{} || res.ptr != end) throw row.error("check", "bad weight '" + std::string{item} + "'");
                c.weights.push_back(v);
                list = space == std::string_view::npos ? std::string_view{} : list.substr(space + 1);
            }
            if (c.weights.empty()) throw row.error("check", "no weights");
            if (rule == "r") c.rule = red::r;
            else if (rule == "r,10=0") c.rule = red::r_10_0;
            else if (rule == "r,10=1") c.rule = red::r_10_1;
            else if (rule == "r%10") c.rule = red::r_mod_10;
            else if (rule == "10-r") c.rule = red::ten_minus_r;
            else if (rule == "11-r") c.rule = red::eleven_minus_r;
            else if (rule == "11-r,10=0") c.rule = red::eleven_minus_r_10_0;
            else if (rule == "11-r,10=X") c.rule = red::eleven_minus_r_10_x;
            else throw row.error("check", "unknown rule '" + std::string{rule} + "'");
        }
        else
        {
            throw row.error("check", "unknown check '" + std::string{text} + "'");
        }
        return c;
    }

    static std::vector<idgen_detail::dated_segment> parse_dated_(const core::tsv_row& row)
    {
        using seg = idgen_detail::dated_segment;
        std::vector<seg> out;
        std::string_view text = row["pattern"];
        std::string body;
        const auto flush = [&] {
            if (body.empty()) return;
            seg s;
            s.body = core::pattern::parse(body);
            if (s.body.lengths().first != s.body.lengths().second)
            {
                throw row.error("pattern", "a generic_dated pattern must have a fixed length");
            }
            out.push_back(std::move(s));
            body.clear();
        };
        while (!text.empty())
        {
            if (text.size() > 1 && text[0] == '{' && text[1] >= 'A' && text[1] <= 'Z')
            {
                const auto close = text.find('}');
                if (close == std::string_view::npos) throw row.error("pattern", "unclosed token");
                const std::string_view token = text.substr(1, close - 1);
                flush();
                seg s;
                if (token == "YYMMDD") s.type = seg::kind::yymmdd;
                else if (token == "DDMMYY") s.type = seg::kind::ddmmyy;
                else if (token == "YYYY") s.type = seg::kind::yyyy;
                else if (token == "S:odd_male") s.type = seg::kind::sex_odd_male;
                else if (token == "S:even_male") s.type = seg::kind::sex_even_male;
                else if (token.starts_with("C:"))
                {
                    s.type = seg::kind::century;
                    s.table = std::string{token.substr(2)};
                }
                else throw row.error("pattern", "unknown token '" + std::string{token} + "'");
                out.push_back(std::move(s));
                text.remove_prefix(close + 1);
                continue;
            }
            body += text[0];
            text.remove_prefix(1);
        }
        flush();
        return out;
    }

    static std::vector<idgen_detail::param> parse_params_(const core::tsv_row& row)
    {
        std::vector<idgen_detail::param> out;
        for (const auto part : row.list("params"))
        {
            const auto eq = part.find('=');
            if (eq == std::string_view::npos) throw row.error("params", "expected name=value in '" + std::string{part} + "'");
            idgen_detail::param p{.name = std::string{part.substr(0, eq)}, .text = std::string{part.substr(eq + 1)}};
            if (const auto day = idgen_detail::parse_date(p.text)) p.day = *day;
            out.push_back(std::move(p));
        }
        return out;
    }

    static idgen_detail::scheme parse_scheme_(const core::tsv_row& row)
    {
        using namespace idgen_detail;
        scheme s;
        s.key = row.get<std::string>("key");
        s.stream = "scheme:" + s.key;
        s.country = upper(row.get<std::string>("country"));
        s.label = row.get<std::string>("label");
        s.native_label = std::string{cell_(row, "native_label")};
        s.builder = row.get<std::string>("builder");
        const auto entry = std::ranges::find(builders, std::string_view{s.builder}, &builder_entry::name);
        if (entry == builders.end()) throw row.error("builder", "no builder '" + s.builder + "'");
        s.build = entry->build;
        s.valid = entry->valid;
        s.display = std::string{cell_(row, "display")};
        s.assigned = parse_assigned_(row);
        s.safe = parse_safe_(row);
        s.check = parse_check_(row);
        s.from = date_cell_(row, "from", no_start);
        s.until = date_cell_(row, "until", no_end);
        if (s.from != no_start && s.until != no_end && s.from >= s.until) throw row.error("until", "not after from");
        if (const auto place = cell_(row, "place_source"); !place.empty())
        {
            if (place == "birthplace") s.place_source = place_from::birthplace;
            else if (place == "residence") s.place_source = place_from::residence;
            else throw row.error("place_source", "unknown source '" + std::string{place} + "'");
        }
        if (row.has("params")) s.params = parse_params_(row);
        const bool is_generic = s.builder == "generic" || s.builder == "generic_dated";
        if (is_generic && s.check.type == check_rule::kind::none) throw row.error("check", "a generic scheme needs a check");
        if (!is_generic && s.check.type != check_rule::kind::none) throw row.error("check", "builder '" + s.builder + "' owns its check");
        try
        {
            if (s.builder == "generic_dated") s.dated = parse_dated_(row);
            else s.pattern = core::pattern::parse(cell_(row, "pattern"));
            if (const auto safe_text = cell_(row, "safe_pattern"); !safe_text.empty()) s.safe_pattern = core::pattern::parse(safe_text);
        }
        catch (const std::invalid_argument& e)
        {
            throw row.error("pattern", e.what());
        }
        if (is_generic)
        {
            // A pattern the check rule cannot read fails here, not on some later draw.
            try
            {
                core::rng r{0};
                const std::string body = s.dated.empty() ? s.pattern.draw(r) : std::string(16, '0');
                if (s.dated.empty() && body.empty()) throw row.error("pattern", "a generic scheme needs a pattern");
                (void)compute_check(s.check, body);
            }
            catch (const std::invalid_argument& e)
            {
                throw row.error("check", std::string{"does not fit the pattern: "} + e.what());
            }
        }
        return s;
    }

    static idgen_detail::place_row parse_place_(const core::tsv_row& row)
    {
        using namespace idgen_detail;
        place_row p;
        p.code = row.get<std::string>("code");
        p.weight = row.get<double>("weight");
        if (!(p.weight >= 0.0)) throw row.error("weight", "must not be negative");
        p.from = date_cell_(row, "from", no_start);
        p.until = date_cell_(row, "until", no_end);
        p.admin1 = std::string{cell_(row, "admin1")};
        p.country = std::string{cell_(row, "country")};
        p.reserved = cell_(row, "reserved") == "1";
        return p;
    }

    static idgen_detail::anchor parse_anchor_(const core::tsv_row& row)
    {
        const auto day = idgen_detail::parse_date(row["date"]);
        if (!day) throw row.error("date", "expected a date");
        return {.t = *day * idgen_detail::seconds_per_day, .value = row.get<std::int64_t>("value")};
    }

    // Parse a row the way the library will; throws core::tsv_error at the row.
    static void validate_(table t, const core::tsv_row& row)
    {
        switch (t)
        {
        case table::countries:
            if (!(row.get<double>("population") >= 0.0)) throw row.error("population", "must not be negative");
            break;
        case table::schemes: (void)parse_scheme_(row); break;
        case table::places: (void)parse_place_(row); break;
        case table::place_cities:
            (void)row.get<std::uint32_t>("geonameid");
            (void)row.get<std::string>("code");
            break;
        case table::series: (void)parse_anchor_(row); break;
        case table::documents: (void)parse_format_(row); break;
        case table::validity: (void)parse_validity_(row); break;
        case table::name_rules:
        {
            const std::string_view kind = row["kind"];
            if (kind != "particle" && kind != "given_skip" && kind != "char" && kind != "word")
            {
                throw row.error("kind", "unknown kind '" + std::string{kind} + "'");
            }
            if (kind == "word" && row["replacement"].size() != row["word"].size())
            {
                throw row.error("replacement", "must be as long as the word");
            }
            break;
        }
        case table::test_numbers: break;
        }
    }

    static int small_cell_(const core::tsv_row& row, std::string_view column, std::string_view text)
    {
        const int value = row_int_(row, column, text);
        if (value < 0 || value > 200) throw row.error(column, "expected 0 to 200, got '" + std::string{text} + "'");
        return value;
    }

    static idgen_detail::document_format parse_format_(const core::tsv_row& row)
    {
        using namespace idgen_detail;
        document_format f;
        f.key = std::string{row["key"]};
        const std::string_view kind = row["kind"];
        if (kind != "passport" && kind != "id_card") throw row.error("kind", "unknown kind '" + std::string{kind} + "'");
        try
        {
            f.pattern = core::pattern::parse(row["pattern"]);
        }
        catch (const std::invalid_argument& e)
        {
            throw row.error("pattern", e.what());
        }
        f.from = date_cell_(row, "from", no_start);
        f.until = date_cell_(row, "until", no_end);
        if (f.until <= f.from) throw row.error("until", "must be after from");
        const std::string_view mrz = row["mrz"];
        if (mrz == "td1") f.mrz = mrz_type::td1;
        else if (mrz == "td3") f.mrz = mrz_type::td3;
        else if (mrz != "none") throw row.error("mrz", "unknown type '" + std::string{mrz} + "'");
        f.evidence = std::string{row["evidence"]};
        if (f.evidence != "issuer" && f.evidence != "reference" && f.evidence != "specimen" && f.evidence != "placeholder" && f.evidence != "generic")
        {
            throw row.error("evidence", "unknown evidence '" + f.evidence + "'");
        }
        f.prefix_scheme = std::string{cell_(row, "prefix_places")};
        f.mrz_code = std::string{cell_(row, "mrz_code")};
        if (f.mrz_code.size() > 2U || !std::ranges::all_of(f.mrz_code, [](char c) { return is_letter(c) || c == '<'; }))
        {
            throw row.error("mrz_code", "expected up to two capitals or fillers");
        }
        f.optional_scheme = std::string{cell_(row, "mrz_optional")};
        f.number_scheme = std::string{cell_(row, "number_scheme")};
        if (const auto text = cell_(row, "min_age"); !text.empty()) f.min_age = small_cell_(row, "min_age", text);
        if (const auto text = cell_(row, "weight"); !text.empty())
        {
            f.weight = row.get<double>("weight");
            if (!(f.weight >= 0.0)) throw row.error("weight", "must not be negative");
        }
        if (const auto text = cell_(row, "year_at"); !text.empty())
        {
            f.year_at = small_cell_(row, "year_at", text);
            if (static_cast<std::size_t>(f.year_at) + 2U > f.pattern.lengths().first) throw row.error("year_at", "past the end of the number");
        }
        return f;
    }

    static idgen_detail::validity_rule parse_validity_(const core::tsv_row& row)
    {
        using namespace idgen_detail;
        validity_rule v;
        v.from_age = small_cell_(row, "from_age", row["from_age"]);
        v.from = date_cell_(row, "from", no_start);
        v.until = date_cell_(row, "until", no_end);
        if (v.until <= v.from) throw row.error("until", "must be after from");
        std::string_view years = cell_(row, "years");
        const std::string_view until_age = cell_(row, "until_age");
        if (years.empty() == until_age.empty()) throw row.error("years", "needs years or until_age, not both");
        if (!until_age.empty())
        {
            v.until_age = small_cell_(row, "until_age", until_age);
            if (v.until_age <= v.from_age) throw row.error("until_age", "must be above from_age");
        }
        while (!years.empty())
        {
            const auto bar = years.find('|');
            v.years.push_back(small_cell_(row, "years", years.substr(0, bar)));
            years = bar == std::string_view::npos ? std::string_view{} : years.substr(bar + 1);
        }
        const std::string_view rule = cell_(row, "expiry_rule");
        if (rule == "day_before") v.rule = expiry_rule::day_before;
        else if (rule == "birthday") v.rule = expiry_rule::birthday;
        else if (rule == "application") v.rule = expiry_rule::application;
        else if (!rule.empty() && rule != "same_day") throw row.error("expiry_rule", "unknown rule '" + std::string{rule} + "'");
        return v;
    }

    // `Г=H;^Є=YE`: this letter is written so, or at a name's start only with `^`.
    static std::vector<idgen_detail::translit_rule> parse_translit_(const core::tsv_row& row)
    {
        using namespace idgen_detail;
        std::vector<translit_rule> rules;
        std::string_view list = cell_(row, "translit");
        while (!list.empty())
        {
            const auto sep = list.find(';');
            std::string_view item = list.substr(0, sep);
            list = sep == std::string_view::npos ? std::string_view{} : list.substr(sep + 1);
            translit_rule t;
            if (item.starts_with('^'))
            {
                t.first = true;
                item.remove_prefix(1);
            }
            std::size_t i = 0;
            const auto cp = item.empty() ? std::nullopt : next_code_point(item, i);
            if (!cp || i >= item.size() || item[i] != '=') throw row.error("translit", "expected LETTER=LATIN, got '" + std::string{item} + "'");
            t.cp = letter_upper(*cp);
            t.to = std::string{item.substr(i + 1)};
            if (!std::ranges::all_of(t.to, is_letter)) throw row.error("translit", "the Latin form must be capitals");
            rules.push_back(std::move(t));
        }
        return rules;
    }

    static std::size_t kind_index_(std::string_view kind) { return kind == "passport" ? 0U : 1U; }

    // documents.tsv and validity.tsv into each country's books.
    static void build_documents_(model& m, const tables& t)
    {
        using namespace idgen_detail;
        const auto where = [](const core::tsv_row& row) { return row.source() + ":" + std::to_string(row.line()) + ": "; };
        std::unordered_map<std::string, document_book*> by_name; // "de-id-card"
        for (const auto& row : t.at(index_(table::documents)).rows())
        {
            const auto it = m.country_index.find(upper(row["country"]));
            if (it == m.country_index.end()) throw std::invalid_argument(where(row) + "no country '" + std::string{row["country"]} + "'");
            auto& country = m.countries[it->second];
            auto f = parse_format_(row);
            for (const auto& key : {f.prefix_scheme, f.optional_scheme, f.number_scheme})
            {
                if (key.empty()) continue;
                const auto scheme = m.scheme_index.find(key);
                if (scheme == m.scheme_index.end() || m.schemes[scheme->second].country != country.code)
                {
                    throw std::invalid_argument(where(row) + "no scheme '" + key + "' in " + country.code);
                }
            }
            if (f.weight <= 0.0) continue;
            const std::size_t kind = kind_index_(row["kind"]);
            auto& book = country.documents.at(kind);
            if (!book)
            {
                book.emplace();
                std::string name = country.code;
                std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(c - 'A' + 'a'); });
                by_name.emplace(name + (kind == 0U ? "-passport" : "-id-card"), &*book);
            }
            book->formats.push_back(std::move(f));
        }
        for (const auto& row : t.at(index_(table::validity)).rows())
        {
            const auto it = by_name.find(std::string{row["document"]});
            if (it == by_name.end()) throw std::invalid_argument(where(row) + "no document '" + std::string{row["document"]} + "'");
            it->second->rules.push_back(parse_validity_(row));
        }
        for (auto& country : m.countries)
        {
            for (std::size_t kind = 0; kind < country.documents.size(); ++kind)
            {
                auto& book = country.documents.at(kind);
                if (!book) continue;
                std::ranges::stable_sort(book->formats, {}, &document_format::from);
                book->min_age = book->formats.front().min_age;
                day_t covered = to_day(civil{.y = 1900, .m = 1, .d = 1});
                for (const auto& f : book->formats)
                {
                    if (f.from > covered) throw std::invalid_argument("document " + f.key + ": no format covers the dates before it");
                    covered = std::max(covered, f.until);
                    book->min_age = std::min(book->min_age, f.min_age);
                }
                if (covered != no_end) throw std::invalid_argument("document " + book->formats.back().key + ": no format covers the dates after it");
                if (book->rules.empty())
                {
                    book->generic_validity = true;
                    if (kind == 0U) book->rules = {{.from_age = 0, .years = {5}}, {.from_age = 16, .years = {10}}};
                    else book->rules = {{.from_age = 0, .years = {10}}};
                }
                std::ranges::stable_sort(book->rules, {}, &validity_rule::from_age);
                for (const auto& rule : book->rules)
                {
                    int longest = rule.until_age >= 0 ? rule.until_age - rule.from_age : 0;
                    for (const int years : rule.years)
                    {
                        if (years == 0) book->permanent = true;
                        longest = std::max(longest, years);
                    }
                    book->window_years = std::max(book->window_years, longest + 1); // a birthday or application rule may add up to a year
                }
            }
        }
    }

    // Where a scheme named by a row of another table goes; throws when none does.
    static idgen_detail::scheme& scheme_of_(model& m, const core::tsv_row& row)
    {
        const auto it = m.scheme_index.find(std::string{row["scheme"]});
        if (it == m.scheme_index.end())
        {
            throw std::invalid_argument(row.source() + ":" + std::to_string(row.line()) + ": no scheme '" + std::string{row["scheme"]} + "'");
        }
        return m.schemes[it->second];
    }

    static void index_places_(idgen_detail::place_book& book)
    {
        using namespace idgen_detail;
        std::vector<day_t> starts{no_start};
        for (std::uint32_t i = 0; i < book.rows.size(); ++i)
        {
            const auto& row = book.rows[i];
            book.by_code.push_back(i);
            if (row.reserved) book.reserved.push_back(i);
            std::string_view list = row.admin1;
            while (!list.empty())
            {
                const auto sep = list.find(';');
                book.by_admin1[std::string{list.substr(0, sep)}].push_back(i);
                list = sep == std::string_view::npos ? std::string_view{} : list.substr(sep + 1);
            }
            if (row.weight > 0.0)
            {
                book.any.add(i, row.weight);
                if (row.from != no_start) starts.push_back(row.from);
                if (row.until != no_end) starts.push_back(row.until);
            }
        }
        std::ranges::stable_sort(book.by_code, {}, [&](std::uint32_t i) { return std::string_view{book.rows[i].code}; });
        std::ranges::sort(starts);
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (const day_t start : starts)
        {
            place_era era{.start = start, .table = {}};
            for (std::uint32_t i = 0; i < book.rows.size(); ++i)
            {
                const auto& row = book.rows[i];
                if (row.weight > 0.0 && row.valid_on(start)) era.table.add(i, row.weight);
            }
            book.eras.push_back(std::move(era));
        }
    }

    static model build_model_(const tables& t)
    {
        using namespace idgen_detail;
        model m; // moved whole: the vector keeps its buffer, so companion pointers stay valid
        for (const auto& row : t.at(index_(table::schemes)).rows())
        {
            m.scheme_index.emplace(std::string{row["key"]}, static_cast<std::uint32_t>(m.schemes.size()));
            m.schemes.push_back(parse_scheme_(row));
        }
        for (const auto& row : t.at(index_(table::places)).rows()) scheme_of_(m, row).places.rows.push_back(parse_place_(row));
        for (auto& s : m.schemes)
        {
            index_places_(s.places);
            if (s.place_source != place_from::none && s.places.any.empty())
            {
                throw std::invalid_argument("scheme " + s.key + " has a place_source and no place with weight");
            }
        }
        for (const auto& row : t.at(index_(table::place_cities)).rows())
        {
            auto& book = scheme_of_(m, row).places;
            const auto [lo, hi] = book.code_range(row["code"]);
            if (lo == hi)
            {
                throw std::invalid_argument(row.source() + ":" + std::to_string(row.line()) + ": code '" + std::string{row["code"]} + "' is not in places.tsv");
            }
            book.cities.emplace(row.get<std::uint32_t>("geonameid"), static_cast<std::uint32_t>(lo));
        }
        for (const auto& row : t.at(index_(table::series)).rows())
        {
            auto& list = scheme_of_(m, row).series[std::string{row["segment"]}];
            list.push_back(parse_anchor_(row));
        }
        for (auto& s : m.schemes)
        {
            for (auto& [segment, list] : s.series)
            {
                std::ranges::sort(list, {}, &anchor::t);
                for (std::size_t i = 1; i < list.size(); ++i)
                {
                    if (list[i].value <= list[i - 1].value)
                    {
                        throw std::invalid_argument("scheme " + s.key + " series '" + segment + "': anchors must rise");
                    }
                }
            }
        }
        for (const auto& row : t.at(index_(table::test_numbers)).rows())
        {
            scheme_of_(m, row).test_numbers.emplace_back(row["value"]);
        }
        for (auto& s : m.schemes)
        {
            std::ranges::sort(s.test_numbers);
            if (s.safe == safe_method::test_numbers && s.test_numbers.empty())
            {
                throw std::invalid_argument("scheme " + s.key + " uses test numbers and has none");
            }
            if (s.safe == safe_method::unassigned_place && s.places.reserved.empty())
            {
                throw std::invalid_argument("scheme " + s.key + " uses unassigned places and has none");
            }
        }
        for (const auto& row : t.at(index_(table::name_rules)).rows())
        {
            auto& rules = scheme_of_(m, row).names;
            const std::string word{row["word"]};
            const std::string_view kind = row["kind"];
            if (kind == "particle") rules.particles.push_back(word);
            else if (kind == "given_skip") rules.given_skip.push_back(word);
            else if (kind == "char") rules.chars.emplace_back(word, cell_(row, "replacement"));
            else rules.words.emplace_back(word, cell_(row, "replacement"));
        }
        for (const auto& row : t.at(index_(table::countries)).rows())
        {
            country_entry c;
            c.code = upper(row["country_code"]);
            c.icao = std::string{row["icao_code"]};
            c.population = row.get<double>("population");
            c.translit = parse_translit_(row);
            const auto index = static_cast<std::uint32_t>(m.countries.size());
            const std::string_view primary = row["primary_scheme"];
            if (!primary.empty())
            {
                const auto it = m.scheme_index.find(std::string{primary});
                if (it == m.scheme_index.end() || m.schemes[it->second].country != c.code)
                {
                    throw std::invalid_argument(row.source() + ":" + std::to_string(row.line()) + ": no primary scheme '" + std::string{primary} + "' in " + c.code);
                }
                c.schemes.push_back(it->second);
                c.has_primary = true;
                m.served_countries.add(index, c.population);
            }
            m.all_countries.add(index, c.population);
            m.country_index.emplace(c.code, index);
            m.countries.push_back(std::move(c));
        }
        for (std::uint32_t i = 0; i < m.schemes.size(); ++i)
        {
            const auto it = m.country_index.find(m.schemes[i].country);
            if (it == m.country_index.end()) throw std::invalid_argument("scheme " + m.schemes[i].key + ": no country " + m.schemes[i].country);
            auto& list = m.countries[it->second].schemes;
            if (std::ranges::find(list, i) == list.end()) list.push_back(i);
        }
        // The Argentine CUIL embeds the person's DNI.
        for (auto& s : m.schemes)
        {
            if (s.builder != "ar_cuil") continue;
            for (const auto& other : m.schemes)
            {
                if (other.builder == "ar_dni" && other.country == s.country) s.companion = &other;
            }
        }
        build_documents_(m, t);
        return m;
    }

    static documents read_directory_(const std::filesystem::path& dir)
    {
        documents docs;
        for (const auto t : all_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec)) docs.at(index_(t)) = core::tsv_document::load(file);
        }
        return docs;
    }

    void apply_(documents docs, core::layer_mode mode, bool reset)
    {
        tables next = _tables;
        for (const auto t : all_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode, [t](const core::tsv_row& row) { validate_(t, row); });
        }
        auto built = build_model_(next);
        _tables = std::move(next);
        _model = std::move(built);
    }

    // -- Requests ------------------------------------------------------------

    struct selected
    {
        std::uint32_t country{0};
        std::vector<std::uint32_t> schemes;
    };

    [[nodiscard]] std::uint32_t country_index_(std::string_view code) const
    {
        const auto it = _model.country_index.find(idgen_detail::upper(code));
        if (it == _model.country_index.end()) throw std::invalid_argument("unknown country '" + std::string{code} + "'");
        return it->second;
    }

    [[nodiscard]] const idgen_detail::scheme& scheme_or_throw_(std::string_view key) const
    {
        const auto it = _model.scheme_index.find(std::string{key});
        if (it == _model.scheme_index.end()) throw std::invalid_argument("unknown scheme '" + std::string{key} + "'");
        return _model.schemes[it->second];
    }

    static idgen_detail::day_t reference_of_(const identity_request& request)
    {
        using namespace idgen_detail;
        if (!request.reference_date.ok()) throw std::invalid_argument("invalid reference date");
        const day_t reference = to_day(request.reference_date);
        if (reference < to_day(civil{.y = 1900, .m = 1, .d = 1}) || reference > to_day(civil{.y = 2090, .m = 12, .d = 31}))
        {
            throw std::invalid_argument("the reference date must be from 1900-01-01 to 2090-12-31");
        }
        return reference;
    }

    static idgen_detail::day_t birth_of_(const identity_request& request, idgen_detail::day_t reference, const core::rng& call)
    {
        using namespace idgen_detail;
        const day_t earliest = to_day(civil{.y = 1900, .m = 1, .d = 1});
        if (request.birth_date)
        {
            if (!request.birth_date->ok()) throw std::invalid_argument("invalid birth date");
            const day_t birth = to_day(*request.birth_date);
            if (birth > reference) throw std::invalid_argument("the birth date is after the reference date");
            if (birth < earliest) throw std::invalid_argument("the birth date is before 1900");
            return birth;
        }
        // Ages 18 to 80, flat: anniversaries count back from the reference date.
        const civil ref = civil_of(reference);
        const day_t hi = std::max(earliest, anniversary_(ref, -18));
        const day_t lo = std::max(earliest, anniversary_(ref, -81) + 1);
        auto r = call.split("birth_date");
        return core::uniform_int(r, lo, std::max(lo, hi));
    }

    static idgen_detail::day_t anniversary_(const idgen_detail::civil& c, int years)
    {
        using namespace idgen_detail;
        const int y = c.y + years;
        const int d = real_date(y, c.m, c.d) ? c.d : 28; // Feb 29 in a year without one
        return to_day(civil{.y = y, .m = c.m, .d = d});
    }

    static sex sex_draw_(const core::rng& call)
    {
        auto r = call.split("sex");
        return core::bounded(r, 2) == 0 ? sex::male : sex::female;
    }

    [[nodiscard]] selected select_country_(const identity_request& request, const core::rng& call) const
    {
        selected out;
        const bool every = request.schemes && std::ranges::find(*request.schemes, "*") != request.schemes->end();
        const bool named = request.schemes && !request.schemes->empty() && !every;
        if (request.country)
        {
            out.country = country_index_(*request.country);
        }
        else if (named)
        {
            // The keys say the country; they must agree, checked below.
            out.country = country_index_(scheme_or_throw_(request.schemes->front()).country);
        }
        else
        {
            const bool wants_number = !request.schemes || every;
            const auto& pool = wants_number ? _model.served_countries : _model.all_countries;
            if (pool.empty()) throw std::runtime_error("no country can serve this request");
            auto r = call.split("country");
            out.country = request.weighted ? pool.pick(r) : pool.entries()[core::bounded(r, pool.size())].value;
        }
        const auto& country = _model.countries[out.country];
        if (!request.schemes)
        {
            if (country.has_primary) out.schemes.push_back(country.schemes.front());
        }
        else if (every)
        {
            out.schemes = country.schemes;
        }
        else if (named)
        {
            for (const auto& key : *request.schemes)
            {
                const auto& s = scheme_or_throw_(key);
                if (s.country != country.code) throw std::invalid_argument("scheme '" + key + "' is not of country " + country.code);
                const auto index = _model.scheme_index.at(key);
                if (std::ranges::find(out.schemes, index) == out.schemes.end()) out.schemes.push_back(index);
            }
            // Primary first.
            if (country.has_primary)
            {
                const auto primary = std::ranges::find(out.schemes, country.schemes.front());
                if (primary != out.schemes.end()) std::rotate(out.schemes.begin(), primary, primary + 1);
            }
        }
        return out;
    }

    // -- Places ---------------------------------------------------------------

    [[nodiscard]] static std::string draw_place_(const idgen_detail::place_book& book, idgen_detail::day_t when, bool weighted, core::rng& r)
    {
        const auto& table = book.table_on(when);
        const std::uint32_t index = weighted ? table.pick(r) : table.entries()[core::bounded(r, table.size())].value;
        return book.rows[index].code;
    }

    // The code for a birth abroad: this country's row, else the catch-all.
    [[nodiscard]] static std::optional<std::string> abroad_code_(const idgen_detail::place_book& book, std::string_view country, idgen_detail::day_t when)
    {
        const idgen_detail::place_row* any = nullptr;
        for (const auto& row : book.rows)
        {
            if (!row.valid_on(when)) continue;
            if (row.country == country) return row.code;
            if (row.country == "*" && any == nullptr) any = &row;
        }
        if (any != nullptr) return any->code;
        return std::nullopt;
    }

    // Where the scheme's place code comes from: a city, else a region, else a draw.
    [[nodiscard]] static std::string place_code_(const idgen_detail::scheme& s, const identity_request& request, idgen_detail::day_t birth, idgen_detail::day_t assigned, const core::rng& call)
    {
        using namespace idgen_detail;
        const bool from_birth = s.place_source == place_from::birthplace;
        const day_t when = from_birth ? birth : assigned;
        core::rng r = call.split(from_birth ? "birthplace" : "residence").split(s.key);
        const auto same_country = [&](const std::optional<id_place>& p) { return p && upper(p->country_code) == s.country; };

        const id_place* place = nullptr;
        if (from_birth)
        {
            if (request.birthplace) place = &*request.birthplace;
            else if (same_country(request.residence)) place = &*request.residence;
        }
        else if (same_country(request.residence))
        {
            place = &*request.residence;
        }
        else if (!request.residence && same_country(request.birthplace))
        {
            place = &*request.birthplace;
        }
        if (place != nullptr && upper(place->country_code) != s.country)
        {
            auto code = abroad_code_(s.places, upper(place->country_code), when);
            if (!code) throw std::invalid_argument("scheme " + s.key + " has no code for a birthplace in " + place->country_code);
            return *code;
        }
        if (place != nullptr && place->geonameid)
        {
            if (const auto it = s.places.cities.find(*place->geonameid); it != s.places.cities.end())
            {
                const std::string& code = s.places.rows[s.places.by_code[it->second]].code;
                const auto [lo, hi] = s.places.code_range(code);
                for (std::size_t i = lo; i < hi; ++i)
                {
                    if (s.places.rows[s.places.by_code[i]].valid_on(when)) return code;
                }
            }
        }
        if (place != nullptr && !place->admin1_code.empty())
        {
            if (const auto it = s.places.by_admin1.find(place->admin1_code); it != s.places.by_admin1.end())
            {
                core::weighted_table<std::uint32_t> table;
                for (const auto i : it->second)
                {
                    const auto& row = s.places.rows[i];
                    if (row.weight > 0.0 && row.valid_on(when)) table.add(i, row.weight);
                }
                if (!table.empty())
                {
                    const std::uint32_t index = request.weighted ? table.pick(r) : table.entries()[core::bounded(r, table.size())].value;
                    return s.places.rows[index].code;
                }
            }
        }
        return draw_place_(s.places, when, request.weighted, r);
    }

    // -- Numbers ------------------------------------------------------------------

    // A test number near the person: Sweden's list is sorted by birth date.
    [[nodiscard]] static std::string test_number_(const idgen_detail::scheme& s, const idgen_detail::civil& birth, sex bio_sex, core::rng& r)
    {
        using namespace idgen_detail;
        const auto& list = s.test_numbers;
        if (s.builder != "se_personnummer") return list[core::bounded(r, list.size())];
        const std::string start = num(static_cast<std::uint64_t>(birth.y), 4) + two(birth.m) + two(birth.d);
        const auto base = static_cast<std::size_t>(std::ranges::lower_bound(list, start) - list.begin());
        const std::size_t offset = core::bounded(r, 16);
        const bool want_odd = bio_sex == sex::male;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const std::string& candidate = list[(base + offset + i) % list.size()];
            if (((candidate[10] - '0') % 2 == 1) == want_odd) return candidate;
        }
        return list[(base + offset) % list.size()];
    }

    [[nodiscard]] static idgen_detail::civil future_date_(const idgen_detail::civil& birth, idgen_detail::day_t reference, core::rng& r)
    {
        using namespace idgen_detail;
        const int year = civil_of(reference).y + core::uniform_int(r, 1, 5);
        return {.y = year, .m = birth.m, .d = real_date(year, birth.m, birth.d) ? birth.d : 28};
    }

    [[nodiscard]] static std::optional<id_number> make_number_(const idgen_detail::scheme& s, const identity_request& request, idgen_detail::day_t birth, sex bio_sex, idgen_detail::day_t reference, const core::rng& call)
    {
        using namespace idgen_detail;
        core::rng r = call.split(s.stream);
        const day_t assigned = assigned_day(s, birth, r);
        if (assigned > reference || assigned >= s.until) return std::nullopt;

        const civil born = civil_of(birth);
        id_number n;
        n.scheme = s.key;
        n.label = s.label;
        n.native_label = s.native_label;
        n.assigned = ymd_of(assigned);
        n.reserved = request.safe;
        if (request.safe && s.safe == safe_method::test_numbers)
        {
            n.value = test_number_(s, born, bio_sex, r);
            n.formatted = apply_display(s, n.value);
            return n;
        }
        const bool impossible = request.safe && s.safe == safe_method::impossible_date;
        std::string place;
        if (s.place_source != place_from::none)
        {
            if (request.safe && s.safe == safe_method::unassigned_place)
            {
                place = s.places.rows[s.places.reserved[core::bounded(r, s.places.reserved.size())]].code;
            }
            else
            {
                place = place_code_(s, request, birth, assigned, call);
            }
        }
        build_ctx b{.s = s,
                    .date = impossible ? future_date_(born, reference, r) : born,
                    .birth_day = birth,
                    .bio_sex = bio_sex,
                    .assigned = assigned,
                    .reference = reference,
                    .place = std::move(place),
                    .given = request.given_names ? &*request.given_names : nullptr,
                    .surname = request.surname ? &*request.surname : nullptr,
                    .second = request.second_surname ? &*request.second_surname : nullptr,
                    .r = r,
                    .call = call,
                    .safe = request.safe,
                    .wrong = request.safe && s.safe == safe_method::wrong_check,
                    .reserved_range = request.safe && s.safe == safe_method::reserved_range,
                    .impossible = impossible};
        constexpr int max_attempts = 65536;
        bool done = false;
        for (int attempt = 0; attempt < max_attempts && !done; ++attempt)
        {
            n.value.clear();
            done = s.build(b, n.value);
        }
        if (!done) throw std::runtime_error("scheme " + s.key + ": could not build a number");
        n.formatted = apply_display(s, n.value);
        n.place_code = std::move(b.place);
        return n;
    }

    // -- Documents ----------------------------------------------------------------

    [[nodiscard]] static int age_on_(const idgen_detail::civil& born, idgen_detail::day_t day)
    {
        const idgen_detail::civil c = idgen_detail::civil_of(day);
        const bool before_birthday = c.m < born.m || (c.m == born.m && c.d < born.d);
        return c.y - born.y - (before_birthday ? 1 : 0);
    }

    // When a document issued on this day expires: no_end when it does not, empty
    // when no rule covers that date and age. May draw a choice of years or an
    // application date.
    [[nodiscard]] static std::optional<idgen_detail::day_t> expiry_of_(const idgen_detail::document_book& book, const idgen_detail::civil& born, idgen_detail::day_t issued, core::rng& r)
    {
        using namespace idgen_detail;
        const int age = age_on_(born, issued);
        const validity_rule* rule = nullptr;
        for (const auto& candidate : book.rules)
        {
            if (candidate.from_age <= age && candidate.from <= issued && issued < candidate.until) rule = &candidate;
        }
        if (rule == nullptr) return std::nullopt;
        day_t end = 0;
        if (rule->until_age >= 0)
        {
            end = anniversary_(born, rule->until_age);
        }
        else
        {
            const int years = rule->years.size() == 1U ? rule->years.front() : rule->years[core::bounded(r, rule->years.size())];
            if (years == 0) return no_end;
            const day_t counted_from = rule->rule == expiry_rule::application ? issued - static_cast<day_t>(core::bounded(r, 31)) : issued;
            end = anniversary_(civil_of(counted_from), years);
            if (rule->rule == expiry_rule::birthday)
            {
                const int year = civil_of(end).y;
                const day_t birthday = anniversary_(born, year - born.y);
                end = birthday >= end ? birthday : anniversary_(born, year + 1 - born.y);
            }
        }
        if (rule->rule == expiry_rule::day_before) --end;
        return end;
    }

    // The format in force on a day; several draw by weight.
    [[nodiscard]] static const idgen_detail::document_format* format_on_(const idgen_detail::document_book& book, idgen_detail::day_t day, core::rng& r)
    {
        const idgen_detail::document_format* only = nullptr;
        double total = 0.0;
        for (const auto& f : book.formats)
        {
            if (f.from <= day && day < f.until)
            {
                only = &f;
                total += f.weight;
            }
        }
        if (only == nullptr || total == only->weight) return only;
        double target = core::uniform01(r) * total;
        for (const auto& f : book.formats)
        {
            if (!(f.from <= day && day < f.until)) continue;
            target -= f.weight;
            if (target < 0.0) return &f;
        }
        return only;
    }

    // The person's number in a scheme a document names. Empty when not held, or for
    // a birthplace the scheme has no code for: nobody asked for this number itself.
    [[nodiscard]] std::optional<id_number> number_on_document_(const std::string& key, const identity_request& request, idgen_detail::day_t birth, sex bio_sex, idgen_detail::day_t reference, const core::rng& call) const
    {
        try
        {
            return make_number_(_model.schemes[_model.scheme_index.at(key)], request, birth, bio_sex, reference, call);
        }
        catch (const std::invalid_argument&)
        {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<id_document> make_document_(id_document_kind kind, const country_entry& country, const identity_request& request, idgen_detail::day_t birth, sex bio_sex, idgen_detail::day_t reference, const core::rng& call) const
    {
        using namespace idgen_detail;
        const auto& held = country.documents.at(static_cast<std::size_t>(kind));
        if (!held) return std::nullopt;
        const document_book& book = *held;
        const civil born = civil_of(birth);
        const day_t earliest = anniversary_(born, book.min_age);
        if (earliest > reference) return std::nullopt;

        // A person renews when a document runs out, so the one in force was issued
        // uniformly over the days whose document still runs on the reference date.
        core::rng r = call.split(kind == id_document_kind::passport ? "passport" : "id_card");
        const day_t lo = book.permanent ? earliest : std::max(earliest, anniversary_(civil_of(reference), -book.window_years));
        constexpr int max_attempts = 1024;
        day_t issued = 0;
        day_t expires = 0;
        const document_format* format = nullptr;
        std::optional<id_number> own; // the national number, for a format that prints one
        const std::string* own_key = nullptr;
        const auto try_day = [&](day_t day) {
            const auto end = expiry_of_(book, born, day, r);
            if (!end || *end <= reference) return false;
            const document_format* f = format_on_(book, day, r);
            if (f == nullptr || age_on_(born, day) < f->min_age) return false;
            if (!f->number_scheme.empty())
            {
                if (own_key == nullptr || *own_key != f->number_scheme)
                {
                    own = number_on_document_(f->number_scheme, request, birth, bio_sex, reference, call);
                    own_key = &f->number_scheme;
                }
                if (own && to_day(own->assigned) > day) return false; // no card shows a number from its future
            }
            issued = day;
            expires = *end;
            format = f;
            return true;
        };
        bool found = false;
        for (int attempt = 0; attempt < max_attempts && !found; ++attempt) found = try_day(core::uniform_int(r, lo, reference));
        if (!found && !try_day(reference)) return std::nullopt;

        id_document d;
        d.kind = kind;
        d.format = format->key;
        d.issuing_state = country.icao;
        d.issued = ymd_of(issued);
        if (expires != no_end) d.expires = ymd_of(expires);
        d.evidence = format->evidence;
        d.generic_validity = book.generic_validity;
        d.reserved = request.safe;
        if (!format->prefix_scheme.empty())
        {
            d.number = draw_place_(_model.schemes[_model.scheme_index.at(format->prefix_scheme)].places, issued, request.weighted, r);
        }
        const bool prints_own = !format->number_scheme.empty() && own.has_value();
        if (prints_own)
        {
            d.number = printed_number(format->pattern, own->value, own->formatted); // made safe by the scheme's own method
        }
        else
        {
            d.number += format->pattern.draw(r);
            if (format->year_at >= 0)
            {
                d.number.replace(d.number.size() - format->pattern.lengths().first + static_cast<std::size_t>(format->year_at), 2U, two(civil_of(issued).y));
            }
            if (request.safe) d.number = outside_formats(book, std::move(d.number));
        }

        // The zone needs a name it can write, and an issuing state (design 5.3).
        if (format->mrz == mrz_type::none || !request.surname || country.icao.empty()) return d;
        auto primary = mrz_words(*request.surname, country.translit);
        if (!primary) return d;
        // A name the caller gave and the zone cannot write leaves the zone out: one
        // that silently lost part of the name would be wrong.
        const auto given = [](const std::optional<std::string>& name) { return name && !name->empty(); };
        if (given(request.second_surname))
        {
            const auto second = mrz_words(*request.second_surname, country.translit);
            if (!second) return d;
            *primary += "<" + *second;
        }
        const auto secondary = given(request.given_names) ? mrz_words(*request.given_names, country.translit) : std::nullopt;
        if (given(request.given_names) && !secondary) return d;
        const std::size_t name_width = format->mrz == mrz_type::td3 ? 39U : 30U;
        mrz_fields f;
        f.code = format->mrz_code.empty() ? (kind == id_document_kind::passport ? "P<" : "ID") : format->mrz_code;
        f.state = country.icao;
        f.name = mrz_name(*primary, secondary ? std::string_view{*secondary} : std::string_view{}, name_width);
        f.number = alphanumeric(d.number);
        const auto yymmdd = [](day_t day) {
            const civil c = civil_of(day);
            return two(c.y) + two(c.m) + two(c.d);
        };
        f.birth = yymmdd(birth);
        f.sex = bio_sex == sex::male ? 'M' : 'F';
        f.expiry = expires == no_end ? std::string(6, '<') : yymmdd(expires);
        if (!format->optional_scheme.empty())
        {
            const auto carried = number_on_document_(format->optional_scheme, request, birth, bio_sex, reference, call);
            if (carried && to_day(carried->assigned) <= issued) f.optional = alphanumeric(carried->value);
        }
        d.mrz = format->mrz == mrz_type::td3 ? mrz_td3(f) : mrz_td1(f);
        return d;
    }

    // -- State ------------------------------------------------------------

    tables _tables{make_tables_()};
    model _model;
    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit idg(auto_probe_tag /*tag*/)
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    // -- Helpers ----------------------------------------------------------

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_ID_EMBED
        return embedded::idgen::files;
#else
        return {};
#endif
    }

    std::uint64_t draw_seed_() { return _engine(); }

    void require_data_() const
    {
        if (!has_data())
        {
            throw std::runtime_error("No identity data loaded. Call load() first.");
        }
    }
};

} // namespace dasmig

#endif // DASMIG_IDGEN_HPP
