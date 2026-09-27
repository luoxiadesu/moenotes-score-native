#include "analysis.hpp"
#include "hash.hpp"
#include "internal.hpp"
#include "moenotes_score.h"
#include "runtime_machine.hpp"
#include <cfenv>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
using mns::Json;
struct mns_prepared {
    std::shared_ptr<mns::Prepared> data;
    Json request;
};
struct mns_deck {
    std::shared_ptr<mns::Prepared> prepared;
    Json data;
};
struct mns_analysis {
    std::shared_ptr<mns::Prepared> prepared;
    Json deck, request, source;
    std::unique_ptr<mns::Analyzer> engine;
};
namespace {
thread_local char last_error[2048]{};
void set_error(const char *message) noexcept {
    if (!message)
        message = "unknown failure";
    std::strncpy(last_error, message, sizeof(last_error) - 1);
    last_error[sizeof(last_error) - 1] = '\0';
}
template <class F> mns_status protect(F &&f) noexcept {
    try {
        last_error[0] = '\0';
        if (std::fegetround() != FE_TONEAREST)
            throw mns::Error("rounding mode must be nearest-even");
        f();
        return MNS_OK;
    } catch (const std::bad_alloc &) {
        set_error("allocation failed");
        return MNS_OUT_OF_MEMORY;
    } catch (const mns::Error &e) {
        set_error(e.what());
        return MNS_INVALID_INPUT;
    } catch (const std::invalid_argument &e) {
        set_error(e.what());
        return MNS_INVALID_INPUT;
    } catch (const std::out_of_range &e) {
        set_error(e.what());
        return MNS_INVALID_INPUT;
    } catch (const std::bad_variant_access &e) {
        set_error(e.what());
        return MNS_INVALID_INPUT;
    } catch (const std::exception &e) {
        set_error(e.what());
        return MNS_INTERNAL_ERROR;
    } catch (...) {
        set_error("unknown internal failure");
        return MNS_INTERNAL_ERROR;
    }
}
Json parse(const char *s, size_t n) {
    if (!s && n)
        throw mns::Error("null JSON input");
    return Json::parse(std::string_view(s ? s : "", n));
}
void output(const Json &j, char **out) {
    if (!out)
        throw mns::Error("null output");
    auto s = j.dump();
    auto *p = static_cast<char *>(std::malloc(s.size() + 1));
    if (!p)
        throw std::bad_alloc();
    std::memcpy(p, s.c_str(), s.size() + 1);
    *out = p;
}
void keys(const Json &j, std::initializer_list<std::string_view> allowed) {
    if (!j.is_object())
        throw mns::Error("expected object");
    for (auto &[key, v] : j.items()) {
        (void)v;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw mns::Error("unknown field: " + key);
    }
}
Json path_json(const mns::PathScore &p) {
    Json s = Json::array();
    for (auto x : p.sections)
        s.push_back(x);
    Json c = Json::array();
    for (auto x : p.contributions)
        c.push_back(x);
    return Json{{"total", p.total}, {"solo", p.solo}, {"sections", s}, {"contributions", c}};
}
Json mean_json(const mns::MeanScore &p) {
    Json s = Json::array();
    for (auto x : p.sections)
        s.push_back(x);
    Json c = Json::array();
    for (auto x : p.contributions)
        c.push_back(x);
    return Json{{"total", p.total}, {"solo", p.solo}, {"sections", s}, {"contributions", c}};
}
Json rational(const mns::RationalValue &r) {
    return Json{{"numerator", r.numerator}, {"denominator", r.denominator}};
}
Json extreme(const mns::ExtremeResult &r) {
    Json choices = Json::array();
    for (auto x : r.choices)
        choices.push_back(x);
    return Json{{"status", r.status},
                {"lower", r.lower},
                {"upper", r.upper},
                {"witness", r.witness ? path_json(*r.witness) : Json(nullptr)},
                {"choices", choices}};
}
std::vector<mns::PathScore> evaluate(const std::shared_ptr<mns::Prepared> &p, const Json &deck,
                                     const Json &request, mns::BranchRandom &rng,
                                     const mns::RuntimeKernel &compiled) {
    auto run = request.value("mode", "free") == "challenge_skip" ? mns::RuntimeResult{} : compiled(rng);
    auto results = mns::score_path(*p, deck, request, run);
    std::vector<mns::PathScore> out;
    for (auto &r : results)
        out.push_back({r.total, r.solo, r.sections, r.contributions});
    return out;
}
} // namespace
extern "C" {
const char *mns_version(void) {
    return "0.1.0-dev.1";
}
const char *mns_last_error(void) {
    return last_error;
}
void mns_string_free(char *p) {
    std::free(p);
}
mns_status mns_prepare(const char *master, size_t master_size, const void *chart, size_t chart_size,
                       const char *name, const char *req, size_t req_size, mns_prepared **out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        if (!out || !chart || !name)
            throw mns::Error("null prepare argument");
        auto request = parse(req, req_size);
        keys(request, {"schema_version", "music_score_id", "mode", "gekisou_enabled", "context"});
        if (mns::integer(request.at("schema_version"), "schema_version", 1, 1) != 1)
            throw mns::Error("invalid schema");
        if (request.contains("context"))
            keys(request.at("context"), {"missions", "music_type"});
        auto bytes = std::span(static_cast<const uint8_t *>(chart), chart_size);
        auto handle = std::make_unique<mns_prepared>();
        handle->request = request;
        handle->data = mns::prepare(parse(master, master_size), bytes, name, request);
        handle->data->source["chart_sha256"] = mns::sha256(bytes);
        handle->data->source["master_sha256"] = mns::sha256(std::string_view(master, master_size));
        handle->data->source["request_sha256"] = mns::sha256(request.dump());
        handle->data->master.source = Json{{"master_sha256", handle->data->source.at("master_sha256")}};
        *out = handle.release();
    });
}
void mns_prepared_free(mns_prepared *p) {
    delete p;
}
mns_status mns_prepared_report(const mns_prepared *p, char **out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        if (!p)
            throw mns::Error("null prepared");
        output(p->data->source, out);
    });
}
mns_status mns_deck_resolve(const mns_prepared *p, const char *req, size_t n, mns_deck **out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        if (!p || !out)
            throw mns::Error("null deck argument");
        auto request = parse(req, n);
        keys(request, {"player_data", "deck_id", "deck", "account", "deck_context"});
        request["music_score_id"] = p->data->music_score_id;
        if (p->request.contains("context"))
            request["context"] = p->request.at("context");
        auto handle = std::make_unique<mns_deck>();
        handle->prepared = p->data;
        handle->data = mns::resolve_deck(p->data->master, request);
        Json resolved = handle->data;
        resolved.items().erase("source");
        handle->data["source"]["resolved_input_sha256"] = mns::sha256(resolved.dump());
        if (handle->data.at("slots").size() != 5)
            throw mns::Error("five occupied slots required");
        for (auto &s : handle->data.at("slots"))
            if (s.at("member_id").get<int>() <= 0)
                throw mns::Error("five occupied slots required");
        *out = handle.release();
    });
}
void mns_deck_free(mns_deck *d) {
    delete d;
}
mns_status mns_deck_report(const mns_deck *d, char **out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        if (!d)
            throw mns::Error("null deck");
        output(d->data, out);
    });
}
mns_status mns_analysis_create(const mns_prepared *p, const mns_deck *d, const char *opt, size_t n,
                               mns_analysis **out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        if (!p || !d || !out || p->data != d->prepared)
            throw mns::Error("prepared/deck mismatch");
        auto options = n ? parse(opt, n) : Json::object();
        keys(options,
             {"contributions", "ranks", "memory_limit_bytes", "exact_memory_limit_bytes", "estimator_seed"});
        auto handle = std::make_unique<mns_analysis>();
        handle->prepared = p->data;
        handle->deck = d->data;
        handle->request = p->request;
        for (auto key : {"contributions", "ranks"})
            if (options.contains(key))
                handle->request[key] = options.at(key);
        auto prepared = handle->prepared;
        auto deck = handle->deck;
        auto request = handle->request;
        auto mode = request.value("mode", "free");
        bool gek = request.value("gekisou_enabled", mode == "battle" || mode == "arena" || mode == "mission");
        if (options.contains("contributions") && !options.at("contributions").is_boolean())
            throw mns::Error("contributions requires boolean");
        if (options.contains("ranks")) {
            auto &r = options.at("ranks");
            if (!gek || !r.is_array() || r.size() != 3)
                throw mns::Error("ranks require Gekisou and three ranks");
            for (auto &v : r)
                mns::integer(v, "rank", 1, 5);
        }
        mns::AnalysisOptions settings;
        if (options.contains("memory_limit_bytes"))
            settings.memory_limit_bytes = size_t(mns::integer(
                options.at("memory_limit_bytes"), "memory limit", 1024 * 1024, 1024LL * 1024 * 1024));
        if (options.contains("exact_memory_limit_bytes"))
            settings.exact_memory_limit_bytes = size_t(mns::integer(
                options.at("exact_memory_limit_bytes"), "exact memory limit", 65536, 1024LL * 1024 * 1024));
        settings.exact_memory_limit_bytes =
            std::min(settings.exact_memory_limit_bytes, settings.memory_limit_bytes);
        if (options.contains("estimator_seed"))
            settings.seed =
                uint64_t(mns::integer(options.at("estimator_seed"), "estimator seed", 0, INT64_MAX));
        Json config{{"calculation", request},
                    {"memory_limit_bytes", settings.memory_limit_bytes},
                    {"exact_memory_limit_bytes", settings.exact_memory_limit_bytes},
                    {"estimator_seed", settings.seed}};
        handle->source = prepared->source;
        handle->source["resolved_deck_sha256"] = deck.at("source").at("resolved_input_sha256");
        handle->source["analysis_config"] = config;
        handle->source["analysis_config_sha256"] = mns::sha256(config.dump());
        auto compiled =
            mode == "challenge_skip" ? mns::RuntimeKernel{} : mns::compile_runtime(*prepared, deck, request);
        auto kernel = [prepared, deck, request, compiled](std::span<const uint32_t> choices) {
            try {
                mns::BranchRandom rng(choices);
                return mns::KernelEvaluation{{}, evaluate(prepared, deck, request, rng, compiled)};
            } catch (const mns::NeedDecision &decision) {
                return mns::KernelEvaluation{decision.weights, {}};
            }
        };
        auto sampler = [prepared, deck, request, compiled, seed = settings.seed](uint64_t index) {
            std::mt19937_64 random(seed + index * UINT64_C(0x9e3779b97f4a7c15));
            mns::SamplePath sample;
            mns::BranchRandom rng([&](std::span<const uint64_t> w) {
                uint64_t total = 0;
                for (auto x : w) {
                    if (UINT64_MAX - total < x)
                        throw mns::Error("weight sum overflow");
                    total += x;
                }
                if (!total)
                    throw mns::Error("empty lottery");
                uint64_t threshold = uint64_t(-total) % total, value;
                do {
                    value = random();
                } while (value < threshold);
                value %= total;
                for (size_t i = 0; i < w.size(); ++i) {
                    if (value < w[i]) {
                        sample.choices.push_back(uint32_t(i));
                        return i;
                    }
                    value -= w[i];
                }
                throw mns::Error("lottery selection");
            });
            sample.scores = evaluate(prepared, deck, request, rng, compiled);
            return sample;
        };
        mns::StateKernel states;
        if (mode != "challenge_skip") {
            auto machine_root = std::make_shared<mns::RuntimeMachine>(*prepared, deck, request);
            auto wrap = [](std::shared_ptr<mns::RuntimeMachine> machine) {
                machine->advance_until_decision();
                auto key = machine->canonical_key();
                auto progress = machine->progress();
                auto memory = machine->retained_bytes();
                return mns::AnalysisState{std::move(machine), std::move(key), progress, memory};
            };
            states.initial = [prepared, machine_root, wrap] {
                return wrap(std::make_shared<mns::RuntimeMachine>(*machine_root));
            };
            states.evaluate = [prepared, deck, request](const mns::AnalysisState &state) {
                auto machine = std::static_pointer_cast<const mns::RuntimeMachine>(state.opaque);
                if (!machine->finished()) {
                    auto weights = machine->weights();
                    return mns::KernelEvaluation{std::vector<uint64_t>(weights.begin(), weights.end()), {}};
                }
                auto scores = mns::score_path(*prepared, deck, request, machine->result());
                std::vector<mns::PathScore> paths;
                for (auto &score : scores)
                    paths.push_back({score.total, score.solo, score.sections, score.contributions});
                return mns::KernelEvaluation{{}, std::move(paths)};
            };
            states.transition = [prepared, wrap](const mns::AnalysisState &state, uint32_t choice) {
                auto original = std::static_pointer_cast<const mns::RuntimeMachine>(state.opaque);
                auto machine = std::make_shared<mns::RuntimeMachine>(*original);
                machine->choose(choice);
                return wrap(std::move(machine));
            };
        }
        handle->engine =
            std::make_unique<mns::Analyzer>(kernel, mns::score_bounds(*prepared, deck, request), settings,
                                            sampler, mns::NodeBounder{}, std::move(states));
        *out = handle.release();
    });
}
void mns_analysis_free(mns_analysis *a) {
    delete a;
}
mns_status mns_analysis_advance(mns_analysis *a, uint32_t ms, uint64_t work) {
    return protect([&] {
        if (!a)
            throw mns::Error("null analysis");
        a->engine->advance({ms, work});
    });
}
mns_status mns_analysis_snapshot(const mns_analysis *a, char **out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        if (!a)
            throw mns::Error("null analysis");
        auto s = a->engine->snapshot();
        Json ranks = Json::array();
        for (size_t i = 0; i < s.ranks.size(); ++i) {
            auto &r = s.ranks[i];
            auto &e = r.expectation;
            Json expectation{{"status", e.status},
                             {"mean", e.value ? mean_json(*e.value) : Json(nullptr)},
                             {"lower", e.lower},
                             {"upper", e.upper},
                             {"confidence", .95},
                             {"confidence_samples", e.confidence_samples},
                             {"precision_reached", e.precision_reached}};
            if (e.exact) {
                Json exact{{"total", rational(e.exact->total)}, {"solo", rational(e.exact->solo)}};
                Json sections = Json::array(), contributions = Json::array();
                for (auto &v : e.exact->sections)
                    sections.push_back(rational(v));
                for (auto &v : e.exact->contributions)
                    contributions.push_back(rational(v));
                exact["sections"] = sections;
                exact["contributions"] = contributions;
                expectation["exact"] = exact;
            }
            Json condition = a->request.contains("ranks") ? a->request.at("ranks")
                             : s.ranks.size() == 5        ? Json::array({int(i + 1), int(i + 1), int(i + 1)})
                                                          : Json(nullptr);
            ranks.push_back(Json{{"ranks", condition},
                                 {"maximum", extreme(r.maximum)},
                                 {"minimum", extreme(r.minimum)},
                                 {"expectation", expectation}});
        }
        Json report{{"schema_version", 1},
                    {"version", mns_version()},
                    {"source", a->source},
                    {"total_power", a->deck.at("total_power")},
                    {"results", ranks},
                    {"complete", s.complete},
                    {"failed", s.failed},
                    {"error", s.error},
                    {"stop_reason", s.stop_reason},
                    {"samples", s.samples},
                    {"evaluations", s.evaluations},
                    {"enumerated_leaves", s.enumerated_leaves},
                    {"probability_dag", s.probability_dag},
                    {"merged_states", s.merged_states},
                    {"pending_nodes", s.pending_nodes},
                    {"analysis_memory_bytes", s.memory_bytes},
                    {"exact_memory_limited", s.exact_memory_limited},
                    {"explored_probability", rational(s.explored_probability)}};
        output(report, out);
    });
}
void mns_analysis_cancel(mns_analysis *a) {
    if (a)
        a->engine->cancel();
}
void mns_analysis_resume(mns_analysis *a) {
    if (a)
        a->engine->resume();
}
}
