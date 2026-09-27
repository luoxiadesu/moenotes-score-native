#include "internal.hpp"
#include "power_math.hpp"
#include <algorithm>
#include <numeric>
#include <set>
namespace mns {
namespace {
int frame(int t) {
    return int(std::ceil(f32(f32(t) / 40.f)));
}
int64_t checked(int64_t n) {
    if (n < 0 || n > INT32_MAX)
        throw Error("score outside supported nonnegative int32 domain");
    return n;
}
int64_t floor_score(float f) {
    if (!std::isfinite(f) || f < 0 || double(f) > INT32_MAX)
        throw Error("score arithmetic overflow");
    return int64_t(std::floor(f));
}
struct State {
    float combo = 0, note = 1;
    std::array<float, 7> judge{};
    int luck = 0;
};
struct Owner {
    float combo = 0, note = 0, judge = 0;
};
void apply(State &s, const Factor &f) {
    s.combo = f32(s.combo + f32(f32(f.combo_raw) / 100000.f));
    s.note = f32(s.note + f32(f32(f.note_raw) / 100000.f));
    if (f.judgement) {
        if (f.judgement < 3 || f.judgement > 6)
            throw Error("invalid factor judgement");
        s.judge[7 - f.judgement] = f32(s.judge[7 - f.judgement] + f32(f32(f.judgement_raw) / 100000.f));
    } else if (f.judgement_raw)
        throw Error("missing factor judgement target");
    s.luck = wrap32(int64_t(s.luck) + f.luck);
}
float gekfactor(const Prepared &p, const RuntimeResult &r, int t) {
    for (auto &s : r.sections)
        if (s.start_ms <= t && t <= s.end_ms && s.mission == 1) {
            int combo = 0, cut = frame(t) * 40 - 40;
            for (auto [time, c] : s.combo) {
                if (time > cut)
                    break;
                combo = c;
            }
            return f32(1.f + std::min(p.combo_factor(combo, 1), 1.f));
        }
    return 1;
}
int64_t core(const Prepared &p, int power, int life, int op, int scoretype, float combo, float note, int luck,
             bool assist) {
    float difficulty = f32(f32(f32(p.level - 5) * f32(.005)) + 1.f);
    float base = f32(p.adjustment * f32(power));
    base = f32(base * difficulty);
    base = f32(f32(f32(p.note_percent.at(op)) / 100.f) * base);
    base = f32(f32(f32(p.judgement_percent.at(scoretype)) / 100.f) * base);
    base = f32(base * combo);
    base = f32(base * note);
    base = f32(f32(f32(luck) / 100.f) * base);
    base = f32(base / f32(p.denominator));
    auto score = floor_score(base);
    float final = f32(f32(score) * (life > 0 ? 1.f : p.life_factor));
    final = f32(final * (assist ? p.assist_factor : 1.f));
    return floor_score(final);
}
int mapped(int owner, const std::array<int, 5> &order) {
    if (owner < 0)
        return owner;
    int group = owner / 100;
    return group < 5 ? order[group] * 100 + owner % 100 : owner;
}
struct Track {
    int64_t total = 0, base = 0;
    std::array<int64_t, 3> sections{};
    std::array<int64_t, 10> params{}, skills{};
    Json note_detail = Json::array();
};
Track track(const Prepared &p, const Json &deck, const RuntimeResult &r, bool aux, bool assist,
            bool contributions, bool details) {
    Track out;
    int power = int(integer(deck.at("total_power"), "total_power", 1));
    State state;
    std::map<int, Owner> owners;
    std::vector<Factor> fs = r.shared;
    if (!aux)
        fs.insert(fs.end(), r.primary.begin(), r.primary.end());
    std::stable_sort(fs.begin(), fs.end(), [](auto &a, auto &b) {
        return std::tie(a.time_ms, a.owner) < std::tie(b.time_ms, b.owner);
    });
    std::map<int, const Note *> byid;
    for (auto &n : p.notes)
        byid[n.id] = &n;
    auto notes = r.notes;
    std::stable_sort(notes.begin(), notes.end(), [&](auto &a, auto &b) {
        auto *x = byid.at(a.id);
        auto *y = byid.at(b.id);
        return std::tie(x->time_ms, x->id) < std::tie(y->time_ms, y->id);
    });
    size_t fi = 0;
    int combo = 0, previous_time = -1, batch_count = 0;
    for (auto &input : notes) {
        auto &n = *byid.at(input.id);
        int time = n.time_ms;
        while (fi < fs.size() && fs[fi].time_ms <= time) {
            auto &f = fs[fi++];
            apply(state, f);
            if (contributions && f.owner >= 0) {
                auto &o = owners[f.owner];
                o.combo = f32(o.combo + f32(f32(f.combo_raw) / 100000.f));
                o.note = f32(o.note + f32(f32(f.note_raw) / 100000.f));
                o.judge = f32(o.judge + f32(f32(f.judgement_raw) / 100000.f));
            }
        }
        if (previous_time != time) {
            combo += batch_count;
            batch_count = 0;
            previous_time = time;
        }
        ++batch_count;
        int judge = aux ? input.solo_judgement : input.judgement;
        if (aux && judge == 6)
            judge = 5;
        int type = 7 - judge;
        float g = aux ? 1.f : gekfactor(p, r, time), mb = std::min(p.combo_factor(combo), 1.f),
              baseline_combo = f32(g * f32(1.f + mb));
        float cf = f32(g * f32(f32(1.f + mb) + state.combo));
        float nf = f32(state.note + state.judge.at(type));
        auto score =
            core(p, power, input.life, n.operate_type, type, cf, nf, std::min(state.luck + 100, 200), assist);
        out.total = checked(out.total + score);
        for (size_t i = 0; i < p.fevers.size() && i < 3; ++i)
            if (frame(p.fevers[i].time_ms) < frame(time) && frame(time) <= frame(p.fevers[i].end_ms))
                out.sections[i] = checked(out.sections[i] + score);
        if (details)
            out.note_detail.push_back(Json{{"note_id", n.id},
                                           {"time_ms", time},
                                           {"score", score},
                                           {"score_type", type},
                                           {"combo", combo}});
        if (!contributions)
            continue;
        auto base = core(p, power, input.life, n.operate_type, type, baseline_combo, 1, 100, assist);
        out.base += base;
        auto &slots = deck.at("slots");
        for (size_t i = 0; i < 5; ++i) {
            auto &s = slots.at(i);
            int unit = s.at("power").at("total").get<int>();
            int snap = s.at("components").at("support").at("total").get<int>();
            std::array<int64_t, 3> member_bp{};
            for (size_t axis = 0; axis < 3; ++axis)
                member_bp[axis] = power_detail::wrap_add(
                    s.at("power").at("bp").at(axis).get<int64_t>(),
                    -s.at("components").at("support").at("bp").at(axis).get<int64_t>());
            int member = int(power_detail::Power(member_bp).total());
            if (!unit)
                continue;
            auto ub = floor_score(f32(f32(f32(unit) / f32(power)) * f32(base)));
            out.params[2 * i] += floor_score(f32(f32(f32(member) / f32(unit)) * f32(ub)));
            out.params[2 * i + 1] += floor_score(f32(f32(f32(snap) / f32(unit)) * f32(ub)));
        }
        std::map<int, int64_t> independent;
        int64_t total = 0;
        for (auto &[owner, o] : owners) {
            int64_t amount = 0;
            for (auto pair :
                 std::array<std::pair<float, float>, 3>{{{o.combo, 0}, {0, o.note}, {0, o.judge}}})
                if (pair.first || pair.second) {
                    auto c = f32(g * f32(f32(1.f + mb) + pair.first));
                    amount += core(p, power, input.life, n.operate_type, type, c, f32(1.f + pair.second), 100,
                                   assist) -
                              base;
                }
            if (amount) {
                independent[owner] = amount;
                total += amount;
            }
        }
        if (total > 0) {
            int64_t delta = score - base, remainder = delta - total;
            for (auto &[owner, amount] : independent) {
                float ratio = f32(f32(amount) / f32(total));
                int64_t assigned = remainder >= 0 ? amount + int64_t(std::floor(f32(ratio * f32(remainder))))
                                                  : int64_t(std::floor(f32(ratio * f32(delta))));
                int id = mapped(owner, r.order);
                if (id >= 0 && id / 100 < 5 && (id % 100 == 1 || id % 100 == 2))
                    out.skills[2 * (id / 100) + id % 100 - 1] += assigned;
            }
        }
    }
    return out;
}
} // namespace
std::vector<ScoredPath> score_path(const Prepared &p, const Json &deck, const Json &request,
                                   const RuntimeResult &r) {
    auto mode = request.value("mode", "free");
    bool aux = mode == "battle" || mode == "arena";
    bool contribute = request.value("contributions", false), details = request.value("internal_notes", false);
    bool gek = request.value("gekisou_enabled", aux || mode == "mission");
    if (mode == "challenge_skip") {
        ScoredPath result;
        int power = int(integer(deck.at("total_power"), "total_power", 1));
        for (auto &n : p.notes)
            if (n.operate_type != 120 && p.note_percent.contains(n.operate_type))
                result.total =
                    checked(result.total + core(p, power, 100, n.operate_type, 3,
                                                f32(1.f + std::min(p.combo_factor(0), 1.f)), 1, 100, false));
        result.detail = Json{{"calculator_path", "challenge_skip"}, {"reference_score", result.total}};
        if (contribute) {
            std::array<int64_t, 10> params{};
            int64_t sum = 0;
            for (auto &n : p.notes)
                if (n.operate_type != 120 && p.note_percent.contains(n.operate_type)) {
                    auto base = core(p, power, 100, n.operate_type, 3,
                                     f32(1.f + std::min(p.combo_factor(0), 1.f)), 1, 100, false);
                    for (size_t i = 0; i < 5; ++i) {
                        const auto &s = deck.at("slots").at(i);
                        int unit = s.at("power").at("total").get<int>();
                        int snap = s.at("components").at("support").at("total").get<int>();
                        std::array<int64_t, 3> member_bp{};
                        for (size_t axis = 0; axis < 3; ++axis)
                            member_bp[axis] = power_detail::wrap_add(
                                s.at("power").at("bp").at(axis).get<int64_t>(),
                                -s.at("components").at("support").at("bp").at(axis).get<int64_t>());
                        const auto member = power_detail::Power(member_bp).total();
                        if (!unit)
                            continue;
                        auto ub = floor_score(f32(f32(f32(unit) / f32(power)) * f32(base)));
                        params[2 * i] += floor_score(f32(f32(f32(member) / f32(unit)) * f32(ub)));
                        params[2 * i + 1] += floor_score(f32(f32(f32(snap) / f32(unit)) * f32(ub)));
                    }
                }
            for (auto v : params) {
                result.contributions.push_back(v);
                sum += v;
            }
            result.contributions.push_back(0);
            result.contributions.push_back(result.total - sum);
        }
        return {result};
    }
    auto main = track(p, deck, r, false, false, contribute, details);
    auto solo = aux ? track(p, deck, r, true, false, false, false) : Track{};
    std::vector<std::array<int, 3>> ranks;
    if (gek && !request.value("internal_no_rank", false)) {
        if (request.contains("ranks")) {
            auto &list = request.at("ranks");
            if (!list.is_array() || list.size() != 3)
                throw Error("ranks requires three entries");
            std::array<int, 3> v{};
            for (size_t i = 0; i < 3; ++i)
                v[i] = int(integer(list.at(i), "rank", 1, 5));
            ranks.push_back(v);
        } else
            for (int rank = 1; rank <= 5; ++rank)
                ranks.push_back({rank, rank, rank});
    } else
        ranks.push_back({0, 0, 0});
    std::set<int> missions(p.missions.begin(), p.missions.end());
    int pattern = missions.size() == 1 ? 1 : missions.size() == 2 ? 3 : 2;
    std::array<std::array<int, 5>, 3> percents{};
    std::set<std::pair<int, int>> seen;
    if (gek && !request.value("internal_no_rank", false)) {
        for (auto &row : p.master.rows("MasterLiveGekisouRankingScoreBonus"))
            if (row.at("_missionPattern").get<int>() == pattern) {
                int section = int(integer(row.at("_count"), "rank section", 1, 3)) - 1;
                int rank = int(integer(row.at("_rank"), "rank", 1, 5)) - 1;
                if (!seen.insert({section, rank}).second)
                    throw Error("duplicate rank bonus");
                percents[section][rank] = int(integer(row.at("_scoreBonusPercent"), "rank percent"));
            }
        if (seen.size() != 15)
            throw Error("incomplete rank bonus table");
    }
    std::vector<ScoredPath> results;
    for (auto ranks_for : ranks) {
        ScoredPath out;
        out.total = main.total;
        out.solo = solo.total;
        out.sections = main.sections;
        int64_t fixed = 0;
        for (size_t i = 0; i < 3; ++i)
            if (ranks_for[i]) {
                auto bonus = checked(main.sections[i] * int64_t(percents[i][ranks_for[i] - 1]) / 100);
                fixed += bonus;
                out.total = checked(out.total + bonus);
            }
        out.detail = Json{{"ranks", Json::array({ranks_for[0], ranks_for[1], ranks_for[2]})},
                          {"note_score", main.total},
                          {"ranking_score", fixed},
                          {"reference_score", aux ? solo.total : out.total}};
        if (details)
            out.detail["notes"] = main.note_detail;
        if (contribute) {
            int64_t allocated = 0;
            Json cards = Json::array();
            for (size_t i = 0; i < 10; ++i) {
                int64_t v = main.params[i] + main.skills[i];
                out.contributions.push_back(v);
                allocated += v;
                cards.push_back(Json{{"slot", int(i / 2)},
                                     {"kind", i % 2 ? "support" : "member"},
                                     {"parameter", main.params[i]},
                                     {"skill", main.skills[i]},
                                     {"total", v}});
            }
            out.contributions.push_back(fixed);
            out.contributions.push_back(main.total - allocated);
            out.detail["cards"] = cards;
            out.detail["rounding_and_unowned"] = main.total - allocated;
        }
        results.push_back(std::move(out));
    }
    return results;
}
} // namespace mns
