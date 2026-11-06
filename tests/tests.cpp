#include "catch_amalgamated.hpp"
#include "../dasmig/idgen.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
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

static const std::uint64_t kat_data_hash = 0x1555dee22129657dULL;

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    idg gen;
    gen.load(resource_dir("lite"));
    REQUIRE(gen.data_hash() == kat_data_hash);

    REQUIRE(gen.get_identity(identity_request{}, std::uint64_t{1}).to_json() ==
            R"x({"country_code":"EC","birth_date":"1958-05-06","sex":"female","numbers":[{"scheme":"ec-cedula","label":"Identity card number","native_label":"Número de cédula de identidad","value":"2329781211","formatted":"232978121-1","assigned":"1958-05-11","place_code":"23","reserved":false}],"signature":{"seed":"0000000000000001","data":"1555dee22129657d"}})x");
    REQUIRE(gen.get_identity(person("IT", {"it-cf"}), std::uint64_t{3}).to_json() ==
            R"x({"country_code":"IT","birth_date":"1961-10-16","sex":"female","numbers":[{"scheme":"it-cf","label":"Tax code","native_label":"codice fiscale","value":"GFGLDZ61R56G371M","formatted":"GFGLDZ61R56G371M","assigned":"1977-01-01","place_code":"G371","reserved":false}],"signature":{"seed":"0000000000000003","data":"1555dee22129657d"}})x");
    identity_request every = person("AR", {"*"});
    every.safe = true;
    REQUIRE(gen.get_identity(every, std::uint64_t{5}).to_json() ==
            R"x({"country_code":"AR","birth_date":"1966-05-29","sex":"male","numbers":[{"scheme":"ar-dni","label":"National Identity Document number","native_label":"Documento Nacional de Identidad (DNI)","value":"75438212","formatted":"75.438.212","assigned":"1968-01-01","place_code":"","reserved":true},{"scheme":"ar-cuil","label":"Unique Labour Identification Code","native_label":"Código Único de Identificación Laboral (CUIL)","value":"20754382122","formatted":"20-75438212-2","assigned":"1998-01-02","place_code":"","reserved":true}],"signature":{"seed":"0000000000000005","data":"1555dee22129657d"}})x");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        for (const char c : gen.get_identity(identity_request{}, s).to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    REQUIRE(h == 0x482ecdc153d06a8bULL);
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
    REQUIRE(hash == 0xaa9e2a8cb998acefULL);
}
