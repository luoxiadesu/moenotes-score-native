#include "analysis.hpp"
#include "internal.hpp"

#include <cassert>
#include <iostream>
#include <limits>

namespace mns {
std::vector<ScoreBound> score_bounds(const Prepared &, const Json &, const Json &);
}
using namespace mns;

int main() {
    const auto root = std::string(MNS_SOURCE_DIR) + "/tests/fixtures/";
    const auto tables = Json::parse(read_file(root + "api-master.json"));
    const auto bytes = read_file(root + "api-chart.json");
    const auto request = Json::parse(read_file(root + "api-request.json"));
    auto calculation = request.at("calculation");
    calculation["schema_version"] = 1;
    const auto prepared =
        prepare(tables, std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()),
                "0001_00.json", calculation);
    auto deck_request = Json{{"deck", request.at("deck")}, {"music_score_id", 1}};
    const auto deck = resolve_deck(prepared->master, deck_request);
    const auto bounds = score_bounds(*prepared, deck, calculation);
    assert(bounds.size() == 1 && bounds[0].lower == 0 && bounds[0].upper < INT32_MAX);
    for (std::uint32_t permutation = 0; permutation < 120; ++permutation) {
        const std::array<std::uint32_t, 1> path{permutation};
        BranchRandom random(path);
        const auto runtime = run_runtime(*prepared, deck, calculation, random);
        const auto results = score_path(*prepared, deck, calculation, runtime);
        assert(results.size() == 1 && results[0].total <= bounds[0].upper);
    }
    // Exercise the Gekisou envelope, auxiliary track and five distinct rank
    // conditions using deterministic legal Luck tables; all 120 ordinary skill
    // permutations must satisfy the same proof.
    auto multi = *prepared;
    multi.fevers = {{0, 0, 1000, 0}, {1, 2000, 3000, 0}, {2, 4000, 5000, 0}};
    multi.missions = {1, 2, 3};
    for (const auto &pair : {std::pair{"gekisou_luck_gauge_max", 140},
                             {"gekisou_luck_gauge_max_rush", 70},
                             {"gekisou_luck_rush_score_bonus_percent", 10}})
        multi.master.tables["MasterLiveSettings"].push_back(
            Json{{"_key", pair.first}, {"_value", std::to_string(pair.second)}});
    for (const auto *name :
         {"MasterLiveGekisouRankingScoreBonus", "MasterLiveGekisouLuckBasePoint",
          "MasterLiveGekisouLuckBonusLot", "MasterGekisouSkillEffect", "MasterGekisouSupportSkillEffect"})
        multi.master.tables[name] = Json::array();
    for (int category = 0; category < 2; ++category)
        multi.master.tables["MasterLiveGekisouLuckBasePoint"].push_back(Json{
            {"_noteCategory", category}, {"_noteSimulateJudgement", 5}, {"_basePoint", 7}, {"_weight", 1}});
    for (int type = 0; type < 5; ++type)
        multi.master.tables["MasterLiveGekisouLuckBonusLot"].push_back(
            Json{{"_chanceLotType", type}, {"_lotResult", 3}, {"_weight", 1}});
    for (int section = 1; section <= 3; ++section)
        for (int rank = 1; rank <= 5; ++rank)
            multi.master.tables["MasterLiveGekisouRankingScoreBonus"].push_back(
                Json{{"_missionPattern", 2},
                     {"_count", section},
                     {"_rank", rank},
                     {"_scoreBonusPercent", 100 / rank}});
    auto battle = calculation;
    battle["mode"] = "battle";
    const auto multi_bounds = score_bounds(multi, deck, battle);
    assert(multi_bounds.size() == 5 && multi_bounds[0].upper >= multi_bounds[4].upper);
    for (std::uint32_t permutation = 0; permutation < 120; ++permutation) {
        const std::array<std::uint32_t, 1> path{permutation};
        BranchRandom random(path);
        const auto results = score_path(multi, deck, battle, run_runtime(multi, deck, battle, random));
        assert(results.size() == 5);
        for (std::size_t rank = 0; rank < 5; ++rank)
            assert(results[rank].total <= multi_bounds[rank].upper &&
                   results[rank].solo <= multi_bounds[rank].upper);
    }
    auto rejects = [&](const Prepared &input, const Json &resolved) {
        try {
            (void)score_bounds(input, resolved, calculation);
        } catch (const Error &) {
            return;
        }
        throw std::logic_error("expected invalid score envelope rejection");
    };
    auto invalid = *prepared;
    invalid.adjustment = std::numeric_limits<float>::infinity();
    rejects(invalid, deck);
    invalid = *prepared;
    invalid.combo[0] = {{0, -1.f}};
    rejects(invalid, deck);
    auto enormous_power = deck;
    enormous_power["total_power"] = INT32_MAX;
    rejects(*prepared, enormous_power);
    invalid = *prepared;
    invalid.master.tables["MasterLiveSkillEffect"].at(0)["_effectValue"] = INT32_MAX;
    rejects(invalid, deck);
    auto skip = calculation;
    skip["mode"] = "challenge_skip";
    const auto skip_bounds = score_bounds(*prepared, deck, skip);
    const auto skip_score = score_path(*prepared, deck, skip, RuntimeResult{});
    assert(skip_bounds.size() == 1 && skip_bounds[0].lower == skip_bounds[0].upper);
    assert(skip_bounds[0].upper == skip_score[0].total);
    std::cout << "score bounds tests passed\n";
}
