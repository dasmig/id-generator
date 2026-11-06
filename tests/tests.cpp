#include "catch_amalgamated.hpp"
#include "../dasmig/idgen.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono;
using dasmig::id_number;
using dasmig::id_place;
using dasmig::id_table;
using dasmig::identity;
using dasmig::identity_request;
using dasmig::idg;
using dasmig::sex;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static idg make_loaded_generator()
{
    idg gen;
    if (!gen.load(dasmig::dataset::lite))
    {
        for (const auto* candidate : {"resources/lite", "../resources/lite"})
        {
            if (std::filesystem::is_directory(candidate))
            {
                gen.load(std::filesystem::path{candidate});
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

static const idg& shared_generator()
{
    static const idg gen = make_loaded_generator();
    return gen;
}

static std::filesystem::path resource_dir(std::string_view tier)
{
    for (const auto* base : {"resources", "../resources"})
    {
        const auto dir = std::filesystem::path{base} / tier;
        if (std::filesystem::is_directory(dir)) return dir;
    }
    FAIL("no resources directory");
    return {};
}

static year_month_day date(int y, unsigned m, unsigned d) { return year{y} / month{m} / day{d}; }

// The whole-number digits at t[pos, pos + len).
static int digits(const std::string& t, std::size_t pos, std::size_t len) { return std::stoi(t.substr(pos, len)); }

static int year_of(const year_month_day& d) { return static_cast<int>(d.year()); }
static int month_of(const year_month_day& d) { return static_cast<int>(static_cast<unsigned>(d.month())); }
static int day_of(const year_month_day& d) { return static_cast<int>(static_cast<unsigned>(d.day())); }

static identity_request person(std::string_view country, std::vector<std::string> schemes)
{
    identity_request q;
    q.country = std::string{country};
    q.schemes = std::move(schemes);
    return q;
}

// One number for a request; the test fails when the person has none.
static id_number one_number(const idg& gen, const identity_request& q, std::uint64_t seed)
{
    const auto v = gen.get_identity(q, seed);
    REQUIRE(v.numbers.size() == 1);
    return v.numbers.front();
}

// ---------------------------------------------------------------------------
// What each number encodes, read back by code written from the survey's
// descriptions rather than from the builders. A field left unset is not in the
// number (or not at that date).
// ---------------------------------------------------------------------------

struct decoded
{
    std::optional<int> year;     // the full year
    std::optional<int> yy;       // a year without its century
    std::optional<int> mon;      // month
    std::optional<int> dom;      // day
    std::optional<sex> who;      // sex
};

static sex odd_male(int v) { return v % 2 == 1 ? sex::male : sex::female; }
static sex even_male(int v) { return v % 2 == 0 ? sex::male : sex::female; }

static decoded decode(const id_number& n, const year_month_day& birth)
{
    const std::string& s = n.scheme;
    const std::string& v = n.value;
    decoded d;
    if (s == "ae-eid")
    {
        d.year = digits(v, 3, 4);
    }
    else if (s == "ar-cuil")
    {
        if (n.assigned < date(2021, 1, 1) && v.substr(0, 2) != "23") d.who = v.substr(0, 2) == "20" ? sex::male : sex::female;
    }
    else if (s == "at-svnr")
    {
        d.dom = digits(v, 4, 2);
        d.mon = digits(v, 6, 2);
        d.yy = digits(v, 8, 2);
    }
    else if (s == "ba-jmb" || s == "me-jmbg" || s == "mk-embg" || s == "rs-jmbg" || s == "si-emso")
    {
        d.dom = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        const int g = digits(v, 4, 3);
        d.year = g >= 800 ? 1000 + g : 2000 + g;
        d.who = digits(v, 9, 3) < 500 ? sex::male : sex::female;
    }
    else if (s == "be-nn")
    {
        d.yy = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.dom = digits(v, 4, 2);
        d.who = odd_male(digits(v, 6, 3));
    }
    else if (s == "bg-egn")
    {
        const int m = digits(v, 2, 2);
        d.yy = digits(v, 0, 2);
        d.year = (m > 40 ? 2000 : 1900) + digits(v, 0, 2);
        d.mon = m > 40 ? m - 40 : m;
        d.dom = digits(v, 4, 2);
        d.who = even_male(digits(v, 8, 1));
    }
    else if (s == "cn-ric")
    {
        d.year = digits(v, 6, 4);
        d.mon = digits(v, 10, 2);
        d.dom = digits(v, 12, 2);
        d.who = odd_male(digits(v, 14, 3));
    }
    else if (s == "cz-rc" || s == "sk-rc")
    {
        int m = digits(v, 2, 2);
        d.who = m > 50 ? sex::female : sex::male;
        if (m > 50) m -= 50;
        if (m > 20) m -= 20;
        d.yy = digits(v, 0, 2);
        d.mon = m;
        d.dom = digits(v, 4, 2);
    }
    else if (s == "dk-cpr")
    {
        d.dom = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.yy = digits(v, 4, 2);
        d.who = odd_male(digits(v, 9, 1));
    }
    else if (s == "ee-isikukood" || s == "lt-asmens-kodas")
    {
        const int g = digits(v, 0, 1);
        d.year = 1800 + (((g - 1) / 2) * 100) + digits(v, 1, 2);
        d.who = odd_male(g);
        d.mon = digits(v, 3, 2);
        d.dom = digits(v, 5, 2);
    }
    else if (s == "fi-hetu")
    {
        d.dom = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.year = (v[6] == '-' || v[6] == 'Y' ? 1900 : 2000) + digits(v, 4, 2);
        d.who = odd_male(digits(v, 7, 3));
    }
    else if (s == "fr-nir")
    {
        d.who = v[0] == '1' ? sex::male : sex::female;
        d.yy = digits(v, 1, 2);
        d.mon = digits(v, 3, 2);
    }
    else if (s == "gr-amka")
    {
        d.dom = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.yy = digits(v, 4, 2);
        d.who = odd_male(digits(v, 9, 1));
    }
    else if (s == "id-nik")
    {
        const int dd = digits(v, 6, 2);
        d.who = dd > 40 ? sex::female : sex::male;
        d.dom = dd > 40 ? dd - 40 : dd;
        d.mon = digits(v, 8, 2);
        d.yy = digits(v, 10, 2);
    }
    else if (s == "is-kennitala")
    {
        d.dom = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.year = (v[9] == '0' ? 2000 : 1900) + digits(v, 4, 2);
    }
    else if (s == "it-cf")
    {
        const std::string months = "ABCDEHLMPRST";
        d.yy = digits(v, 6, 2);
        d.mon = static_cast<int>(months.find(v[8])) + 1;
        const int dd = digits(v, 9, 2);
        d.who = dd > 40 ? sex::female : sex::male;
        d.dom = dd > 40 ? dd - 40 : dd;
    }
    else if (s == "kr-rrn")
    {
        d.yy = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.dom = digits(v, 4, 2);
        d.who = odd_male(digits(v, 6, 1));
        d.year = (v[6] == '3' || v[6] == '4' ? 2000 : 1900) + digits(v, 0, 2);
    }
    else if (s == "kz-iin")
    {
        d.yy = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.dom = digits(v, 4, 2);
        d.who = odd_male(digits(v, 6, 1));
    }
    else if (s == "lv-personas-kods")
    {
        if (!v.starts_with("32"))
        {
            d.dom = digits(v, 0, 2);
            d.mon = digits(v, 2, 2);
            d.yy = digits(v, 4, 2);
        }
    }
    else if (s == "mx-curp")
    {
        d.yy = digits(v, 4, 2);
        d.mon = digits(v, 6, 2);
        d.dom = digits(v, 8, 2);
        d.who = v[10] == 'H' ? sex::male : sex::female;
    }
    else if (s == "my-mykad")
    {
        d.yy = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.dom = digits(v, 4, 2);
        d.who = odd_male(digits(v, 11, 1));
    }
    else if (s == "no-fodselsnummer")
    {
        d.dom = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.yy = digits(v, 4, 2);
        d.who = odd_male(digits(v, 8, 1));
    }
    else if (s == "pk-cnic")
    {
        d.who = odd_male(digits(v, 12, 1));
    }
    else if (s == "pl-pesel")
    {
        const int m = digits(v, 2, 2);
        d.yy = digits(v, 0, 2);
        d.mon = m % 20;
        d.year = 1900 + ((m / 20) * 100) + digits(v, 0, 2);
        d.dom = digits(v, 4, 2);
        d.who = odd_male(digits(v, 9, 1));
    }
    else if (s == "ro-cnp")
    {
        d.who = odd_male(digits(v, 0, 1));
        d.yy = digits(v, 1, 2);
        d.mon = digits(v, 3, 2);
        d.dom = digits(v, 5, 2);
    }
    else if (s == "se-personnummer")
    {
        d.year = digits(v, 0, 4);
        d.mon = digits(v, 4, 2);
        d.dom = digits(v, 6, 2);
        d.who = odd_male(digits(v, 10, 1));
    }
    else if (s == "sg-nric")
    {
        if (birth >= date(1968, 1, 1)) d.yy = digits(v, 1, 2);
    }
    else if (s == "tw-nid")
    {
        d.who = v[1] == '1' ? sex::male : sex::female;
    }
    else if (s == "ua-rnokpp")
    {
        const auto day_zero = sys_days{date(1899, 12, 31)};
        const year_month_day when{day_zero + days{digits(v, 0, 5)}};
        d.year = year_of(when);
        d.mon = month_of(when);
        d.dom = day_of(when);
        d.who = odd_male(digits(v, 8, 1));
    }
    else if (s == "vn-cccd")
    {
        const int g = digits(v, 3, 1);
        d.who = g % 2 == 0 ? sex::male : sex::female;
        d.year = 1900 + ((g / 2) * 100) + digits(v, 4, 2);
    }
    else if (s == "za-idnr")
    {
        d.yy = digits(v, 0, 2);
        d.mon = digits(v, 2, 2);
        d.dom = digits(v, 4, 2);
        d.who = digits(v, 6, 4) >= 5000 ? sex::male : sex::female;
    }
    return d;
}

// Whether the place code appears where the scheme encodes it.
static bool place_in_value(const id_number& n)
{
    const std::string& v = n.value;
    const std::string& p = n.place_code;
    const std::string& s = n.scheme;
    if (p.empty()) return true;
    if (s == "bg-egn")
    {
        const int serial = digits(v, 6, 3);
        return serial >= digits(p, 0, 3) && serial <= digits(p, 4, 3);
    }
    if (s == "br-cpf") return v.substr(8, 1) == p;
    if (s == "ca-sin" || s == "pk-cnic") return v.substr(0, 1) == p;
    if (s == "cn-ric" || s == "id-nik") return v.substr(0, 6) == p;
    if (s == "fr-nir")
    {
        const std::string in_number = v.substr(5, 5);
        return in_number == p || (p.starts_with("2") && (p[1] == 'A' || p[1] == 'B') && in_number == "20" + p.substr(2));
    }
    if (s == "it-cf") return v.substr(11, 4) == p;
    if (s == "kr-rrn") return v.substr(7, 2) == p;
    if (s == "mx-curp") return v.substr(11, 2) == p;
    if (s == "my-mykad") return v.substr(6, 2) == p;
    if (s == "ro-cnp") return v.substr(7, 2) == p;
    if (s == "ru-inn") return v.substr(0, 2) == p;
    if (s == "th-pin") return v.substr(1, 2) == p;
    if (s == "tw-nid") return v.substr(0, 1) == p;
    if (s == "us-ssn") return v.substr(0, 3) == p;
    if (s == "vn-cccd") return v.substr(0, 3) == p;
    if (s == "ec-cedula") return v.substr(0, 2) == p;
    if (s == "ba-jmb" || s == "me-jmbg" || s == "mk-embg" || s == "rs-jmbg") return v.substr(7, 2) == p;
    return false;
}

static std::string describe(const id_number& n, const identity& v)
{
    const auto text = [](const year_month_day& d) {
        return std::to_string(year_of(d)) + "-" + std::to_string(month_of(d)) + "-" + std::to_string(day_of(d));
    };
    return n.scheme + " " + n.value + " place '" + n.place_code + "' assigned " + text(n.assigned) + " born " + text(v.birth_date) + " " +
           std::string{dasmig::core::sex_name(v.bio_sex)};
}

// One error text for this person's numbers, or empty.
static std::string coherence_error(const idg& gen, const identity& v, const year_month_day& reference,
                                   const std::set<std::pair<std::string, std::string>>& places)
{
    for (const auto& n : v.numbers)
    {
        if (!gen.is_valid(n.scheme, n.value, reference)) return "not valid: " + describe(n, v);
        if (!gen.is_valid(n.scheme, n.formatted, reference)) return "formatted not valid: " + n.formatted + " " + describe(n, v);
        if (n.assigned < v.birth_date) return "assigned before birth: " + describe(n, v);
        if (n.assigned > reference) return "assigned after the reference date: " + describe(n, v);
        if (n.reserved) return "reserved: " + describe(n, v);
        if (!n.place_code.empty() && !places.contains({n.scheme, n.place_code})) return "place not in the table: " + describe(n, v);
        if (!place_in_value(n)) return "place not in the number: " + describe(n, v);
        const decoded d = decode(n, v.birth_date);
        if (d.year && *d.year != year_of(v.birth_date)) return "year: " + describe(n, v);
        if (d.yy && *d.yy != year_of(v.birth_date) % 100) return "two-digit year: " + describe(n, v);
        if (d.mon && *d.mon != month_of(v.birth_date)) return "month: " + describe(n, v);
        if (d.dom && *d.dom != day_of(v.birth_date)) return "day: " + describe(n, v);
        if (d.who && *d.who != v.bio_sex) return "sex: " + describe(n, v);
    }
    return {};
}

static std::set<std::pair<std::string, std::string>> place_table()
{
    std::set<std::pair<std::string, std::string>> out;
    const auto doc = dasmig::core::tsv_document::load(resource_dir("lite") / "places.tsv");
    for (const auto& row : doc.rows()) out.emplace(row["scheme"], row["code"]);
    return out;
}

static void check_coherence(int draws)
{
    const auto& gen = shared_generator();
    const auto places = place_table();
    for (const auto reference : {date(2005, 6, 1), date(2015, 6, 1), date(2023, 7, 1)})
    {
        for (const auto& country : gen.number_countries())
        {
            identity_request q = person(country, {"*"});
            q.reference_date = reference;
            dasmig::core::rng ages{static_cast<std::uint64_t>(country[0]) + (static_cast<std::uint64_t>(country[1]) << 8U)};
            for (int i = 0; i < draws; ++i)
            {
                // Half the library's own ages, half any age: children and the very old.
                q.birth_date = std::nullopt;
                if (i % 2 == 1)
                {
                    const auto to = sys_days{reference};
                    const auto from = std::max(sys_days{date(1900, 1, 1)}, to - days{36525});
                    q.birth_date = year_month_day{from + days{dasmig::core::uniform_int(ages, 0, static_cast<int>((to - from).count()))}};
                }
                const auto v = gen.get_identity(q, (static_cast<std::uint64_t>(i) * 2654435761ULL) + 17U);
                const std::string error = coherence_error(gen, v, reference, places);
                if (!error.empty()) FAIL(error);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads every table", "[loading]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.countries().size() == 199);
    REQUIRE(gen.number_countries().size() == 63);
    for (const auto t : {id_table::countries, id_table::schemes, id_table::places, id_table::place_cities, id_table::series,
                         id_table::documents, id_table::validity, id_table::name_rules, id_table::test_numbers})
    {
        REQUIRE(gen.layers(t).size() == 1);
    }
    REQUIRE(gen.schemes("br").front() == "br-cpf");
    REQUIRE(gen.schemes("BR").size() == 1);
    REQUIRE(gen.schemes("AR") == std::vector<std::string>{"ar-dni", "ar-cuil"});
    REQUIRE(gen.schemes("AD").empty());
    REQUIRE_THROWS_AS(gen.schemes("ZZ"), std::invalid_argument);
}

TEST_CASE("load - the full tier loads and serves the same schemes", "[loading]")
{
    idg gen;
    gen.load(resource_dir("full"));
    REQUIRE(gen.has_data());
    REQUIRE(gen.number_countries().size() == 63);
    REQUIRE(gen.get_identity(person("IT", {"it-cf"}), 1).numbers.size() == 1);
}

TEST_CASE("load - a path without the tables is a no-op", "[loading]")
{
    idg gen;
    gen.load(std::filesystem::path{"no/such/directory"});
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.data_hash() == 0);
    REQUIRE(gen.countries().empty());
}

#ifndef DASMIG_ID_EMBED // compiled-in data is never missing
TEST_CASE("load(dataset) returns false for a missing tier", "[loading]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "idg_empty_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    idg gen;
    const bool loaded = gen.load(dasmig::dataset::full);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);
    REQUIRE_FALSE(loaded);
    REQUIRE_FALSE(gen.has_data());
}
#endif

TEST_CASE("the singleton loads data on first use", "[loading]")
{
    REQUIRE(idg::instance().has_data());
}

TEST_CASE("a moved generator keeps its data, the CUIL's link to the DNI included", "[loading]")
{
    idg first = make_loaded_generator();
    const auto expected = first.get_identity(person("AR", {"*"}), 1).to_json();
    idg second = std::move(first);
    REQUIRE(second.get_identity(person("AR", {"*"}), 1).to_json() == expected);
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------

TEST_CASE("get_identity throws when no data is loaded", "[generation]")
{
    idg gen;
    REQUIRE_THROWS_AS(gen.get_identity(identity_request{}), std::runtime_error);
    REQUIRE_THROWS_AS(gen.get_identity(identity_request{}, std::uint64_t{1}), std::runtime_error);
}

TEST_CASE("get_identity with a seed is deterministic", "[generation][seed]")
{
    const auto& gen = shared_generator();
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        REQUIRE(gen.get_identity(identity_request{}, s).to_json() == gen.get_identity(identity_request{}, s).to_json());
    }
}

TEST_CASE("a result replays from its seed", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    const auto v = gen.get_identity(identity_request{});
    REQUIRE(gen.get_identity(identity_request{}, v.seed()).to_json() == v.to_json());
}

TEST_CASE("explicit per-call seeds replay across generator instances", "[seed]")
{
    const auto first = make_loaded_generator();
    const auto second = make_loaded_generator();
    REQUIRE(first.get_identity(identity_request{}, 42).to_json() == second.get_identity(identity_request{}, 42).to_json());
}

// ---------------------------------------------------------------------------
// Builders and validators against the authorities' worked examples
// ---------------------------------------------------------------------------

TEST_CASE("every worked example of the survey passes is_valid", "[builders]")
{
    const auto& gen = shared_generator();
    const std::filesystem::path file =
        std::filesystem::is_regular_file("scripts/curated/scheme_examples.tsv") ? "scripts/curated/scheme_examples.tsv" : "../scripts/curated/scheme_examples.tsv";
    const auto doc = dasmig::core::tsv_document::load(file);
    int checked = 0;
    for (const auto& row : doc.rows())
    {
        const std::string scheme{row["scheme"]};
        const std::string value{row["value"]};
        if (scheme == "pt-nif") continue; // the example is a company, outside the personal ranges
        CAPTURE(scheme, value);
        REQUIRE(gen.is_valid(scheme, value));
        ++checked;
    }
    REQUIRE(checked >= 100);
}

TEST_CASE("changing the check character of a worked example fails is_valid", "[builders]")
{
    const auto& gen = shared_generator();
    const std::vector<std::pair<std::string, std::string>> examples{
        {"ae-eid", "784199012345676"},  {"ar-cuil", "20123456786"},      {"at-svnr", "1237010180"},
        {"au-tfn", "123456782"},        {"be-nn", "42012205181"},        {"bg-egn", "0847038794"},
        {"br-cpf", "11144477735"},      {"ca-sin", "123456782"},         {"ch-ahv", "7569217076985"},
        {"cl-run", "123456785"},        {"cn-ric", "11010519491231002X"}, {"cz-rc", "7103192745"},
        {"de-idnr", "86095742719"},     {"ec-cedula", "1714307103"},     {"ee-isikukood", "37605030299"},
        {"es-dni", "12345678Z"},        {"fi-hetu", "131052-308T"},      {"fr-nir", "151024610204325"},
        {"gr-amka", "01013099997"},     {"hk-hkid", "A1234563"},         {"hr-oib", "33392005961"},
        {"ie-ppsn", "6433435F"},        {"il-idnr", "039337423"},        {"in-aadhaar", "234123412346"},
        {"ir-codemeli", "7731689956"},  {"is-kennitala", "1201743399"},  {"it-cf", "RSSMRA80A01H501U"},
        {"jp-mynumber", "621498320257"}, {"kr-rrn", "9710139019902"},    {"kz-iin", "910509300750"},
        {"lt-asmens-kodas", "33309240064"}, {"lv-personas-kods", "16117519997"}, {"mx-curp", "BOXW310820HNERXN09"},
        {"nl-bsn", "999900006"},        {"no-fodselsnummer", "02013299997"}, {"nz-ird", "49091850"},
        {"pe-dni", "101174102"},        {"pl-pesel", "02070803628"},     {"ro-cnp", "1630615123457"},
        {"ru-inn", "123456789047"},     {"ru-snils", "11223344595"},     {"sa-nid", "1012345672"},
        {"se-personnummer", "196408233234"}, {"sg-nric", "S1234567D"},   {"si-emso", "2902932505526"},
        {"th-pin", "3100600445635"},    {"tr-tckn", "17291716060"},      {"tw-nid", "A123456789"},
        {"ua-rnokpp", "1759013776"},    {"uy-cedula", "12345672"},       {"za-idnr", "7503305044089"},
    };
    for (const auto& [scheme, value] : examples)
    {
        std::string changed = value;
        std::size_t last = changed.size() - 1;
        if (scheme == "at-svnr") last = 3;                      // its check digit is the fourth
        else if (scheme == "is-kennitala") last = 8;            // Iceland's is the ninth
        const char was = changed[last];
        changed[last] = was == '1' ? '2' : '1';
        CAPTURE(scheme, value, changed);
        REQUIRE(gen.is_valid(scheme, value));
        REQUIRE_FALSE(gen.is_valid(scheme, changed));
    }
}

TEST_CASE("is_valid reads the display form, rejects other text and unknown schemes", "[builders]")
{
    const auto& gen = shared_generator();
    REQUIRE(gen.is_valid("br-cpf", "111.444.777-35"));
    REQUIRE(gen.is_valid("cl-run", "12.345.678-5"));
    REQUIRE(gen.is_valid("cz-rc", "710319/2745"));
    REQUIRE(gen.is_valid("hk-hkid", "A123456(3)"));
    REQUIRE_FALSE(gen.is_valid("br-cpf", "11144477736"));
    REQUIRE_FALSE(gen.is_valid("br-cpf", "11111111111")); // repeated digits pass the check, not the scheme
    REQUIRE_FALSE(gen.is_valid("br-cpf", ""));
    REQUIRE_FALSE(gen.is_valid("it-cf", "not a number"));
    REQUIRE_FALSE(gen.is_valid("ch-ahv", "ABCDEFGHIJKLM"));
    REQUIRE_THROWS_AS(gen.is_valid("xx-none", "1"), std::invalid_argument);
}

TEST_CASE("is_valid compares an encoded birth date with the reference date", "[builders]")
{
    const auto& gen = shared_generator();
    REQUIRE(gen.is_valid("se-personnummer", "196408233234", date(2023, 7, 1)));
    REQUIRE_FALSE(gen.is_valid("se-personnummer", "196408233234", date(1960, 1, 1)));
    REQUIRE_FALSE(gen.is_valid("it-cf", "RSSMRA80A32H501N")); // the 32nd of January
}

TEST_CASE("the codice fiscale is built from the names and the commune", "[builders]")
{
    const auto& gen = shared_generator();
    identity_request q = person("IT", {"it-cf"});
    q.surname = "Rossi";
    q.given_names = "Mario";
    q.birth_date = date(1980, 1, 1);
    q.sex = sex::male;
    q.birthplace = id_place{.country_code = "IT", .admin1_code = {}, .geonameid = 3169070U}; // Rome
    const auto n = one_number(gen, q, 7);
    REQUIRE(n.value == "RSSMRA80A01H501U");
    REQUIRE(n.place_code == "H501");
    REQUIRE(n.formatted == n.value);

    // A given name with four consonants keeps the first, third and fourth.
    q.given_names = "Gianfranco";
    q.surname = "Verdi";
    q.birth_date = date(1972, 7, 21);
    q.birthplace = id_place{.country_code = "IT", .admin1_code = {}, .geonameid = 3173435U}; // Milan
    REQUIRE(one_number(gen, q, 7).value == "VRDGFR72L21F205" + one_number(gen, q, 7).value.substr(15));
    REQUIRE(one_number(gen, q, 7).value.substr(6, 5) == "72L21");

    // A short surname fills up with X; women's days add 40; accents fold.
    q.surname = "Fò";
    q.given_names = "Lucía";
    q.sex = sex::female;
    q.birth_date = date(1990, 3, 5);
    const std::string short_name = one_number(gen, q, 7).value;
    REQUIRE(short_name.substr(0, 6) == "FOXLCU");
    REQUIRE(short_name.substr(6, 5) == "90C45");

    // Without names, or with names outside Latin script: drawn in the same shape.
    q.surname = std::nullopt;
    q.given_names = std::nullopt;
    REQUIRE(gen.is_valid("it-cf", one_number(gen, q, 7).value));
    q.surname = "Иванов";
    REQUIRE(gen.is_valid("it-cf", one_number(gen, q, 7).value));
}

TEST_CASE("the CURP applies RENAPO's name rules", "[builders]")
{
    const auto& gen = shared_generator();
    identity_request q = person("MX", {"mx-curp"});
    q.surname = "Salazar";
    q.second_surname = "Bravo";
    q.given_names = "Concepción";
    q.birth_date = date(1956, 6, 26);
    q.sex = sex::female;
    q.birthplace = id_place{.country_code = "MX", .admin1_code = "09", .geonameid = std::nullopt};
    REQUIRE(one_number(gen, q, 3).value.substr(0, 16) == "SABC560626MDFLRN");

    // MARIA is skipped when another name follows; a surname's particles too; Ñ counts as X.
    q.surname = "Gómez";
    q.second_surname = "Díaz";
    q.given_names = "María José";
    q.birthplace = id_place{.country_code = "MX", .admin1_code = "32", .geonameid = std::nullopt};
    REQUIRE(one_number(gen, q, 3).value.substr(0, 4) == "GODJ");
    REQUIRE(one_number(gen, q, 3).value.substr(10, 6) == "MZSMZS");

    q.surname = "de la Fuente";
    q.second_surname = "Ibáñez";
    q.given_names = "María";
    const std::string alone = one_number(gen, q, 3).value;
    REQUIRE(alone.substr(0, 4) == "FUIM");
    REQUIRE(alone.substr(13, 3) == "NBR");

    // Without a second surname its letters are X.
    q.second_surname = std::nullopt;
    REQUIRE(one_number(gen, q, 3).value.substr(0, 4) == "FUXM");

    // Born abroad; the century letter from 2000.
    q.birthplace = id_place{.country_code = "ES", .admin1_code = {}, .geonameid = std::nullopt};
    q.birth_date = date(2004, 2, 29);
    const std::string abroad = one_number(gen, q, 3).value;
    REQUIRE(abroad.substr(11, 2) == "NE");
    REQUIRE(abroad[16] >= 'A');
}

TEST_CASE("the NIR follows Corsica's department change and births abroad", "[builders]")
{
    const auto& gen = shared_generator();
    identity_request q = person("FR", {"fr-nir"});
    q.sex = sex::male;
    q.birthplace = id_place{.country_code = "FR", .admin1_code = "94", .geonameid = std::nullopt}; // Corsica
    q.birth_date = date(1970, 3, 1);
    const auto old_form = one_number(gen, q, 5);
    REQUIRE(old_form.value.substr(5, 2) == "20");
    REQUIRE(gen.is_valid("fr-nir", old_form.value));

    q.birth_date = date(1980, 3, 1);
    const auto new_form = one_number(gen, q, 5);
    REQUIRE((new_form.value.substr(5, 2) == "2A" || new_form.value.substr(5, 2) == "2B"));
    REQUIRE(gen.is_valid("fr-nir", new_form.value));
    REQUIRE(new_form.formatted.size() == 21);

    q.birthplace = id_place{.country_code = "BR", .admin1_code = {}, .geonameid = std::nullopt};
    REQUIRE(one_number(gen, q, 5).value.substr(5, 2) == "99");
}

TEST_CASE("the Argentine CUIL carries the person's DNI", "[builders]")
{
    const auto& gen = shared_generator();
    identity_request q = person("AR", {"ar-dni", "ar-cuil"});
    q.birth_date = date(1990, 5, 5);
    for (std::uint64_t seed = 0; seed < 500; ++seed)
    {
        const auto v = gen.get_identity(q, seed);
        REQUIRE(v.numbers.size() == 2);
        std::string dni = v.numbers[0].value;
        dni.insert(0, 8 - dni.size(), '0');
        REQUIRE(v.numbers[1].value.substr(2, 8) == dni);
    }
    q.safe = true;
    const auto safe = gen.get_identity(q, 1);
    REQUIRE(safe.numbers[1].value.substr(2, 8) == safe.numbers[0].value);
}

TEST_CASE("display forms", "[builders]")
{
    const auto& gen = shared_generator();
    identity_request q = person("CZ", {"cz-rc"});
    q.birth_date = date(1950, 1, 1);
    q.sex = sex::male;
    const auto nine = one_number(gen, q, 1); // before 1954: nine digits
    REQUIRE(nine.value.size() == 9);
    REQUIRE(nine.formatted.size() == 10);
    REQUIRE(nine.formatted[6] == '/');

    q.birth_date = date(1990, 1, 1);
    const auto ten = one_number(gen, q, 1);
    REQUIRE(ten.value.size() == 10);
    REQUIRE(ten.formatted[6] == '/');

    q = person("AR", {"ar-dni"});
    q.birth_date = date(1945, 1, 1); // seven digits drop the leading group
    const auto dni = one_number(gen, q, 1);
    REQUIRE(dni.value.size() == 7);
    REQUIRE(dni.formatted.size() == 9);
    REQUIRE(dni.formatted[1] == '.');
}

// ---------------------------------------------------------------------------
// Coherence
// ---------------------------------------------------------------------------

TEST_CASE("every number fits the person, in every country and at three dates", "[coherence]")
{
    check_coherence(300);
}

TEST_CASE("every number fits the person, 100,000 per country", "[.coherence-100k]")
{
    check_coherence(100000);
}

TEST_CASE("numbers follow their schemes' dates and the person's", "[coherence]")
{
    const auto& gen = shared_generator();
    struct start
    {
        std::string country;
        std::string scheme;
        year_month_day from;
    };
    const std::vector<start> starts{
        {"MX", "mx-curp", date(1996, 10, 23)}, {"VN", "vn-cccd", date(2016, 1, 1)}, {"AE", "ae-eid", date(2005, 1, 1)},
        {"HR", "hr-oib", date(2009, 1, 1)},    {"DE", "de-idnr", date(2007, 1, 1)}, {"IN", "in-aadhaar", date(2010, 9, 1)},
        {"BR", "br-cpf", date(1968, 12, 30)},
    };
    for (const auto& s : starts)
    {
        identity_request q = person(s.country, {s.scheme});
        q.reference_date = year_month_day{sys_days{s.from} - days{1}};
        q.birth_date = date(1950, 1, 1);
        CAPTURE(s.scheme);
        REQUIRE(gen.get_identity(q, 1).numbers.empty()); // the scheme did not exist yet
        q.reference_date = date(2030, 1, 1);
        const auto n = one_number(gen, q, 1);
        REQUIRE(n.assigned >= s.from); // adults get it when it starts
    }

    // Too young for a number given at an age.
    identity_request child = person("GB", {"gb-nino"});
    child.birth_date = date(2015, 1, 1);
    REQUIRE(gen.get_identity(child, 1).numbers.empty());

    // A CPF from birth since December 2015.
    identity_request baby = person("BR", {"br-cpf"});
    baby.birth_date = date(2020, 1, 1);
    const auto cpf = one_number(gen, baby, 1);
    REQUIRE(cpf.assigned <= date(2020, 1, 4));

    // A National Insurance number around the sixteenth birthday.
    identity_request young = person("GB", {"gb-nino"});
    young.birth_date = date(1990, 1, 1);
    const auto nino = one_number(gen, young, 1);
    REQUIRE(nino.assigned >= date(2005, 1, 1));
    REQUIRE(nino.assigned <= date(2007, 1, 10));
}

TEST_CASE("sequential numbers sort by date", "[coherence]")
{
    const auto& gen = shared_generator();
    for (const std::string scheme : {"ar-dni", "cl-run", "pe-dni"})
    {
        const std::string country = scheme == "ar-dni" ? "AR" : (scheme == "cl-run" ? "CL" : "PE");
        identity_request q = person(country, {scheme});
        std::int64_t previous = 0;
        for (int y = 1985; y <= 2005; ++y)
        {
            q.birth_date = date(y, 6, 1);
            q.reference_date = date(2030, 1, 1);
            const std::string value = one_number(gen, q, static_cast<std::uint64_t>(y)).value;
            const std::int64_t number = std::stoll(value.substr(0, scheme == "ar-dni" ? value.size() : value.size() - 1));
            CAPTURE(scheme, y, number, previous);
            REQUIRE(number > previous);
            previous = number;
        }
    }

    // The Social Security group order within one area, before randomization.
    const auto rank = [](int group) {
        if (group % 2 == 1 && group <= 9) return group;
        if (group % 2 == 0 && group >= 10) return 100 + group;
        if (group % 2 == 0) return 200 + group;
        return 300 + group;
    };
    identity_request ssn = person("US", {"us-ssn"});
    ssn.residence = id_place{.country_code = "US", .admin1_code = "NH", .geonameid = std::nullopt};
    std::map<int, std::vector<std::pair<sys_days, int>>> by_area;
    for (std::uint64_t seed = 0; seed < 4000; ++seed)
    {
        const auto v = gen.get_identity(ssn, seed);
        if (v.numbers.empty()) continue;
        const auto& n = v.numbers.front();
        if (n.assigned >= date(2011, 6, 25)) continue;
        by_area[digits(n.value, 0, 3)].emplace_back(sys_days{n.assigned}, rank(digits(n.value, 3, 2)));
    }
    REQUIRE(by_area.size() >= 2);
    for (auto& [area, list] : by_area)
    {
        std::ranges::sort(list);
        REQUIRE(list.size() > 50);
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            for (std::size_t k = i + 1; k < list.size(); ++k)
            {
                if (list[k].first - list[i].first < days{30}) continue;
                if (list[k].second < list[i].second) FAIL("area " << area << " group order");
            }
        }
    }
}

TEST_CASE("after the 2011 randomization the SSN encodes no place", "[coherence]")
{
    const auto& gen = shared_generator();
    identity_request q = person("US", {"us-ssn"});
    q.birth_date = date(2015, 1, 1);
    q.reference_date = date(2040, 1, 1);
    const auto n = one_number(gen, q, 1);
    REQUIRE(n.place_code.empty());
    REQUIRE(gen.is_valid("us-ssn", n.value));
}

// ---------------------------------------------------------------------------
// Safe numbers
// ---------------------------------------------------------------------------

TEST_CASE("safe numbers follow each scheme's declared method", "[safe]")
{
    const auto& gen = shared_generator();
    const std::set<std::string> rejected_by_date{"dk-cpr", "kr-rrn", "vn-cccd"};
    const std::set<std::string> accepted{"ar-dni", "de-idnr", "fi-hetu", "gb-nino", "id-nik", "is-kennitala", "my-mykad",
                                         "no-fodselsnummer", "pk-cnic", "se-personnummer", "us-ssn"};
    int seen = 0;
    for (const auto reference : {date(2015, 6, 1), date(2023, 7, 1), date(2085, 1, 1)})
    {
        for (const auto& country : gen.number_countries())
        {
            identity_request q = person(country, {"*"});
            q.safe = true;
            q.reference_date = reference;
            for (std::uint64_t seed = 0; seed < 100; ++seed)
            {
                const auto v = gen.get_identity(q, seed);
                for (const auto& n : v.numbers)
                {
                    CAPTURE(describe(n, v));
                    if (!n.reserved) FAIL("not reserved");
                    const bool valid = gen.is_valid(n.scheme, n.value, reference);
                    if (valid != accepted.contains(n.scheme)) FAIL("is_valid " << valid);
                    if (rejected_by_date.contains(n.scheme) && reference.year() < year{2080} &&
                        !gen.is_valid(n.scheme, n.value) && n.scheme != "dk-cpr")
                    {
                        FAIL("only the reference date should reject this number");
                    }
                    ++seen;
                }
            }
        }
    }
    REQUIRE(seen > 10000);
}

TEST_CASE("safe numbers use the reserved ranges and lists", "[safe]")
{
    const auto& gen = shared_generator();
    const auto safe_person = [](std::string_view country, const std::string& scheme) {
        identity_request q = person(country, {scheme});
        q.safe = true;
        q.birth_date = date(1980, 5, 5); // an adult: every scheme has been assigned
        q.reference_date = date(2023, 7, 1);
        return q;
    };
    std::set<std::string> se_list;
    std::set<std::string> is_list;
    const auto test_list_doc = dasmig::core::tsv_document::load(resource_dir("lite") / "test_numbers.tsv");
    for (const auto& row : test_list_doc.rows())
    {
        (row["scheme"] == "se-personnummer" ? se_list : is_list).emplace(row["value"]);
    }
    std::set<std::pair<std::string, std::string>> reserved_places;
    const auto places_doc = dasmig::core::tsv_document::load(resource_dir("lite") / "places.tsv");
    for (const auto& row : places_doc.rows())
    {
        if (row["reserved"] == "1") reserved_places.emplace(row["scheme"], row["code"]);
    }
    for (std::uint64_t seed = 0; seed < 500; ++seed)
    {
        CAPTURE(seed);
        REQUIRE(one_number(gen, safe_person("DE", "de-idnr"), seed).value.front() == '0');
        REQUIRE(digits(one_number(gen, safe_person("FI", "fi-hetu"), seed).value, 7, 3) >= 900);
        REQUIRE(one_number(gen, safe_person("GB", "gb-nino"), seed).value.starts_with("QQ"));
        REQUIRE(digits(one_number(gen, safe_person("NO", "no-fodselsnummer"), seed).value, 2, 2) > 80);
        const auto us = one_number(gen, safe_person("US", "us-ssn"), seed);
        REQUIRE((us.value.starts_with("000") || us.value.starts_with("666")));
        REQUIRE(se_list.contains(one_number(gen, safe_person("SE", "se-personnummer"), seed).value));
        REQUIRE(is_list.contains(one_number(gen, safe_person("IS", "is-kennitala"), seed).value));
        for (const auto& [country, scheme] : std::vector<std::pair<std::string, std::string>>{{"ID", "id-nik"}, {"MY", "my-mykad"}, {"PK", "pk-cnic"}})
        {
            REQUIRE(reserved_places.contains({scheme, one_number(gen, safe_person(country, scheme), seed).place_code}));
        }
        const auto dni = std::stoll(one_number(gen, safe_person("AR", "ar-dni"), seed).value);
        REQUIRE(dni > 59000000);
        REQUIRE(dni < 90000000);
        REQUIRE((dni < 60000000 || dni >= 70000000));
    }

    // A test personnummer near the person's birth date, of the person's sex.
    identity_request q = safe_person("SE", "se-personnummer");
    q.birth_date = date(1980, 5, 5);
    q.sex = sex::female;
    for (std::uint64_t seed = 0; seed < 50; ++seed)
    {
        const std::string value = one_number(gen, q, seed).value;
        REQUIRE(digits(value, 10, 1) % 2 == 0);
    }

    // A nine-digit birth number has no check digit to get wrong: the date is impossible.
    q = safe_person("CZ", "cz-rc");
    q.birth_date = date(1950, 6, 15);
    REQUIRE_FALSE(gen.is_valid("cz-rc", one_number(gen, q, 1).value));
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

TEST_CASE("the country comes from the request or from the scheme keys", "[requests]")
{
    const auto& gen = shared_generator();
    REQUIRE(gen.get_identity(person("it", {"it-cf"}), 1).country_code == "IT");
    identity_request q;
    q.schemes = std::vector<std::string>{"it-cf"};
    REQUIRE(gen.get_identity(q, 1).country_code == "IT");

    identity_request none;
    none.schemes = std::vector<std::string>{};
    std::set<std::string> served;
    for (const auto& c : gen.number_countries()) served.insert(c);
    bool unserved_seen = false;
    for (std::uint64_t seed = 0; seed < 2000 && !unserved_seen; ++seed)
    {
        const auto v = gen.get_identity(none, seed);
        REQUIRE(v.numbers.empty());
        unserved_seen = !served.contains(v.country_code);
    }
    REQUIRE(unserved_seen);

    // A country without a scheme gets no number and no error.
    REQUIRE(gen.get_identity(identity_request{.country = "AD"}, 1).numbers.empty());
    REQUIRE(gen.get_identity(person("AD", {"*"}), 1).numbers.empty());
}

TEST_CASE("the primary number comes first, and * gives them all", "[requests]")
{
    const auto& gen = shared_generator();
    identity_request q{.country = "AR"};
    q.birth_date = date(1990, 1, 1);
    REQUIRE(one_number(gen, q, 1).scheme == "ar-dni");
    q.schemes = std::vector<std::string>{"ar-cuil", "ar-dni"};
    auto v = gen.get_identity(q, 1);
    REQUIRE(v.numbers[0].scheme == "ar-dni");
    REQUIRE(v.numbers[1].scheme == "ar-cuil");
    q.schemes = std::vector<std::string>{"*"};
    v = gen.get_identity(q, 1);
    REQUIRE(v.numbers.size() == 2);
    REQUIRE(v.numbers[0].scheme == "ar-dni");
}

TEST_CASE("given values are honoured and shift no other stream", "[requests]")
{
    const auto& gen = shared_generator();
    identity_request base = person("PL", {"pl-pesel"});
    base.reference_date = date(2030, 1, 1);
    const auto first = gen.get_identity(base, 99);

    identity_request given = base;
    given.sex = first.bio_sex;
    REQUIRE(gen.get_identity(given, 99).to_json() == first.to_json());
    given = base;
    given.birth_date = first.birth_date;
    REQUIRE(gen.get_identity(given, 99).to_json() == first.to_json());

    // Another sex moves the birth-date stream nowhere.
    given = base;
    given.sex = first.bio_sex == sex::male ? sex::female : sex::male;
    const auto other = gen.get_identity(given, 99);
    REQUIRE(other.birth_date == first.birth_date);
    REQUIRE(other.bio_sex == *given.sex);

    // Another birth date moves the sex stream nowhere.
    given = base;
    given.birth_date = date(1970, 1, 1);
    REQUIRE(gen.get_identity(given, 99).bio_sex == first.bio_sex);

    // A given birthplace leaves the rest alone; the birth date is still the drawn one.
    identity_request place = person("CN", {"cn-ric"});
    const auto drawn = gen.get_identity(place, 99);
    place.birthplace = id_place{.country_code = "CN", .admin1_code = {}, .geonameid = std::nullopt};
    const auto country_only = gen.get_identity(place, 99);
    REQUIRE(country_only.to_json() == drawn.to_json()); // only the country: nothing to use
}

TEST_CASE("adding a scheme leaves the others unchanged", "[requests]")
{
    const auto& gen = shared_generator();
    identity_request q = person("AR", {"ar-dni"});
    q.birth_date = date(1990, 1, 1);
    const auto one = gen.get_identity(q, 5);
    q.schemes = std::vector<std::string>{"ar-dni", "ar-cuil"};
    const auto two = gen.get_identity(q, 5);
    REQUIRE(two.numbers.size() == 2);
    REQUIRE(two.numbers[0].value == one.numbers[0].value);
    REQUIRE(two.numbers[0].assigned == one.numbers[0].assigned);
    REQUIRE(two.birth_date == one.birth_date);
    REQUIRE(two.bio_sex == one.bio_sex);
}

TEST_CASE("places: a city, a region, a birth abroad, or a draw", "[requests]")
{
    const auto& gen = shared_generator();
    identity_request q = person("BR", {"br-cpf"});
    q.birth_date = date(1980, 1, 1);
    q.residence = id_place{.country_code = "BR", .admin1_code = "27", .geonameid = std::nullopt}; // Sao Paulo
    for (std::uint64_t seed = 0; seed < 50; ++seed) REQUIRE(one_number(gen, q, seed).place_code == "8");

    // A residence the scheme has no region for falls back to a draw.
    q.residence = id_place{.country_code = "BR", .admin1_code = "no such region", .geonameid = std::nullopt};
    REQUIRE_FALSE(one_number(gen, q, 1).place_code.empty());

    // A residence abroad cannot say anything about the Brazilian region: a draw.
    q.residence = id_place{.country_code = "PT", .admin1_code = "27", .geonameid = std::nullopt};
    REQUIRE_FALSE(one_number(gen, q, 1).place_code.empty());

    // The birthplace serves the residence when it is in the country, and the other way round.
    q.residence = std::nullopt;
    q.birthplace = id_place{.country_code = "BR", .admin1_code = "27", .geonameid = std::nullopt};
    REQUIRE(one_number(gen, q, 1).place_code == "8");

    identity_request born = person("IT", {"it-cf"});
    born.residence = id_place{.country_code = "IT", .admin1_code = {}, .geonameid = 3169070U};
    REQUIRE(one_number(gen, born, 1).place_code == "H501");

    // Born abroad: the code for that country, or a rejection.
    born.residence = std::nullopt;
    born.birthplace = id_place{.country_code = "br", .admin1_code = {}, .geonameid = std::nullopt};
    REQUIRE(one_number(gen, born, 1).place_code == "Z602");

    identity_request cn = person("CN", {"cn-ric"});
    cn.birthplace = id_place{.country_code = "BR", .admin1_code = {}, .geonameid = std::nullopt};
    REQUIRE_THROWS_AS(gen.get_identity(cn, 1), std::invalid_argument);
}

TEST_CASE("place draws honour eras", "[requests]")
{
    const auto& gen = shared_generator();
    // Yugoslav areas of Bosnia ended with their register; Korea's places stop with 2020's random numbers.
    identity_request q = person("KR", {"kr-rrn"});
    q.birth_date = date(2021, 1, 1);
    q.reference_date = date(2030, 1, 1);
    REQUIRE(one_number(gen, q, 1).place_code.empty());
    q.birth_date = date(1990, 1, 1);
    REQUIRE_FALSE(one_number(gen, q, 1).place_code.empty());
}

TEST_CASE("invalid requests are rejected", "[requests][errors]")
{
    const auto& gen = shared_generator();
    REQUIRE_THROWS_AS(gen.get_identity(identity_request{.country = "ZZ"}, 1), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.get_identity(person("BR", {"xx-none"}), 1), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.get_identity(person("BR", {"it-cf"}), 1), std::invalid_argument);

    identity_request q;
    q.schemes = std::vector<std::string>{"xx-none"};
    REQUIRE_THROWS_AS(gen.get_identity(q, 1), std::invalid_argument);

    q = person("BR", {"br-cpf"});
    q.birth_date = date(2030, 1, 1);
    REQUIRE_THROWS_AS(gen.get_identity(q, 1), std::invalid_argument);
    q.birth_date = date(1899, 12, 31);
    REQUIRE_THROWS_AS(gen.get_identity(q, 1), std::invalid_argument);
    q.birth_date = year{2001} / month{2} / day{30};
    REQUIRE_THROWS_AS(gen.get_identity(q, 1), std::invalid_argument);

    q = person("BR", {"br-cpf"});
    q.reference_date = year{2001} / month{2} / day{30};
    REQUIRE_THROWS_AS(gen.get_identity(q, 1), std::invalid_argument);
    q.reference_date = date(1899, 12, 31);
    REQUIRE_THROWS_AS(gen.get_identity(q, 1), std::invalid_argument);
    q.reference_date = date(2091, 1, 1);
    REQUIRE_THROWS_AS(gen.get_identity(q, 1), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Documents
// ---------------------------------------------------------------------------

using dasmig::id_document;
using dasmig::id_document_kind;
namespace idd = dasmig::idgen_detail;

namespace
{

// Dates and tables read here, not through the library's code.
year_month_day years_later(const year_month_day& d, int n)
{
    const year_month_day y{d.year() + std::chrono::years{n}, d.month(), d.day()};
    return y.ok() ? y : year_month_day{y.year(), y.month(), std::chrono::day{28}};
}

int age_at(const year_month_day& born, const year_month_day& on)
{
    const int years = static_cast<int>(on.year()) - static_cast<int>(born.year());
    const bool before = std::pair{static_cast<unsigned>(on.month()), static_cast<unsigned>(on.day())} <
                        std::pair{static_cast<unsigned>(born.month()), static_cast<unsigned>(born.day())};
    return years - (before ? 1 : 0);
}

year_month_day read_date(std::string_view text)
{
    const auto number = [&](std::size_t at, std::size_t len) {
        int v = 0;
        for (const char c : text.substr(at, len)) v = (v * 10) + (c - '0');
        return v;
    };
    return year_month_day{std::chrono::year{number(0, 4)}, std::chrono::month{static_cast<unsigned>(number(5, 2))},
                          std::chrono::day{static_cast<unsigned>(number(8, 2))}};
}

struct format_row
{
    std::string key;
    std::string country;
    std::string kind;
    dasmig::core::pattern pattern;
    year_month_day from;
    std::optional<year_month_day> until;
    std::string mrz;
    std::string evidence;
    std::string mrz_code;
    std::string optional_scheme;
    int min_age{0};
    std::string number_scheme{};
};

struct validity_row
{
    int from_age{0};
    std::string years;
    int until_age{-1};
    year_month_day from;
    std::optional<year_month_day> until;
    std::string rule;
};

struct document_tables
{
    std::map<std::string, format_row> formats;
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> by_country; // (country, kind) -> keys
    std::map<std::string, std::vector<validity_row>> validity;                          // "de-id-card" -> rows
};

const document_tables& tables_on_disk()
{
    static const document_tables tables = [] {
        document_tables t;
        const auto docs = dasmig::core::tsv_document::load(resource_dir("lite") / "documents.tsv");
        for (const auto& row : docs.rows())
        {
            format_row f{.key = std::string{row["key"]},
                         .country = std::string{row["country"]},
                         .kind = std::string{row["kind"]},
                         .pattern = dasmig::core::pattern::parse(row["pattern"]),
                         .from = read_date(row["from"]),
                         .until = row["until"].empty() ? std::nullopt : std::optional{read_date(row["until"])},
                         .mrz = std::string{row["mrz"]},
                         .evidence = std::string{row["evidence"]},
                         .mrz_code = std::string{row["mrz_code"]},
                         .optional_scheme = std::string{row["mrz_optional"]},
                         .min_age = row["min_age"].empty() ? 0 : row.get<int>("min_age"),
                         .number_scheme = std::string{row["number_scheme"]}};
            t.by_country[{f.country, f.kind}].push_back(f.key);
            const std::string key = f.key;
            t.formats.emplace(key, std::move(f));
        }
        const auto rules = dasmig::core::tsv_document::load(resource_dir("lite") / "validity.tsv");
        for (const auto& row : rules.rows())
        {
            t.validity[std::string{row["document"]}].push_back(
                {.from_age = row.get<int>("from_age"),
                 .years = std::string{row["years"]},
                 .until_age = row["until_age"].empty() ? -1 : row.get<int>("until_age"),
                 .from = read_date(row["from"]),
                 .until = row["until"].empty() ? std::nullopt : std::optional{read_date(row["until"])},
                 .rule = std::string{row["expiry_rule"]}});
        }
        return t;
    }();
    return tables;
}

std::string yymmdd(const year_month_day& d)
{
    return std::format("{:02}{:02}{:02}", static_cast<int>(d.year()) % 100, static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
}

std::string document_name(std::string country, id_document_kind kind)
{
    for (char& c : country) c = static_cast<char>(c - 'A' + 'a');
    return country + (kind == id_document_kind::passport ? "-passport" : "-id-card");
}

std::string strip_fillers(std::string_view text)
{
    std::string out{text};
    while (!out.empty() && out.back() == '<') out.pop_back();
    return out;
}

std::string letters_and_digits(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

// The zone read back field by field, each check digit recomputed.
void check_mrz(const id_document& d, const year_month_day& born, dasmig::sex bio_sex, const format_row& format)
{
    using dasmig::core::icao9303_digit;
    const std::string expires = d.expires ? yymmdd(*d.expires) : "<<<<<<";
    const std::string state = d.issuing_state + std::string(3 - d.issuing_state.size(), '<');
    const std::string code = !format.mrz_code.empty() ? format.mrz_code : (d.kind == id_document_kind::passport ? "P<" : "ID");
    const char sex_letter = bio_sex == dasmig::sex::male ? 'M' : 'F';
    const std::string number = letters_and_digits(d.number);
    std::string number_field;
    std::string overflow;
    const auto read_number = [&](std::string_view field, char check, std::string_view optional) {
        number_field = strip_fillers(field);
        overflow.clear();
        if (check == '<')
        {
            // The rest of the number, then its digit over all of it, then a filler.
            const auto end = optional.find('<');
            REQUIRE(end != std::string_view::npos);
            REQUIRE(end >= 2);
            overflow = std::string{optional.substr(0, end - 1)};
            REQUIRE(optional[end - 1] == icao9303_digit(std::string{field} + overflow));
        }
        else
        {
            REQUIRE(check == icao9303_digit(field));
        }
        REQUIRE(number_field + overflow == number);
    };
    if (format.mrz == "td3")
    {
        REQUIRE(d.mrz.size() == 2);
        const std::string& l1 = d.mrz[0];
        const std::string& l2 = d.mrz[1];
        REQUIRE(l1.size() == 44);
        REQUIRE(l2.size() == 44);
        REQUIRE(l1.substr(0, 2) == code);
        REQUIRE(l1.substr(2, 3) == state);
        read_number(std::string_view{l2}.substr(0, 9), l2[9], std::string_view{l2}.substr(28, 14));
        REQUIRE(l2.substr(10, 3) == state);
        REQUIRE(l2.substr(13, 6) == yymmdd(born));
        REQUIRE(l2[19] == icao9303_digit(l2.substr(13, 6)));
        REQUIRE(l2[20] == sex_letter);
        REQUIRE(l2.substr(21, 6) == expires);
        REQUIRE(l2[27] == icao9303_digit(l2.substr(21, 6)));
        REQUIRE(l2[42] == icao9303_digit(l2.substr(28, 14)));
        REQUIRE(l2[43] == icao9303_digit(l2.substr(0, 10) + l2.substr(13, 7) + l2.substr(21, 22)));
    }
    else
    {
        REQUIRE(format.mrz == "td1");
        REQUIRE(d.mrz.size() == 3);
        const std::string& l1 = d.mrz[0];
        const std::string& l2 = d.mrz[1];
        REQUIRE(l1.size() == 30);
        REQUIRE(l2.size() == 30);
        REQUIRE(d.mrz[2].size() == 30);
        REQUIRE(l1.substr(0, 2) == code);
        REQUIRE(l1.substr(2, 3) == state);
        read_number(std::string_view{l1}.substr(5, 9), l1[14], std::string_view{l1}.substr(15, 15));
        REQUIRE(l2.substr(0, 6) == yymmdd(born));
        REQUIRE(l2[6] == icao9303_digit(l2.substr(0, 6)));
        REQUIRE(l2[7] == sex_letter);
        REQUIRE(l2.substr(8, 6) == expires);
        REQUIRE(l2[14] == icao9303_digit(l2.substr(8, 6)));
        REQUIRE(l2.substr(15, 3) == state);
        REQUIRE(l2[29] == icao9303_digit(l1.substr(5, 25) + l2.substr(0, 7) + l2.substr(8, 7) + l2.substr(18, 11)));
    }
}

// The dates of a document against validity.tsv, or the generic rule.
void check_dates(const id_document& d, const year_month_day& born, const year_month_day& reference, const format_row& format, const std::string& country)
{
    REQUIRE(sys_days{d.issued} <= sys_days{reference});
    REQUIRE(sys_days{d.issued} >= sys_days{born});
    if (d.expires) REQUIRE(sys_days{*d.expires} > sys_days{reference});
    REQUIRE(age_at(born, d.issued) >= format.min_age);
    REQUIRE(sys_days{d.issued} >= sys_days{format.from});
    if (format.until) REQUIRE(sys_days{d.issued} < sys_days{*format.until});

    const auto& tables = tables_on_disk();
    const auto rows = tables.validity.find(document_name(country, d.kind));
    REQUIRE(d.generic_validity == (rows == tables.validity.end()));
    const int age = age_at(born, d.issued);
    if (d.generic_validity)
    {
        REQUIRE(d.expires);
        const int years = d.kind == id_document_kind::id_card ? 10 : (age < 16 ? 5 : 10);
        REQUIRE(*d.expires == years_later(d.issued, years));
        return;
    }
    const validity_row* rule = nullptr;
    for (const auto& row : rows->second)
    {
        const bool in_force = sys_days{row.from} <= sys_days{d.issued} && (!row.until || sys_days{d.issued} < sys_days{*row.until});
        if (in_force && row.from_age <= age && (rule == nullptr || row.from_age > rule->from_age)) rule = &row;
    }
    REQUIRE(rule != nullptr);
    const int minus = rule->rule == "day_before" ? 1 : 0;
    if (rule->until_age >= 0)
    {
        REQUIRE(d.expires);
        REQUIRE(sys_days{*d.expires} == sys_days{years_later(born, rule->until_age)} - days{minus});
        return;
    }
    bool matches = false;
    std::string_view choices = rule->years;
    while (!choices.empty())
    {
        const auto bar = choices.find('|');
        int years = 0;
        for (const char c : choices.substr(0, bar)) years = (years * 10) + (c - '0');
        choices = bar == std::string_view::npos ? std::string_view{} : choices.substr(bar + 1);
        if (years == 0)
        {
            matches = matches || !d.expires;
        }
        else if (d.expires && rule->rule == "birthday")
        {
            const auto on_time = sys_days{years_later(d.issued, years)};
            matches = matches || (sys_days{*d.expires} >= on_time && sys_days{*d.expires} < on_time + days{367} &&
                                  (d.expires->month() == born.month()) && (d.expires->day() == born.day() || born.day() == std::chrono::day{29}));
        }
        else if (d.expires && rule->rule == "application")
        {
            for (int back = 0; back <= 30; ++back)
            {
                matches = matches || *d.expires == years_later(year_month_day{sys_days{d.issued} - days{back}}, years);
            }
        }
        else if (d.expires)
        {
            matches = matches || sys_days{*d.expires} == sys_days{years_later(d.issued, years)} - days{minus};
        }
    }
    CAPTURE(d.format, age, idd::iso_date(idd::to_day(d.issued)));
    REQUIRE(matches);
}

identity_request named_person(std::string_view country)
{
    identity_request q;
    q.country = std::string{country};
    q.schemes = std::vector<std::string>{};
    q.documents = {id_document_kind::passport, id_document_kind::id_card};
    q.surname = "Garcia Lopez";
    q.given_names = "Maria Jose";
    return q;
}

const id_document* find_document(const dasmig::identity& v, id_document_kind kind)
{
    for (const auto& d : v.documents)
    {
        if (d.kind == kind) return &d;
    }
    return nullptr;
}

} // namespace

TEST_CASE("the MRZ reproduces ICAO's worked examples", "[documents][mrz]")
{
    // Doc 9303 Part 4, Appendix: the specimen passport of Utopia.
    idd::mrz_fields td3{.code = "PP",
                        .state = "UTO",
                        .name = idd::mrz_name("ERIKSSON", "ANNA<MARIA", 39),
                        .number = "L898902C3",
                        .birth = "740812",
                        .sex = 'F',
                        .expiry = "340415",
                        .optional = "ZE184226B"};
    REQUIRE(idd::mrz_td3(td3) == std::vector<std::string>{"PPUTOERIKSSON<<ANNA<MARIA<<<<<<<<<<<<<<<<<<<", "L898902C36UTO7408122F3404159ZE184226B<<<<<16"});

    // Doc 9303 Part 5: the specimen card of Utopia.
    idd::mrz_fields td1{.code = "I<",
                        .state = "UTO",
                        .name = idd::mrz_name("ERIKSSON", "ANNA<MARIA", 30),
                        .number = "D23145890",
                        .birth = "740812",
                        .sex = 'F',
                        .expiry = "120415",
                        .optional = ""};
    REQUIRE(idd::mrz_td1(td1) == std::vector<std::string>{"I<UTOD231458907<<<<<<<<<<<<<<<", "7408122F1204159UTO<<<<<<<<<<<6", "ERIKSSON<<ANNA<MARIA<<<<<<<<<<"});

    // A long number overflows: nine in the field, a filler for the check digit,
    // the rest and a digit over all twelve characters in the optional field.
    td1.number = "D23145890734";
    const auto long_card = idd::mrz_td1(td1);
    REQUIRE(long_card[0].substr(0, 20) == "I<UTOD23145890<7349<");
    REQUIRE(long_card[0].size() == 30);
    REQUIRE(long_card[1].size() == 30);

    // A personal number takes the upper line, and the middle one after an overflow leaves no room.
    td1.number = "D23145890";
    td1.optional = "12345678901";
    REQUIRE(idd::mrz_td1(td1)[0].substr(15) == "12345678901<<<<");
    td1.number = "D23145890734";
    const auto both = idd::mrz_td1(td1);
    REQUIRE(both[0].substr(15, 5) == "7349<");
    REQUIRE(both[1].substr(18, 11) == "12345678901");
}

TEST_CASE("long names are shortened as ICAO's examples show", "[documents][mrz]")
{
    const auto line = [](std::string_view primary, std::string_view secondary) { return "PPUTO" + idd::mrz_name(primary, secondary, 39); };
    // Doc 9303 Part 4, 4.2.3.
    REQUIRE(line("NILAVADHANANANDA", "CHAYAPA<DEJTHAMRONG<KRASUANG") == "PPUTONILAVADHANANANDA<<CHAYAPA<DEJTHAMRONG<K");
    REQUIRE(line("NILAVADHANANANDA", "ARNPOL<PETCH<CHARONGUANG") == "PPUTONILAVADHANANANDA<<ARNPOL<PETCH<CHARONGU");
    REQUIRE(line("BENNELONG<WOOLOOMOOLOO<WARRANDYTE<WARNAMBOOL", "DINGO<POTOROO") == "PPUTOBENNELONG<WOOLOOMOOLOO<WARRANDYTE<W<<DI");
    REQUIRE(line("PAPANDROPOULOUS", "JONATHON<WARREN<TREVOR") == "PPUTOPAPANDROPOULOUS<<JONATHON<WARREN<TREVOR");
    REQUIRE(line("ARKFREITH", "") == "PPUTOARKFREITH<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
    // The last character of a shortened name is a letter.
    const std::string cut = idd::mrz_name("AAAAAAAAAAAAAAAAAAAAAAAA", "BBBBBBBBBBBB<CCCC", 39);
    REQUIRE(cut.size() == 39);
    REQUIRE(cut == "AAAAAAAAAAAAAAAAAAAAAAAA<<BBBBBBBBBBB<C");
}

TEST_CASE("names are written in the MRZ's alphabet", "[documents][mrz]")
{
    const std::vector<idd::translit_rule> none;
    const auto word = [&](std::string_view name) { return idd::mrz_words(name, none).value_or("(none)"); };
    // Doc 9303 Part 4, 4.2.3.1 and Part 3, section 6.A.
    REQUIRE(word("Smith-Jones") == "SMITH<JONES");
    REQUIRE(word("O'Connor") == "OCONNOR");
    REQUIRE(word("O’Connor") == "OCONNOR");
    REQUIRE(word("van der Muellen") == "VAN<DER<MUELLEN");
    REQUIRE(word("Müller") == "MUELLER");
    REQUIRE(word("Åström") == "AASTROEM");
    REQUIRE(word("Muñoz") == "MUNOZ");
    REQUIRE(word("Søren") == "SOEREN");
    REQUIRE(word("Þór") == "THOR");
    REQUIRE(word("Straße") == "STRASSE");
    REQUIRE(word("Œuvre") == "OEUVRE");
    REQUIRE(word("Łukasz") == "LUKASZ");
    REQUIRE(word("Çelik") == "CELIK");
    REQUIRE(word("Ștefan") == "STEFAN");
    REQUIRE(word("Nguyễn") == "NGUYEN");
    // Section 6.B; the soft sign is not in the table and is written as nothing.
    REQUIRE(word("Щербаков") == "SHCHERBAKOV");
    REQUIRE(word("Юрий") == "IURII");
    REQUIRE(word("ИЛЬЯ") == "ILIA");
    REQUIRE(word("Хрущёв") == "KHRUSHCHEV");
    // No form: Greek has no table, Han and Arabic need a Latin form from the caller.
    REQUIRE(word("Γιώργος") == "(none)");
    REQUIRE(word("王伟") == "(none)");
    REQUIRE(word("محمد") == "(none)");
    REQUIRE(word("...") == "(none)");
    REQUIRE(word("Ann\xC3") == "(none)"); // malformed UTF-8
}

TEST_CASE("national transliteration rules apply to the issuing country", "[documents][mrz]")
{
    const auto& gen = shared_generator();
    const auto name_line = [&](std::string_view country, const std::string& surname) {
        identity_request q;
        q.country = std::string{country};
        q.schemes = std::vector<std::string>{};
        q.documents = {id_document_kind::passport};
        q.surname = surname;
        q.given_names = "Ann";
        q.birth_date = date(1980, 1, 1);
        return gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.front();
    };
    // Doc 9303 Part 3, section 6.B's exceptions.
    REQUIRE(name_line("RU", "Григорьев").substr(5).starts_with("GRIGOREV<<ANN"));
    REQUIRE(name_line("UA", "Григор").substr(5).starts_with("HRYHOR<<ANN"));
    REQUIRE(name_line("UA", "Яремчук").substr(5).starts_with("YAREMCHUK<<ANN"));
    REQUIRE(name_line("UA", "Коляда").substr(5).starts_with("KOLIADA<<ANN")); // not the first letter
    REQUIRE(name_line("BG", "Щерев").substr(5).starts_with("SHTEREV<<ANN"));
    REQUIRE(name_line("BY", "Гёра").substr(5).starts_with("HIORA<<ANN"));
    REQUIRE(name_line("RS", "Жарков").substr(5).starts_with("ZARKOV<<ANN"));
    REQUIRE(name_line("RS", "Горан").substr(5).starts_with("GORAN<<ANN")); // the table's H is left out
    REQUIRE(name_line("MK", "Ќосе").substr(5).starts_with("KJOSE<<ANN"));
}

TEST_CASE("every country's documents fit its tables", "[documents][coherence]")
{
    const auto& gen = shared_generator();
    const auto& tables = tables_on_disk();
    const auto reference_dates = {date(2005, 6, 1), date(2015, 6, 1), date(2023, 7, 1)};
    const auto countries = gen.countries();
    REQUIRE(countries.size() == 199);
    std::map<std::pair<std::string, std::string>, int> seen;
    int checked = 0;
    for (const auto& country : countries)
    {
        CAPTURE(country);
        dasmig::core::rng ages{0xD0C5ULL + static_cast<std::uint64_t>(country[0]) + (static_cast<std::uint64_t>(country[1]) << 8U)};
        for (const auto& reference : reference_dates)
        {
            for (std::uint64_t seed = 0; seed < 40; ++seed)
            {
                identity_request q = named_person(country);
                q.reference_date = reference;
                // Half the people at the library's own ages, half at any age up to a hundred.
                if (seed % 2 == 1)
                {
                    q.birth_date = year_month_day{sys_days{reference} - days{dasmig::core::uniform_int(ages, 0, 36524)}};
                }
                const auto v = gen.get_identity(q, seed);
                for (const auto& d : v.documents)
                {
                    const auto kind_name = d.kind == id_document_kind::passport ? "passport" : "id_card";
                    CAPTURE(d.format, d.number);
                    const auto format = tables.formats.find(d.format);
                    REQUIRE(format != tables.formats.end());
                    REQUIRE(format->second.country == country);
                    REQUIRE(format->second.kind == kind_name);
                    if (format->second.number_scheme.empty()) REQUIRE(format->second.pattern.matches(d.number));
                    REQUIRE(d.evidence == format->second.evidence);
                    REQUIRE((d.evidence == "generic") == d.format.contains("generic"));
                    REQUIRE_FALSE(d.reserved);
                    check_dates(d, v.birth_date, reference, format->second, country);
                    if (format->second.mrz == "none")
                    {
                        REQUIRE(d.mrz.empty());
                    }
                    else
                    {
                        check_mrz(d, v.birth_date, v.bio_sex, format->second);
                    }
                    ++seen[{country, kind_name}];
                    ++checked;
                }
                // A document exists exactly where the tables have one and the person is old enough.
                for (const auto kind : {id_document_kind::passport, id_document_kind::id_card})
                {
                    const auto keys = tables.by_country.find({country, kind == id_document_kind::passport ? "passport" : "id_card"});
                    const bool held = find_document(v, kind) != nullptr;
                    if (keys == tables.by_country.end())
                    {
                        REQUIRE_FALSE(held);
                    }
                    else
                    {
                        int min_age = 200;
                        for (const auto& key : keys->second) min_age = std::min(min_age, tables.formats.at(key).min_age);
                        CAPTURE(min_age, idd::iso_date(idd::to_day(v.birth_date)));
                        REQUIRE(held == (age_at(v.birth_date, reference) >= min_age));
                    }
                }
            }
        }
        REQUIRE(seen[{country, "passport"}] > 0);
    }
    // The survey's counts: every country has a passport, and 182 issue a card.
    int with_card = 0;
    for (const auto& country : countries) with_card += seen.contains({country, "id_card"}) ? 1 : 0;
    REQUIRE(with_card == 182);
    REQUIRE(checked > 35000);
}

TEST_CASE("documents follow the rules the survey found", "[documents]")
{
    const auto& gen = shared_generator();
    const auto document = [&](std::string_view country, id_document_kind kind, const year_month_day& born, std::uint64_t seed, const year_month_day& reference = date(2023, 7, 1)) {
        identity_request q = named_person(country);
        q.documents = {kind};
        q.birth_date = born;
        q.reference_date = reference;
        return gen.get_identity(q, seed);
    };

    SECTION("no card where none is issued, or below the minimum age")
    {
        REQUIRE(document("DK", id_document_kind::id_card, date(1980, 1, 1), 1).documents.empty());
        REQUIRE(document("IE", id_document_kind::id_card, date(1980, 1, 1), 1).documents.empty());
        REQUIRE(document("RU", id_document_kind::id_card, date(2010, 1, 1), 1).documents.empty());
        REQUIRE(document("RU", id_document_kind::id_card, date(2005, 1, 1), 1).documents.size() == 1);
        REQUIRE(document("DK", id_document_kind::passport, date(1980, 1, 1), 1).documents.size() == 1);
    }
    SECTION("Spain's card is permanent from 70, and the zone shows no expiry")
    {
        int permanent = 0;
        for (std::uint64_t seed = 0; seed < 200; ++seed)
        {
            const auto v = document("ES", id_document_kind::id_card, date(1935, 3, 3), seed);
            const auto& d = v.documents.front();
            if (d.expires) continue;
            ++permanent;
            REQUIRE(age_at(v.birth_date, d.issued) >= 70);
            REQUIRE(d.mrz.size() == 3);
            REQUIRE(d.mrz[1].substr(8, 7) == "<<<<<<0");
        }
        REQUIRE(permanent > 100);
    }
    SECTION("Russia's series holds the year of issue, and the card runs to the next age")
    {
        for (std::uint64_t seed = 0; seed < 300; ++seed)
        {
            const auto v = document("RU", id_document_kind::id_card, date(1990, 6, 15), seed);
            const auto& d = v.documents.front();
            REQUIRE(d.number.substr(2, 2) == std::format("{:02}", static_cast<int>(d.issued.year()) % 100));
            REQUIRE(d.mrz.empty());
            REQUIRE(age_at(v.birth_date, d.issued) >= 20);
            REQUIRE(*d.expires == date(2035, 6, 15));
        }
    }
    SECTION("Hungary's card ends on a birthday")
    {
        for (std::uint64_t seed = 0; seed < 100; ++seed)
        {
            const auto v = document("HU", id_document_kind::id_card, date(1980, 4, 9), seed);
            const auto expires = *v.documents.front().expires;
            REQUIRE(expires.month() == std::chrono::April);
            REQUIRE(expires.day() == std::chrono::day{9});
        }
    }
    SECTION("the format follows the date of issue")
    {
        std::set<std::string> formats;
        for (std::uint64_t seed = 0; seed < 300; ++seed)
        {
            const auto v = document("DE", id_document_kind::passport, date(1970, 1, 1), seed, date(2012, 1, 1));
            const auto& d = v.documents.front();
            formats.insert(d.format);
            REQUIRE(d.format == (sys_days{d.issued} >= sys_days{date(2007, 11, 1)} ? "de-passport-2007" : "de-passport-current"));
        }
        REQUIRE(formats.size() == 2);
    }
    SECTION("the document in force is uniform over its validity")
    {
        // A 40-year-old German holds a ten-year passport issued in the last ten years.
        std::array<int, 10> by_year{};
        constexpr int draws = 5000;
        for (std::uint64_t seed = 0; seed < draws; ++seed)
        {
            const auto v = document("DE", id_document_kind::passport, date(1983, 1, 1), seed);
            const int age = (sys_days{date(2023, 7, 1)} - sys_days{v.documents.front().issued}).count() / 366;
            REQUIRE(age < 10);
            ++by_year.at(static_cast<std::size_t>(age));
        }
        for (const int count : by_year) REQUIRE(count == Catch::Approx(draws / 10.0).margin(draws * 0.02));
    }
    SECTION("a newborn's passport starts at birth")
    {
        const auto v = document("DE", id_document_kind::passport, date(2023, 7, 1), 1);
        REQUIRE(v.documents.front().issued == date(2023, 7, 1));
    }
}

TEST_CASE("the zone needs a name it can write", "[documents][mrz]")
{
    const auto& gen = shared_generator();
    identity_request q = named_person("FR");
    q.documents = {id_document_kind::passport};
    q.birth_date = date(1980, 1, 1);
    REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.size() == 2);

    SECTION("no names")
    {
        q.surname = std::nullopt;
        q.given_names = std::nullopt;
        REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.empty());
    }
    SECTION("a given name alone is not enough")
    {
        q.surname = std::nullopt;
        REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.empty());
    }
    SECTION("a script without a table")
    {
        q.surname = "王";
        REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.empty());
    }
    SECTION("a surname alone is a mononym")
    {
        q.given_names = std::nullopt;
        q.surname = "Arkfreith";
        REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.front().substr(5, 12) == "ARKFREITH<<<");
    }
    SECTION("a given name without a form leaves the zone out")
    {
        q.given_names = "王";
        q.surname = "Arkfreith";
        REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.empty());
    }
    SECTION("the second surname follows the first")
    {
        q.surname = "García";
        q.second_surname = "López";
        q.given_names = "María José";
        REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.front().starts_with("P<FRAGARCIA<LOPEZ<<MARIA<JOSE<<"));
    }
    SECTION("the number and the dates do not depend on the name")
    {
        const auto named = gen.get_identity(q, std::uint64_t{7}).documents.front();
        q.surname = std::nullopt;
        q.given_names = std::nullopt;
        const auto bare = gen.get_identity(q, std::uint64_t{7}).documents.front();
        REQUIRE(named.number == bare.number);
        REQUIRE(named.issued == bare.issued);
        REQUIRE(named.expires == bare.expires);
    }
}

TEST_CASE("the optional field carries the person's own number", "[documents][mrz]")
{
    const auto& gen = shared_generator();
    identity_request q;
    q.country = "SE";
    q.schemes = std::vector<std::string>{"se-personnummer"};
    q.documents = {id_document_kind::passport};
    q.surname = "Andersson";
    q.given_names = "Karl";
    q.birth_date = date(1980, 1, 1);
    const auto v = gen.get_identity(q, std::uint64_t{3});
    const std::string& line = v.documents.front().mrz[1];
    REQUIRE(strip_fillers(line.substr(28, 14)) == letters_and_digits(v.numbers.front().value));

    // Whether the number was asked for changes nothing.
    q.schemes = std::vector<std::string>{};
    REQUIRE(gen.get_identity(q, std::uint64_t{3}).documents.front().mrz == v.documents.front().mrz);

    // A number assigned after the document was issued is not on it. An Argentine
    // child gets the DNI some time after birth and may hold a passport first.
    identity_request child;
    child.country = "AR";
    child.schemes = std::vector<std::string>{"ar-dni"};
    child.documents = {id_document_kind::passport};
    child.surname = "Perez";
    child.given_names = "Ana";
    child.birth_date = date(1966, 5, 29);
    child.reference_date = date(1969, 1, 1);
    int without_number = 0;
    int with_number = 0;
    for (std::uint64_t seed = 0; seed < 300; ++seed)
    {
        const auto a = gen.get_identity(child, seed);
        const auto& passport = a.documents.front();
        const std::string carried = strip_fillers(passport.mrz[1].substr(28, 14));
        if (sys_days{a.numbers.front().assigned} > sys_days{passport.issued})
        {
            REQUIRE(carried.empty());
            ++without_number;
        }
        else
        {
            REQUIRE(carried == a.numbers.front().value);
            ++with_number;
        }
    }
    REQUIRE(without_number > 0);
    REQUIRE(with_number > 0);

    // A safe number goes in the zone too.
    q.schemes = std::vector<std::string>{"se-personnummer"};
    q.safe = true;
    const auto safe = gen.get_identity(q, std::uint64_t{3});
    REQUIRE(strip_fillers(safe.documents.front().mrz[1].substr(28, 14)) == letters_and_digits(safe.numbers.front().value));
}

TEST_CASE("a zone never leaves out a name it was given", "[documents]")
{
    const auto& gen = shared_generator();
    identity_request q;
    q.country = "FR";
    q.schemes = std::vector<std::string>{};
    q.documents = {id_document_kind::passport};
    q.surname = "Martin";
    q.given_names = "Anne";
    REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.front().starts_with("P<FRAMARTIN<<ANNE<"));
    q.given_names = "Αννα"; // Doc 9303 Part 3 has no table for Greek
    REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.empty());
    q.given_names = "";     // an empty name is no name: one identifier
    REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.front().starts_with("P<FRAMARTIN<<<"));
    q.given_names = "Anne";
    q.second_surname = "Παπα";
    REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.empty());
    q.second_surname = "";
    REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.front().starts_with("P<FRAMARTIN<<ANNE<"));
}

TEST_CASE("a card whose number is the national number shows the person's", "[documents][coherence]")
{
    const auto& gen = shared_generator();
    const auto& tables = tables_on_disk();
    std::set<std::string> cards;
    for (const auto& [key, format] : tables.formats)
    {
        if (!format.number_scheme.empty()) cards.insert(format.country);
    }
    REQUIRE(cards.size() == 18);
    for (const auto& country : cards)
    {
        CAPTURE(country);
        int shown = 0;
        for (std::uint64_t seed = 0; seed < 200; ++seed)
        {
            identity_request q;
            q.country = country;
            q.schemes = std::vector<std::string>{"*"};
            q.documents = {id_document_kind::id_card};
            q.safe = seed % 4 == 3;
            const auto v = gen.get_identity(q, seed);
            if (v.documents.empty()) continue;
            const auto& d = v.documents.front();
            const auto& format = tables.formats.at(d.format);
            if (format.number_scheme.empty()) continue;
            for (const auto& n : v.numbers)
            {
                if (n.scheme != format.number_scheme) continue;
                CAPTURE(n.scheme, n.value, n.formatted, d.number);
                const std::string digits = letters_and_digits(d.number);
                REQUIRE(letters_and_digits(n.value).starts_with(digits));
                REQUIRE(digits.size() + 1 >= letters_and_digits(n.value).size()); // whole, or without Peru's check digit
                REQUIRE(sys_days{n.assigned} <= sys_days{d.issued});
                ++shown;
            }
        }
        REQUIRE(shown > 0);
    }
    // Asking for the number or not changes nothing on the card.
    identity_request with;
    with.country = "CN";
    with.documents = {id_document_kind::id_card};
    identity_request without = with;
    without.schemes = std::vector<std::string>{};
    const auto a = gen.get_identity(with, std::uint64_t{5});
    const auto b = gen.get_identity(without, std::uint64_t{5});
    REQUIRE(a.documents.front().number == b.documents.front().number);
    REQUIRE(a.documents.front().number == a.numbers.front().value);
}

TEST_CASE("safe documents fall outside every format of the country", "[documents][safe]")
{
    const auto& gen = shared_generator();
    const auto& tables = tables_on_disk();
    for (const auto& country : gen.countries())
    {
        CAPTURE(country);
        for (std::uint64_t seed = 0; seed < 25; ++seed)
        {
            identity_request q = named_person(country);
            q.safe = true;
            q.birth_date = date(1980, 1, 1);
            const auto v = gen.get_identity(q, seed);
            for (const auto& d : v.documents)
            {
                CAPTURE(d.format, d.number);
                REQUIRE(d.reserved);
                const auto& format = tables.formats.at(d.format);
                if (!format.number_scheme.empty()) continue; // the national number, made safe by its own method
                for (const auto& key : tables.by_country.at({country, format.kind}))
                {
                    REQUIRE_FALSE(tables.formats.at(key).pattern.matches(d.number));
                }
                if (format.mrz != "none") check_mrz(d, v.birth_date, v.bio_sex, format);
            }
        }
    }
}

TEST_CASE("documents move no other stream", "[documents][requests]")
{
    const auto& gen = shared_generator();
    identity_request q;
    q.country = "BR";
    q.birth_date = date(1985, 2, 3);
    const auto without = gen.get_identity(q, std::uint64_t{11});
    q.documents = {id_document_kind::passport};
    const auto with = gen.get_identity(q, std::uint64_t{11});
    REQUIRE(with.numbers.front().value == without.numbers.front().value);
    REQUIRE(with.birth_date == without.birth_date);
    REQUIRE(with.bio_sex == without.bio_sex);
    REQUIRE(with.documents.size() == 1);

    q.documents = {id_document_kind::id_card, id_document_kind::passport};
    const auto both = gen.get_identity(q, std::uint64_t{11});
    REQUIRE(both.documents.size() == 2);
    REQUIRE(both.documents[0].kind == id_document_kind::id_card); // in the order asked
    REQUIRE(both.documents[1].number == with.documents.front().number);
    REQUIRE(both.documents[1].issued == with.documents.front().issued);

    q.documents = {id_document_kind::passport, id_document_kind::passport};
    REQUIRE(gen.get_identity(q, std::uint64_t{11}).documents.size() == 1);

    q.documents = {id_document_kind::passport};
    q.schemes = std::vector<std::string>{}; // a country is drawn among all 199 without a number
    q.country = std::nullopt;
    std::set<std::string> countries;
    for (std::uint64_t seed = 0; seed < 3000; ++seed)
    {
        const auto v = gen.get_identity(q, seed);
        countries.insert(v.country_code);
        REQUIRE(v.documents.size() == 1);
    }
    REQUIRE(countries.size() > 63);
}

TEST_CASE("documents in JSON and in the summary", "[documents][json]")
{
    const auto& gen = shared_generator();
    identity_request q = named_person("ES");
    q.birth_date = date(1935, 3, 3);
    q.schemes = std::vector<std::string>{"es-dni"};
    std::uint64_t permanent_seed = 0;
    while (gen.get_identity(q, permanent_seed).documents.back().expires) ++permanent_seed;
    const auto v = gen.get_identity(q, permanent_seed);
    const std::string json = v.to_json();
    REQUIRE(json.contains(R"("documents":[{"kind":"passport","format":")"));
    REQUIRE(json.contains(R"("kind":"id_card")"));
    REQUIRE(json.contains(R"("expires":null)"));
    REQUIRE(json.contains(R"("mrz":[")"));
    REQUIRE(json.contains(R"("generic_validity":false)"));
    REQUIRE(json.ends_with(R"x(}})x"));
    REQUIRE(v.to_string().contains("passport " + v.documents[0].number + " (valid to "));
    REQUIRE(v.to_string().contains("identity card " + v.documents[1].number + " (no expiry)"));

    identity_request none;
    none.country = "BR";
    REQUIRE(gen.get_identity(none, std::uint64_t{1}).to_json().contains(R"("documents":[])"));
}

TEST_CASE("document tables can be layered", "[documents][layers]")
{
    idg gen;
    gen.load(resource_dir("lite"));
    identity_request q;
    q.country = "AD"; // one generic format and two validity rows, 0 and 20
    q.schemes = std::vector<std::string>{};
    q.documents = {id_document_kind::passport};
    q.birth_date = date(1985, 2, 3);
    const std::string header = "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\n";
    const std::string validity_columns = "document\tfrom_age\tfrom\tyears\tuntil_age\tuntil\texpiry_rule\n";

    SECTION("a format replaces the row with its key")
    {
        gen.extend_from_string(dasmig::id_table::documents, header + "ad-passport-generic\tAD\tpassport\tZZ#{6}\t1900-01-01\ttd3\tissuer\n", "mine");
        const auto after = gen.get_identity(q, std::uint64_t{1}).documents.front();
        REQUIRE(after.number.starts_with("ZZ"));
        REQUIRE(after.number.size() == 8);
        REQUIRE(after.evidence == "issuer");
    }
    SECTION("a validity rule decides the expiry")
    {
        gen.extend_from_string(dasmig::id_table::validity, validity_columns + "ad-passport\t20\t1900-01-01\t1\t\t\tsame_day\n", "mine");
        const auto d = gen.get_identity(q, std::uint64_t{1}).documents.front();
        REQUIRE(*d.expires == years_later(d.issued, 1));
    }
    SECTION("the holder's choice of years")
    {
        gen.extend_from_string(dasmig::id_table::validity, validity_columns + "ad-passport\t20\t1900-01-01\t2|4\t\t\tsame_day\n", "mine");
        std::set<int> lengths;
        for (std::uint64_t seed = 0; seed < 200; ++seed)
        {
            const auto d = gen.get_identity(q, seed).documents.front();
            lengths.insert(static_cast<int>(d.expires->year()) - static_cast<int>(d.issued.year()));
        }
        REQUIRE(lengths == std::set<int>{2, 4});
    }
    SECTION("a rule can run to an age")
    {
        gen.extend_from_string(dasmig::id_table::validity, validity_columns + "ad-passport\t20\t1900-01-01\t\t60\t\tday_before\n", "mine");
        const auto d = gen.get_identity(q, std::uint64_t{1}).documents.front();
        REQUIRE(sys_days{*d.expires} == sys_days{date(2045, 2, 3)} - days{1});
    }
    SECTION("the prefix comes from a scheme's place codes")
    {
        q.country = "EC";
        gen.extend_from_string(dasmig::id_table::documents,
                               "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\tprefix_places\n"
                               "ec-passport-generic\tEC\tpassport\t#{6}\t1900-01-01\ttd3\tissuer\tec-cedula\n",
                               "mine");
        const auto d = gen.get_identity(q, std::uint64_t{1}).documents.front();
        REQUIRE(d.number.size() == 8); // two for the province
    }
    SECTION("two formats on one date draw by weight")
    {
        gen.extend_from_string(dasmig::id_table::documents,
                               "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\tweight\n"
                               "ad-passport-a\tAD\tpassport\tAA#{7}\t1900-01-01\ttd3\tissuer\t3\n",
                               "mine");
        int added = 0;
        constexpr int draws = 2000;
        for (std::uint64_t seed = 0; seed < draws; ++seed) added += gen.get_identity(q, seed).documents.front().format == "ad-passport-a" ? 1 : 0;
        REQUIRE(added == Catch::Approx(draws * 0.75).margin(draws * 0.05));
    }
    SECTION("a format of weight 0 is never drawn")
    {
        gen.extend_from_string(dasmig::id_table::documents,
                               "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\tweight\n"
                               "ad-passport-a\tAD\tpassport\tAA#{7}\t1900-01-01\ttd3\tissuer\t0\n",
                               "mine");
        for (std::uint64_t seed = 0; seed < 100; ++seed) REQUIRE(gen.get_identity(q, seed).documents.front().format == "ad-passport-generic");
    }
    SECTION("translit rules are read from the countries table")
    {
        gen.extend_from_string(dasmig::id_table::countries, "country_code\tpopulation\ticao_code\tprimary_scheme\ttranslit\nAD\t88406\tAND\t\tÃ=XX\n", "mine");
        q.surname = "Bastião";
        q.given_names = "Bob";
        REQUIRE(gen.get_identity(q, std::uint64_t{1}).documents.front().mrz.front().substr(5, 14) == "BASTIXXO<<BOB<");
    }

    const auto data = gen.data_hash();
    SECTION("bad rows change nothing")
    {
        const auto rejected_tsv = [&](dasmig::id_table table, const std::string& text) {
            REQUIRE_THROWS_AS(gen.extend_from_string(table, text, "bad"), dasmig::core::tsv_error);
            REQUIRE(gen.data_hash() == data);
        };
        rejected_tsv(dasmig::id_table::documents, header + "x\tBR\tcar\t#{9}\t1900-01-01\ttd3\tissuer\n");
        rejected_tsv(dasmig::id_table::documents, header + "x\tBR\tpassport\t#{\t1900-01-01\ttd3\tissuer\n");
        rejected_tsv(dasmig::id_table::documents, header + "x\tBR\tpassport\t#{9}\t1900-01-01\ttd9\tissuer\n");
        rejected_tsv(dasmig::id_table::documents, header + "x\tBR\tpassport\t#{9}\t1900-01-01\ttd3\trumour\n");
        rejected_tsv(dasmig::id_table::documents, "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\tyear_at\nx\tBR\tpassport\t#{3}\t1900-01-01\ttd3\tissuer\t2\n");
        rejected_tsv(dasmig::id_table::documents, "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\tmrz_code\nx\tBR\tpassport\t#{3}\t1900-01-01\ttd3\tissuer\tPPP\n");
        const std::string& validity_header = validity_columns;
        rejected_tsv(dasmig::id_table::validity, validity_header + "br-passport\t0\t1900-01-01\t5\t18\t\tsame_day\n");
        rejected_tsv(dasmig::id_table::validity, validity_header + "br-passport\t0\t1900-01-01\t\t\t\tsame_day\n");
        rejected_tsv(dasmig::id_table::validity, validity_header + "br-passport\t0\t1900-01-01\tfive\t\t\tsame_day\n");
        rejected_tsv(dasmig::id_table::validity, validity_header + "br-passport\t0\t1900-01-01\t5\t\t\tsoon\n");
        rejected_tsv(dasmig::id_table::validity, validity_header + "br-passport\t20\t1900-01-01\t\t10\t\tsame_day\n");
        rejected_tsv(dasmig::id_table::countries, "country_code\tpopulation\ticao_code\tprimary_scheme\ttranslit\nBR\t1\tBRA\tbr-cpf\tnonsense\n");
    }
    SECTION("tables that no longer fit together change nothing")
    {
        const auto rejected = [&](dasmig::id_table table, const std::string& text) {
            REQUIRE_THROWS_AS(gen.extend_from_string(table, text, "bad"), std::invalid_argument);
            REQUIRE(gen.data_hash() == data);
        };
        rejected(dasmig::id_table::documents, header + "x\tZZ\tpassport\t#{9}\t1900-01-01\ttd3\tissuer\n");     // no such country
        rejected(dasmig::id_table::documents, header + "x\tDK\tid_card\t#{9}\t2000-01-01\ttd1\tissuer\n");      // a gap before it
        rejected(dasmig::id_table::documents, "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\tuntil\nx\tDK\tid_card\t#{9}\t1900-01-01\ttd1\tissuer\t2000-01-01\n"); // a gap after it
        rejected(dasmig::id_table::documents, "key\tcountry\tkind\tpattern\tfrom\tmrz\tevidence\tmrz_optional\nx\tDK\tpassport\t#{9}\t1900-01-01\ttd3\tissuer\tbr-cpf\n"); // another country's scheme
        rejected(dasmig::id_table::validity, "document\tfrom_age\tfrom\tyears\nzz-passport\t0\t1900-01-01\t5\n"); // no such document
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

TEST_CASE("countries, sexes, ages and places follow their distributions", "[stats]")
{
    const auto& gen = shared_generator();
    constexpr int draws = 20000;
    std::map<std::string, int> countries;
    int men = 0;
    int young = 0;
    int old = 0;
    for (std::uint64_t s = 0; s < draws; ++s)
    {
        const auto v = gen.get_identity(identity_request{}, s);
        ++countries[v.country_code];
        if (v.bio_sex == sex::male) ++men;
        const auto age = (identity_request::default_reference_date.year() - v.birth_date.year()).count();
        REQUIRE(age >= 18);
        REQUIRE(age <= 81);
        if (age < 50) ++young;
        else ++old;
    }
    REQUIRE(men == Catch::Approx(draws * 0.5).margin(draws * 0.02));
    REQUIRE(young == Catch::Approx(draws * 32.0 / 63.0).margin(draws * 0.03));
    REQUIRE(old == Catch::Approx(draws * 31.0 / 63.0).margin(draws * 0.03));

    double total = 0.0;
    std::map<std::string, double> population;
    std::set<std::string> served;
    for (const auto& c : gen.number_countries()) served.insert(c);
    const auto countries_doc = dasmig::core::tsv_document::load(resource_dir("lite") / "countries.tsv");
    for (const auto& row : countries_doc.rows())
    {
        const std::string code{row["country_code"]};
        if (!served.contains(code)) continue;
        population[code] = row.get<double>("population");
        total += population[code];
    }
    for (const auto* code : {"IN", "CN", "US", "BR"})
    {
        CAPTURE(code);
        REQUIRE(countries[code] == Catch::Approx(draws * population[code] / total).margin(draws * 0.02));
    }

    // Unweighted, the countries are uniform.
    identity_request flat;
    flat.weighted = false;
    std::map<std::string, int> uniform;
    for (std::uint64_t s = 0; s < draws; ++s) ++uniform[gen.get_identity(flat, s).country_code];
    REQUIRE(uniform.size() == 63);
    REQUIRE(uniform["IN"] == Catch::Approx(draws / 63.0).margin(draws * 0.01));

    // Sao Paulo weighs 56 of the 218 million in the CPF regions.
    identity_request br = person("BR", {"br-cpf"});
    br.birth_date = date(1980, 1, 1);
    int sao_paulo = 0;
    for (std::uint64_t s = 0; s < draws; ++s)
    {
        if (one_number(gen, br, s).place_code == "8") ++sao_paulo;
    }
    REQUIRE(sao_paulo == Catch::Approx(draws * 56033164.0 / 217846000.0).margin(draws * 0.02));
}

// ---------------------------------------------------------------------------
// Conversion and JSON
// ---------------------------------------------------------------------------

TEST_CASE("string conversion and streaming give the summary", "[conversion]")
{
    const auto& gen = shared_generator();
    identity_request q = person("BR", {"br-cpf"});
    const auto v = gen.get_identity(q, 5);
    const std::string s = v;
    std::ostringstream os;
    os << v;
    REQUIRE(s == "BR, Individual Taxpayer Registry number " + v.numbers.front().formatted);
    REQUIRE(os.str() == s);
}

TEST_CASE("to_json and signature", "[json]")
{
    const auto& gen = shared_generator();
    const auto v = gen.get_identity(person("BR", {"br-cpf"}), 99);
    REQUIRE(v.signature().seed == 99);
    REQUIRE(v.seed() == 99);
    REQUIRE(v.signature().data == gen.data_hash());
    const auto json = v.to_json();
    REQUIRE(json.starts_with(R"({"country_code":"BR","birth_date":")"));
    REQUIRE(json.find(R"("scheme":"br-cpf")") != std::string::npos);
    REQUIRE(json.find(R"x("native_label":"Cadastro de Pessoas Físicas (CPF)")x") != std::string::npos);
    REQUIRE(json.find(R"("reserved":false)") != std::string::npos);
    REQUIRE(json.find(R"("signature":{"seed":"0000000000000063")") != std::string::npos);

    dasmig::core::json_writer w;
    w.begin_array();
    v.to_json(w);
    w.end_array();
    REQUIRE(w.str() == "[" + json + "]");
}

// ---------------------------------------------------------------------------
// Known answers (rule 2): the same seed and data give these exact results on
// every compiler and platform. A change here is a change of output and needs
// a major version.
// ---------------------------------------------------------------------------

static const std::uint64_t kat_data_hash = 0x66cc134c6484ade6ULL;

// A person who holds both documents, on a fixed date.
static identity_request kat_holder()
{
    identity_request q;
    q.documents = {id_document_kind::passport, id_document_kind::id_card};
    q.surname = "Rossi";
    q.given_names = "Anna Maria";
    q.reference_date = date(2024, 1, 15);
    return q;
}

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    idg gen;
    gen.load(resource_dir("lite"));
    REQUIRE(gen.data_hash() == kat_data_hash);

    REQUIRE(gen.get_identity(identity_request{}, std::uint64_t{1}).to_json() ==
            R"x({"country_code":"EC","birth_date":"1958-05-06","sex":"female","numbers":[{"scheme":"ec-cedula","label":"Identity card number","native_label":"Número de cédula de identidad","value":"2357812144","formatted":"235781214-4","assigned":"1958-05-11","place_code":"23","reserved":false}],"documents":[],"signature":{"seed":"0000000000000001","data":"66cc134c6484ade6"}})x");
    REQUIRE(gen.get_identity(person("IT", {"it-cf"}), std::uint64_t{3}).to_json() ==
            R"x({"country_code":"IT","birth_date":"1961-10-16","sex":"female","numbers":[{"scheme":"it-cf","label":"Tax code","native_label":"codice fiscale","value":"LDZGFG61R56G371P","formatted":"LDZGFG61R56G371P","assigned":"1977-01-01","place_code":"G371","reserved":false}],"documents":[],"signature":{"seed":"0000000000000003","data":"66cc134c6484ade6"}})x");
    identity_request every = person("AR", {"*"});
    every.safe = true;
    REQUIRE(gen.get_identity(every, std::uint64_t{5}).to_json() ==
            R"x({"country_code":"AR","birth_date":"1966-05-29","sex":"male","numbers":[{"scheme":"ar-dni","label":"National Identity Document number","native_label":"Documento Nacional de Identidad (DNI)","value":"75438212","formatted":"75.438.212","assigned":"1968-01-01","place_code":"","reserved":true},{"scheme":"ar-cuil","label":"Unique Labour Identification Code","native_label":"Código Único de Identificación Laboral (CUIL)","value":"20754382122","formatted":"20-75438212-2","assigned":"1998-01-02","place_code":"","reserved":true}],"documents":[],"signature":{"seed":"0000000000000005","data":"66cc134c6484ade6"}})x");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        for (const char c : gen.get_identity(identity_request{}, s).to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    REQUIRE(h == 0xeae80057d1eafe83ULL);
}

TEST_CASE("known answers for documents", "[kat][documents]")
{
    idg gen;
    gen.load(resource_dir("lite"));
    REQUIRE(gen.data_hash() == kat_data_hash);

    identity_request germany = kat_holder();
    germany.country = "DE";
    germany.schemes = std::vector<std::string>{};
    REQUIRE(gen.get_identity(germany, std::uint64_t{7}).to_json() ==
            R"x({"country_code":"DE","birth_date":"1959-02-10","sex":"female","numbers":[],"documents":[{"kind":"passport","format":"de-passport-2007","number":"JHM7XW56H","issuing_state":"D","issued":"2023-08-30","expires":"2033-08-30","mrz":["P<D<<ROSSI<<ANNA<MARIA<<<<<<<<<<<<<<<<<<<<<<","JHM7XW56H6D<<5902109F3308305<<<<<<<<<<<<<<04"],"evidence":"reference","generic_validity":false,"reserved":false},{"kind":"id_card","format":"de-id-card-2010","number":"PZ19W6JPY","issuing_state":"D","issued":"2023-07-12","expires":"2033-07-12","mrz":["IDD<<PZ19W6JPY8<<<<<<<<<<<<<<<","5902109F3307124D<<<<<<<<<<<<<0","ROSSI<<ANNA<MARIA<<<<<<<<<<<<<"],"evidence":"reference","generic_validity":false,"reserved":false}],"signature":{"seed":"0000000000000007","data":"66cc134c6484ade6"}})x");
    identity_request belgium = kat_holder();
    belgium.country = "BE";
    belgium.schemes = std::vector<std::string>{"be-nn"};
    belgium.safe = true;
    REQUIRE(gen.get_identity(belgium, std::uint64_t{9}).to_json() ==
            R"x({"country_code":"BE","birth_date":"1984-11-18","sex":"male","numbers":[{"scheme":"be-nn","label":"National Register number (identification number)","native_label":"numéro de registre national / rijksregisternummer (numéro d'identification)","value":"84111895754","formatted":"84.11.18-957.54","assigned":"1984-11-19","place_code":"","reserved":true}],"documents":[{"kind":"passport","format":"be-passport-current","number":"0N533763","issuing_state":"BEL","issued":"2021-04-04","expires":"2028-04-03","mrz":["P<BELROSSI<<ANNA<MARIA<<<<<<<<<<<<<<<<<<<<<<","0N533763<2BEL8411187M2804039<<<<<<<<<<<<<<02"],"evidence":"specimen","generic_validity":false,"reserved":true},{"kind":"id_card","format":"be-id-card-2020","number":"X46967047359","issuing_state":"BEL","issued":"2022-02-21","expires":"2032-02-21","mrz":["IDBELX46967047<3591<<<<<<<<<<<","8411187M3202218BEL841118957545","ROSSI<<ANNA<MARIA<<<<<<<<<<<<<"],"evidence":"reference","generic_validity":false,"reserved":true}],"signature":{"seed":"0000000000000009","data":"66cc134c6484ade6"}})x");

    const identity_request holder = kat_holder();
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        for (const char c : gen.get_identity(holder, s).to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    REQUIRE(h == 0xf53997b1715a4f7eULL);
}

// ---------------------------------------------------------------------------
// Extending data (rule 8)
// ---------------------------------------------------------------------------

static const char* const scheme_header =
    "key\tcountry\tlabel\tnative_label\tbuilder\tdisplay\tassigned\tsafe_method\tpattern\tcheck\tplace_source\tfrom\tuntil\tsafe_pattern\tparams\n";

TEST_CASE("a directory layer adds a country with a generic scheme", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();
    const auto tmp = std::filesystem::temp_directory_path() / "idg_layer_dir";
    std::filesystem::create_directories(tmp);
    {
        std::ofstream{tmp / "countries.tsv"} << "country_code\tpopulation\ticao_code\tprimary_scheme\ttranslit\nXA\t1000\tXXA\txa-num\t\n";
        std::ofstream{tmp / "schemes.tsv"} << scheme_header
                                            << "xa-num\tXA\tTest number\tNumero\tgeneric\t### ###-#\tbirth;lag:1\twrong_check\t[1-9]#{5}\tluhn\t\t1970-01-01\t\t\t\n";
    }
    gen.extend(tmp);
    std::filesystem::remove_all(tmp);

    REQUIRE(gen.data_hash() != hash);
    REQUIRE(gen.countries().size() == 200);
    REQUIRE(gen.number_countries().size() == 64);
    REQUIRE(gen.schemes("XA") == std::vector<std::string>{"xa-num"});
    const auto v = gen.get_identity(identity_request{.country = "XA"}, 4);
    REQUIRE(v.country_code == "XA");
    REQUIRE(v.signature().data == gen.data_hash());
    const auto& n = v.numbers.front();
    REQUIRE(n.value.size() == 7);
    REQUIRE(n.formatted.size() == 9);
    REQUIRE(n.native_label == "Numero");
    REQUIRE(dasmig::core::luhn_valid(n.value));
    REQUIRE(gen.is_valid("xa-num", n.value));
    REQUIRE(gen.is_valid("xa-num", n.formatted));

    // Reloading the base drops every layer.
    gen.load(resource_dir("lite"));
    REQUIRE(gen.countries().size() == 199);
}

TEST_CASE("a layer adds a place, a test number and a generic_dated scheme", "[extend]")
{
    auto gen = make_loaded_generator();
    identity_request q = person("BR", {"br-cpf"});
    q.birth_date = date(1980, 1, 1);
    q.residence = id_place{.country_code = "BR", .admin1_code = "XY", .geonameid = std::nullopt};

    const auto& info = gen.extend_from_string(id_table::places, "scheme\tcode\tfrom\tlabel\tweight\tadmin1\nbr-cpf\t5\t1900-01-02\tNew\t1000\tXY\n", "places.tsv");
    REQUIRE(info.added == 1);
    REQUIRE(one_number(gen, q, 1).place_code == "5");

    gen.extend_from_string(id_table::schemes,
                           std::string{scheme_header} + "br-test\tBR\tDated\t\tgeneric_dated\t\tbirth\twrong_check\t{YYMMDD}##{S:even_male}\tluhn\t\t\t\t\t\n",
                           "schemes.tsv");
    identity_request dated = person("BR", {"br-test"});
    dated.birth_date = date(1999, 12, 31);
    dated.sex = sex::female;
    const auto n = one_number(gen, dated, 1);
    REQUIRE(n.value.starts_with("991231"));
    REQUIRE((n.value[8] - '0') % 2 == 1);
    REQUIRE(gen.is_valid("br-test", n.value));
    REQUIRE_FALSE(gen.is_valid("br-test", "991331" + n.value.substr(6)));
    REQUIRE(gen.schemes("BR") == std::vector<std::string>{"br-cpf", "br-test"});

    gen.extend_from_string(id_table::test_numbers, "scheme\tvalue\nbr-test\t12345\n", "test.tsv");
    REQUIRE(gen.is_valid("br-test", "12345"));
}

TEST_CASE("a bad layer is rejected and leaves the data unchanged", "[extend][errors]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();

    REQUIRE_THROWS_WITH(gen.extend_from_string(id_table::places, "scheme\tcode\tfrom\tlabel\tweight\nbr-cpf\t5\t1900-01-02\tNew\theavy\n", "bad.tsv"),
                        "bad.tsv:2: column 'weight': expected a number, got 'heavy'");
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::places, "scheme\tcode\tfrom\tlabel\tweight\nbr-cpf\t5\t1900-01-02\tNew\t-1\n", "negative.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::places, "scheme\tcode\tfrom\tlabel\nbr-cpf\t5\t1900-01-02\tNew\n", "partial.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::places, "scheme\tcode\tfrom\tlabel\tweight\nxx-none\t5\t1900-01-02\tNew\t1\n", "orphan.tsv"), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::places, "scheme\tcode\tfrom\tlabel\tweight\nbr-cpf\t5\tsoon\tNew\t1\n", "date.tsv"), dasmig::core::tsv_error);

    const std::string header = scheme_header;
    const auto scheme_row = [&](const std::string& builder, const std::string& assigned, const std::string& safe, const std::string& pattern, const std::string& check) {
        return header + "xb\tBR\tX\t\t" + builder + "\t\t" + assigned + "\t" + safe + "\t" + pattern + "\t" + check + "\t\t\t\t\t\n";
    };
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("nope", "birth", "wrong_check", "###", ""), "builder.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("generic", "birth", "wrong_check", "###", ""), "nocheck.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("generic", "birth", "wrong_check", "@@@", "luhn"), "letters.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("generic", "birth", "wrong_check", "###", "weights:1 2:rule"), "rule.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("generic", "someday", "wrong_check", "###", "luhn"), "assigned.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("generic", "birth", "maybe", "###", "luhn"), "safe.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("generic_dated", "birth", "wrong_check", "{NOPE}", "luhn"), "token.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("generic", "birth", "test_numbers", "###", "luhn"), "notest.tsv"), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::schemes, scheme_row("it_cf", "birth", "wrong_check", "###", "luhn"), "owncheck.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::series, "scheme\tsegment\tdate\tvalue\nar-dni\t-\t2030-01-01\t1\n", "falling.tsv"), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.extend_from_string(id_table::countries, "country_code\tpopulation\ticao_code\tprimary_scheme\nXB\t1\tXXB\txx-none\n", "primary.tsv"), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.extend(std::filesystem::path{"wrong_name.tsv"}), std::invalid_argument);

    REQUIRE(gen.data_hash() == hash);
    REQUIRE(gen.layers(id_table::places).size() == 1);
    REQUIRE(gen.layers(id_table::schemes).size() == 1);
}

#ifdef DASMIG_ID_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    idg files;
    files.load(resource_dir("lite"));

    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "idg_embed_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    idg gen;
    const bool loaded = gen.load(dasmig::dataset::lite);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);

    REQUIRE(loaded);
    REQUIRE(gen.layers(id_table::countries).front().source == "embedded:lite/countries.tsv");
    REQUIRE(gen.data_hash() == files.data_hash());
    REQUIRE(gen.get_identity(identity_request{}, std::uint64_t{1}).to_json() == files.get_identity(identity_request{}, std::uint64_t{1}).to_json());
}
#endif

// ---------------------------------------------------------------------------
// Batch generation
// ---------------------------------------------------------------------------

TEST_CASE("identity batch: items equal the single calls for any thread count", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = identity_request{};
    const auto batch = gen.get_identity_batch(request, {.count = 200, .threads = 1}, 77);
    REQUIRE(batch.size() == 200);
    for (std::uint64_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == gen.get_identity(request, dasmig::core::item_seed(77, i)).to_json());
    }

    // A range is computed directly, on any number of threads.
    const auto tail = gen.get_identity_batch(request, {.count = 100, .first = 100, .threads = 4}, 77);
    REQUIRE(tail.size() == 100);
    for (std::size_t i = 0; i < tail.size(); ++i)
    {
        REQUIRE(tail[i].to_json() == batch[100 + i].to_json());
    }
    for (const std::size_t threads : {2U, 7U, 0U})
    {
        const auto other = gen.get_identity_batch(request, {.count = 200, .threads = threads}, 77);
        REQUIRE(other.size() == batch.size());
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            REQUIRE(other[i].to_json() == batch[i].to_json());
        }
    }

    // Without a seed the batch still has the requested size.
    REQUIRE(gen.get_identity_batch(request, {.count = 5}).size() == 5);
}

TEST_CASE("identity batch: uniqueness by primary number", "[batch]")
{
    const auto& gen = shared_generator();
    identity_request q = person("BR", {"br-cpf"});
    q.birth_date = date(1980, 1, 1);
    const auto batch = gen.get_identity_batch(
        q,
        {.count = 500, .unique = dasmig::core::unique_rule::by<identity>([](const identity& v) { return v.numbers.front().value; })},
        9);
    std::set<std::string> seen;
    for (const auto& v : batch) seen.insert(v.numbers.front().value);
    REQUIRE(seen.size() == 500);
}

TEST_CASE("identity batch: streaming gives the same items in order", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = identity_request{};
    const auto batch = gen.get_identity_batch(request, {.count = 300, .first = 10, .threads = 3}, 5);
    std::uint64_t next = 10;
    std::size_t seen = 0;
    gen.for_each_identity(request, {.count = 300, .first = 10, .threads = 3}, 5,
                          [&](std::uint64_t index, const identity& item)
                          {
                              REQUIRE(index == next);
                              REQUIRE(item.to_json() == batch[seen].to_json());
                              ++next;
                              ++seen;
                          });
    REQUIRE(seen == batch.size());
}

TEST_CASE("identity batch: known answer of 10,000 items", "[batch][kat]")
{
    idg gen;
    gen.load(resource_dir("lite"));
    const auto request = identity_request{};
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    gen.for_each_identity(request, {.count = 10000, .threads = 4}, 1,
                          [&](std::uint64_t, const identity& item)
                          { hash = dasmig::core::hash_combine(hash, dasmig::core::fnv1a64(item.to_json())); });
    REQUIRE(hash == 0x483b67e8e07129f2ULL);
}
