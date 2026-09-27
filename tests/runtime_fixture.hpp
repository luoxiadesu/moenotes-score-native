#pragma once
#include "internal.hpp"
#include <cassert>
#include <iostream>
using namespace mns;
namespace {
Json condition(int kind, Json values = Json::array(), Json targets = Json::array(), bool positive = true) {
    return Json{{"_id", kind},
                {"_conditionType", kind},
                {"_conditionValues", values},
                {"_conditionTargetIDs", targets},
                {"_isPositive", positive}};
}
Json effect(int id, int type, int skill = 1, int trigger = 0, int value = 1, double duration = 2) {
    return Json{{"_id", id},
                {"_liveSkillID", skill},
                {"_gekisouSkillID", skill},
                {"_supportSkillID", skill},
                {"_gekisouSupportSkillID", skill},
                {"_level", 1},
                {"_skillEffectType", type},
                {"_skillTriggerConditionGroup", trigger},
                {"_skillTriggerType", 1},
                {"_skillConditionGroup", 0},
                {"_skillReleaseConditionGroup", 0},
                {"_effectExecuteLimitResetConditionGroup", 0},
                {"_skillCumulativeConditionID", 0},
                {"_skillTargetIDs", Json::array()},
                {"_activationTimeSecond", duration},
                {"_effectValue", value},
                {"_maxEffectValue", 0},
                {"_effectLimitCount", 0},
                {"_effectExecuteLimitCount", 0}};
}
Prepared prepared() {
    Prepared p;
    p.missions = {1, 2, 3};
    p.music_id = 1;
    p.denominator = 100;
    p.level = 5;
    p.note_percent[1] = 100;
    p.judgement_percent[1] = 230;
    p.judgement_percent[2] = 100;
    for (auto name :
         {"MasterMemberCard", "MasterCharacter", "MasterLiveSkillEffect", "MasterGekisouSkillEffect",
          "MasterSupportCard", "MasterSupportCardRank", "MasterSupportSkillEffect",
          "MasterGekisouSupportSkillEffect", "MasterSkillCondition", "MasterSkillConditionSet",
          "MasterSkillTarget", "MasterSkillCumulativeCondition", "MasterLiveGekisouLuckBasePoint",
          "MasterLiveGekisouLuckBonusLot", "MasterLiveJudgementTiming", "MasterLiveSettings"})
        p.master.tables[name] = Json::array();
    for (int i = 1; i <= 5; ++i)
        p.master.tables["MasterMemberCard"].push_back(
            Json{{"_id", i}, {"_characterID", 1}, {"_liveSkillID", 1}, {"_gekisouSkillID", i == 1 ? 1 : 0}});
    p.master.tables["MasterCharacter"].push_back(Json{{"_id", 1}, {"_bandID", 1}});
    p.master.tables["MasterLiveSkillEffect"].push_back(effect(1, 2000, 1, 0, 1000, 5));
    for (auto pair : {std::pair{"life_base", 1000},
                      {"gekisou_luck_gauge_max", 140},
                      {"gekisou_luck_gauge_max_rush", 70},
                      {"gekisou_luck_rush_score_bonus_percent", 10}})
        p.master.tables["MasterLiveSettings"].push_back(
            Json{{"_key", pair.first}, {"_value", std::to_string(pair.second)}});
    p.master.tables["MasterLiveJudgementTiming"].push_back(Json{{"_assistLevel", 0},
                                                                {"_noteSimulateJudgement", 6},
                                                                {"_noteJudgementType", 1},
                                                                {"_beforeMs", 0},
                                                                {"_afterMs", 0}});
    for (int m = 1; m <= 3; ++m) {
        p.master.tables["MasterSkillTarget"].push_back(
            Json{{"_id", m}, {"_skillTargetType", 5}, {"_gekisouMissionType", m}});
        p.master.tables["MasterSkillCondition"].push_back(
            condition(7010 + m, Json::array(), Json::array({m})));
        p.master.tables["MasterSkillCondition"].as_array().back()["_conditionType"] = 7010;
        p.master.tables["MasterSkillConditionSet"].push_back(
            Json{{"_id", m}, {"_group", m}, {"_conditionIds", Json::array({7010 + m})}});
    }
    p.master.tables["MasterLiveGekisouLuckBasePoint"].push_back(
        Json{{"_noteCategory", 0}, {"_noteSimulateJudgement", 5}, {"_basePoint", 7}, {"_weight", 1}});
    for (int type = 0; type < 5; ++type)
        p.master.tables["MasterLiveGekisouLuckBonusLot"].push_back(
            Json{{"_chanceLotType", type}, {"_lotResult", 3}, {"_weight", 1}});
    for (int i = 0; i < 5; ++i)
        p.skills.push_back({i, 20000 + i * 1000, 0, 0});
    for (int i = 0; i < 3; ++i)
        p.fevers.push_back({i, 8000 * i, 8000 * i + 4000, 1});
    for (int t : {0, 40, 1999, 2000, 2000, 2040, 8001, 16000, 16040, 17999, 18000, 19999, 20000})
        p.notes.push_back({int(p.notes.size()), t, 1});
    return p;
}
Json deck() {
    Json slots = Json::array();
    for (int i = 1; i <= 5; ++i)
        slots.push_back(Json{{"member_id", i}, {"live_skill_level", 1}, {"gekisou_skill_level", 1}});
    return Json{{"total_power", 10000}, {"slots", slots}};
}
RuntimeResult run(const Prepared &p) {
    std::array<uint32_t, 1> choices{0};
    BranchRandom random(choices);
    return run_runtime(p, deck(), Json{{"mode", "battle"}}, random);
}
} // namespace
