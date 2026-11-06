// Benchmarks for id-generator; see bench.hpp for the options.

#include "../dasmig/idgen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"id-generator", argc, argv};

    run.load("load", [&] {
        dasmig::idg gen;
        return gen.load(run.tier());
    });

    dasmig::idg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_identity", [&](std::uint64_t seed) { return gen.get_identity({}, seed); });
    run.calls("get_identity to_json", [&](std::uint64_t seed) {
        return gen.get_identity({}, seed).to_json();
    });

    // The design's second target: an identity with a passport and its zone.
    dasmig::identity_request with_passport;
    with_passport.documents = {dasmig::id_document_kind::passport};
    with_passport.surname = "Rossi";
    with_passport.given_names = "Anna Maria";
    run.calls("get_identity passport", [&](std::uint64_t seed) { return gen.get_identity(with_passport, seed); });

    run.batches("get_identity_batch", [&](std::size_t count, std::size_t threads, std::uint64_t seed) {
        return gen.get_identity_batch({}, {.count = count, .threads = threads}, seed);
    });

    return run.finish();
}
