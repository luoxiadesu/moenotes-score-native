#include "internal.hpp"
#include "moenotes_chart_parser.h"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <regex>
#include <set>
namespace mns {
namespace {
struct AllocationLimit {
    size_t used = 0;
    static constexpr size_t limit = 128ULL * 1024 * 1024;
};
struct alignas(std::max_align_t) AllocationHeader {
    size_t size;
};
void *parser_malloc(void *context, size_t n) {
    auto &state = *static_cast<AllocationLimit *>(context);
    if (n > state.limit - state.used || n > SIZE_MAX - sizeof(AllocationHeader))
        return nullptr;
    auto *p = static_cast<AllocationHeader *>(std::malloc(sizeof(AllocationHeader) + n));
    if (!p)
        return nullptr;
    p->size = n;
    state.used += n;
    return p + 1;
}
void parser_free(void *context, void *ptr) {
    if (!ptr)
        return;
    auto &state = *static_cast<AllocationLimit *>(context);
    auto *p = static_cast<AllocationHeader *>(ptr) - 1;
    state.used -= p->size;
    std::free(p);
}
void *parser_realloc(void *context, void *ptr, size_t n) {
    if (!ptr)
        return parser_malloc(context, n);
    if (!n) {
        parser_free(context, ptr);
        return nullptr;
    }
    auto &state = *static_cast<AllocationLimit *>(context);
    auto *p = static_cast<AllocationHeader *>(ptr) - 1;
    auto old = p->size;
    if (n > state.limit - (state.used - old) || n > SIZE_MAX - sizeof(AllocationHeader))
        return nullptr;
    auto *q = static_cast<AllocationHeader *>(std::realloc(p, sizeof(AllocationHeader) + n));
    if (!q)
        return nullptr;
    q->size = n;
    state.used = state.used - old + n;
    return q + 1;
}
} // namespace
static std::string basename(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s.substr(s.find_last_of('/') + 1);
}
std::shared_ptr<Prepared> prepare(Json tables, std::span<const uint8_t> bytes, std::string_view chart_name,
                                  const Json &request) {
    if (!tables.is_object() || !request.is_object())
        throw Error("master and request must be objects");
    if (request.contains("gekisou_enabled") && !request.at("gekisou_enabled").is_boolean())
        throw Error("gekisou_enabled must be boolean");
    if (bytes.empty() || bytes.size() > 16 * 1024 * 1024)
        throw Error("chart must contain 1..16 MiB");
    auto p = std::make_shared<Prepared>();
    p->master.tables = std::move(tables);
    for (auto &[name, table] : p->master.tables.items()) {
        const auto &rows = p->master.rows(name);
        if (!rows.is_array())
            throw Error(name + ": expected _allData array");
        std::set<std::string> ids;
        for (auto &r : rows) {
            if (!r.is_object() || !r.contains("_id"))
                throw Error(name + ": missing row id");
            auto &id = r.at("_id");
            if (!id.is_string() && !id.is_number_integer())
                throw Error(name + ": invalid id");
            if (!ids.insert(id.dump()).second)
                throw Error(name + ": duplicate id");
        }
    }
    p->master.build_indexes();
    p->music_score_id = int(integer(request.at("music_score_id"), "music_score_id", 1));
    auto &score = p->master.by_id("MasterLiveMusicScore", p->music_score_id);
    p->level = int(integer(score.at("_musicScoreLevel"), "music level", 1, 100));
    auto stem = basename(std::string(chart_name));
    if (stem.ends_with(".gz"))
        stem.resize(stem.size() - 3);
    auto dot = stem.find_last_of('.');
    if (dot != std::string::npos)
        stem.resize(dot);
    auto expected = basename(score.at("_musicScoreTextFileName").get<std::string>());
    if (std::regex_match(stem, std::regex("[0-9]{4}_[0-9]{2}")) && stem != expected)
        throw Error("chart filename does not match music_score_id");
    const Json *song = nullptr;
    for (auto &r : p->master.rows("MasterLiveMusic"))
        for (auto key : {"_easyID", "_normalID", "_hardID", "_expertID"})
            if (r.contains(key) && r.at(key).get<int64_t>() == p->music_score_id) {
                if (song)
                    throw Error("ambiguous music score song");
                song = &r;
            }
    if (!song)
        throw Error("music score has no song");
    p->music_id = int(song->at("_id").get<int64_t>());
    for (int i = 0; i < 3; ++i)
        p->missions[i] = song->at("_gekisouMission" + std::to_string(i + 1)).get<int>();
    if (request.contains("context") && request.at("context").contains("missions")) {
        auto &m = request.at("context").at("missions");
        if (!m.is_array() || m.size() != 3)
            throw Error("context.missions requires three missions");
        for (size_t i = 0; i < 3; ++i)
            p->missions[i] = int(integer(m.at(i), "mission", 1, 3));
    }
    std::map<std::string, std::string> settings;
    for (auto &r : p->master.rows("MasterLiveSettings")) {
        auto k = r.at("_key").get<std::string>();
        if (!settings.emplace(k, r.at("_value").get<std::string>()).second)
            throw Error("duplicate settings key");
    }
    p->adjustment = f32(std::stod(settings.at("note_score_adjustment_factor")));
    p->life_factor = f32(std::stod(settings.at("note_score_life_onus_factor")));
    p->assist_factor = f32(f32(std::stod(settings.at("assist_score_percent"))) / 100.f);
    for (auto &r : p->master.rows("MasterLiveNoteParameter")) {
        int k = r.at("_noteOperateType").get<int>();
        if (!p->note_percent.emplace(k, int(integer(r.at("_scorePercent"), "note weight"))).second)
            throw Error("duplicate note operation");
    }
    for (auto &r : p->master.rows("MasterLiveJudgementParameter")) {
        int k = 7 - int(integer(r.at("_noteSimulateJudgement"), "judgement", 1, 6));
        if (!p->judgement_percent.emplace(k, int(integer(r.at("_scorePercent"), "judgement weight"))).second)
            throw Error("duplicate judgement");
    }
    for (auto &r : p->master.rows("MasterLiveComboScoreBonus")) {
        int kind = int(integer(r.at("_comboBonusType"), "combo kind", 0, 1));
        p->combo[kind].emplace_back(int(integer(r.at("_requiredComboCount"), "combo threshold")),
                                    f32(r.at("_bonusFactor").get<double>()));
    }
    for (auto &v : p->combo) {
        std::sort(v.begin(), v.end());
        float total = 0;
        int prev = -1;
        for (auto &[threshold, value] : v) {
            if (threshold == prev)
                throw Error("duplicate combo threshold");
            prev = threshold;
            value = total = f32(total + value);
        }
    }
    moenotes_parse_options_t options{};
    moenotes_default_parse_options(&options);
    options.slide_combo_unit = 8;
    AllocationLimit allocation;
    moenotes_allocator_t allocator{&allocation, parser_malloc, parser_realloc, parser_free};
    moenotes_score_t *raw = nullptr;
    char error[512]{};
    auto status =
        moenotes_score_parse(bytes.data(), bytes.size(), &options, &allocator, &raw, error, sizeof(error));
    if (status != MOENOTES_OK)
        throw Error(std::string("chart parse: ") + error);
    std::unique_ptr<moenotes_score_t, decltype(&moenotes_score_free)> handle(raw, moenotes_score_free);
    size_t count = moenotes_score_note_count(raw);
    if (count > 1000000)
        throw Error("chart exceeds one million nodes");
    int32_t weighted = 0;
    std::set<int> ids;
    for (size_t i = 0; i < count; ++i) {
        moenotes_note_view_t n{};
        if (moenotes_score_note_at(raw, i, &n) != MOENOTES_OK)
            throw Error("chart note access");
        if (n.position.time_ms < 0 || n.position.time_ms > 3600000)
            throw Error("chart time must be within one hour");
        auto found = p->note_percent.find(n.operate_type);
        bool judged = moenotes_operate_type_is_judgement(n.operate_type) != 0;
        if (found != p->note_percent.end()) {
            if (!judged && found->second != 0)
                throw Error("weighted nonjudgement node unsupported");
            weighted = wrap32(int64_t(weighted) + found->second);
        } else if (judged)
            throw Error("missing judgement operation weight");
        if (!ids.insert(n.id).second)
            throw Error("duplicate chart node id");
        p->notes.push_back({n.id, n.position.time_ms, int(n.operate_type), judged, n.critical != 0, -1});
        if (judged)
            p->last_ms = std::max(p->last_ms, n.position.time_ms);
    }
    p->denominator = int(std::ceil(f32(f32(weighted) / 100.f)));
    if (p->denominator <= 0)
        throw Error("invalid weighted note count");
    for (size_t i = 0; i < moenotes_score_event_count(raw); ++i) {
        moenotes_event_t e{};
        moenotes_score_event_at(raw, i, &e);
        Event v{int(i), e.position.time_ms, e.end_position.time_ms, int(e.type)};
        if (e.type == MOENOTES_EVENT_SKILL)
            p->skills.push_back(v);
        if (e.type == MOENOTES_EVENT_FEVER)
            p->fevers.push_back(v);
    }
    std::stable_sort(p->notes.begin(), p->notes.end(),
                     [](auto &a, auto &b) { return std::tie(a.time_ms, a.id) < std::tie(b.time_ms, b.id); });
    for (size_t i = 0; i < p->fevers.size(); ++i) {
        auto &e = p->fevers[i];
        if (e.time_ms < 0 || e.end_ms <= e.time_ms || e.end_ms > 3600000 ||
            (i && p->fevers[i - 1].end_ms >= e.time_ms))
            throw Error("invalid fever ranges");
        for (auto &n : p->notes)
            if (e.time_ms <= n.time_ms && n.time_ms <= e.end_ms)
                n.fever_index = int(i);
    }
    auto mode = request.value("mode", "free");
    bool gek = request.value("gekisou_enabled", mode == "battle" || mode == "arena" || mode == "mission");
    if (mode != "free" && mode != "battle" && mode != "arena" && mode != "challenge" && mode != "mission" &&
        mode != "challenge_skip")
        throw Error("unsupported mode");
    if ((mode == "battle" || mode == "arena" || mode == "mission") && !gek)
        throw Error("mode requires Gekisou");
    if (mode == "challenge_skip" && gek)
        throw Error("Challenge skip does not execute Gekisou");
    if (gek && (p->fevers.size() != 3 ||
                std::any_of(p->missions.begin(), p->missions.end(), [](int m) { return m < 1 || m > 3; })))
        throw Error("Gekisou requires three valid missions");
    if (p->skills.size() != 5 && mode != "challenge_skip")
        throw Error("five skill events required");
    p->source = Json{{"chart_name", std::string(chart_name)},
                     {"music_score_id", p->music_score_id},
                     {"parser_version", moenotes_version_string()},
                     {"warnings", moenotes_score_warnings(raw)},
                     {"clock", "ideal_1ms_v1"},
                     {"probability_model", "independent_master_weights_v1"}};
    return p;
}
} // namespace mns
