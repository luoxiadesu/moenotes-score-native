#pragma once
#include "json.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <span>
namespace mns {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "MoeNotes requires IEEE-754 binary32");
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "MoeNotes statistics require IEEE-754 binary64");
#ifdef __FAST_MATH__
#error "MoeNotes score cannot be compiled with fast-math"
#endif
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
inline float f32(double v) {
    volatile float x = static_cast<float>(v);
    return x;
}
inline int32_t wrap32(int64_t v) {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(v));
}
inline int64_t integer(const Json &v, const char *label, int64_t lo = 0, int64_t hi = INT32_MAX) {
    if (!v.is_number_integer())
        throw Error(std::string(label) + ": expected integer");
    auto n = v.get<int64_t>();
    if (n < lo || n > hi)
        throw Error(std::string(label) + ": out of range");
    return n;
}
struct Master {
    Json tables;
    Json source;
    std::map<std::string, std::map<int64_t, size_t>, std::less<>> indexes;
    void build_indexes() {
        indexes.clear();
        for (auto &[name, table] : tables.items()) {
            const auto &list = rows(name);
            for (size_t i = 0; i < list.size(); ++i) {
                const auto &id = list.at(i).at("_id");
                if (id.is_number_integer())
                    indexes[name].emplace(id.get<int64_t>(), i);
            }
        }
    }
    const Json &rows(std::string_view name) const {
        const auto &v = tables.at(name);
        return v.is_object() ? v.at("_allData") : v;
    }
    const Json &by_id(std::string_view name, int64_t id) const {
        if (auto table = indexes.find(name); table != indexes.end()) {
            auto entry = table->second.find(id);
            if (entry != table->second.end())
                return rows(name).at(entry->second);
            throw Error("missing master reference " + std::string(name) + " " + std::to_string(id));
        }
        for (const auto &r : rows(name))
            if (r.at("_id").get<int64_t>() == id)
                return r;
        throw Error("missing master reference " + std::string(name) + " " + std::to_string(id));
    }
};
struct Note {
    int id = 0, time_ms = 0, operate_type = 0;
    bool judgement = true, critical = false;
    int fever_index = -1;
};
struct Event {
    int index = 0, time_ms = 0, end_ms = 0, type = 0;
};
struct Prepared {
    Master master;
    std::vector<Note> notes;
    std::vector<Event> skills, fevers;
    Json source;
    int music_score_id = 0, music_id = 0, level = 0, denominator = 0, last_ms = 0;
    std::array<int, 3> missions{};
    std::map<int, int> note_percent, judgement_percent;
    std::array<std::vector<std::pair<int, float>>, 2> combo;
    float adjustment = 3, life_factor = .3f, assist_factor = .9f;
    float combo_factor(int count, int kind = 0) const {
        float v = 0;
        for (auto [threshold, f] : combo.at(kind)) {
            if (threshold > count)
                break;
            v = f;
        }
        return v;
    }
};
struct Factor {
    int time_ms = 0, owner = -1;
    int combo_raw = 0, note_raw = 0, judgement = 0, judgement_raw = 0, luck = 0;
};
struct ResolvedNote {
    int id = 0, time_ms = 0, judgement = 5, solo_judgement = 5, life = 1000;
};
struct Section {
    int start_ms = 0, end_ms = 0, mission = 0, mission_value = 0;
    std::vector<std::pair<int, int>> combo;
};
struct RuntimeResult {
    std::vector<Factor> shared, primary;
    std::vector<ResolvedNote> notes;
    std::vector<Section> sections;
    std::array<int, 5> order{0, 1, 2, 3, 4};
    Json diagnostics = Json::object();
};
struct NeedDecision {
    std::vector<uint64_t> weights;
};
class BranchRandom {
    std::span<const uint32_t> choices_;
    size_t at_ = 0;
    std::function<size_t(std::span<const uint64_t>)> generator_;

  public:
    explicit BranchRandom(std::span<const uint32_t> choices) : choices_(choices) {}
    explicit BranchRandom(std::function<size_t(std::span<const uint64_t>)> generator)
        : generator_(std::move(generator)) {}
    size_t choose(std::span<const uint64_t> weights) {
        size_t only = 0, count = 0;
        for (size_t i = 0; i < weights.size(); ++i)
            if (weights[i]) {
                only = i;
                ++count;
            }
        if (!count)
            throw Error("empty random distribution");
        if (count == 1)
            return only;
        if (generator_) {
            ++at_;
            return generator_(weights);
        }
        if (at_ == choices_.size())
            throw NeedDecision{std::vector<uint64_t>(weights.begin(), weights.end())};
        auto result = choices_[at_++];
        if (result >= weights.size() || !weights[result])
            throw Error("invalid random branch");
        return result;
    }
    bool probability(int percent) {
        if (percent < 0 || percent > 100)
            throw Error("invalid probability");
        std::array<uint64_t, 2> w{uint64_t(100 - percent), uint64_t(percent)};
        return choose(w) == 1;
    }
    size_t consumed() const {
        return at_;
    }
};
Json resolve_deck(const Master &master, const Json &request);
using RuntimeKernel = std::function<RuntimeResult(BranchRandom &)>;
// Prepared must outlive the returned immutable, independently executable kernel.
RuntimeKernel compile_runtime(const Prepared &, const Json &resolved_deck, const Json &request);
RuntimeResult run_runtime(const Prepared &prepared, const Json &resolved_deck, const Json &request,
                          BranchRandom &random);
std::shared_ptr<Prepared> prepare(Json master_tables, std::span<const uint8_t> chart,
                                  std::string_view chart_name, const Json &request);
struct ScoredPath {
    int64_t total = 0, solo = 0;
    std::array<int64_t, 3> sections{};
    std::vector<int64_t> contributions;
    Json detail;
};
std::vector<ScoredPath> score_path(const Prepared &, const Json &deck, const Json &request,
                                   const RuntimeResult &);
struct ScoreBound;
std::vector<ScoreBound> score_bounds(const Prepared &, const Json &deck, const Json &request);
} // namespace mns
