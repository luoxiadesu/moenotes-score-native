#include "analysis.hpp"

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace mns;

static PathScore score(std::int64_t total, std::int64_t solo) {
    return {total, solo, {total / 2, total / 3, total / 5}, {total - 1, 1}};
}
static KernelEvaluation small_tree(std::span<const std::uint32_t> choices) {
    if (choices.empty())
        return {{1, 0, 3}, {}};
    assert(choices[0] != 1); // Zero mass must never be visited or sampled.
    if (choices[0] == 0)
        return {{}, {score(10, 20), score(100, 20)}};
    if (choices.size() == 1)
        return {{2, 1}, {}};
    if (choices[1] == 0)
        return {{}, {score(20, 40), score(50, 40)}};
    return {{}, {score(50, 100), score(10, 100)}};
}
static std::vector<ScoreBound> tree_bounds() {
    return {{0, 100}, {0, 100}};
}
static void exact_tree() {
    Analyzer analyzer(small_tree, tree_bounds());
    analyzer.advance({10000, 1000});
    const auto output = analyzer.snapshot();
    assert(output.complete && output.stop_reason == "exact");
    assert(output.enumerated_leaves == 3 && output.pending_nodes == 0);
    assert(output.explored_probability.numerator == "1" && output.explored_probability.denominator == "1");
    const auto &first = output.ranks[0];
    assert(first.minimum.status == "exact" && first.minimum.lower == 10 && first.minimum.upper == 10);
    assert(first.maximum.status == "exact" && first.maximum.lower == 50 && first.maximum.upper == 50);
    assert(first.minimum.choices == std::vector<std::uint32_t>{0});
    assert((first.maximum.choices == std::vector<std::uint32_t>{2, 1}));
    assert(first.maximum.witness->solo == 100);
    assert(first.expectation.status == "exact");
    assert(first.expectation.exact->total.numerator == "25");
    assert(first.expectation.exact->total.denominator == "1");
    assert(first.expectation.exact->solo.numerator == "50");
    assert(first.expectation.exact->sections[1].numerator == "31");
    assert(first.expectation.exact->sections[1].denominator == "4");
    assert(first.expectation.exact->contributions[0].numerator == "24");
    assert(first.expectation.exact->contributions[1].numerator == "1");
    const auto &second = output.ranks[1];
    assert(second.minimum.witness->total == 10 && second.maximum.witness->total == 100);
    assert(second.expectation.exact->total.numerator == "105" &&
           second.expectation.exact->total.denominator == "2");
}
static void resumable() {
    Analyzer uninterrupted(small_tree, tree_bounds()), sliced(small_tree, tree_bounds());
    uninterrupted.advance({10000, 7});
    for (int i = 0; i != 7; ++i)
        sliced.advance({10000, 1});
    auto a = uninterrupted.snapshot(), b = sliced.snapshot();
    assert(a.evaluations == b.evaluations && a.samples == b.samples);
    assert(a.enumerated_leaves == b.enumerated_leaves && a.pending_nodes == b.pending_nodes);
    assert(a.explored_probability.numerator == b.explored_probability.numerator);
    assert(a.explored_probability.denominator == b.explored_probability.denominator);
    sliced.cancel();
    assert(sliced.snapshot().cancelled);
    sliced.advance({10000, 1000});
    assert(sliced.snapshot().cancelled && sliced.snapshot().stop_reason == "cancelled");
    assert(sliced.snapshot().evaluations == b.evaluations);
    sliced.resume();
    sliced.advance({10000, 1000});
    assert(sliced.snapshot().complete);
    Analyzer untouched(small_tree, tree_bounds());
    untouched.cancel();
    assert(untouched.snapshot().stop_reason == "cancelled");
    untouched.resume();
    untouched.advance({0, 1000});
    assert(untouched.snapshot().evaluations == 0);
    untouched.advance({10000, 0});
    assert(untouched.snapshot().evaluations == 0);
}
static void fraction() {
    Analyzer analyzer(
        [](auto choices) -> KernelEvaluation {
            if (choices.empty())
                return {{2, 1}, {}};
            return {{}, {score(choices[0] ? 1 : 0, 0)}};
        },
        {{0, 1}});
    analyzer.advance({10000, 100});
    const auto mean = analyzer.snapshot().ranks[0].expectation;
    assert(mean.exact->total.numerator == "1" && mean.exact->total.denominator == "3");
    assert(mean.lower < mean.upper);
    using ExactCheck = boost::multiprecision::cpp_dec_float_100;
    assert(ExactCheck(mean.lower) <= ExactCheck(1) / 3);
    assert(ExactCheck(mean.upper) >= ExactCheck(1) / 3);
}
static void sampled_memory_bound() {
    AnalysisOptions options;
    options.memory_limit_bytes = 65536;
    options.exact_memory_limit_bytes = 4096;
    const auto kernel = [](auto choices) -> KernelEvaluation {
        if (choices.empty())
            return {std::vector<std::uint64_t>(1000, 1), {}};
        return {{}, {score(choices[0] % 2 ? 100 : 0, 0)}};
    };
    // Direct complete-path sampling permits large event counts without prefix
    // replay; the seeded engine's own stream is preserved between callbacks.
    auto generator = std::make_shared<std::mt19937_64>(42);
    auto sampler = [generator](std::uint64_t) -> SamplePath {
        std::uint64_t draw;
        do {
            draw = (*generator)();
        } while (draw < static_cast<std::uint64_t>(-std::uint64_t{1000}) % 1000);
        const auto outcome = static_cast<std::uint32_t>(draw % 1000);
        return {{score(outcome % 2 ? 100 : 0, 0)}, {outcome}};
    };
    Analyzer analyzer(kernel, {{0, 100}}, options, sampler);
    analyzer.advance({10000, 300});
    auto output = analyzer.snapshot();
    assert(!output.complete && output.exact_memory_limited && output.pending_nodes == 1);
    assert(output.explored_probability.numerator == "0");
    assert(output.samples == 299 && output.enumerated_leaves == 0);
    assert(output.memory_bytes <= options.memory_limit_bytes);
    assert(output.ranks[0].expectation.status == "estimated" && !output.ranks[0].expectation.exact);
    assert(output.ranks[0].expectation.confidence_samples == 256);
    assert(output.ranks[0].expectation.lower <= 50 && output.ranks[0].expectation.upper >= 50);
    assert(output.ranks[0].minimum.status == "exact" && output.ranks[0].maximum.status == "exact");
    // Confidence intervals are updated only at fixed sample counts, preventing
    // arbitrary progress calls from spending an unbounded failure probability.
    const auto interval = output.ranks[0].expectation;
    analyzer.advance({10000, 10});
    output = analyzer.snapshot();
    assert(output.ranks[0].expectation.lower == interval.lower);
    assert(output.ranks[0].expectation.upper == interval.upper);
    assert(output.ranks[0].expectation.value->total == interval.value->total);
}
static void prefix_sampling_resume() {
    const auto tree = [](auto choices) -> KernelEvaluation {
        if (choices.empty())
            return {std::vector<std::uint64_t>(1000, 1), {}};
        return {{}, {score(choices[0] % 101, 0)}};
    };
    AnalysisOptions options;
    options.exact_memory_limit_bytes = 4096;
    options.seed = 16384;
    Analyzer full(tree, {{0, 100}}, options), sliced(tree, {{0, 100}}, options);
    full.advance({10000, 777});
    for (int index = 0; index != 111; ++index)
        sliced.advance({10000, 7});
    const auto a = full.snapshot(), b = sliced.snapshot();
    assert(a.evaluations == b.evaluations && a.samples == b.samples);
    assert(a.ranks[0].expectation.value->total == b.ranks[0].expectation.value->total);
    assert(a.ranks[0].expectation.lower == b.ranks[0].expectation.lower);
    assert(a.ranks[0].minimum.choices == b.ranks[0].minimum.choices);
    options.enable_sampling = false;
    Analyzer frozen(tree, {{0, 100}}, options);
    frozen.advance({10000, 777});
    auto out = frozen.snapshot();
    assert(out.stop_reason == "memory_limit" && out.pending_nodes == 1);
    assert(!out.complete && out.ranks[0].expectation.status == "pending");
    assert(out.explored_probability.numerator == "0");
}
static void proven_prefix_bounds() {
    AnalysisOptions options;
    options.enable_sampling = false;
    const auto tree = [](auto choices) -> KernelEvaluation {
        if (choices.empty())
            return {{1, 1}, {}};
        if (choices[0] == 0)
            return {{}, {score(50, 0)}};
        if (choices.size() == 1)
            return {{1, 1}, {}};
        return {{}, {score(choices[1] ? 30 : 20, 0)}};
    };
    const auto bounder = [](auto choices) -> std::vector<ScoreBound> {
        if (choices.empty())
            return {{10, 50}};
        if (choices[0] == 0)
            return {{50, 50}};
        if (choices.size() == 1)
            return {{20, 30}};
        return {{choices[1] ? 30 : 20, choices[1] ? 30 : 20}};
    };
    Analyzer analyzer(tree, {{0, 100}}, options, {}, bounder);
    analyzer.advance({10000, 2});
    auto output = analyzer.snapshot();
    assert(!output.complete && output.enumerated_leaves == 1);
    assert(output.ranks[0].maximum.status == "exact" && output.ranks[0].maximum.upper == 50);
    assert(output.ranks[0].minimum.lower == 20 && output.ranks[0].minimum.upper == 50);
    assert(output.ranks[0].expectation.status == "pending");
    analyzer.advance({10000, 1});
    output = analyzer.snapshot();
    assert(output.pending_nodes == 2 && output.explored_probability.numerator == "1");
    assert(output.explored_probability.denominator == "2");
    analyzer.advance({10000, 100});
    output = analyzer.snapshot();
    assert(output.complete && output.ranks[0].minimum.lower == 20);
    assert(output.ranks[0].expectation.value->total == 37.5);
}
static void failed_session() {
    int evaluations = 0;
    Analyzer analyzer(
        [&evaluations](auto) -> KernelEvaluation {
            ++evaluations;
            throw std::invalid_argument("kernel failure");
        },
        {{0, 100}});
    try {
        analyzer.advance();
        assert(false);
    } catch (const std::invalid_argument &) {
    }
    const auto output = analyzer.snapshot();
    assert(output.failed && output.stop_reason == "failed" && output.error == "kernel failure");
    analyzer.resume();
    try {
        analyzer.advance();
        assert(false);
    } catch (const std::runtime_error &) {
    }
    assert(evaluations == 1);
}
static void certified_confidence_arithmetic() {
    using BigFloat = boost::multiprecision::cpp_dec_float_100;
    AnalysisOptions options;
    options.exact_memory_limit_bytes = 4096;
    constexpr std::int64_t top = INT32_MAX;
    auto frozen = [](auto choices) -> KernelEvaluation {
        if (choices.empty())
            return {std::vector<std::uint64_t>(1000, 1), {}};
        return {{}, {score(top, 0)}};
    };
    for (const int pattern : {0, 1, 2}) {
        // Very small variance around INT32_MAX exercises cancellation that
        // would lose information in double sum-of-squares arithmetic. The
        // alternating endpoint case exercises a large exact second moment.
        const auto value = [pattern](std::uint64_t index) -> std::int64_t {
            if (pattern == 0)
                return top;
            if (pattern == 1)
                return top - ((index % 3) == 0 ? 1 : 0);
            return index % 2 ? top : 0;
        };
        const ScoreBound bound = pattern == 2 ? ScoreBound{0, top} : ScoreBound{top - 1, top};
        Analyzer analyzer(frozen, {bound}, options, [value](std::uint64_t index) -> SamplePath {
            return {{score(value(index), 0)}, {std::uint32_t(index % 1000)}};
        });
        analyzer.advance({10000, 129});
        const auto output = analyzer.snapshot();
        assert(output.samples == 128);
        const auto &estimate = output.ranks[0].expectation;
        assert(estimate.confidence_samples == 128);
        BigFloat sum = 0, square_sum = 0;
        for (std::uint64_t index = 0; index < 128; ++index) {
            const BigFloat sample = value(index);
            sum += sample;
            square_sum += sample * sample;
        }
        const BigFloat mean = sum / 128;
        const BigFloat variance = (128 * square_sum - sum * sum) / (128 * 127);
        // k=3 corresponds to checkpoints 32,64,128; one rank, 5% total error.
        const BigFloat delta = BigFloat(1) / 20 / 3 / 4;
        const BigFloat logarithm = log(4 / delta);
        const BigFloat width = sqrt(2 * variance * logarithm / 128) +
                               BigFloat(7) * (bound.upper - bound.lower) * logarithm / (3 * 127);
        const BigFloat lower = std::max(BigFloat(bound.lower), BigFloat(mean - width));
        const BigFloat upper = std::min(BigFloat(bound.upper), BigFloat(mean + width));
        assert(BigFloat(estimate.lower) <= lower);
        assert(BigFloat(estimate.upper) >= upper);
        assert(estimate.value->total == mean.convert_to<double>());
    }
}
struct TinyState {
    std::uint8_t id;
};
static AnalysisState tiny_state(std::uint8_t id) {
    static const std::array<std::uint64_t, 8> progress{0, 1, 2, 4, 5, 5, 7, 7};
    return {std::make_shared<TinyState>(TinyState{id}), {id}, progress.at(id), sizeof(TinyState)};
}
static void probability_dag() {
    auto visits = std::make_shared<std::array<unsigned, 8>>();
    StateKernel states;
    states.initial = [] { return tiny_state(0); };
    states.evaluate = [visits](const AnalysisState &state) -> KernelEvaluation {
        const auto id = std::static_pointer_cast<const TinyState>(state.opaque)->id;
        ++visits->at(id);
        if (id == 0)
            return {{1, 0, 3}, {}};
        if (id == 1 || id == 2)
            return {{1}, {}};
        if (id == 3)
            return {{2, 1}, {}};
        return {{},
                {score(id == 4 ? 10 : 40, id == 4 ? 20 : 80), score(id == 4 ? 70 : 10, id == 4 ? 20 : 80)}};
    };
    states.transition = [](const AnalysisState &state, std::uint32_t choice) {
        const auto id = std::static_pointer_cast<const TinyState>(state.opaque)->id;
        if (id == 0) {
            assert(choice != 1);
            return tiny_state(choice == 0 ? 1 : 2);
        }
        if (id == 1 || id == 2)
            return tiny_state(3);
        if (id == 3)
            return tiny_state(choice ? 5 : 4);
        throw std::logic_error("terminal transition");
    };
    // The two root arms reach exactly the same state at progress 4. Their
    // probabilities 1/4 and 3/4 must merge before this state is evaluated.
    AnalysisOptions options;
    options.enable_sampling = false;
    Analyzer analyzer({}, {{0, 100}, {0, 100}}, options, {}, {}, states);
    analyzer.advance({10000, 4});
    auto partial = analyzer.snapshot();
    assert(partial.probability_dag && !partial.complete);
    assert(partial.explored_probability.numerator == "0");
    analyzer.cancel();
    analyzer.advance();
    assert(analyzer.snapshot().cancelled);
    analyzer.resume();
    for (unsigned step = 0; step < 100 && !analyzer.snapshot().complete; ++step)
        analyzer.advance({10000, 1});
    const auto result = analyzer.snapshot();
    assert(result.complete && result.merged_states == 1 && result.enumerated_leaves == 2);
    assert(result.explored_probability.numerator == "1" && result.explored_probability.denominator == "1");
    for (unsigned id = 0; id != 6; ++id)
        assert(visits->at(id) == 1);
    assert(result.ranks[0].expectation.exact->total.numerator == "20");
    assert(result.ranks[1].expectation.exact->total.numerator == "50");
    assert(result.ranks[0].expectation.exact->solo.numerator == "40");
    assert(result.ranks[0].expectation.exact->contributions[0].numerator == "19");
    assert(result.ranks[0].maximum.witness->total == 40 && result.ranks[1].maximum.witness->total == 70);
    assert((result.ranks[0].maximum.choices == std::vector<std::uint32_t>{0, 0, 1}));
    Analyzer uninterrupted({}, {{0, 100}, {0, 100}}, options, {}, {}, states);
    Analyzer sliced({}, {{0, 100}, {0, 100}}, options, {}, {}, states);
    uninterrupted.advance({10000, 7});
    for (unsigned step = 0; step != 7; ++step)
        sliced.advance({10000, 1});
    const auto full_partial = uninterrupted.snapshot(), sliced_partial = sliced.snapshot();
    assert(full_partial.pending_nodes == sliced_partial.pending_nodes);
    assert(full_partial.merged_states == sliced_partial.merged_states);
    assert(full_partial.evaluations == sliced_partial.evaluations);
    assert(full_partial.explored_probability.numerator == sliced_partial.explored_probability.numerator);
    // Compare against exhaustive prefix enumeration of the same distribution.
    auto tree = [](auto choices) -> KernelEvaluation {
        if (choices.empty())
            return {{1, 0, 3}, {}};
        if (choices.size() == 1)
            return {{1}, {}};
        if (choices.size() == 2)
            return {{2, 1}, {}};
        return {{},
                {score(choices[2] ? 40 : 10, choices[2] ? 80 : 20),
                 score(choices[2] ? 10 : 70, choices[2] ? 80 : 20)}};
    };
    Analyzer exhaustive(tree, {{0, 100}, {0, 100}}, options);
    exhaustive.advance({10000, 1000});
    const auto expected = exhaustive.snapshot();
    assert(expected.enumerated_leaves == 4);
    for (unsigned rank = 0; rank != 2; ++rank) {
        assert(result.ranks[rank].expectation.exact->total.numerator ==
               expected.ranks[rank].expectation.exact->total.numerator);
        assert(result.ranks[rank].expectation.exact->total.denominator ==
               expected.ranks[rank].expectation.exact->total.denominator);
        assert(result.ranks[rank].maximum.upper == expected.ranks[rank].maximum.upper);
        assert(result.ranks[rank].minimum.lower == expected.ranks[rank].minimum.lower);
    }
}
static void dag_failure_and_memory() {
    AnalysisOptions options;
    options.enable_sampling = false;
    StateKernel state;
    state.initial = [] { return tiny_state(0); };
    state.evaluate = [](const AnalysisState &) -> KernelEvaluation { return {{1, 1}, {}}; };
    state.transition = [](const AnalysisState &, std::uint32_t) { return tiny_state(0); };
    Analyzer cyclic({}, {{0, 100}}, options, {}, {}, state);
    try {
        cyclic.advance({10000, 20});
        assert(false);
    } catch (const std::invalid_argument &) {
    }
    assert(cyclic.snapshot().failed);
    state.transition = [](const AnalysisState &, std::uint32_t) {
        auto result = tiny_state(1);
        result.progress = 0;
        return result;
    };
    Analyzer nonprogress({}, {{0, 100}}, options, {}, {}, state);
    try {
        nonprogress.advance({10000, 20});
        assert(false);
    } catch (const std::invalid_argument &) {
    }
    assert(nonprogress.snapshot().failed);
    state.transition = [](const AnalysisState &, std::uint32_t choice) {
        auto result = tiny_state(choice ? 2 : 1);
        if (choice)
            result.retained_bytes = 1024 * 1024;
        return result;
    };
    options.exact_memory_limit_bytes = 16384;
    Analyzer bounded({}, {{0, 100}}, options, {}, {}, state);
    bounded.advance({10000, 20});
    const auto output = bounded.snapshot();
    assert(!output.complete && output.exact_memory_limited && output.pending_nodes == 2);
    assert(output.explored_probability.numerator == "0" && output.stop_reason == "memory_limit");
    assert(output.memory_bytes < options.memory_limit_bytes);
}
static void invalid_inputs() {
    auto rejects = [](AnalysisKernel kernel, std::vector<ScoreBound> bounds = {{0, 100}}) {
        try {
            Analyzer analyzer(std::move(kernel), std::move(bounds));
            analyzer.advance({10000, 100});
        } catch (const std::invalid_argument &) {
            return;
        }
        throw std::logic_error("expected input rejection");
    };
    rejects([](auto) -> KernelEvaluation { return {{0, 0}, {}}; });
    rejects([](auto) -> KernelEvaluation { return {{1}, {score(1, 1)}}; });
    rejects([](auto) -> KernelEvaluation { return {{}, {}}; });
    rejects([](auto) -> KernelEvaluation { return {{}, {score(101, 0)}}; });
    rejects([](auto) -> KernelEvaluation { return {{UINT64_MAX, 1}, {}}; });
    rejects(small_tree, {{-1, 100}});
    rejects([](auto choices) -> KernelEvaluation {
        if (choices.empty())
            return {{1, 1}, {}};
        auto result = score(50, 0);
        if (choices[0])
            result.contributions.push_back(0);
        return {{}, {result}};
    });
}
static void tiny_tree_exhaustive() {
    // Uneven conditional probabilities, repeated totals and changing optimal
    // rank paths are checked against a separately accumulated rational result.
    for (std::uint32_t root_weight = 1; root_weight <= 7; ++root_weight) {
        Analyzer analyzer(
            [root_weight](auto choices) -> KernelEvaluation {
                if (choices.empty())
                    return {{root_weight, 8 - root_weight}, {}};
                if (choices.size() == 1)
                    return {{1, 2, 3}, {}};
                const auto total = 10 * choices[0] + choices[1];
                return {{}, {score(total, total * 2)}};
            },
            {{0, 20}});
        analyzer.advance({10000, 1000});
        const auto output = analyzer.snapshot();
        assert(output.complete && output.enumerated_leaves == 6);
        assert(output.ranks[0].minimum.lower == 0 && output.ranks[0].maximum.upper == 12);
        const double expected = (8 - root_weight) * 10. / 8. + 4. / 3.;
        assert(std::abs(output.ranks[0].expectation.value->total - expected) < 1e-12);
    }
}
int main() {
    exact_tree();
    resumable();
    fraction();
    sampled_memory_bound();
    prefix_sampling_resume();
    proven_prefix_bounds();
    failed_session();
    certified_confidence_arithmetic();
    probability_dag();
    dag_failure_and_memory();
    invalid_inputs();
    tiny_tree_exhaustive();
    std::cout << "analysis tests passed\n";
}
