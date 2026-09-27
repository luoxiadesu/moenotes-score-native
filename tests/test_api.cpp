#include "json.hpp"
#include "moenotes_score.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace mns;
void check(mns_status code) {
    if (code)
        throw std::runtime_error(mns_last_error());
}
Json snapshot(mns_analysis *a) {
    char *raw = nullptr;
    check(mns_analysis_snapshot(a, &raw));
    auto j = Json::parse(raw);
    mns_string_free(raw);
    return j;
}
int main() {
    try {
        auto master = read_file(std::string(MNS_SOURCE_DIR) + "/tests/fixtures/api-master.json");
        auto chart = read_file(std::string(MNS_SOURCE_DIR) + "/tests/fixtures/api-chart.json");
        auto request =
            Json::parse(read_file(std::string(MNS_SOURCE_DIR) + "/tests/fixtures/api-request.json"));
        auto q = request.at("calculation");
        q["schema_version"] = 1;
        auto req = q.dump();
        mns_prepared *p = nullptr;
        check(mns_prepare(master.data(), master.size(), chart.data(), chart.size(), "0001_00.json",
                          req.data(), req.size(), &p));
        mns_prepared *bad = reinterpret_cast<mns_prepared *>(1);
        assert(mns_prepare(master.data(), master.size(), chart.data(), chart.size(), "0001_01.json.gz",
                           req.data(), req.size(), &bad) == MNS_INVALID_INPUT);
        assert(bad == nullptr);
        auto d = Json{{"deck", request.at("deck")}}.dump();
        mns_deck *deck = nullptr;
        check(mns_deck_resolve(p, d.data(), d.size(), &deck));
        std::string opts = "{\"contributions\":true}";
        mns_analysis *a = nullptr;
        check(mns_analysis_create(p, deck, opts.data(), opts.size(), &a));
        auto first_source = snapshot(a).at("source");
        assert(first_source.at("resolved_deck_sha256").get<std::string>().size() == 64);
        assert(first_source.at("analysis_config_sha256").get<std::string>().size() == 64);
        assert(first_source.at("analysis_config").at("calculation").at("contributions").get<bool>());
        mns_analysis *different = nullptr;
        std::string different_opts = "{\"contributions\":true,\"estimator_seed\":99}";
        check(mns_analysis_create(p, deck, different_opts.data(), different_opts.size(), &different));
        auto second_source = snapshot(different).at("source");
        assert(first_source.at("resolved_deck_sha256") == second_source.at("resolved_deck_sha256"));
        assert(first_source.at("analysis_config_sha256") != second_source.at("analysis_config_sha256"));
        mns_analysis_free(different);
        // Account identity must not enter the reproducibility fingerprint.
        Json player{
            {"memberCards", Json::array()}, {"supportCards", Json::array()}, {"decks", Json::array()}};
        Json slots = Json::array();
        for (int i = 0; i < 5; ++i) {
            player["memberCards"].push_back(Json{
                {"masterId", i + 1}, {"cardRank", 1}, {"liveSkillLevel", 1}, {"performanceSkillLevel", 1}});
            slots.push_back(Json{{"slotIndex", i},
                                 {"performanceOrderIndex", i},
                                 {"memberCardId", i + 1},
                                 {"supportCardId", -1}});
        }
        player["decks"].push_back(Json{{"id", 1}, {"cards", slots}});
        auto account_input = Json{{"player_data", player}}.dump();
        mns_deck *account_deck = nullptr;
        check(mns_deck_resolve(p, account_input.data(), account_input.size(), &account_deck));
        char *account_raw = nullptr;
        check(mns_deck_report(account_deck, &account_raw));
        auto account_report = Json::parse(account_raw);
        mns_string_free(account_raw);
        mns_deck_free(account_deck);
        player["token"] = "test-only-secret";
        player["name"] = "test-only-profile";
        account_input = Json{{"player_data", player}}.dump();
        check(mns_deck_resolve(p, account_input.data(), account_input.size(), &account_deck));
        check(mns_deck_report(account_deck, &account_raw));
        auto private_report = Json::parse(account_raw);
        mns_string_free(account_raw);
        mns_deck_free(account_deck);
        assert(private_report.at("source").at("resolved_input_sha256") ==
               account_report.at("source").at("resolved_input_sha256"));
        assert(private_report.dump().find("test-only-secret") == std::string::npos);
        assert(private_report.dump().find("test-only-profile") == std::string::npos);
        auto power_context = Json{{"deck", request.at("deck")},
                                  {"deck_context", Json{{"music_type_extra_bonus_bp", 1000},
                                                        {"music_tag_extra_bonus_bp", 1000}}}}
                                 .dump();
        mns_deck *different_deck = nullptr;
        check(mns_deck_resolve(p, power_context.data(), power_context.size(), &different_deck));
        char *report = nullptr;
        check(mns_deck_report(different_deck, &report));
        auto report_json = Json::parse(report);
        mns_string_free(report);
        assert(report_json.at("source").at("resolved_input_sha256") !=
               first_source.at("resolved_deck_sha256"));
        assert(report_json.at("source").at("master").at("master_sha256") == first_source.at("master_sha256"));
        mns_deck_free(different_deck);
        mns_deck_free(deck);
        mns_prepared_free(p);
        check(mns_analysis_advance(a, 1000, 5));
        auto partial = snapshot(a);
        assert(!partial.at("complete").get<bool>());
        mns_analysis_cancel(a);
        check(mns_analysis_advance(a, 1000, 5));
        assert(snapshot(a).at("stop_reason").get<std::string>() == "cancelled");
        mns_analysis_resume(a);
        check(mns_analysis_advance(a, 10000, UINT64_MAX));
        auto result = snapshot(a);
        assert(result.at("complete").get<bool>());
        auto &r = result.at("results").at(0);
        assert(r.at("expectation").at("status").get<std::string>() == "exact");
        assert(r.at("maximum").at("upper") == r.at("maximum").at("lower"));
        int64_t sum = 0;
        for (auto &v : r.at("maximum").at("witness").at("contributions"))
            sum += v.get<int64_t>();
        assert(sum == r.at("maximum").at("witness").at("total").get<int64_t>());
        std::cout << result.dump() << '\n';
        mns_analysis_free(a);
        char *out = reinterpret_cast<char *>(1);
        assert(mns_analysis_snapshot(nullptr, &out) == MNS_INVALID_INPUT);
        assert(out == nullptr);
        mns_analysis_free(nullptr);
        mns_deck_free(nullptr);
        mns_prepared_free(nullptr);
        mns_string_free(nullptr);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
