#include "../dasmig/idgen.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using namespace std::chrono;

    // The singleton loads the lite tier (compiled in, or from resources/).
    auto& gen = dasmig::idg::instance();
    if (!gen.has_data())
    {
        std::cerr << "Could not load the identity data.\n";
        return 1;
    }
    std::cout << gen.countries().size() << " countries, " << gen.number_countries().size()
              << " with national numbers.\n\n";

    std::cout << "Random people, countries by population:\n";
    for (int i = 0; i < 5; ++i)
    {
        std::cout << "  " << gen.get_identity(dasmig::identity_request{}) << '\n';
    }

    std::cout << "\nA codice fiscale from a name, a birth date and a commune:\n";
    dasmig::identity_request request;
    request.country = "IT";
    request.surname = "Rossi";
    request.given_names = "Mario";
    request.birth_date = year{1980} / January / day{1};
    request.sex = dasmig::sex::male;
    request.birthplace = dasmig::id_place{.country_code = "IT", .admin1_code = {}, .geonameid = 3169070U};
    std::cout << "  " << gen.get_identity(request, std::uint64_t{42}).to_json() << '\n';

    std::cout << "\nEvery number an Argentine holds, and the numbers nobody holds:\n";
    dasmig::identity_request argentine;
    argentine.country = "AR";
    argentine.schemes = std::vector<std::string>{"*"};
    std::cout << "  " << gen.get_identity(argentine, std::uint64_t{7}) << '\n';
    argentine.safe = true;
    std::cout << "  " << gen.get_identity(argentine, std::uint64_t{7}) << '\n';

    std::cout << "\nA passport and an identity card, with their machine-readable zones:\n";
    dasmig::identity_request traveller;
    traveller.country = "DE";
    traveller.surname = "Müller";
    traveller.given_names = "Jürgen Karl";
    traveller.documents = {dasmig::id_document_kind::passport, dasmig::id_document_kind::id_card};
    const auto held = gen.get_identity(traveller, std::uint64_t{11});
    std::cout << "  " << held << '\n';
    for (const auto& document : held.documents)
    {
        for (const auto& line : document.mrz) std::cout << "    " << line << '\n';
    }

    std::cout << "\nChecking text against a scheme:\n";
    std::cout << "  111.444.777-35: " << gen.is_valid("br-cpf", "111.444.777-35") << '\n';
    std::cout << "  111.444.777-36: " << gen.is_valid("br-cpf", "111.444.777-36") << '\n';
    return 0;
}
