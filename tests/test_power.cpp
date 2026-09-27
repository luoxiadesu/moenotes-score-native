#include "internal.hpp"
#include "power_math.hpp"
#include <cassert>
#include <iostream>
using namespace mns;
namespace {
Json fixture() {
    return Json::parse(read_file(std::string(MNS_SOURCE_DIR) + "/tests/fixtures/power.json"));
}
Json run(Json request) {
    return resolve_deck(Master{request.at("master_tables"), Json::object()}, request);
}
void rejects(Json request) {
    bool threw = false;
    try {
        (void)run(std::move(request));
    } catch (const Error &) {
        threw = true;
    }
    assert(threw);
}
void compare(const Json &actual, const Json &expected, const std::string &path) {
    if (expected.is_object()) {
        if (!actual.is_object())
            throw std::runtime_error(path + ": expected object");
        for (const auto &[key, value] : expected.items()) {
            if (!actual.contains(key))
                throw std::runtime_error(path + "/" + key + ": absent");
            compare(actual.at(key), value, path + "/" + key);
        }
    } else if (expected.is_array()) {
        if (!actual.is_array() || actual.size() != expected.size())
            throw std::runtime_error(path + ": array size");
        for (std::size_t i = 0; i < expected.size(); ++i)
            compare(actual[i], expected[i], path + "/" + std::to_string(i));
    } else if (actual != expected)
        throw std::runtime_error(path + ": " + actual.dump() + " != " + expected.dump());
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2) {
            const auto cases = Json::parse(read_file(argv[1]));
            std::size_t n = 0;
            for (const auto &c : cases) {
                compare(run(c.at("request")), c.at("expected"), "case " + std::to_string(n));
                ++n;
            }
            std::cout << n << " differential cases passed\n";
            return 0;
        }
        const auto cases =
            Json::parse(read_file(std::string(MNS_SOURCE_DIR) + "/tests/fixtures/power-cases.json"));
        std::size_t n = 0;
        for (const auto &c : cases) {
            compare(run(c.at("request")), c.at("expected"), "case " + std::to_string(n));
            ++n;
        }
        using power_detail::Power;
        assert(Power(std::array<int64_t, 3>{999999999, 0, 0}).to_floor().bp[0] == 1000000000);
        assert(Power(std::array<int64_t, 3>{999999999, 0, 0}).integer()[0] == 99999);
        assert(power_detail::growth({35279, 35279, 35279}, {4086, 4086, 4086}, 0).integer()[0] == 14415);
        assert(power_detail::growth({35279, 35279, 35279}, {4086, 4086, 4086}, 1).integer()[0] == 14414);
        assert(power_detail::growth({100000, 100000, 100000}, {30000, 30000, 30000}, 2).integer()[0] ==
               -129497);
        auto r = fixture();
        r["player_data"]["memberCards"][0]["exp"] = true;
        rejects(r);
        r = fixture();
        r["player_data"]["memberCards"][0]["exp"] = 1.5;
        rejects(r);
        r = fixture();
        r["deck_context"] = Json{{"music_type_extra_bonus_bp", 1000}};
        r["context"] = Json{{"music_type", 2}};
        assert(run(r)["total_power"].get<int64_t>() == 3000);
        r["context"]["music_type"] = 99;
        assert(run(r)["total_power"].get<int64_t>() == 3300);
        r["context"]["music_type"] = 7;
        rejects(r);
        r = fixture();
        r["player_data"]["decks"][0]["cards"][0]["slotIndex"] = 1;
        rejects(r);
        r = fixture();
        r["player_data"]["decks"][0]["cards"][0]["performanceOrderIndex"] = 1;
        rejects(r);
        r = fixture();
        r["player_data"]["decks"][0]["cards"][0]["memberCardId"] = 999;
        rejects(r);
        r = fixture();
        r["player_data"]["decks"][0]["cards"][0]["memberCardId"] = 2;
        rejects(r);
        r = fixture();
        r["player_data"]["memoryStatus"] = Json::array();
        rejects(r);
        r = fixture();
        r["player_data"]["memoryStatus"] = Json{{"unknown", Json::array()}};
        rejects(r);
        r = fixture();
        r["deck_context"] = false;
        rejects(r);
        r = fixture();
        r["deck_context"] = Json{{"music_type_extra_bonus_bp", -1}};
        rejects(r);
        r = fixture();
        r["deck_context"] = Json{{"member_event_rates", Json::array({0})}};
        rejects(r);
        r = fixture();
        r["master_tables"].items().erase("MasterMemoryMusic");
        rejects(r);
        r = fixture();
        auto duplicate = r["master_tables"]["MasterMemberCardRank"][0];
        duplicate["_id"] = 99;
        r["master_tables"]["MasterMemberCardRank"].push_back(duplicate);
        rejects(r);
        r = fixture();
        r["player_data"]["token"] = "do-not-return";
        r["player_data"]["name"] = "do-not-return";
        assert(run(r).dump().find("do-not-return") == std::string::npos);
        r = fixture();
        r.items().erase("player_data");
        Json members = Json::array();
        for (int id = 1; id <= 5; ++id)
            members.push_back(Json{{"member_card_id", id},
                                   {"level", 1},
                                   {"awake_count", 0},
                                   {"rank", 1},
                                   {"live_skill_level", 2},
                                   {"gekisou_skill_level", 3}});
        r["deck"] = Json{{"members", members}, {"performance_order", Json::array({4, 3, 2, 1, 0})}};
        assert(run(r)["total_power"].get<int64_t>() == 3000);
        assert(run(r)["performance_order"] == Json::array({4, 3, 2, 1, 0}));
        r["account"] =
            Json{{"vip_point", 10}, {"character_exp", Json::array({Json{{"character_id", 1}, {"exp", 10}}})}};
        assert(run(r)["total_power"].get<int64_t>() == 3495);
        r["deck"]["members"][0]["unexpected"] = 1;
        rejects(r);
        std::cout << n << " Python differential cases and native power/input regressions passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
