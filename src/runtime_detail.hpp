#pragma once
#include "internal.hpp"
#include <algorithm>
#include <climits>
#include <numeric>
#include <set>
#include <tuple>
namespace mns::runtime_detail {
inline int iv(const Json &r, std::string_view k, int d = 0) {
    if (!r.contains(k))
        return d;
    const auto &value = r.at(k);
    if (!value.is_number_integer())
        throw Error("master integer expected: " + std::string(k));
    int64_t n = value.get<int64_t>();
    if (n < INT32_MIN || n > INT32_MAX)
        throw Error("master integer outside int32: " + std::string(k));
    return int(n);
}
inline int metric(int64_t value) {
    if (value < 0 || value > INT32_MAX)
        throw Error("runtime counter exceeds int32");
    return int(value);
}
inline int floor_metric(float value) {
    if (!std::isfinite(value) || value < 0 || double(value) > INT32_MAX)
        throw Error("runtime float counter exceeds int32");
    return int(std::floor(value));
}
inline int judgement_type(const Note &n) {
    switch (n.operate_type) {
    case 1:
        return n.critical ? 2 : 1;
    case 20:
        return n.critical ? 15 : 10;
    case 22:
        return 11;
    case 40:
    case 41:
    case 42:
    case 102:
        return 5;
    case 62:
        return 22;
    case 21:
    case 60:
    case 61:
    case 63:
    case 104:
    case 105:
    case 120:
        return 21;
    case 101:
        return 1;
    default:
        throw Error("unknown judgement operation " + std::to_string(n.operate_type));
    }
}
inline int quantize(int bp, bool rounded = false) {
    float scaled = f32(f32(f32(bp) / 10000.f) * 100000.f);
    if (!std::isfinite(scaled) || scaled < INT32_MIN || scaled >= 2147483648.f)
        throw Error("skill factor overflow");
    if (!rounded)
        return int(std::floor(scaled));
    double down = std::floor(scaled), part = scaled - down;
    return int(down + (part > .5 || (part == .5 && int64_t(down) % 2 != 0)));
}
inline bool approximately(float a, float b) {
    return std::abs(f32(a - b)) < std::max(f32(1e-6f * std::max(std::abs(a), std::abs(b))),
                                           f32(std::numeric_limits<float>::denorm_min() * 8));
}
struct Rule {
    const Json *row = nullptr;
    int owner = 0, slot = 0, band = 0, skill = 0, skill_index = 0;
    bool support = false, gekisou = false;
    std::vector<const Json *> conditions, trigger, release, reset, targets;
    const Json *cumulative = nullptr;
    int effect_type = 0, effect_value = 0, effect_id = 0, trigger_type = 0;
    int kind() const {
        return effect_type;
    }
    int value() const {
        return effect_value;
    }
    int id() const {
        return effect_id;
    }
    int trigger_kind() const {
        return trigger_type;
    }
};
struct Tables {
    const Master &master;
    std::vector<const Json *> conditions(int group) const {
        if (!group)
            return {};
        const Json *found = nullptr;
        for (const auto &r : master.rows("MasterSkillConditionSet"))
            if (iv(r, "_group") == group) {
                if (found)
                    throw Error("compound skill condition group unsupported");
                found = &r;
            }
        if (!found)
            throw Error("missing skill condition group");
        std::vector<const Json *> out;
        for (const auto &id : found->at("_conditionIds"))
            out.push_back(&master.by_id("MasterSkillCondition", integer(id, "condition reference", 1)));
        return out;
    }
    Rule rule(const Json &r, int owner, int slot, int band, int skill, bool support, bool gekisou,
              int index = 0) const {
        Rule v;
        v.row = &r;
        v.effect_type = iv(r, "_skillEffectType");
        v.effect_value = iv(r, "_effectValue");
        v.effect_id = iv(r, "_id");
        v.owner = owner;
        v.slot = slot;
        v.band = band;
        v.skill = skill;
        v.support = support;
        v.gekisou = gekisou;
        v.skill_index = index;
        v.conditions = conditions(iv(r, "_skillConditionGroup"));
        v.trigger = conditions(iv(r, "_skillTriggerConditionGroup"));
        v.trigger_type = v.trigger.empty() ? 0 : iv(*v.trigger[0], "_conditionType");
        v.release = conditions(iv(r, "_skillReleaseConditionGroup"));
        v.reset = conditions(iv(r, "_effectExecuteLimitResetConditionGroup"));
        for (const auto &id : r.at("_skillTargetIDs"))
            v.targets.push_back(&master.by_id("MasterSkillTarget", integer(id, "skill target reference", 1)));
        if (iv(r, "_skillCumulativeConditionID"))
            v.cumulative =
                &master.by_id("MasterSkillCumulativeCondition", iv(r, "_skillCumulativeConditionID"));
        return v;
    }
};
inline std::set<int> judgement_targets(const Rule &r) {
    std::set<int> out;
    for (auto t : r.targets) {
        if (iv(*t, "_skillTargetType") != 4 || iv(*t, "_judgement") < 1 || iv(*t, "_judgement") > 6)
            throw Error("unsupported skill judgement target");
        out.insert(iv(*t, "_judgement"));
    }
    return out;
}
inline void require(bool ok, const Rule &r, const char *what) {
    if (!ok)
        throw Error("unsupported effect " + std::to_string(r.id()) + " type " + std::to_string(r.kind()) +
                    ": " + what);
}
inline bool complete_condition(const std::vector<const Json *> &cs) {
    return cs.size() == 1 && iv(*cs[0], "_conditionType") == 7013 && cs[0]->at("_isPositive").get<bool>() &&
           cs[0]->at("_conditionValues").empty() && cs[0]->at("_conditionTargetIDs").empty();
}
inline int mission_for(const Rule &r, const Master &master) {
    if (r.trigger.empty())
        return 0;
    const auto &c = *r.trigger[0];
    int kind = iv(c, "_conditionType");
    if (kind != 7010 && kind != 7020)
        return 0;
    require(c.at("_isPositive").get<bool>() && c.at("_conditionValues").empty() &&
                c.at("_conditionTargetIDs").size() == 1,
            r, "mission trigger");
    auto &t = master.by_id("MasterSkillTarget", c.at("_conditionTargetIDs").at(0).get<int>());
    int mission = iv(t, "_gekisouMissionType");
    require(iv(t, "_skillTargetType") == 5 && mission >= 1 && mission <= 3, r, "mission target");
    return mission;
}
inline bool evaluate(const Rule &r, const Master &master, BranchRandom &rng, int life, int combo = 0,
                     bool trigger = false) {
    for (auto cp : trigger ? r.trigger : r.conditions) {
        const auto &c = *cp;
        int kind = iv(c, "_conditionType");
        bool pass = false;
        if (kind == 2001 || kind == 7005 || kind == 4011) {
            require(c.at("_conditionValues").size() == 1 && c.at("_conditionTargetIDs").empty(), r,
                    "condition arguments");
            int v = c.at("_conditionValues").at(0).get<int>();
            pass = kind == 2001 ? life >= v : kind == 7005 ? combo >= v : rng.probability(v);
        } else if (kind == 5000) {
            require(r.support && c.at("_conditionValues").empty(), r, "attached band condition");
            for (const auto &id : c.at("_conditionTargetIDs")) {
                const auto &t = master.by_id("MasterSkillTarget", integer(id, "skill target reference", 1));
                require(iv(t, "_skillTargetType") == 3, r, "band target");
                pass |= r.band == iv(t, "_bandID");
            }
        } else
            throw Error("unsupported ordered skill condition " + std::to_string(kind));
        if (pass != c.at("_isPositive").get<bool>())
            return false;
    }
    return true;
}
inline int duration(const Rule &r) {
    double seconds = r.row->value<double>("_activationTimeSecond", 0);
    float ms = f32(f32(seconds) * 1000.f);
    require(std::isfinite(ms) && ms >= 0 && ms < 2147483648.f, r, "duration");
    return int(std::ceil(ms));
}
inline void validate(const Rule &r, const Master &master) {
    // Validate every selected condition even when short-circuit evaluation
    // would make it unreachable in this particular ideal path.
    for (const auto *group : {&r.conditions, &r.trigger, &r.release, &r.reset})
        for (const Json *c : *group) {
            require(c->at("_isPositive").is_boolean() && c->at("_conditionValues").is_array() &&
                        c->at("_conditionTargetIDs").is_array(),
                    r, "condition shape");
            for (const auto &value : c->at("_conditionValues"))
                integer(value, "condition value", 0, INT32_MAX);
            for (const auto &id : c->at("_conditionTargetIDs"))
                integer(id, "condition target", 1, INT32_MAX);
        }
    require(iv(*r.row, "_effectExecuteLimitCount") >= 0, r, "negative execute limit");
    if (r.cumulative) {
        integer(r.cumulative->at("_maxCumulativeCount"), "cumulative max count");
        for (const auto &value : r.cumulative->at("_conditionValues"))
            integer(value, "cumulative threshold", 1);
        for (const auto &id : r.cumulative->at("_conditionTargetIDs"))
            integer(id, "cumulative target", 1);
    }
    require(r.value() >= 0 && iv(*r.row, "_maxEffectValue") >= 0 && iv(*r.row, "_effectLimitCount") >= 0, r,
            "negative effect value/limit");
    if (!r.gekisou && !r.support) {
        require(r.kind() == 2000 || r.kind() == 2002 || r.kind() == 2004, r, "ordinary member type");
        require(duration(r) > 0 && !r.cumulative && r.release.empty() && !iv(*r.row, "_effectLimitCount") &&
                    !iv(*r.row, "_effectExecuteLimitCount") && r.reset.empty() &&
                    !iv(*r.row, "_maxEffectValue"),
                r, "ordinary member state");
        if (r.kind() == 2004) {
            auto ts = judgement_targets(r);
            require(!ts.empty() && *ts.begin() >= 3, r, "member target");
        } else
            require(r.targets.empty(), r, "unexpected target");
        for (auto c : r.conditions)
            require(iv(*c, "_conditionType") == 2001, r, "member life condition");
        return;
    }
    if (!r.gekisou) {
        require(r.kind() == 2000 || r.kind() == 3001 || r.kind() == 12006 || r.kind() == 15000, r,
                "ordinary support type");
        require(r.trigger.size() == 1 && (r.trigger_kind() == 4010 || r.trigger_kind() == 8000) &&
                    iv(*r.row, "_skillTriggerType") == 1 && r.trigger[0]->at("_isPositive").get<bool>() &&
                    r.trigger[0]->at("_conditionValues").empty() &&
                    r.trigger[0]->at("_conditionTargetIDs").empty(),
                r, "ordinary support trigger");
        require(r.kind() == 2000 || r.trigger_kind() == 4010, r, "ordinary effect/trigger pair");
        require((r.kind() == 3001 || r.kind() == 15000) ? duration(r) == 0 : duration(r) > 0, r,
                "ordinary effect duration");
        require(r.release.empty() && r.reset.empty() && !r.cumulative &&
                    !iv(*r.row, "_effectExecuteLimitCount") && !iv(*r.row, "_maxEffectValue"),
                r, "ordinary support state");
        for (auto c : r.conditions)
            require(iv(*c, "_conditionType") == 5000, r, "ordinary band condition");
        if (r.kind() == 12006) {
            auto ts = judgement_targets(r);
            require(!ts.empty() && *ts.rbegin() < 5 && r.value() == 5 && iv(*r.row, "_effectLimitCount") > 0,
                    r, "ideal ordinary converter proof");
        } else
            require(r.targets.empty() && !iv(*r.row, "_effectLimitCount"), r, "ordinary target/limit");
        return;
    }
    for (auto c : r.conditions)
        if (iv(*c, "_conditionType") == 4011) {
            require(c->at("_conditionValues").size() == 1 && c->at("_conditionTargetIDs").empty(), r,
                    "probability condition arguments");
            require(c->at("_conditionValues").at(0).get<int>() >= 0 &&
                        c->at("_conditionValues").at(0).get<int>() <= 100,
                    r, "probability range");
        }
    int mission = mission_for(r, master), trigger = r.trigger_kind();
    if (trigger == 7000)
        require(r.trigger[0]->at("_conditionValues").size() == 1 &&
                    r.trigger[0]->at("_conditionValues").at(0).get<int>() >= 0 &&
                    r.trigger[0]->at("_conditionValues").at(0).get<int>() <= 3 &&
                    r.trigger[0]->at("_conditionTargetIDs").empty() &&
                    r.trigger[0]->at("_isPositive").get<bool>(),
                r, "Luck result trigger arguments");
    if (!r.support) {
        require(r.kind() == 12000 || r.kind() == 13000 || r.kind() == 11001 || r.kind() == 11003 ||
                    r.kind() == 13002 || r.kind() == 11002,
                r, "Gekisou member type");
        require(r.targets.empty() && !iv(*r.row, "_effectLimitCount"), r, "Gekisou member target/limit");
        require((r.kind() == 12000 && (mission == 1 || trigger == 7005)) ||
                    (r.kind() == 13000 && mission == 3) ||
                    (r.kind() == 13002 && mission == 3 && trigger == 7010) ||
                    (r.kind() == 11001 && mission == 2) ||
                    (r.kind() == 11003 && mission == 2 && trigger == 7010) ||
                    (r.kind() == 11002 && trigger == 7000),
                r, "Gekisou member effect/trigger pair");
        require(r.kind() == 13002 || !iv(*r.row, "_maxEffectValue"), r, "unexpected effect cap");
        if (r.kind() == 11002 || r.kind() == 11003)
            require(duration(r) == 0, r, "instant Luck duration");
        if (trigger == 7005) {
            require(r.kind() == 12000 && iv(*r.row, "_skillTriggerType") == 2 && duration(r) == 0 &&
                        r.conditions.empty() && r.release.empty() && !r.cumulative &&
                        !iv(*r.row, "_effectExecuteLimitCount") && r.reset.empty(),
                    r, "sustained combo state");
            for (auto c : r.trigger)
                require(iv(*c, "_conditionType") == 7005 || iv(*c, "_conditionType") == 2001, r,
                        "sustained combo condition");
        } else if (trigger == 7000) {
            require(r.kind() == 11002 && r.trigger.size() == 1 && iv(*r.row, "_skillTriggerType") == 1 &&
                        r.conditions.empty() && r.release.empty() && !r.cumulative &&
                        !iv(*r.row, "_effectExecuteLimitCount") && r.reset.empty(),
                    r, "Luck result member");
        } else {
            require(mission && r.trigger.size() == 1 &&
                        iv(*r.row, "_skillTriggerType") == (trigger == 7020 ? 2 : 1),
                    r, "Gekisou start trigger");
            require(r.release.empty() || complete_condition(r.release), r, "Gekisou release");
            require(iv(*r.row, "_effectExecuteLimitCount") <= 1 &&
                        (r.reset.empty() || complete_condition(r.reset)) &&
                        (!iv(*r.row, "_effectExecuteLimitCount") || complete_condition(r.reset)),
                    r, "Gekisou execute/reset");
            for (auto c : r.conditions)
                require(iv(*c, "_conditionType") == 2001 || iv(*c, "_conditionType") == 4011, r,
                        "Gekisou start condition");
        }
        if (r.kind() == 13002)
            require(r.cumulative && iv(*r.cumulative, "_skillCumulativeConditionType") == 1000 &&
                        r.cumulative->at("_conditionValues").size() == 1 &&
                        r.cumulative->at("_conditionValues").at(0).get<int>() > 0 &&
                        r.cumulative->at("_conditionTargetIDs").size() == 1 &&
                        iv(master.by_id("MasterSkillTarget",
                                        r.cumulative->at("_conditionTargetIDs").at(0).get<int>()),
                           "_judgement") == 6,
                    r, "cumulative Just");
        else
            require(!r.cumulative, r, "unexpected cumulative rule");
        return;
    }
    for (auto c : r.conditions)
        require(iv(*c, "_conditionType") == 5000 || (r.kind() == 11005 && iv(*c, "_conditionType") == 4011),
                r, "Gekisou support condition");
    require(r.kind() == 2000 || r.kind() == 2001 || r.kind() == 4004 || r.kind() == 12006 ||
                r.kind() == 11005 || r.kind() == 13005 || r.kind() == 12004 || r.kind() == 11003,
            r, "Gekisou support type");
    if (r.kind() == 2000) {
        require(trigger == 7021 && r.trigger.size() == 1 && iv(*r.row, "_skillTriggerType") == 2 &&
                    r.targets.empty() && duration(r) == 0 && !r.cumulative && r.release.empty() &&
                    r.reset.empty() && !iv(*r.row, "_maxEffectValue") && !iv(*r.row, "_effectLimitCount") &&
                    !iv(*r.row, "_effectExecuteLimitCount"),
                r, "rush score state");
        require(r.trigger[0]->at("_isPositive").get<bool>() && r.trigger[0]->at("_conditionValues").empty() &&
                    r.trigger[0]->at("_conditionTargetIDs").empty(),
                r, "positive Rush condition");
    } else if (r.kind() == 2001) {
        require((mission == 1 || mission == 3) && trigger == 7010 && r.trigger.size() == 1 &&
                    iv(*r.row, "_skillTriggerType") == 1 && complete_condition(r.release) && r.cumulative &&
                    r.targets.empty() && duration(r) == 0 && !iv(*r.row, "_effectLimitCount") &&
                    !iv(*r.row, "_effectExecuteLimitCount") && r.reset.empty(),
                r, "cumulative score state");
        require(r.cumulative->at("_conditionValues").size() == 1 &&
                    r.cumulative->at("_conditionValues").at(0).get<int>() > 0 &&
                    iv(*r.cumulative, "_maxCumulativeCount") > 0 &&
                    iv(*r.cumulative, "_skillCumulativeConditionType") == (mission == 3 ? 1000 : 7001),
                r, "cumulative score metric");
        const auto &targets = r.cumulative->at("_conditionTargetIDs");
        if (mission == 1)
            require(targets.empty(), r, "Combo cumulative targets");
        else
            require(targets.size() == 1 &&
                        iv(master.by_id("MasterSkillTarget", targets.at(0).get<int>()), "_skillTargetType") ==
                            4 &&
                        iv(master.by_id("MasterSkillTarget", targets.at(0).get<int>()), "_judgement") == 6,
                    r, "raw Just cumulative target");
    } else if (r.kind() == 13005) {
        require(mission == 3 && trigger == 7020 && r.trigger.size() == 2 &&
                    iv(*r.row, "_skillTriggerType") == 1 && !iv(*r.row, "_maxEffectValue") &&
                    iv(*r.trigger[1], "_conditionType") == 1030 &&
                    r.trigger[1]->at("_conditionValues").size() == 1 &&
                    r.trigger[1]->at("_conditionValues").at(0).get<int>() > 0 &&
                    complete_condition(r.release) && r.reset.size() == 1 &&
                    iv(*r.reset[0], "_conditionType") == 7010 && iv(*r.row, "_effectLimitCount") > 0 &&
                    iv(*r.row, "_effectExecuteLimitCount") > 0,
                r, "Just conversion state");
        judgement_targets(r);
        require(r.trigger[1]->at("_isPositive").get<bool>() && !r.cumulative &&
                    r.reset[0]->at("_isPositive").get<bool>() && r.reset[0]->at("_conditionValues").empty() &&
                    r.reset[0]->at("_conditionTargetIDs") == r.trigger[0]->at("_conditionTargetIDs"),
                r, "Just conversion count/reset conditions");
        for (auto &id : r.trigger[1]->at("_conditionTargetIDs"))
            require(iv(master.by_id("MasterSkillTarget", integer(id, "skill target reference", 1)),
                       "_skillTargetType") == 4,
                    r, "Just conversion counted judgement target");
    } else if (r.kind() == 11003) {
        require(
            trigger == 7000 && r.trigger.size() == 1 && r.trigger[0]->at("_conditionValues").size() == 1 &&
                iv(*r.row, "_effectExecuteLimitCount") == 1 && complete_condition(r.reset) &&
                r.targets.empty() && !r.cumulative && duration(r) == 0 &&
                iv(*r.row, "_skillTriggerType") == 1 && (r.release.empty() || complete_condition(r.release)),
            r, "Luck result gauge support");
    } else {
        require(mission && r.trigger.size() == 1 && !r.cumulative &&
                    (r.release.empty() || complete_condition(r.release)),
                r, "Gekisou support start");
        require(iv(*r.row, "_skillTriggerType") == (trigger == 7020 ? 2 : 1) &&
                    !iv(*r.row, "_effectExecuteLimitCount") && r.reset.empty() &&
                    !iv(*r.row, "_maxEffectValue"),
                r, "support start execution state");
        if (r.kind() == 4004) {
            require(mission == 3 && judgement_targets(r) == std::set<int>{6}, r,
                    "ideal timing relaxation proof");
        }
        if (r.kind() == 12006) {
            auto ts = judgement_targets(r);
            require(mission == 1 && !ts.empty() && *ts.rbegin() < 5 && r.value() == 5, r,
                    "ideal conversion proof");
        }
        if (r.kind() == 12004) {
            auto ts = judgement_targets(r);
            require(mission == 1 && !ts.empty() && *ts.rbegin() <= 2, r, "ideal Combo protection proof");
        }
        if (r.kind() == 11005)
            require(mission == 2 && trigger == 7010 && r.value() >= 2 && r.value() <= 4 &&
                        r.targets.empty() && duration(r) == 0 && complete_condition(r.release) &&
                        iv(*r.row, "_effectLimitCount") > 0,
                    r, "minimum Luck outcome");
    }
}
struct CountActivation {
    const Rule *rule = nullptr;
    int start = 0, end = 0, begin_sequence = -1, end_sequence = -1, observed_end = 0;
    bool retired = false;
};
struct Guarantee {
    const Rule *rule;
    int end, remaining;
};
struct Converter {
    const Rule *rule;
    int end, remaining;
    std::set<int> targets;
};
struct SupportState {
    const Rule *rule;
    bool accepted = false, active = false;
    int count = 0, executed = 0, raw = 0;
};
struct Luck {
    int normal = 1, rush = 1, gauge = 0, lots = 0, rush_combo = 0, point = 0, next = -1;
    int maximum() const {
        return rush_combo ? rush : normal;
    }
    int type() const {
        return rush_combo > 3 ? 1 : std::array<int, 4>{0, 4, 3, 2}[rush_combo];
    }
    void add(int value) {
        if (value < 0 || int64_t(gauge) + value > INT32_MAX)
            throw Error("Luck gauge overflow");
        gauge += value;
        lots = metric(int64_t(lots) + gauge / maximum());
        gauge %= maximum();
    }
    void apply(int result) {
        int old = maximum();
        point = metric(int64_t(point) + std::array<int, 4>{0, 5, 10, 10}.at(result));
        rush_combo = result == 3 ? metric(int64_t(rush_combo) + 1) : 0;
        if (old != maximum() && gauge > maximum()) {
            lots = metric(int64_t(lots) + gauge / maximum());
            gauge %= maximum();
        }
    }
};
using Lottery = std::vector<std::pair<uint64_t, int>>;
inline int choose_lottery(const Lottery &table, BranchRandom &rng, int minimum = 0) {
    Lottery eligible;
    uint64_t excluded = 0;
    for (auto entry : table) {
        if (entry.second >= minimum)
            eligible.push_back(entry);
        else
            excluded += entry.first;
    }
    if (eligible.empty())
        throw Error("Luck minimum excludes every outcome");
    uint64_t extra = excluded / eligible.size();
    // Equal-result entries can merge only after native per-entry redistribution.
    std::map<int, uint64_t> merged;
    for (auto [w, result] : eligible)
        merged[result] += w + extra;
    std::vector<uint64_t> weights;
    std::vector<int> values;
    for (auto [result, w] : merged) {
        values.push_back(result);
        weights.push_back(w);
    }
    return values.at(rng.choose(weights));
}
inline int setting(const Master &master, std::string_view key, int fallback = -1) {
    for (const auto &r : master.rows("MasterLiveSettings"))
        if (r.at("_key").get<std::string>() == key) {
            const auto &v = r.at("_value");
            if (v.is_string()) {
                const auto text = v.get<std::string>();
                size_t used = 0;
                const auto value = std::stoll(text, &used);
                if (used != text.size() || value < INT32_MIN || value > INT32_MAX)
                    throw Error("invalid integer setting " + std::string(key));
                return int(value);
            }
            return iv(r, "_value");
        }
    if (fallback >= 0)
        return fallback;
    throw Error("missing live setting " + std::string(key));
}
inline void factor_pair(std::vector<Factor> &commands, const Rule &r, int start, int end) {
    int raw = quantize(r.value(), r.kind() != 2000);
    std::vector<int> targets{0};
    if (r.kind() == 2004) {
        targets.clear();
        for (auto t : r.targets)
            targets.push_back(iv(*t, "_judgement"));
    }
    for (int judgement : targets) {
        Factor f;
        f.time_ms = start;
        f.owner = r.owner;
        f.judgement = judgement;
        if (r.kind() == 2000)
            f.note_raw = raw;
        else if (r.kind() == 2002)
            f.combo_raw = raw;
        else
            f.judgement_raw = raw;
        commands.push_back(f);
        f.time_ms = end;
        f.note_raw = -f.note_raw;
        f.combo_raw = -f.combo_raw;
        f.judgement_raw = -f.judgement_raw;
        commands.push_back(f);
    }
}

struct RuntimeData {
    const Prepared *prepared = nullptr;
    Json deck, request;
    RuntimeResult initial;
    std::vector<Rule> ordinary, members, supports;
    int life = 0, max_life = 0;
    std::set<int> frames, just_types;
    bool needs_rank = false, gekisou = false;
    std::map<int, int> rank_thresholds;
    std::vector<const Note *> notes;
    std::array<int, 3> completes{}, finishes{};
    std::map<std::pair<int, int>, Lottery> base;
    std::map<int, Lottery> bonus;
    int rush_bonus = 0;
    Luck luck;
};
std::shared_ptr<const RuntimeData> compile_runtime_data(const Prepared &, const Json &, const Json &);
} // namespace mns::runtime_detail
