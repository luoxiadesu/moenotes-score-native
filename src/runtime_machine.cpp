#include "runtime_machine.hpp"
#include "runtime_detail.hpp"
#include <cstring>
namespace mns {
using namespace runtime_detail;
namespace {
struct Rules {
    std::vector<Rule> ordinary, members, supports;
};
struct MachineState {
    RuntimeResult out;
    std::shared_ptr<const Rules> rules;
    std::set<int> frames;
    Luck luck;
    int life = 0;
    std::vector<CountActivation> count_effects;
    std::vector<Guarantee> guarantees;
    std::vector<Converter> converters;
    std::vector<SupportState> support_state;
    std::vector<std::pair<const Note *, int>> history;
    std::vector<std::pair<int, int>> pending_results;
    int section = -1, combo = 0, maximum = 0, just = 0, raw_just = 0, inflated = 0, raw_checker = 0,
        previous_rank = 2;
    size_t position = 0;
    bool dirty = false;
};
void select_order(MachineState &state, const RuntimeData &data, uint32_t permutation) {
    for (uint32_t i = 0; i < permutation; ++i)
        std::next_permutation(state.out.order.begin(), state.out.order.end());
    auto rules = std::make_shared<Rules>();
    auto reorder = [&](const std::vector<Rule> &source, std::vector<Rule> &target) {
        target.reserve(source.size());
        for (int native = 0; native < 5; ++native)
            for (const auto &rule : source)
                if (rule.slot == state.out.order[native]) {
                    target.push_back(rule);
                    target.back().slot = native;
                    target.back().owner = native * 100 + (rule.support ? 2 : 1);
                }
    };
    reorder(data.ordinary, rules->ordinary);
    reorder(data.members, rules->members);
    reorder(data.supports, rules->supports);
    std::stable_sort(rules->members.begin(), rules->members.end(), [](const Rule &a, const Rule &b) {
        return std::tuple(a.skill, a.slot, a.id()) < std::tuple(b.skill, b.slot, b.id());
    });
    state.rules = rules;
    for (const auto &rule : rules->supports)
        state.support_state.push_back({&rule});
}
void execute_event(MachineState &state, const RuntimeData &data, BranchRandom &random) {
    const auto &p = *data.prepared;
    const auto &deck = data.deck;
    const auto &request = data.request;
    const auto &ordinary = state.rules->ordinary;
    const auto &members = state.rules->members;
    const auto &supports = state.rules->supports;
    const auto &notes = data.notes;
    const auto &base = data.base;
    const auto &bonus = data.bonus;
    const auto &just_types = data.just_types;
    const auto &completes = data.completes;
    const auto &finishes = data.finishes;
    const auto &rank_thresholds = data.rank_thresholds;
    const auto max_life = data.max_life;
    const auto rush_bonus = data.rush_bonus;
    const auto needs_rank = data.needs_rank;
    const auto gekisou = data.gekisou;
    auto &out = state.out;
    auto &frames = state.frames;
    auto &luck = state.luck;
    auto &life = state.life;
    auto &count_effects = state.count_effects;
    auto &guarantees = state.guarantees;
    auto &converters = state.converters;
    auto &support_state = state.support_state;
    auto &history = state.history;
    auto &pending_results = state.pending_results;
    auto &section = state.section;
    auto &combo = state.combo;
    auto &maximum = state.maximum;
    auto &just = state.just;
    auto &raw_just = state.raw_just;
    auto &inflated = state.inflated;
    auto &raw_checker = state.raw_checker;
    auto &previous_rank = state.previous_rank;
    auto &position = state.position;
    auto &dirty = state.dirty;
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
        if (section >= 0 && out.sections[section].mission == 3 && time < out.sections[section].end_ms &&
            just_types.contains(judgement_type(*n)))
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
                int64_t end =
                    int64_t(time) + floor_metric(std::ceil(f32(float(duration(r)) + float(extension))));
                if (end > INT32_MAX)
                    throw Error("member skill end overflow");
                factor_pair(out.shared, r, time, int(end));
            }
    }
    for (const auto &r : ordinary)
        if (r.support && r.kind() == 2000) {
            int hits = r.trigger_kind() == 8000 ? rank_hits
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
        return;
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
                return std::pair(int64_t(r->skill) * 1000 + (r->support ? 500 + r->skill_index * 10 : 400) +
                                     r->slot,
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
                    {r, end, iv(*r->row, "_effectLimitCount") ? iv(*r->row, "_effectLimitCount") : -1});
            else if (r->kind() != 12004) {
                count_effects.push_back({r, time, end, -1, -1,
                                         metric(int64_t(end) + (end == completes[section] ? 0 : 1)), false});
                frames.insert(count_effects.back().observed_end);
                dirty = true;
            }
        }
        for (auto &s : support_state)
            if (s.rule->kind() != 11005 && s.rule->kind() != 12004)
                s.accepted = evaluate(*s.rule, p.master, random, life);
    }
    converters.erase(std::remove_if(converters.begin(), converters.end(),
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
                    count_effects.push_back(
                        {&r, stamp, sec.end_ms + 2, int(history.size()) - 1, -1, sec.end_ms + 2, false});
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
                if (r.kind() == 11002 && r.trigger[0]->at("_conditionValues").at(0).get<int>() == result)
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
                    counted.insert(iv(p.master.by_id("MasterSkillTarget", tid.get<int>()), "_judgement"));
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
                                              int(std::min<int64_t>(INT32_MAX, int64_t(time) + duration(r)))),
                                     iv(*r.row, "_effectLimitCount"), judgement_targets(r)});
                            }
                        }
                    }
            }
            continue;
        }
        if (r.kind() != 2000 && r.kind() != 2001)
            continue;
        bool active = r.kind() == 2000 ? (sec.mission == 2 && luck.rush_combo > 0 && !complete) : s.active;
        if (r.kind() == 2001) {
            if (start && mission_for(r, p.master) == sec.mission)
                active = true;
            if (complete)
                active = false;
        }
        int bp = r.value();
        if (r.kind() == 2001 && active) {
            int metric = sec.mission == 3 ? raw_checker : combo;
            int count =
                std::min(int(std::floor(
                             f32(f32(metric) / f32(r.cumulative->at("_conditionValues").at(0).get<int>())))),
                         iv(*r.cumulative, "_maxCumulativeCount"));
            bp = wrap32(int64_t(bp) * count);
            if (iv(*r.row, "_maxEffectValue") > 0)
                bp = std::min(bp, iv(*r.row, "_maxEffectValue"));
        }
        int raw = active ? quantize(bp) : 0;
        bool changed =
            active != s.active || (r.kind() == 2001 && active &&
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
                bool ignored = n->operate_type == 0 || n->operate_type == 80 || n->operate_type == 82 ||
                               (n->operate_type >= 100 && n->operate_type <= 105) || n->operate_type == 121 ||
                               n->operate_type == 122;
                int point = 0;
                if (!ignored) {
                    bool sub = n->operate_type == 21 || n->operate_type == 60 || n->operate_type == 61 ||
                               n->operate_type == 62 || n->operate_type == 63 || n->operate_type == 120;
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
struct Bytes {
    std::vector<uint8_t> values;
    void u64(uint64_t value) {
        for (int i = 0; i < 8; ++i)
            values.push_back(uint8_t(value >> (i * 8)));
    }
    void i32(int value) {
        uint32_t v = std::bit_cast<uint32_t>(int32_t(value));
        for (int i = 0; i < 4; ++i)
            values.push_back(uint8_t(v >> (i * 8)));
    }
    void boolean(bool value) {
        values.push_back(value ? 1 : 0);
    }
    void rule(const Rule *rule) {
        boolean(rule != nullptr);
        if (!rule)
            return;
        i32(rule->owner);
        i32(rule->slot);
        i32(rule->skill);
        i32(rule->skill_index);
        i32(rule->id());
        boolean(rule->support);
        boolean(rule->gekisou);
    }
    void factor(const Factor &f) {
        i32(f.time_ms);
        i32(f.owner);
        i32(f.combo_raw);
        i32(f.note_raw);
        i32(f.judgement);
        i32(f.judgement_raw);
        i32(f.luck);
    }
    void result(const RuntimeResult &result) {
        for (int value : result.order)
            i32(value);
        u64(result.shared.size());
        for (const auto &f : result.shared)
            factor(f);
        u64(result.primary.size());
        for (const auto &f : result.primary)
            factor(f);
        u64(result.notes.size());
        for (const auto &n : result.notes) {
            i32(n.id);
            i32(n.time_ms);
            i32(n.judgement);
            i32(n.solo_judgement);
            i32(n.life);
        }
        u64(result.sections.size());
        for (const auto &s : result.sections) {
            i32(s.start_ms);
            i32(s.end_ms);
            i32(s.mission);
            i32(s.mission_value);
            u64(s.combo.size());
            for (auto [t, v] : s.combo) {
                i32(t);
                i32(v);
            }
        }
    }
};
size_t rule_memory(const Rule &r) {
    return sizeof(Rule) + (r.conditions.capacity() + r.trigger.capacity() + r.release.capacity() +
                           r.reset.capacity() + r.targets.capacity()) *
                              sizeof(const Json *);
}
size_t state_memory(const MachineState &s) {
    size_t size = sizeof(MachineState) + s.frames.size() * 64 + s.out.shared.capacity() * sizeof(Factor) +
                  s.out.primary.capacity() * sizeof(Factor) + s.out.notes.capacity() * sizeof(ResolvedNote) +
                  s.out.sections.capacity() * sizeof(Section);
    for (const auto &section : s.out.sections)
        size += section.combo.capacity() * sizeof(std::pair<int, int>);
    size += s.count_effects.capacity() * sizeof(CountActivation) +
            s.guarantees.capacity() * sizeof(Guarantee) + s.converters.capacity() * sizeof(Converter) +
            s.support_state.capacity() * sizeof(SupportState) +
            s.history.capacity() * sizeof(std::pair<const Note *, int>) +
            s.pending_results.capacity() * sizeof(std::pair<int, int>);
    for (const auto &converter : s.converters)
        size += converter.targets.size() * 64;
    if (s.rules) {
        size += sizeof(Rules);
        for (auto list : {&s.rules->ordinary, &s.rules->members, &s.rules->supports}) {
            size += list->capacity() * sizeof(Rule);
            for (const auto &r : *list)
                size += rule_memory(r) - sizeof(Rule);
        }
    }
    return size;
}
} // namespace
struct RuntimeMachine::Impl {
    std::shared_ptr<const RuntimeData> data;
    MachineState state;
    bool ordered = false, done = false;
    uint64_t events = 0, decisions = 0;
    std::vector<uint32_t> local_choices;
    std::vector<uint64_t> pending_weights;
    explicit Impl(std::shared_ptr<const RuntimeData> input) : data(std::move(input)) {
        state.out = data->initial;
        state.frames = data->frames;
        state.luck = data->luck;
        state.life = data->life;
        state.out.notes.reserve(data->notes.size());
        state.out.shared.reserve(data->ordinary.size() * 2);
    }
};
RuntimeMachine::RuntimeMachine(const Prepared &p, const Json &deck, const Json &request)
    : impl_(std::make_unique<Impl>(compile_runtime_data(p, deck, request))) {}
RuntimeMachine::RuntimeMachine(const RuntimeMachine &other) : impl_(std::make_unique<Impl>(*other.impl_)) {}
RuntimeMachine &RuntimeMachine::operator=(const RuntimeMachine &other) {
    if (this != &other)
        impl_ = std::make_unique<Impl>(*other.impl_);
    return *this;
}
RuntimeMachine::RuntimeMachine(RuntimeMachine &&) noexcept = default;
RuntimeMachine &RuntimeMachine::operator=(RuntimeMachine &&) noexcept = default;
RuntimeMachine::~RuntimeMachine() = default;
void RuntimeMachine::advance_until_decision() {
    auto &impl = *impl_;
    if (impl.done || !impl.pending_weights.empty())
        return;
    if (!impl.ordered) {
        if (impl.local_choices.empty()) {
            impl.pending_weights.assign(120, 1);
            return;
        }
        select_order(impl.state, *impl.data, impl.local_choices.at(0));
        impl.ordered = true;
        impl.decisions = 1;
        impl.local_choices.clear();
    }
    while (!impl.state.frames.empty()) {
        // Only this ideal update is speculative. Published history and its
        // controller state remain immutable until every random choice exists.
        MachineState transaction = impl.state;
        BranchRandom random{std::span<const uint32_t>(impl.local_choices)};
        try {
            execute_event(transaction, *impl.data, random);
        } catch (const NeedDecision &need) {
            impl.pending_weights = need.weights;
            return;
        }
        if (random.consumed() != impl.local_choices.size())
            throw Error("unconsumed runtime event choice");
        impl.state = std::move(transaction);
        impl.decisions += impl.local_choices.size();
        impl.local_choices.clear();
        ++impl.events;
        if (impl.events >= UINT32_MAX)
            throw Error("runtime continuation progress exhausted");
    }
    auto &out = impl.state.out;
    std::stable_sort(out.shared.begin(), out.shared.end(), [](const Factor &a, const Factor &b) {
        return std::tie(a.time_ms, a.owner) < std::tie(b.time_ms, b.owner);
    });
    std::stable_sort(out.primary.begin(), out.primary.end(),
                     [](const Factor &a, const Factor &b) { return a.time_ms < b.time_ms; });
    out.diagnostics = Json{{"clock", "ideal_1ms"},
                           {"final_life", impl.state.life},
                           {"random_decisions", impl.decisions},
                           {"joint_native_validation", "pending"}};
    impl.done = true;
}
void RuntimeMachine::choose(uint32_t choice) {
    auto &impl = *impl_;
    if (impl.done || impl.pending_weights.empty())
        throw Error("runtime is not waiting for a choice");
    if (choice >= impl.pending_weights.size() || !impl.pending_weights[choice])
        throw Error("invalid runtime continuation choice");
    if (impl.local_choices.size() >= UINT32_MAX - 1)
        throw Error("runtime event choice count exhausted");
    impl.local_choices.push_back(choice);
    impl.pending_weights.clear();
}
bool RuntimeMachine::finished() const {
    return impl_->done;
}
std::span<const uint64_t> RuntimeMachine::weights() const {
    return impl_->pending_weights;
}
const RuntimeResult &RuntimeMachine::result() const {
    if (!impl_->done)
        throw Error("runtime continuation is unfinished");
    return impl_->state.out;
}
uint64_t RuntimeMachine::progress() const {
    return impl_->ordered ? ((impl_->events + 1) << 32) | impl_->local_choices.size()
                          : impl_->local_choices.size();
}
uint64_t RuntimeMachine::committed_events() const {
    return impl_->events;
}
size_t RuntimeMachine::retained_bytes() const {
    return 1024 + sizeof(Impl) + sizeof(RuntimeMachine) + state_memory(impl_->state) +
           impl_->local_choices.capacity() * sizeof(uint32_t) +
           impl_->pending_weights.capacity() * sizeof(uint64_t);
}
std::vector<uint8_t> RuntimeMachine::canonical_key() const {
    const auto &impl = *impl_;
    const auto &state = impl.state;
    Bytes bytes;
    // The owning analysis session supplies the immutable kernel namespace.
    // Keys are portable bytes; never intern across different prepared kernels.
    bytes.u64(1);
    bytes.boolean(impl.ordered);
    bytes.boolean(impl.done);
    bytes.u64(impl.events);
    bytes.u64(impl.decisions);
    bytes.u64(impl.local_choices.size());
    for (auto choice : impl.local_choices)
        bytes.u64(choice);
    bytes.u64(impl.pending_weights.size());
    for (auto weight : impl.pending_weights)
        bytes.u64(weight);
    bytes.result(state.out);
    bytes.u64(state.frames.size());
    for (int time : state.frames)
        bytes.i32(time);
    for (int value :
         {state.life, state.luck.normal, state.luck.rush, state.luck.gauge, state.luck.lots,
          state.luck.rush_combo, state.luck.point, state.luck.next, state.section, state.combo, state.maximum,
          state.just, state.raw_just, state.inflated, state.raw_checker, state.previous_rank})
        bytes.i32(value);
    bytes.u64(state.position);
    bytes.boolean(state.dirty);
    bytes.u64(state.count_effects.size());
    for (const auto &a : state.count_effects) {
        bytes.rule(a.rule);
        for (int v : {a.start, a.end, a.begin_sequence, a.end_sequence, a.observed_end})
            bytes.i32(v);
        bytes.boolean(a.retired);
    }
    bytes.u64(state.guarantees.size());
    for (const auto &g : state.guarantees) {
        bytes.rule(g.rule);
        bytes.i32(g.end);
        bytes.i32(g.remaining);
    }
    bytes.u64(state.converters.size());
    for (const auto &c : state.converters) {
        bytes.rule(c.rule);
        bytes.i32(c.end);
        bytes.i32(c.remaining);
        bytes.u64(c.targets.size());
        for (int t : c.targets)
            bytes.i32(t);
    }
    bytes.u64(state.support_state.size());
    for (const auto &s : state.support_state) {
        bytes.rule(s.rule);
        bytes.boolean(s.accepted);
        bytes.boolean(s.active);
        bytes.i32(s.count);
        bytes.i32(s.executed);
        bytes.i32(s.raw);
    }
    bytes.u64(state.history.size());
    for (auto [n, judge] : state.history) {
        bytes.i32(n->id);
        bytes.i32(n->time_ms);
        bytes.i32(n->operate_type);
        bytes.boolean(n->critical);
        bytes.i32(judge);
    }
    bytes.u64(state.pending_results.size());
    for (auto [t, result] : state.pending_results) {
        bytes.i32(t);
        bytes.i32(result);
    }
    return std::move(bytes.values);
}
} // namespace mns
