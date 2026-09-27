#include "runtime_detail.hpp"

namespace mns {
using namespace runtime_detail;
std::shared_ptr<const runtime_detail::RuntimeData>
runtime_detail::compile_runtime_data(const Prepared &p, const Json &deck, const Json &request) {
    RuntimeResult out;
    Tables tables{p.master};
    const auto &slots = deck.at("slots");
    if (slots.size() != 5 || p.skills.size() != 5)
        throw Error("runtime needs five occupied slots and five skill events");
    std::string mode = request.value("mode", "free");
    bool gekisou =
        request.value<bool>("gekisou_enabled", mode == "battle" || mode == "arena" || mode == "mission");
    if (gekisou && p.fevers.size() != 3)
        throw Error("Gekisou requires three ranges");
    for (size_t i = 0; i < p.skills.size(); ++i)
        if (p.skills[i].time_ms < 0 || (i && p.skills[i].time_ms <= p.skills[i - 1].time_ms))
            throw Error("ordinary skill events must be strictly increasing");
    if (gekisou)
        for (int mission : p.missions)
            if (mission < 1 || mission > 3)
                throw Error("invalid Gekisou mission");
    BranchRandom static_conditions(std::span<const uint32_t>{});
    std::vector<Rule> ordinary, members, supports;
    for (int native = 0; native < 5; ++native) {
        const auto &slot = slots.at(out.order[native]);
        int id = iv(slot, "member_id");
        if (id <= 0)
            throw Error("empty runtime member");
        const auto &card = p.master.by_id("MasterMemberCard", id);
        int band = iv(p.master.by_id("MasterCharacter", iv(card, "_characterID")), "_bandID");
        auto append = [&](std::vector<Rule> &target, std::string_view table, std::string_view key, int skill,
                          int level, bool is_support, bool is_gekisou, int index = 0) {
            if (!skill)
                return;
            size_t before = target.size();
            for (const auto &row : p.master.rows(table))
                if (iv(row, key) == skill && iv(row, "_level") == level) {
                    auto r = tables.rule(row, native * 100 + (is_support ? 2 : 1), native, band, skill,
                                         is_support, is_gekisou, index);
                    validate(r, p.master);
                    target.push_back(std::move(r));
                }
            if (target.size() == before)
                throw Error("missing selected skill level " + std::to_string(skill));
        };
        append(ordinary, "MasterLiveSkillEffect", "_liveSkillID", iv(card, "_liveSkillID"),
               iv(slot, "live_skill_level", 1), false, false);
        if (gekisou)
            append(members, "MasterGekisouSkillEffect", "_gekisouSkillID", iv(card, "_gekisouSkillID"),
                   iv(slot, "gekisou_skill_level", 1), false, true);
        if (slot.contains("support") && !slot.at("support").is_null()) {
            auto &support = slot.at("support");
            auto &sc = p.master.by_id("MasterSupportCard", iv(support, "master_id"));
            const Json *rank = nullptr;
            for (const auto &r : p.master.rows("MasterSupportCardRank"))
                if (iv(r, "_group") == iv(sc, "_supportCardRankGroup") &&
                    iv(r, "_rank") == iv(support, "rank", 1)) {
                    if (rank)
                        throw Error("ambiguous support rank");
                    rank = &r;
                }
            if (!rank)
                throw Error("missing support rank");
            for (int i = 0; i < 2; ++i) {
                std::string suffix = i ? "02" : "01";
                append(ordinary, "MasterSupportSkillEffect", "_supportSkillID",
                       iv(sc, "_supportSkillId" + suffix), iv(*rank, "_supportSkill" + suffix + "Level"),
                       true, false, i);
                if (gekisou)
                    append(supports, "MasterGekisouSupportSkillEffect", "_gekisouSupportSkillID",
                           iv(sc, "_gekisouSupportSkillId" + suffix),
                           iv(*rank, "_gekisouSupportSkill" + suffix + "Level"), true, true, i);
            }
        }
    }
    std::stable_sort(members.begin(), members.end(), [](const Rule &a, const Rule &b) {
        return std::tuple(a.skill, a.slot, a.id()) < std::tuple(b.skill, b.slot, b.id());
    });
    int life = setting(p.master, "life_base"), max_life = life;
    if (life <= 0 || life > INT32_MAX / 2)
        throw Error("invalid master initial life");
    std::vector<Rule> accepted_ordinary;
    for (const auto &r : ordinary)
        if (!r.support || evaluate(r, p.master, static_conditions, life))
            accepted_ordinary.push_back(r);
    ordinary = std::move(accepted_ordinary);
    bool needs_rank =
        std::any_of(ordinary.begin(), ordinary.end(), [](const Rule &r) { return r.trigger_kind() == 8000; });
    std::map<int, int> rank_thresholds;
    if (needs_rank) {
        int group = iv(p.master.by_id("MasterLiveMusic", p.music_id), "_liveScoreRankGroup");
        for (const auto &r : p.master.rows("MasterLiveScoreRank"))
            if (iv(r, "_group") == group)
                rank_thresholds[iv(r, "_liveScoreRank")] = iv(r, "_requiredScore");
        if (rank_thresholds.empty())
            throw Error("missing score rank thresholds");
    }
    std::set<int> just_types;
    int slow_after = 0;
    for (const auto &r : p.master.rows("MasterLiveJudgementTiming")) {
        slow_after = std::max(slow_after, iv(r, "_afterMs"));
        if (iv(r, "_assistLevel") == 0 && iv(r, "_noteSimulateJudgement") == 6 && iv(r, "_beforeMs") >= 0 &&
            iv(r, "_afterMs") >= 0)
            just_types.insert(iv(r, "_noteJudgementType"));
    }
    std::vector<const Note *> notes;
    for (const auto &n : p.notes)
        if (n.judgement) {
            judgement_type(n);
            notes.push_back(&n);
        }
    std::stable_sort(notes.begin(), notes.end(), [](auto a, auto b) {
        return std::tie(a->time_ms, a->id) < std::tie(b->time_ms, b->id);
    });
    if (notes.empty())
        throw Error("runtime has no judgement notes");
    std::array<int, 3> completes{}, finishes{};
    std::set<int> frames{0};
    for (auto n : notes)
        frames.insert(n->time_ms);
    for (const auto &e : p.skills)
        frames.insert(e.time_ms);
    if (gekisou)
        for (size_t i = 0; i < p.fevers.size(); ++i) {
            auto &e = p.fevers[i];
            if (e.time_ms < 0 || e.end_ms < e.time_ms || int64_t(e.end_ms) + slow_after + 501 > INT32_MAX)
                throw Error("invalid range time");
            completes[i] = e.end_ms + slow_after + 500;
            finishes[i] = completes[i] + 1;
            if (i + 1 < p.fevers.size() && finishes[i] >= p.fevers[i + 1].time_ms)
                throw Error("overlapping Gekisou controller ranges");
            out.sections.push_back(Section{e.time_ms, e.end_ms, p.missions[i], 0, {}});
            frames.insert(e.time_ms);
            frames.insert(e.end_ms + 1);
            frames.insert(completes[i]);
            frames.insert(finishes[i]);
        }
    std::map<std::pair<int, int>, Lottery> base;
    std::map<int, Lottery> bonus;
    int rush_bonus = 0;
    Luck luck;
    if (gekisou) {
        luck.normal = setting(p.master, "gekisou_luck_gauge_max");
        luck.rush = setting(p.master, "gekisou_luck_gauge_max_rush");
        rush_bonus = setting(p.master, "gekisou_luck_rush_score_bonus_percent");
        if (luck.normal <= 0 || luck.rush <= 0)
            throw Error("invalid Luck gauge maximum");
        for (const auto &r : p.master.rows("MasterLiveGekisouLuckBasePoint")) {
            if (iv(r, "_weight") < 0 || iv(r, "_basePoint") < 0)
                throw Error("negative Luck base distribution");
        }
        for (const auto &r : p.master.rows("MasterLiveGekisouLuckBonusLot")) {
            if (iv(r, "_weight") < 0 || iv(r, "_lotResult") < 0 || iv(r, "_lotResult") > 3)
                throw Error("invalid Luck bonus distribution");
        }
        for (const auto &r : p.master.rows("MasterLiveGekisouLuckBasePoint"))
            if (iv(r, "_weight") > 0 && iv(r, "_basePoint") >= 0)
                base[{iv(r, "_noteCategory"), iv(r, "_noteSimulateJudgement")}].push_back(
                    {uint64_t(iv(r, "_weight")), iv(r, "_basePoint")});
        for (const auto &r : p.master.rows("MasterLiveGekisouLuckBonusLot"))
            if (iv(r, "_weight") > 0)
                bonus[iv(r, "_chanceLotType")].push_back({uint64_t(iv(r, "_weight")), iv(r, "_lotResult")});
    }
    auto data = std::make_shared<RuntimeData>();
    data->prepared = &p;
    data->deck = deck;
    data->request = request;
    data->initial = std::move(out);
    data->ordinary = std::move(ordinary);
    data->members = std::move(members);
    data->supports = std::move(supports);
    data->life = life;
    data->max_life = max_life;
    data->frames = std::move(frames);
    data->needs_rank = needs_rank;
    data->gekisou = gekisou;
    data->rank_thresholds = std::move(rank_thresholds);
    data->notes = std::move(notes);
    data->just_types = std::move(just_types);
    data->completes = completes;
    data->finishes = finishes;
    data->base = std::move(base);
    data->bonus = std::move(bonus);
    data->rush_bonus = rush_bonus;
    data->luck = luck;
    return data;
}
RuntimeKernel compile_runtime(const Prepared &p, const Json &deck, const Json &request) {
    auto data = runtime_detail::compile_runtime_data(p, deck, request);
    auto out = data->initial;
    auto ordinary = data->ordinary, members = data->members, supports = data->supports;
    auto life = data->life, max_life = data->max_life;
    auto frames = data->frames;
    auto needs_rank = data->needs_rank;
    auto gekisou = data->gekisou;
    auto rank_thresholds = data->rank_thresholds;
    auto notes = data->notes;
    auto just_types = data->just_types;
    auto completes = data->completes, finishes = data->finishes;
    auto base = data->base;
    auto bonus = data->bonus;
    auto rush_bonus = data->rush_bonus;
    auto luck = data->luck;
    // The prepared object must outlive this immutable compiled kernel. Each
    // invocation owns its execution state; callers may run kernels concurrently.
    return [&p, deck, request, initial = std::move(out), ordinary_rules = std::move(ordinary),
            member_rules = std::move(members), support_rules = std::move(supports), initial_life = life,
            max_life, initial_frames = std::move(frames), needs_rank,
            rank_thresholds = std::move(rank_thresholds), notes = std::move(notes),
            just_types = std::move(just_types), completes, finishes, base = std::move(base),
            bonus = std::move(bonus), rush_bonus, initial_luck = luck, gekisou](BranchRandom &random) {
        RuntimeResult out = initial;
        std::array<uint64_t, 120> order_weights;
        order_weights.fill(1);
        size_t permutation = random.choose(order_weights);
        for (size_t i = 0; i < permutation; ++i)
            std::next_permutation(out.order.begin(), out.order.end());
        auto reordered = [&](const std::vector<Rule> &source) {
            std::vector<Rule> result;
            result.reserve(source.size());
            for (int native = 0; native < 5; ++native)
                for (const auto &rule : source)
                    if (rule.slot == out.order[native]) {
                        result.push_back(rule);
                        result.back().slot = native;
                        result.back().owner = native * 100 + (rule.support ? 2 : 1);
                    }
            return result;
        };
        auto ordinary = reordered(ordinary_rules);
        auto members = reordered(member_rules);
        auto supports = reordered(support_rules);
        std::stable_sort(members.begin(), members.end(), [](const Rule &a, const Rule &b) {
            return std::tuple(a.skill, a.slot, a.id()) < std::tuple(b.skill, b.slot, b.id());
        });
        int life = initial_life;
        auto frames = initial_frames;
        Luck luck = initial_luck;
        out.notes.reserve(notes.size());
        out.shared.reserve(ordinary.size() * 2);
        std::vector<CountActivation> count_effects;
        std::vector<Guarantee> guarantees;
        std::vector<Converter> converters;
        std::vector<SupportState> support_state;
        for (const auto &r : supports)
            support_state.push_back({&r});
        std::vector<std::pair<const Note *, int>> history;
        std::vector<std::pair<int, int>> pending_results;
        int section = -1, combo = 0, maximum = 0, just = 0, raw_just = 0, inflated = 0, raw_checker = 0,
            previous_rank = 2;
        size_t position = 0;
        bool dirty = false;
        auto emit = [&](int time, int owner, int raw) {
            Factor f;
            f.time_ms = time;
            f.owner = owner;
            f.note_raw = raw;
            out.primary.push_back(f);
        };
        auto applicable = [](const CountActivation &a, int time, int sequence) {
            return (time > a.start || (time == a.start && sequence >= a.begin_sequence)) &&
                   (!a.retired || time < a.end || (time == a.end && sequence < a.end_sequence));
        };
        auto update_counters = [&](size_t first) {
            auto &result = out.sections.at(section);
            if (first == 0) {
                combo = maximum = just = raw_just = inflated = 0;
                result.combo.clear();
            }
            for (size_t seq = first; seq < history.size(); ++seq) {
                auto [n, judgement] = history[seq];
                float cf = 1, jf = 1;
                for (auto &a : count_effects)
                    if (applicable(a, n->time_ms, int(seq))) {
                        if (a.rule->kind() == 12000)
                            cf = f32(cf + a.rule->value());
                        if (a.rule->kind() == 13000)
                            jf = f32(jf + a.rule->value());
                    }
                combo = metric(int64_t(combo) + floor_metric(cf));
                maximum = std::max(maximum, combo);
                if (judgement == 6) {
                    ++raw_just;
                    int add = floor_metric(jf);
                    inflated = metric(int64_t(inflated) + add);
                    just = metric(int64_t(just) + add);
                    for (auto &a : count_effects)
                        if (a.rule->kind() == 13002 && applicable(a, n->time_ms, int(seq))) {
                            auto &r = *a.rule;
                            int count = inflated / r.cumulative->at("_conditionValues").at(0).get<int>();
                            if (iv(*r.cumulative, "_maxCumulativeCount") > 0)
                                count = std::min(count, iv(*r.cumulative, "_maxCumulativeCount"));
                            int extra = metric(int64_t(count) * r.value());
                            if (iv(*r.row, "_maxEffectValue") > 0)
                                extra = std::min(extra, iv(*r.row, "_maxEffectValue"));
                            just = metric(int64_t(just) + extra);
                        }
                }
                if (result.mission == 1) {
                    if (!result.combo.empty() && result.combo.back().first == n->time_ms)
                        result.combo.back().second = combo;
                    else
                        result.combo.push_back({n->time_ms, combo});
                }
            }
            result.mission_value = result.mission == 1 ? maximum : result.mission == 3 ? just : luck.point;
            dirty = false;
        };
        auto add_gauge = [&](const Rule &r) {
            int32_t product = wrap32(int64_t(luck.maximum()) * r.value());
            luck.add(int(std::floor(f32(f32(product) / 10000.f))));
        };
        auto roll = [&](int time, int type) {
            int minimum = 0;
            for (auto &g : guarantees)
                if (time < g.end && g.remaining != 0)
                    minimum = std::max(minimum, g.rule->value() - 1);
            auto it = bonus.find(type);
            if (it == bonus.end())
                throw Error("missing Luck bonus distribution");
            int result = choose_lottery(it->second, random, minimum);
            if (minimum > 0)
                for (auto &g : guarantees)
                    if (time < g.end && g.remaining > 0 && g.rule->value() - 1 <= minimum)
                        --g.remaining;
            return result;
        };
        auto consume = [&](int time) {
            if (luck.lots <= 0)
                return;
            --luck.lots;
            if (luck.next < 0)
                luck.next = roll(time, 0);
            int result = luck.next;
            bool was = luck.rush_combo > 0;
            if (was != (result == 3)) {
                Factor f;
                f.time_ms = time;
                f.luck = rush_bonus * (result == 3 ? 1 : -1);
                out.primary.push_back(f);
            }
            luck.apply(result);
            luck.next = roll(time, luck.type());
            pending_results.push_back({time + 1, result});
            frames.insert(time + 1);
        };
        while (!frames.empty()) {
            int time = *frames.begin();
            frames.erase(frames.begin());
            bool start = false, complete = false;
            size_t shared_before = out.shared.size(), primary_before = out.primary.size();
            if (gekisou)
                for (size_t i = 0; i < p.fevers.size(); ++i)
                    if (p.fevers[i].time_ms == time) {
                        section = int(i);
                        start = true;
                        history.clear();
                        count_effects.clear();
                        guarantees.clear();
                        converters.clear();
                        combo = maximum = just = raw_just = inflated = raw_checker = 0;
                        luck = Luck{luck.normal, luck.rush};
                        pending_results.clear();
                        for (auto &s : support_state)
                            s = SupportState{s.rule};
                    }
            if (section >= 0) {
                complete = time == completes[section];
                if (time == finishes[section]) {
                    if (luck.rush_combo) {
                        Factor f;
                        f.time_ms = time;
                        f.luck = -rush_bonus;
                        out.primary.push_back(f);
                        luck.rush_combo = 0;
                    }
                    section = -1;
                }
            }
            std::vector<std::pair<const Note *, int>> batch;
            while (position < notes.size() && notes[position]->time_ms == time) {
                auto n = notes[position++];
                int judgement = 5;
                if (section >= 0 && out.sections[section].mission == 3 &&
                    time < out.sections[section].end_ms && just_types.contains(judgement_type(*n)))
                    judgement = 6;
                for (auto &c : converters)
                    if (c.remaining && c.targets.contains(judgement) && judgement != 6 &&
                        just_types.contains(judgement_type(*n))) {
                        judgement = 6;
                        --c.remaining;
                        break;
                    }
                out.notes.push_back({n->id, time, judgement, 5, life});
                if (section >= 0 && judgement == 6)
                    ++raw_checker;
                batch.push_back({n, judgement});
            }
            int rank_hits = 0;
            if (needs_rank) {
                Json q = request;
                q["internal_no_rank"] = true;
                auto scores = score_path(p, deck, q, out);
                int64_t score = scores.at(0).total;
                int rank = 2;
                for (auto [r, threshold] : rank_thresholds)
                    if (score >= threshold)
                        rank = std::max(rank, r);
                if (time != 0)
                    rank_hits = std::max(0, rank - previous_rank);
                previous_rank = std::max(previous_rank, rank);
            }
            std::vector<int> activated;
            for (size_t i = 0; i < p.skills.size(); ++i)
                if (p.skills[i].time_ms == time)
                    activated.push_back(int(i));
            for (const auto &r : ordinary)
                if (r.support && r.kind() == 3001 &&
                    std::find(activated.begin(), activated.end(), r.slot) != activated.end() && life > 0)
                    life = int(std::min<int64_t>(2 * max_life, int64_t(life) + r.value()));
            for (int index : activated) {
                int extension = 0;
                for (const auto &r : ordinary)
                    if (r.support && r.kind() == 15000 && r.slot == index)
                        extension = metric(int64_t(extension) + r.value());
                for (const auto &r : ordinary)
                    if (!r.support && r.slot == index && evaluate(r, p.master, random, life)) {
                        int64_t end = int64_t(time) +
                                      floor_metric(std::ceil(f32(float(duration(r)) + float(extension))));
                        if (end > INT32_MAX)
                            throw Error("member skill end overflow");
                        factor_pair(out.shared, r, time, int(end));
                    }
            }
            for (const auto &r : ordinary)
                if (r.support && r.kind() == 2000) {
                    int hits = r.trigger_kind() == 8000
                                   ? rank_hits
                                   : int(std::count(activated.begin(), activated.end(), r.slot));
                    for (int i = 0; i < hits; ++i) {
                        int64_t end = int64_t(time) + duration(r);
                        if (end > INT32_MAX)
                            throw Error("support end overflow");
                        factor_pair(out.shared, r, time, int(end));
                    }
                }
            if (needs_rank && out.shared.size() != shared_before && time < INT32_MAX)
                frames.insert(time + 1);
            if (section < 0)
                continue;
            auto &sec = out.sections[section];
            if (start) {
                std::vector<const Rule *> starts;
                for (const auto &r : members)
                    if (mission_for(r, p.master) == sec.mission)
                        starts.push_back(&r);
                for (const auto &r : supports)
                    if (mission_for(r, p.master) == sec.mission && (r.kind() == 11005 || r.kind() == 12004))
                        starts.push_back(&r);
                std::stable_sort(starts.begin(), starts.end(), [](auto a, auto b) {
                    auto key = [](auto r) {
                        return std::pair(int64_t(r->skill) * 1000 +
                                             (r->support ? 500 + r->skill_index * 10 : 400) + r->slot,
                                         r->id());
                    };
                    return key(a) < key(b);
                });
                for (auto r : starts) {
                    if (!evaluate(*r, p.master, random, life))
                        continue;
                    int64_t end64 = int64_t(time) + duration(*r);
                    if (end64 > INT32_MAX)
                        throw Error("Gekisou duration end overflow");
                    int end = int(end64);
                    if (r->trigger_kind() == 7020 || (r->support && duration(*r) == 0))
                        end = completes[section];
                    else if (!r->release.empty() && r->kind() != 11003)
                        end = std::min(end, completes[section]);
                    if (r->kind() == 11003)
                        add_gauge(*r);
                    else if (r->kind() == 11005)
                        guarantees.push_back(
                            {r, end,
                             iv(*r->row, "_effectLimitCount") ? iv(*r->row, "_effectLimitCount") : -1});
                    else if (r->kind() != 12004) {
                        count_effects.push_back({r, time, end, -1, -1,
                                                 metric(int64_t(end) + (end == completes[section] ? 0 : 1)),
                                                 false});
                        frames.insert(count_effects.back().observed_end);
                        dirty = true;
                    }
                }
                for (auto &s : support_state)
                    if (s.rule->kind() != 11005 && s.rule->kind() != 12004)
                        s.accepted = evaluate(*s.rule, p.master, random, life);
            }
            converters.erase(
                std::remove_if(converters.begin(), converters.end(),
                               [&](auto &c) { return !c.remaining || time > c.end || complete; }),
                converters.end());
            for (auto &a : count_effects)
                if (!a.retired && time >= a.observed_end) {
                    a.retired = true;
                    a.end_sequence = int(history.size()) - 1;
                    dirty = true;
                }
            // Dynamic Combo sees the preceding Gekisou update. OverrideTime points to
            // the final member of the preceding chord, never to all same-time notes.
            if (time <= sec.end_ms + 1)
                for (const auto &r : members)
                    if (r.trigger_kind() == 7005 && sec.mission == 1) {
                        bool enabled = evaluate(r, p.master, random, life, combo, true) && !complete;
                        auto it = std::find_if(count_effects.begin(), count_effects.end(),
                                               [&](auto &a) { return a.rule == &r && !a.retired; });
                        if (enabled && it == count_effects.end() && !history.empty()) {
                            int stamp = history.back().first->time_ms;
                            count_effects.push_back({&r, stamp, sec.end_ms + 2, int(history.size()) - 1, -1,
                                                     sec.end_ms + 2, false});
                            dirty = true;
                        } else if (!enabled && it != count_effects.end()) {
                            it->end = time;
                            it->end_sequence = int(history.size()) - 1;
                            it->retired = true;
                            dirty = true;
                        }
                    }
            for (auto [at, result] : pending_results)
                if (at == time && !complete) {
                    for (const auto &r : members)
                        if (r.kind() == 11002 &&
                            r.trigger[0]->at("_conditionValues").at(0).get<int>() == result)
                            luck.point = metric(int64_t(luck.point) + r.value());
                    for (auto &s : support_state)
                        if (s.accepted && s.rule->kind() == 11003 && !s.executed &&
                            s.rule->trigger[0]->at("_conditionValues").at(0).get<int>() == result) {
                            add_gauge(*s.rule);
                            ++s.executed;
                        }
                }
            pending_results.erase(std::remove_if(pending_results.begin(), pending_results.end(),
                                                 [&](auto x) { return x.first <= time; }),
                                  pending_results.end());
            for (auto &s : support_state) {
                auto &r = *s.rule;
                if (!s.accepted)
                    continue;
                if (r.kind() == 13005) {
                    if (!complete && sec.mission == 3) {
                        std::set<int> counted;
                        for (auto &tid : r.trigger[1]->at("_conditionTargetIDs"))
                            counted.insert(
                                iv(p.master.by_id("MasterSkillTarget", tid.get<int>()), "_judgement"));
                        for (auto [n, j] : batch)
                            if (counted.contains(j)) {
                                ++s.count;
                                if (s.count >= r.trigger[1]->at("_conditionValues").at(0).get<int>()) {
                                    s.count = 0;
                                    if (s.executed < iv(*r.row, "_effectExecuteLimitCount")) {
                                        ++s.executed;
                                        converters.push_back(
                                            {&r,
                                             std::min(completes[section],
                                                      int(std::min<int64_t>(INT32_MAX,
                                                                            int64_t(time) + duration(r)))),
                                             iv(*r.row, "_effectLimitCount"), judgement_targets(r)});
                                    }
                                }
                            }
                    }
                    continue;
                }
                if (r.kind() != 2000 && r.kind() != 2001)
                    continue;
                bool active =
                    r.kind() == 2000 ? (sec.mission == 2 && luck.rush_combo > 0 && !complete) : s.active;
                if (r.kind() == 2001) {
                    if (start && mission_for(r, p.master) == sec.mission)
                        active = true;
                    if (complete)
                        active = false;
                }
                int bp = r.value();
                if (r.kind() == 2001 && active) {
                    int metric = sec.mission == 3 ? raw_checker : combo;
                    int count = std::min(
                        int(std::floor(
                            f32(f32(metric) / f32(r.cumulative->at("_conditionValues").at(0).get<int>())))),
                        iv(*r.cumulative, "_maxCumulativeCount"));
                    bp = wrap32(int64_t(bp) * count);
                    if (iv(*r.row, "_maxEffectValue") > 0)
                        bp = std::min(bp, iv(*r.row, "_maxEffectValue"));
                }
                int raw = active ? quantize(bp) : 0;
                bool changed = active != s.active ||
                               (r.kind() == 2001 && active &&
                                !approximately(f32(f32(bp) / 10000.f), f32(f32(s.raw) / 100000.f)));
                if (changed) {
                    if (s.active)
                        emit(time, r.owner, -s.raw);
                    if (active)
                        emit(time, r.owner, raw);
                    s.active = active;
                    s.raw = raw;
                    // Some future basis-point values quantize to a different
                    // float than the applier's desired factor. Native Stay
                    // then replaces the handle on every ideal millisecond.
                    if (active && r.kind() == 2001 && time < completes[section] &&
                        !approximately(f32(f32(bp) / 10000.f), f32(f32(raw) / 100000.f)))
                        frames.insert(time + 1);
                }
            }
            bool changed_counts = dirty;
            if (!complete && time <= sec.end_ms) {
                size_t first_new = history.size();
                for (auto [n, j] : batch)
                    history.push_back({n, j});
                // A changed backdated count command requires replay. Ordinary note
                // arrivals extend the existing exact prefix in linear time.
                if (dirty || first_new != history.size())
                    update_counters(dirty ? 0 : first_new);
                if (sec.mission == 2) {
                    for (auto [n, j] : batch) {
                        bool ignored = n->operate_type == 0 || n->operate_type == 80 ||
                                       n->operate_type == 82 ||
                                       (n->operate_type >= 100 && n->operate_type <= 105) ||
                                       n->operate_type == 121 || n->operate_type == 122;
                        int point = 0;
                        if (!ignored) {
                            bool sub = n->operate_type == 21 || n->operate_type == 60 ||
                                       n->operate_type == 61 || n->operate_type == 62 ||
                                       n->operate_type == 63 || n->operate_type == 120;
                            auto it = base.find({sub ? 1 : 0, 5});
                            if (it == base.end())
                                throw Error("missing Perfect Luck base distribution");
                            point = choose_lottery(it->second, random);
                        }
                        float factor = 1;
                        for (auto &a : count_effects)
                            if (a.rule->kind() == 11001 && time >= a.start && time < a.end)
                                factor = f32(factor + f32(f32(a.rule->value()) / 10000.f));
                        luck.add(floor_metric(f32(f32(point) * factor)));
                        consume(time);
                    }
                    if (batch.empty() && time < sec.end_ms)
                        consume(time);
                }
            } else if (dirty && time < completes[section])
                update_counters(0);
            sec.mission_value = sec.mission == 1 ? maximum : sec.mission == 3 ? just : luck.point;
            if (!batch.empty() || changed_counts)
                if (time < sec.end_ms)
                    frames.insert(time + 1);
            if (luck.lots > 0 && time + 1 < sec.end_ms)
                frames.insert(time + 1);
            if (needs_rank && out.primary.size() != primary_before && time < finishes[section])
                frames.insert(time + 1);
        }
        std::stable_sort(out.shared.begin(), out.shared.end(), [](const Factor &a, const Factor &b) {
            return std::tie(a.time_ms, a.owner) < std::tie(b.time_ms, b.owner);
        });
        std::stable_sort(out.primary.begin(), out.primary.end(),
                         [](const Factor &a, const Factor &b) { return a.time_ms < b.time_ms; });
        out.diagnostics = Json{{"clock", "ideal_1ms"},
                               {"final_life", life},
                               {"random_decisions", random.consumed()},
                               {"joint_native_validation", "pending"}};
        return out;
    };
}
RuntimeResult run_runtime(const Prepared &p, const Json &deck, const Json &request, BranchRandom &random) {
    return compile_runtime(p, deck, request)(random);
}
} // namespace mns
