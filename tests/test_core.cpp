#include "hash.hpp"
#include "internal.hpp"
#include <cassert>
#include <iostream>
using namespace mns;
int main() {
    assert(sha256(std::string_view("abc")) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(sha256(std::string_view("")) ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Prepared p;
    p.level = 5;
    p.denominator = 100;
    p.note_percent[1] = 100;
    p.judgement_percent[2] = 100;
    p.notes = {{0, 10, 1}, {1, 50, 1}};
    RuntimeResult r;
    r.notes = {{0, 10, 5, 5, 1000}, {1, 50, 5, 5, 1000}};
    r.shared = {{31, 1, 0, 50000, 0, 0, 0}};
    Json deck{{"total_power", 10000}};
    auto result = score_path(p, deck, Json::object(), r);
    assert(result[0].total == 750);
    // Free-mode section statistics use chart ranges without activating a
    // Gekisou mission or adding a ranking bonus. Boundaries use score frames.
    p.fevers = {{0, 0, 40, 1}, {1, 40, 80, 1}};
    auto sectioned = score_path(p, deck, Json::object(), r);
    assert(sectioned[0].total == result[0].total);
    assert(sectioned[0].sections[0] == 300 && sectioned[0].sections[1] == 450);
    assert(sectioned[0].sections[2] == 0);
    assert(sectioned[0].detail.at("ranking_score").get<int64_t>() == 0);
    p.fevers.clear();
    r.shared = {{50, 1, 0, 30000, 0, 0, 0}};
    assert(score_path(p, deck, Json::object(), r)[0].total == 690);
    r.shared.push_back({10, -1});
    assert(score_path(p, deck, Json::object(), r)[0].total == 690);
    assert(score_path(p, deck, Json::object(), r)[0].total == 690);
    r.shared = {{10, 1, 0, 50000, 0, 0, 0}, {50, 1, 0, -50000, 0, 0, 0}};
    assert(score_path(p, deck, Json::object(), r)[0].total == 750);
    r.shared = {{10, 1, 0, 0, 0, 50000, 0}};
    bool failed = false;
    try {
        score_path(p, deck, Json::object(), r);
    } catch (const Error &) {
        failed = true;
    }
    assert(failed);
    for (auto text : {"{\"x\":1,\"x\":2}", "{\"x\":NaN}", "{\"x\":18446744073709551615}"}) {
        failed = false;
        try {
            Json::parse(text);
        } catch (const std::exception &) {
            failed = true;
        }
        assert(failed);
    }
    std::cout << "core precision, ordering, no-op determinism, JSON and SHA checks passed\n";
}
