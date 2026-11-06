// Draws identities for scripts/validate_numbers.py and writes one row per number.
//
//   sample_identities <resource dir> <identities per country and date> <output.tsv>
//
// Half the people take the library's own ages (18 to 80); the others are born at
// any time in the hundred years before the date, so children and the newest
// century digits and series are in the sample too.

#include "../dasmig/idgen.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

using namespace std::chrono;

constexpr auto realistic_dates = {year{2005} / June / day{1}, year{2015} / June / day{1}, year{2023} / July / day{1}};

// After every encoded date a check might compare with today, for the impossible_date method.
constexpr auto safe_dates = {year{2040} / January / day{1}};

std::string iso(const year_month_day& d)
{
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(d.year()), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
}

void sample(const dasmig::idg& gen, int count, bool safe, const auto& dates, std::ofstream& out)
{
    for (const year_month_day reference : dates)
    {
        for (const auto& country : gen.number_countries())
        {
            dasmig::identity_request q;
            q.country = country;
            q.schemes = std::vector<std::string>{"*"};
            q.reference_date = reference;
            q.safe = safe;
            dasmig::core::rng ages{0x51ED5EEDULL + static_cast<std::uint64_t>(country[0]) + (static_cast<std::uint64_t>(country[1]) << 8U)};
            for (int i = 0; i < count; ++i)
            {
                q.birth_date = std::nullopt;
                if (i % 2 == 1)
                {
                    const auto to = sys_days{reference};
                    const auto from = std::max(sys_days{year{1900} / January / day{1}}, to - days{36525});
                    q.birth_date = year_month_day{from + days{dasmig::core::uniform_int(ages, 0, static_cast<int>((to - from).count()))}};
                }
                const auto seed = (static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL) + 1U;
                const auto v = gen.get_identity(q, seed);
                for (const auto& n : v.numbers)
                {
                    out << (safe ? "1" : "0") << '\t' << n.scheme << '\t' << n.value << '\t' << n.formatted << '\t' << iso(v.birth_date) << '\t'
                        << dasmig::core::sex_name(v.bio_sex) << '\t' << iso(n.assigned) << '\t' << iso(reference) << '\t' << n.place_code << '\n';
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: sample_identities <resource dir> <count> <output.tsv>\n";
        return 2;
    }
    dasmig::idg gen;
    gen.load(std::filesystem::path{argv[1]});
    if (!gen.has_data())
    {
        std::cerr << "no data in " << argv[1] << '\n';
        return 1;
    }
    std::ofstream out{argv[3], std::ios::binary};
    out << "safe\tscheme\tvalue\tformatted\tbirth\tsex\tassigned\treference\tplace\n";
    const int count = std::atoi(argv[2]);
    sample(gen, count, false, realistic_dates, out);
    sample(gen, count, true, safe_dates, out);
    return out.good() ? 0 : 1;
}
