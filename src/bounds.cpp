#include "analysis.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace mns {
namespace {
int value(const Json &row, std::string_view key, int fallback = 0) {
    return row.contains(key) ? int(integer(row.at(key), std::string(key).c_str())) : fallback;
}
int setting(const Master &master, std::string_view key) {
    for (const auto &row : master.rows("MasterLiveSettings"))
        if (row.at("_key").get<std::string>() == key) {
            if (row.at("_value").is_number_integer())
                return int(integer(row.at("_value"), std::string(key).c_str()));
            const auto text = row.at("_value").get<std::string>();
            std::size_t consumed = 0;
            const auto result = std::stoll(text, &consumed);
            if (consumed != text.size() || result < 0 || result > INT32_MAX)
                throw Error("invalid nonnegative integer setting: " + std::string(key));
            return int(result);
        }
    throw Error("missing setting for score bound: " + std::string(key));
}
std::vector<const Json *> conditions(const Master &master, int group) {
    if (!group)
        return {};
    const Json *found = nullptr;
    for (const auto &row : master.rows("MasterSkillConditionSet"))
        if (value(row, "_group") == group) {
            if (found)
                throw Error("compound condition set cannot be bounded");
            found = &row;
        }
    if (!found)
        throw Error("missing condition set for score bound");
    std::vector<const Json *> output;
    for (const auto &id : found->at("_conditionIds"))
        output.push_back(&master.by_id("MasterSkillCondition", integer(id, "condition id", 1)));
    return output;
}
struct Effect {
    const Json *row;
    bool support;
    bool gekisou;
};
std::vector<Effect> selected(const Prepared &prepared, const Json &deck, bool gekisou) {
    std::vector<Effect> output;
    const auto &master = prepared.master;
    auto append = [&](std::string_view table, std::string_view key, int skill, int level, bool support,
                      bool gek) {
        if (!skill)
            return;
        bool found = false;
        for (const auto &row : master.rows(table))
            if (value(row, key) == skill && value(row, "_level") == level) {
                output.push_back({&row, support, gek});
                found = true;
            }
        if (!found)
            throw Error("missing selected skill while bounding score");
    };
    for (const auto &slot : deck.at("slots")) {
        const auto &card = master.by_id("MasterMemberCard", integer(slot.at("member_id"), "member id", 1));
        append("MasterLiveSkillEffect", "_liveSkillID", value(card, "_liveSkillID"),
               value(slot, "live_skill_level", 1), false, false);
        if (gekisou)
            append("MasterGekisouSkillEffect", "_gekisouSkillID", value(card, "_gekisouSkillID"),
                   value(slot, "gekisou_skill_level", 1), false, true);
        if (!slot.contains("support") || slot.at("support").is_null())
            continue;
        const auto &support = slot.at("support");
        const auto &card_support =
            master.by_id("MasterSupportCard", integer(support.at("master_id"), "support id", 1));
        const Json *rank = nullptr;
        for (const auto &row : master.rows("MasterSupportCardRank"))
            if (value(row, "_group") == value(card_support, "_supportCardRankGroup") &&
                value(row, "_rank") == value(support, "rank", 1)) {
                if (rank)
                    throw Error("ambiguous support rank while bounding score");
                rank = &row;
            }
        if (!rank)
            throw Error("missing support rank while bounding score");
        for (const auto *suffix : {"01", "02"}) {
            append("MasterSupportSkillEffect", "_supportSkillID",
                   value(card_support, std::string("_supportSkillId") + suffix),
                   value(*rank, std::string("_supportSkill") + suffix + "Level"), true, false);
            if (gekisou)
                append("MasterGekisouSupportSkillEffect", "_gekisouSupportSkillID",
                       value(card_support, std::string("_gekisouSupportSkillId") + suffix),
                       value(*rank, std::string("_gekisouSupportSkill") + suffix + "Level"), true, true);
        }
    }
    return output;
}
float positive_factor(int bp, bool rounded) {
    const auto raw = f32(f32(f32(bp) / 10000.f) * 100000.f);
    if (!std::isfinite(raw) || raw < 0 || raw >= 2147483648.f)
        throw Error("skill factor cannot be bounded inside int32");
    double integral = std::floor(raw), fraction = raw - integral;
    if (rounded && (fraction > .5 || (fraction == .5 && std::int64_t(integral) % 2)))
        ++integral;
    if (integral > INT32_MAX)
        throw Error("rounded skill factor overflow");
    return f32(f32(integral) / 100000.f);
}
float up_float(double number) {
    if (!std::isfinite(number) || number < 0 || number >= std::numeric_limits<float>::max())
        throw Error("nonfinite score bound");
    return std::nextafter(f32(number), std::numeric_limits<float>::infinity());
}
struct FactorEnvelope {
    double active = 0;
    std::uint64_t operations = 0;
    void add(double factor, std::uint64_t simultaneous, std::uint64_t activations) {
        const auto product =
            std::nextafter(factor * double(simultaneous), std::numeric_limits<double>::infinity());
        active = std::nextafter(active + product, std::numeric_limits<double>::infinity());
        if (activations > (UINT64_MAX - operations) / 2)
            throw Error("too many factor operations for score bound");
        operations += 2 * activations;
    }
    double error(double initial) const {
        // At every exact intermediate point paired additions/removals leave a
        // nonnegative sum bounded by M. Each rounded add has error <=u*|x|.
        // Recurrence E' <= (1+u)E+u*M gives E <= M*n*u/(1-n*u).
        // Inflate the final binary64 computation before rounding to binary32.
        const double product = double(operations) * 0x1p-24;
        if (product >= .25)
            throw Error("factor update count exceeds proven floating point envelope");
        return std::nextafter((initial + active) * product / (1 - product) * (1 + 0x1p-40),
                              std::numeric_limits<double>::infinity());
    }
    float upper(double initial) const {
        return up_float(initial + active + error(initial));
    }
};
int frame(int time) {
    return int(std::ceil(f32(f32(time) / 40.f)));
}
std::int64_t note_upper(const Prepared &prepared, int power, const Note &note, float combo, float factor,
                        int judge_percent, int luck_percent) {
    const auto difficulty = f32(f32(f32(prepared.level - 5) * f32(.005)) + 1.f);
    auto score = f32(prepared.adjustment * f32(power));
    score = f32(score * difficulty);
    score = f32(f32(f32(prepared.note_percent.at(note.operate_type)) / 100.f) * score);
    score = f32(f32(f32(judge_percent) / 100.f) * score);
    score = f32(score * combo);
    score = f32(score * factor);
    score = f32(f32(f32(luck_percent) / 100.f) * score);
    score = f32(score / f32(prepared.denominator));
    if (!std::isfinite(score) || score < 0 || double(score) > INT32_MAX)
        throw Error("per-note score cannot be proven inside int32");
    // Native performs a second float32 cast of the floored integer even at
    // positive life/without assist. Account for that final rounding as well.
    const auto final = f32(std::floor(score));
    if (!std::isfinite(final) || double(final) > INT32_MAX)
        throw Error("final note bound overflow");
    return std::int64_t(std::floor(final));
}
} // namespace

std::vector<ScoreBound> score_bounds(const Prepared &prepared, const Json &deck, const Json &request) {
    const auto mode = request.value("mode", "free");
    if (mode == "challenge_skip") {
        auto calculation = request;
        calculation["contributions"] = false;
        const auto result = score_path(prepared, deck, calculation, RuntimeResult{});
        if (result.size() != 1)
            throw Error("Challenge skip result shape");
        return {{result[0].total, result[0].total}};
    }
    const bool gekisou =
        request.value("gekisou_enabled", mode == "battle" || mode == "arena" || mode == "mission");
    const auto power = int(integer(deck.at("total_power"), "total_power", 1));
    if (!std::isfinite(prepared.adjustment) || prepared.adjustment <= 0 || prepared.denominator <= 0)
        throw Error("nonpositive score adjustment or denominator");
    for (const auto &curve : prepared.combo)
        for (const auto &[threshold, factor] : curve) {
            (void)threshold;
            if (!std::isfinite(factor) || factor < 0)
                throw Error("negative or nonfinite combo factor");
        }
    const auto initial_life = setting(prepared.master, "life_base");
    if (!initial_life || initial_life > INT32_MAX / 2)
        throw Error("initial life outside proven domain");
    const auto effects = selected(prepared, deck, gekisou);
    std::size_t note_count = 0;
    for (const auto &note : prepared.notes)
        if (note.judgement)
            ++note_count;
    std::uint64_t grid_updates = 0;
    if (gekisou) {
        int slow_after = 0;
        for (const auto &row : prepared.master.rows("MasterLiveJudgementTiming")) {
            const auto after = row.contains("_afterMs")
                                   ? integer(row.at("_afterMs"), "judgement after time", INT32_MIN, INT32_MAX)
                                   : 0;
            slow_after = std::max(slow_after, int(after));
        }
        for (const auto &range : prepared.fevers) {
            const auto duration = std::int64_t(range.end_ms) + slow_after + 501 - range.time_ms;
            if (duration <= 0 || std::int64_t(range.end_ms) + slow_after + 501 > INT32_MAX)
                throw Error("Gekisou grid outside proven time domain");
            grid_updates += std::uint64_t(duration) + 1;
        }
    }
    int rank_hits = 0;
    if (prepared.master.tables.contains("MasterLiveScoreRank")) {
        const auto group =
            value(prepared.master.by_id("MasterLiveMusic", prepared.music_id), "_liveScoreRankGroup");
        for (const auto &row : prepared.master.rows("MasterLiveScoreRank"))
            if (value(row, "_group") == group)
                rank_hits = std::max(rank_hits, value(row, "_liveScoreRank") - 2);
    }
    // Bounds on Luck gauge inflow limit Rush toggles, including all legal draws.
    double gauge_factor = 1, gauge_additions = 0;
    std::uint64_t max_lotteries = 0;
    if (gekisou) {
        const auto normal = setting(prepared.master, "gekisou_luck_gauge_max");
        const auto rush = setting(prepared.master, "gekisou_luck_gauge_max_rush");
        if (!normal || !rush)
            throw Error("nonpositive Luck gauge maximum");
        const auto rush_bonus = setting(prepared.master, "gekisou_luck_rush_score_bonus_percent");
        if (rush_bonus > INT32_MAX - 100)
            throw Error("Rush score factor can overflow int32");
        for (const auto &effect : effects)
            if (effect.gekisou) {
                const auto kind = value(*effect.row, "_skillEffectType"),
                           amount = value(*effect.row, "_effectValue");
                if (kind == 11001)
                    gauge_factor += f32(f32(amount) / 10000.f);
                if (kind == 11003) {
                    const auto product = std::int64_t(std::max(normal, rush)) * amount;
                    if (product > INT32_MAX)
                        throw Error("Luck gauge product can wrap outside proven domain");
                    gauge_additions += 3 * std::ceil(double(f32(f32(product) / 10000.f)));
                }
            }
        int point = 0;
        for (const auto &row : prepared.master.rows("MasterLiveGekisouLuckBasePoint"))
            if (value(row, "_weight"))
                point = std::max(point, value(row, "_basePoint"));
        // At most one active copy of each gauge effect, with binary32 additions.
        const auto additions_roundoff = effects.size() * 0x1p-24;
        if (additions_roundoff >= .25)
            throw Error("too many gauge effects for floating point envelope");
        const auto safe_factor = up_float(gauge_factor * (1 + 2 * additions_roundoff));
        const auto per_note = std::ceil(double(f32(f32(point) * safe_factor)));
        const auto inflow = per_note * note_count + gauge_additions;
        if (!std::isfinite(inflow) || inflow > INT32_MAX)
            throw Error("Luck inflow exceeds proven int32 domain");
        max_lotteries = std::uint64_t(std::ceil(inflow / std::min(normal, rush))) + 3;
    }
    FactorEnvelope note_factor, combo_factor;
    std::array<FactorEnvelope, 7> judge_factor;
    for (const auto &effect : effects) {
        const auto &row = *effect.row;
        const auto kind = value(row, "_skillEffectType");
        auto amount = value(row, "_effectValue");
        if (!effect.gekisou) {
            const std::set<int> member_kinds{2000, 2002, 2004}, support_kinds{2000, 3001, 12006, 15000};
            if (!(effect.support ? support_kinds : member_kinds).contains(kind))
                throw Error("ordinary effect has no score bound");
            if (kind != 2000 && kind != 2002 && kind != 2004)
                continue;
            std::uint64_t activations = 1;
            if (effect.support) {
                const auto trigger = conditions(prepared.master, value(row, "_skillTriggerConditionGroup"));
                if (trigger.size() != 1)
                    throw Error("ordinary support trigger cannot be bounded");
                const auto trigger_kind = value(*trigger.front(), "_conditionType");
                if (trigger_kind == 8000)
                    activations = std::uint64_t(rank_hits);
                else if (trigger_kind != 4010)
                    throw Error("ordinary support activation count unknown");
            }
            const auto factor = positive_factor(amount, kind != 2000);
            if (kind == 2000)
                note_factor.add(factor, activations, activations);
            else if (kind == 2002)
                combo_factor.add(factor, activations, activations);
            else
                for (const auto &id : row.at("_skillTargetIDs")) {
                    const auto &target =
                        prepared.master.by_id("MasterSkillTarget", integer(id, "target id", 1));
                    const auto judgement = value(target, "_judgement");
                    if (judgement < 3 || judgement > 6)
                        throw Error("unknown judgement factor target");
                    judge_factor[7 - judgement].add(factor, activations, activations);
                }
        } else if (effect.support && (kind == 2000 || kind == 2001)) {
            std::uint64_t activations = max_lotteries + 3;
            if (kind == 2001) {
                const auto &cumulative = prepared.master.by_id("MasterSkillCumulativeCondition",
                                                               value(row, "_skillCumulativeConditionID"));
                const auto product = std::int64_t(amount) * value(cumulative, "_maxCumulativeCount");
                if (product > INT32_MAX)
                    throw Error("cumulative score factor can wrap outside proven domain");
                amount = int(product);
                if (value(row, "_maxEffectValue"))
                    amount = std::min(amount, value(row, "_maxEffectValue"));
                // The controller runs at most once per integer millisecond.
                // Every update may remove the previous factor and add a new
                // factor, regardless of how dynamic conditions interact.
                activations = grid_updates;
            }
            note_factor.add(positive_factor(amount, false), 1, activations);
        } else if (!effect.support) {
            const std::set<int> known{12000, 13000, 11001, 11003, 13002, 11002};
            if (!known.contains(kind))
                throw Error("Gekisou member effect has no score bound");

        } else {
            const std::set<int> known{4004, 12006, 11005, 13005, 12004, 11003};
            if (!known.contains(kind))
                throw Error("Gekisou support effect has no score bound");
        }
    }
    const auto combo_error = combo_factor.error(0), note_error = note_factor.error(1);
    if (combo_error >= 1)
        throw Error("combo cancellation residual can leave the proven positive domain");
    const auto combo_upper = combo_factor.upper(0), note_upper_factor = note_factor.upper(1);
    std::array<float, 7> judge_upper{};
    for (std::size_t i = 0; i != judge_upper.size(); ++i) {
        if (note_error + judge_factor[i].error(0) >= 1)
            throw Error("note cancellation residual can leave the proven positive domain");
        judge_upper[i] = f32(note_upper_factor + judge_factor[i].upper(0));
    }
    std::int64_t total = 0;
    std::array<std::int64_t, 3> section_scores{};
    const auto perfect_percent = prepared.judgement_percent.at(2);
    const auto just_percent = gekisou ? prepared.judgement_percent.at(1) : perfect_percent;
    if (perfect_percent < 0 || just_percent < 0)
        throw Error("negative judgement weight has no score envelope");
    for (const auto &note : prepared.notes)
        if (note.judgement) {
            if (prepared.note_percent.at(note.operate_type) < 0)
                throw Error("negative note weight has no score envelope");
            bool combo_mission = false;
            for (std::size_t i = 0; gekisou && i < prepared.fevers.size(); ++i)
                if (prepared.missions[i] == 1 && prepared.fevers[i].time_ms <= note.time_ms &&
                    note.time_ms <= prepared.fevers[i].end_ms)
                    combo_mission = true;
            const auto combo = f32((combo_mission ? 2.f : 1.f) * f32(2.f + combo_upper));
            const auto factor = std::max(judge_upper[1], judge_upper[2]);
            const auto upper = note_upper(prepared, power, note, combo, factor,
                                          std::max(perfect_percent, just_percent), gekisou ? 200 : 100);
            total += upper;
            if (total > INT32_MAX)
                throw Error("note total exceeds proven int32 domain");
            for (std::size_t i = 0; gekisou && i < prepared.fevers.size(); ++i)
                if (frame(prepared.fevers[i].time_ms) < frame(note.time_ms) &&
                    frame(note.time_ms) <= frame(prepared.fevers[i].end_ms))
                    section_scores[i] += upper;
        }
    std::vector<std::array<int, 3>> ranks;
    if (!gekisou)
        ranks.push_back({0, 0, 0});
    else if (request.contains("ranks")) {
        if (!request.at("ranks").is_array() || request.at("ranks").size() != 3)
            throw Error("ranks requires three entries");
        ranks.push_back({int(integer(request.at("ranks").at(0), "rank", 1, 5)),
                         int(integer(request.at("ranks").at(1), "rank", 1, 5)),
                         int(integer(request.at("ranks").at(2), "rank", 1, 5))});
    } else
        for (int rank = 1; rank <= 5; ++rank)
            ranks.push_back({rank, rank, rank});
    std::array<std::array<int, 5>, 3> rank_percent{};
    if (gekisou) {
        const auto unique = std::set<int>(prepared.missions.begin(), prepared.missions.end()).size();
        const int pattern = unique == 1 ? 1 : unique == 2 ? 3 : 2;
        std::set<std::pair<int, int>> seen;
        for (const auto &row : prepared.master.rows("MasterLiveGekisouRankingScoreBonus"))
            if (value(row, "_missionPattern") == pattern) {
                const auto section = int(integer(row.at("_count"), "rank section", 1, 3)) - 1;
                const auto rank = int(integer(row.at("_rank"), "rank", 1, 5)) - 1;
                if (!seen.insert({section, rank}).second)
                    throw Error("duplicate rank bonus in score bound");
                rank_percent[section][rank] = value(row, "_scoreBonusPercent");
            }
        if (seen.size() != 15)
            throw Error("incomplete rank bonus table in score bound");
    }
    std::vector<ScoreBound> bounds;
    for (const auto &condition : ranks) {
        auto upper = total;
        for (std::size_t i = 0; i != 3; ++i)
            if (condition[i]) {
                upper += section_scores[i] * std::int64_t(rank_percent[i][condition[i] - 1]) / 100;
                if (upper > INT32_MAX)
                    throw Error("ranked total exceeds proven int32 domain");
            }
        bounds.push_back({0, upper});
    }
    return bounds;
}
} // namespace mns
