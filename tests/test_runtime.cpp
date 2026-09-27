#include "runtime_detail.hpp"
#include "runtime_fixture.hpp"

int main() {
    {
        auto p = prepared();
        p.master.tables["MasterGekisouSkillEffect"].push_back(effect(1, 12000, 1, 1));
        auto r = run(p);
        assert(r.sections[0].mission_value == 10);
        assert(r.sections[0].combo.at(3) == std::pair(2000, 9));
        assert(r.notes.at(7).judgement == 6);
        assert(r.notes.back().judgement == 5);
        assert(r.shared.size() == 10);
    }
    {
        auto p = prepared();
        auto e = effect(1, 12000, 1, 4, 5, 0);
        e["_skillTriggerType"] = 2;
        p.master.tables["MasterGekisouSkillEffect"].push_back(e);
        p.master.tables["MasterSkillCondition"].push_back(condition(7005, Json::array({3})));
        p.master.tables["MasterSkillConditionSet"].push_back(
            Json{{"_group", 4}, {"_conditionIds", Json::array({7005})}});
        p.notes.clear();
        for (int t : {10, 10, 10, 20, 30})
            p.notes.push_back({int(p.notes.size()), t, 1});
        auto r = run(p);
        assert(r.sections[0].mission_value == 20);
        assert(r.sections[0].combo.front() == std::pair(10, 8));
    }
    {
        auto p = prepared();
        p.master.tables["MasterGekisouSkillEffect"].push_back(effect(1, 13000, 1, 3));
        auto r = run(p);
        assert(r.sections[2].mission_value == 8);
    }
    {
        auto p = prepared();
        p.master.tables["MasterGekisouSkillEffect"].push_back(effect(1, 11003, 1, 2, 30000, 0));
        auto r = run(p);
        assert(r.sections[1].mission_value == 30);
        assert(r.primary.front().time_ms == 8000 && r.primary.front().luck == 10);
        assert(r.primary.back().time_ms == 12501 && r.primary.back().luck == -10);
    }
    {
        auto p = prepared();
        p.master.tables["MasterGekisouSkillEffect"].push_back(effect(1, 99999, 1, 2));
        bool rejected = false;
        try {
            run(p);
        } catch (const Error &) {
            rejected = true;
        }
        assert(rejected);
    }
    {
        auto p = prepared();
        p.fevers[0].end_ms = INT32_MAX;
        bool rejected = false;
        try {
            run(p);
        } catch (const Error &) {
            rejected = true;
        }
        assert(rejected);
    }
    {
        auto p = prepared();
        auto e = effect(1, 11003, 1, 2, 14000, 0);
        e["_skillConditionGroup"] = 4;
        p.master.tables["MasterGekisouSkillEffect"].push_back(e);
        p.master.tables["MasterSkillCondition"].push_back(condition(2001, Json::array({2000})));
        p.master.tables["MasterSkillCondition"].push_back(condition(4011, Json::array({50})));
        p.master.tables["MasterSkillConditionSet"].push_back(
            Json{{"_group", 4}, {"_conditionIds", Json::array({2001, 4011})}});
        std::array<uint32_t, 1> choices{0};
        BranchRandom rng(choices);
        auto r = run_runtime(p, deck(), Json{{"mode", "battle"}}, rng);
        assert(rng.consumed() == 1);
        assert(r.sections[1].mission_value == 0);
    }
    {
        auto p = prepared();
        auto e = effect(1, 11003, 1, 2, 14000, 0);
        e["_skillConditionGroup"] = 4;
        p.master.tables["MasterGekisouSkillEffect"].push_back(e);
        p.master.tables["MasterSkillCondition"].push_back(condition(4011, Json::array({50})));
        p.master.tables["MasterSkillConditionSet"].push_back(
            Json{{"_group", 4}, {"_conditionIds", Json::array({4011})}});
        std::array<uint32_t, 1> choices{0};
        BranchRandom rng(choices);
        bool branched = false;
        try {
            run_runtime(p, deck(), Json{{"mode", "battle"}}, rng);
        } catch (const NeedDecision &d) {
            branched = true;
            assert((d.weights == std::vector<uint64_t>{50, 50}));
        }
        assert(branched);
    }
    {
        auto p = prepared();
        p.master.tables["MasterGekisouSkillEffect"].push_back(effect(1, 11003, 1, 2, 10000, 0));
        p.master.tables["MasterLiveGekisouLuckBonusLot"] = Json::array();
        for (int type = 0; type < 5; ++type) {
            for (auto [weight, result] : std::array<std::pair<int, int>, 3>{{{1, 1}, {2, 1}, {4, 3}}})
                p.master.tables["MasterLiveGekisouLuckBonusLot"].push_back(
                    Json{{"_chanceLotType", type}, {"_lotResult", result}, {"_weight", weight}});
        }
        std::array<uint32_t, 1> choices{0};
        BranchRandom rng(choices);
        bool branched = false;
        try {
            run_runtime(p, deck(), Json{{"mode", "battle"}}, rng);
        } catch (const NeedDecision &d) {
            branched = true;
            assert((d.weights == std::vector<uint64_t>{3, 4}));
        }
        assert(branched);
    }
    {
        auto p = prepared();
        p.master.tables["MasterGekisouSkillEffect"].push_back(effect(1, 12000, 1, 1));
        Json d = deck(), request{{"mode", "battle"}};
        auto kernel = compile_runtime(p, d, request);
        for (uint32_t permutation = 0; permutation < 120; ++permutation) {
            std::array<uint32_t, 1> choices{permutation};
            BranchRandom a(choices), b(choices), c(choices);
            auto first = kernel(a), again = kernel(b), fresh = run_runtime(p, d, request, c);
            assert(first.order == again.order && first.order == fresh.order);
            assert(first.shared.size() == fresh.shared.size());
            assert(first.sections[0].mission_value == fresh.sections[0].mission_value);
            auto slot = std::find(first.order.begin(), first.order.end(), 0) - first.order.begin();
            assert(first.sections[0].mission_value == 10);
            assert(slot >= 0 && slot < 5);
            assert(first.sections[0].combo == again.sections[0].combo);
        }
    }
    {
        using namespace runtime_detail;
        auto p = prepared();
        Tables tables{p.master};
        auto rejects = [&](const Json &row, bool support) {
            bool rejected = false;
            try {
                validate(tables.rule(row, 2, 0, 1, 1, support, true), p.master);
            } catch (const std::exception &) {
                rejected = true;
            }
            assert(rejected);
        };
        // Known effect types must not silently accept unrelated trigger logic.
        rejects(effect(1, 11002, 1, 2, 10, 0), false);
        auto negative_limit = effect(1, 13000, 1, 3);
        negative_limit["_effectExecuteLimitCount"] = -1;
        rejects(negative_limit, false);
        p.master.tables["MasterSkillCondition"].push_back(
            condition(7021, Json::array(), Json::array(), false));
        p.master.tables["MasterSkillConditionSet"].push_back(
            Json{{"_group", 4}, {"_conditionIds", Json::array({7021})}});
        auto rush = effect(1, 2000, 1, 4, 100, 0);
        rush["_skillTriggerType"] = 2;
        rejects(rush, true);
        // Unknown sign/threshold shapes reject before a false earlier gate
        // can hide them from this particular path.
        p.master.tables["MasterSkillCondition"].push_back(
            condition(4011, Json::array({INT64_C(4294967346)})));
        p.master.tables["MasterSkillConditionSet"].push_back(
            Json{{"_group", 5}, {"_conditionIds", Json::array({4011})}});
        auto huge = effect(1, 11003, 1, 2, 10000, 0);
        huge["_skillConditionGroup"] = 5;
        rejects(huge, false);
        auto bad_target = effect(1, 2004, 1, 0, 1000, 5);
        bad_target["_skillTargetIDs"] = Json::array({INT64_C(4294967297)});
        bool bad_reference = false;
        try {
            tables.rule(bad_target, 1, 0, 1, 1, false, false);
        } catch (const std::exception &) {
            bad_reference = true;
        }
        assert(bad_reference);
    }
    {
        // A huge extension can round the float32 duration up to 2^31 before
        // the end-time check. Reject before converting that float to int.
        auto p = prepared();
        auto &tables = p.master.tables;
        tables["MasterSkillCondition"].push_back(condition(4010));
        tables["MasterSkillConditionSet"].push_back(
            Json{{"_group", 4}, {"_conditionIds", Json::array({4010})}});
        auto extension = effect(2, 15000, 2, 4, INT32_MAX, 0);
        tables["MasterSupportSkillEffect"].push_back(extension);
        tables["MasterSupportCard"].push_back(Json{{"_id", 1},
                                                   {"_supportCardRankGroup", 1},
                                                   {"_supportSkillId01", 2},
                                                   {"_supportSkillId02", 0},
                                                   {"_gekisouSupportSkillId01", 0},
                                                   {"_gekisouSupportSkillId02", 0}});
        tables["MasterSupportCardRank"].push_back(Json{{"_group", 1},
                                                       {"_rank", 1},
                                                       {"_supportSkill01Level", 1},
                                                       {"_supportSkill02Level", 1},
                                                       {"_gekisouSupportSkill01Level", 1},
                                                       {"_gekisouSupportSkill02Level", 1}});
        auto d = deck();
        d["slots"].at(0)["support"] = Json{{"master_id", 1}, {"rank", 1}};
        std::array<uint32_t, 1> choices{0};
        BranchRandom random(choices);
        bool rejected = false;
        try {
            run_runtime(p, d, Json{{"mode", "free"}}, random);
        } catch (const Error &) {
            rejected = true;
        }
        assert(rejected);
    }
    std::cout << "runtime: expiry chord, causal sustained Combo, natural Just, "
                 "pending Luck, fail-closed validation passed\n";
    return 0;
}
