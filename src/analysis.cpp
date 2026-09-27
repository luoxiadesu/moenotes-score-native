#include "analysis.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <boost/multiprecision/cpp_int.hpp>
#include <chrono>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>

namespace mns {
namespace {
using boost::multiprecision::cpp_int;
using boost::multiprecision::cpp_rational;
constexpr std::size_t allocation_allowance = 64;

RationalValue as_value(const cpp_rational &value) {
    return {numerator(value).str(), denominator(value).str()};
}
std::size_t rational_bytes(const cpp_rational &value) {
    // Access the original backend: numerator(value) returns a copy and could
    // hide spare allocated capacity in a long-lived accumulator.
    return (value.backend().num().capacity() + value.backend().denom().capacity()) *
               sizeof(boost::multiprecision::limb_type) +
           2 * allocation_allowance;
}
std::size_t rational_bits(const cpp_rational &value) {
    const cpp_int n = numerator(value), d = denominator(value);
    return (n == 0 ? 1 : boost::multiprecision::msb(n < 0 ? -n : n) + 1) +
           (d == 0 ? 1 : boost::multiprecision::msb(d) + 1);
}
std::size_t path_bytes(const PathScore &value) {
    return value.contributions.capacity() * sizeof(std::int64_t) + allocation_allowance;
}
std::uint64_t weight_sum(const std::vector<std::uint64_t> &weights) {
    if (weights.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("too many decision outcomes");
    std::uint64_t sum = 0;
    for (const auto weight : weights) {
        if (std::numeric_limits<std::uint64_t>::max() - sum < weight)
            throw std::invalid_argument("decision weight sum overflows uint64");
        sum += weight;
    }
    if (!sum)
        throw std::invalid_argument("decision has no positive probability outcome");
    return sum;
}
// Rejection eliminates modulo bias. Avoid std::uniform_int_distribution so the
// sample stream is identical across conforming standard library implementations.
std::uint64_t bounded_random(std::mt19937_64 &random, std::uint64_t bound) {
    const auto threshold = static_cast<std::uint64_t>(-bound) % bound;
    for (;;) {
        const auto value = random();
        if (value >= threshold)
            return value % bound;
    }
}
std::uint32_t draw(std::mt19937_64 &random, const std::vector<std::uint64_t> &weights) {
    auto value = bounded_random(random, weight_sum(weights));
    for (std::size_t index = 0; index < weights.size(); ++index) {
        if (value < weights[index])
            return static_cast<std::uint32_t>(index);
        value -= weights[index];
    }
    throw std::logic_error("invalid probability table");
}
struct ExactAccumulator {
    cpp_rational total = 0, solo = 0;
    std::array<cpp_rational, 3> sections{};
    std::vector<cpp_rational> contributions;
    std::size_t bytes() const {
        std::size_t result = sizeof(*this) + contributions.capacity() * sizeof(cpp_rational);
        result += rational_bytes(total) + rational_bytes(solo);
        for (const auto &value : sections)
            result += rational_bytes(value);
        for (const auto &value : contributions)
            result += rational_bytes(value);
        return result;
    }
    void add(const PathScore &value, const cpp_rational &probability) {
        total += value.total * probability;
        solo += value.solo * probability;
        for (std::size_t i = 0; i != 3; ++i)
            sections[i] += value.sections[i] * probability;
        if (contributions.empty())
            contributions.resize(value.contributions.size());
        for (std::size_t i = 0; i != contributions.size(); ++i)
            contributions[i] += value.contributions[i] * probability;
    }
    ExactMeanScore exact() const {
        ExactMeanScore result;
        result.total = as_value(total);
        result.solo = as_value(solo);
        for (std::size_t i = 0; i != 3; ++i)
            result.sections[i] = as_value(sections[i]);
        for (const auto &value : contributions)
            result.contributions.push_back(as_value(value));
        return result;
    }
    MeanScore mean() const {
        MeanScore result;
        result.total = total.convert_to<double>();
        result.solo = solo.convert_to<double>();
        for (std::size_t i = 0; i != 3; ++i)
            result.sections[i] = sections[i].convert_to<double>();
        for (const auto &value : contributions)
            result.contributions.push_back(value.convert_to<double>());
        return result;
    }
};
// Convert IEEE-754 binary64 to its exact rational value. No decimal parser or
// implementation-specific floating-to-rational conversion enters a proof.
cpp_rational exact_double(double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const auto exponent = unsigned((bits >> 52) & 0x7ff);
    if (exponent == 0x7ff)
        throw std::logic_error("nonfinite interval operand");
    cpp_int significand = bits & ((std::uint64_t{1} << 52) - 1);
    if (exponent)
        significand += cpp_int(1) << 52;
    if (bits >> 63)
        significand = -significand;
    const int shift = exponent ? int(exponent) - 1075 : -1074;
    if (shift >= 0)
        return cpp_rational(significand << shift);
    return cpp_rational(significand, cpp_int(1) << -shift);
}
double directed_double(const cpp_rational &value, bool upper) {
    auto result = value.convert_to<double>();
    if (!std::isfinite(result))
        throw std::logic_error("interval conversion overflow");
    while (upper ? exact_double(result) < value : exact_double(result) > value)
        result = std::nextafter(result, upper ? std::numeric_limits<double>::infinity()
                                              : -std::numeric_limits<double>::infinity());
    return result;
}
cpp_rational logarithm_upper(const cpp_rational &value) {
    if (value < 1)
        throw std::logic_error("logarithm interval requires argument >=1");
    // ln(2)=2*atanh(1/3). After terms k=0..31, the omitted tail is at most
    // 2*(1/3)^65 / (65*(1-1/9)). All operations and the bound are rational.
    static const cpp_rational ln2_upper = [] {
        cpp_rational sum = 0, power(1, 3);
        for (unsigned k = 0; k != 32; ++k) {
            sum += 2 * power / (2 * k + 1);
            power /= 9;
        }
        return cpp_rational(sum + 2 * power * 9 / (65 * 8));
    }();
    // Exact powers of two replace a platform libm logarithm. The bound loses
    // less than ln(2), trading a little interval width for a numerical proof.
    cpp_int power = 1;
    unsigned exponent = 0;
    while (cpp_rational(power) < value) {
        power <<= 1;
        ++exponent;
    }
    return exponent * ln2_upper;
}
cpp_rational square_root_upper(const cpp_rational &value) {
    if (value < 0)
        throw std::logic_error("negative variance interval");
    if (value == 0)
        return 0;
    auto candidate = std::sqrt(directed_double(value, true));
    if (!std::isfinite(candidate) || candidate < 0)
        throw std::logic_error("invalid square root candidate");
    // libm supplies only a candidate. Exact rational squaring certifies the
    // returned upper bound regardless of its rounding error.
    while (exact_double(candidate) * exact_double(candidate) < value)
        candidate = std::nextafter(candidate, std::numeric_limits<double>::infinity());
    return exact_double(candidate);
}
struct SampleAccumulator {
    cpp_int sum = 0, sum_squares = 0;
    std::uint64_t count = 0;
    double solo = 0;
    std::array<double, 3> sections{};
    std::vector<double> contributions;
    void add(const PathScore &value, std::uint64_t sample_count) {
        count = sample_count;
        sum += value.total;
        sum_squares += cpp_int(value.total) * value.total;
        solo += (value.solo - solo) / count;
        for (std::size_t i = 0; i != 3; ++i)
            sections[i] += (value.sections[i] - sections[i]) / count;
        if (contributions.empty())
            contributions.resize(value.contributions.size());
        for (std::size_t i = 0; i != contributions.size(); ++i)
            contributions[i] += (value.contributions[i] - contributions[i]) / count;
    }
    cpp_rational total_mean() const {
        return cpp_rational(sum, cpp_int(count));
    }
    cpp_rational total_variance() const {
        return cpp_rational(cpp_int(count) * sum_squares - sum * sum, cpp_int(count) * (count - 1));
    }
    std::size_t bytes() const {
        return (sum.backend().capacity() + sum_squares.backend().capacity()) *
                   sizeof(boost::multiprecision::limb_type) +
               contributions.capacity() * sizeof(double) + 3 * allocation_allowance;
    }
    MeanScore mean() const {
        MeanScore result;
        result.total = total_mean().convert_to<double>();
        result.solo = solo;
        for (std::size_t i = 0; i != 3; ++i)
            result.sections[i] = sections[i];
        for (const auto value : contributions)
            result.contributions.push_back(value);
        return result;
    }
};
struct PendingNode {
    std::vector<std::uint32_t> choices;
    cpp_rational probability = 1;
    std::vector<ScoreBound> bounds;
    std::size_t bytes() const {
        return choices.capacity() * sizeof(std::uint32_t) + rational_bytes(probability) +
               bounds.capacity() * sizeof(ScoreBound) + 2 * allocation_allowance;
    }
};
struct DagIdentity {
    std::uint64_t progress;
    std::vector<std::uint8_t> key;
    bool operator<(const DagIdentity &other) const {
        return progress < other.progress || (progress == other.progress && key < other.key);
    }
};
struct DagNode {
    AnalysisState state;
    cpp_rational probability = 1;
    std::vector<std::uint32_t> choices;
    std::optional<KernelEvaluation> evaluation;
    std::size_t branch_cursor = 0;
    std::size_t max_depth = 0;
    std::size_t bytes() const {
        std::size_t result = sizeof(DagNode) + sizeof(DagIdentity) + 6 * allocation_allowance +
                             state.retained_bytes + 2 * state.key.capacity() * sizeof(std::uint8_t) +
                             rational_bytes(probability) + choices.capacity() * sizeof(std::uint32_t);
        if (evaluation) {
            result += evaluation->decision_weights.capacity() * sizeof(std::uint64_t) +
                      evaluation->scores.capacity() * sizeof(PathScore);
            for (const auto &score : evaluation->scores)
                result += path_bytes(score);
        }
        return result;
    }
};
} // namespace

struct Analyzer::Impl {
    AnalysisKernel kernel;
    SampleKernel sampler;
    NodeBounder bounder;
    StateKernel state_kernel;
    std::map<DagIdentity, DagNode> dag;
    std::uint64_t merged_states = 0;
    std::vector<ScoreBound> bounds;
    AnalysisOptions options;
    std::vector<PendingNode> pending;
    std::vector<ExactAccumulator> exact;
    cpp_rational mass = 0;
    std::vector<SampleAccumulator> sample;
    std::vector<RankAnalysis> result;
    std::vector<std::uint32_t> sample_choices;
    std::vector<std::size_t> contribution_sizes;
    bool observed_shape = false;
    std::mt19937_64 random;
    std::atomic<bool> cancelled{false};
    bool complete = false, exact_memory_limited = false, memory_limited = false, failed = false;
    std::string error;
    std::uint64_t evaluations = 0, leaves = 0, samples = 0, scheduler = 0;
    std::uint64_t next_checkpoint = 32, checkpoint = 0;
    std::string stop_reason = "ready";

    Impl(AnalysisKernel callback, std::vector<ScoreBound> limits, AnalysisOptions opts,
         SampleKernel sampling_callback, NodeBounder bound_callback, StateKernel states)
        : kernel(std::move(callback)), sampler(std::move(sampling_callback)),
          bounder(std::move(bound_callback)), state_kernel(std::move(states)), bounds(std::move(limits)),
          options(opts), random(opts.seed) {
        const bool has_state_callback =
            bool(state_kernel.initial) || bool(state_kernel.evaluate) || bool(state_kernel.transition);
        if (has_state_callback &&
            (!state_kernel.initial || !state_kernel.evaluate || !state_kernel.transition))
            throw std::invalid_argument("state kernel needs initial, evaluate and transition callbacks");
        if ((!kernel && !has_state_callback) || bounds.empty())
            throw std::invalid_argument("analysis needs a kernel and rank bounds");
        if (has_state_callback && bounder)
            throw std::invalid_argument("prefix bound callback is not supported with a state kernel");
        if (has_state_callback && !kernel && options.enable_sampling && !sampler)
            throw std::invalid_argument(
                "state-only analysis requires a complete-path sampler or disabled sampling");
        for (const auto &bound : bounds) {
            if (bound.lower < 0 || bound.upper < bound.lower ||
                bound.upper > std::numeric_limits<std::int32_t>::max())
                throw std::invalid_argument("score bounds must be an ordered nonnegative int32 interval");
        }
        if (options.memory_limit_bytes < 16384 || options.exact_memory_limit_bytes < 4096 ||
            options.exact_memory_limit_bytes > options.memory_limit_bytes || !options.max_decisions ||
            options.min_samples < 2 || !std::isfinite(options.confidence) || options.confidence <= 0 ||
            options.confidence >= 1 || !std::isfinite(options.relative_error) || options.relative_error <= 0)
            throw std::invalid_argument("invalid analysis options");
        exact.resize(bounds.size());
        sample.resize(bounds.size());
        result.resize(bounds.size());
        contribution_sizes.resize(bounds.size());
        next_checkpoint = options.min_samples;
        if (state_kernel.initial) {
            DagNode root;
            root.state = state_kernel.initial();
            if (!root.state.opaque || root.state.key.empty())
                throw std::invalid_argument("state kernel returned an empty state/key");
            DagIdentity identity{root.state.progress, root.state.key};
            dag.emplace(std::move(identity), std::move(root));
        } else
            pending.emplace_back();
        if (bounder) {
            pending.back().bounds = checked_bounds({}, bounds);
            bounds = pending.back().bounds;
        }
        for (std::size_t i = 0; i != bounds.size(); ++i) {
            result[i].minimum.lower = result[i].maximum.lower = bounds[i].lower;
            result[i].minimum.upper = result[i].maximum.upper = bounds[i].upper;
            result[i].expectation.lower = static_cast<double>(bounds[i].lower);
            result[i].expectation.upper = static_cast<double>(bounds[i].upper);
        }
        if (bytes() > options.memory_limit_bytes)
            throw std::invalid_argument("analysis memory budget too small for initial state");
        if (exact_bytes() > options.exact_memory_limit_bytes)
            exact_memory_limited = true;
    }

    std::vector<ScoreBound> checked_bounds(std::span<const std::uint32_t> choices,
                                           const std::vector<ScoreBound> &parent) const {
        auto output = bounder(choices);
        if (output.size() != bounds.size())
            throw std::invalid_argument("node bound rank count changed");
        for (std::size_t i = 0; i != output.size(); ++i) {
            if (output[i].lower < parent[i].lower || output[i].upper > parent[i].upper ||
                output[i].lower > output[i].upper)
                throw std::invalid_argument("node bound is not nested inside its proven parent bound");
        }
        return output;
    }

    std::size_t exact_bytes() const {
        std::size_t size = pending.capacity() * sizeof(PendingNode) +
                           exact.capacity() * sizeof(ExactAccumulator) + rational_bytes(mass);
        for (const auto &node : pending)
            size += node.bytes();
        for (const auto &value : exact)
            size += value.bytes();
        for (const auto &entry : dag)
            size += entry.second.bytes();
        return size + 3 * allocation_allowance;
    }
    std::size_t bytes() const {
        std::size_t size = sizeof(*this) + exact_bytes() + bounds.capacity() * sizeof(ScoreBound) +
                           sample.capacity() * sizeof(SampleAccumulator) +
                           result.capacity() * sizeof(RankAnalysis) +
                           sample_choices.capacity() * sizeof(std::uint32_t) +
                           contribution_sizes.capacity() * sizeof(std::size_t) + 8 * allocation_allowance;
        for (const auto &value : sample)
            size += value.bytes();
        for (const auto &rank : result) {
            for (const auto *extreme : {&rank.minimum, &rank.maximum}) {
                size += extreme->choices.capacity() * sizeof(std::uint32_t) + allocation_allowance;
                if (extreme->witness)
                    size += path_bytes(*extreme->witness);
            }
            if (rank.expectation.value)
                size +=
                    rank.expectation.value->contributions.capacity() * sizeof(double) + allocation_allowance;
        }
        return size;
    }
    void validate(const KernelEvaluation &value, const std::vector<std::uint32_t> &choices) {
        if (value.decision_weights.empty() == value.scores.empty())
            throw std::invalid_argument("kernel must return either a decision or complete scores");
        if (!value.decision_weights.empty()) {
            weight_sum(value.decision_weights);
            if (choices.size() >= options.max_decisions)
                throw std::invalid_argument("random path exceeds max_decisions");
            return;
        }
        if (value.scores.size() != bounds.size())
            throw std::invalid_argument("kernel rank result count changed");
        for (std::size_t i = 0; i != bounds.size(); ++i) {
            const auto &score = value.scores[i];
            if (score.total < bounds[i].lower || score.total > bounds[i].upper)
                throw std::invalid_argument("kernel total violates proven score bounds");
            if (score.solo < 0 || score.solo > std::numeric_limits<std::int32_t>::max())
                throw std::invalid_argument("kernel solo score is outside nonnegative int32");
            for (const auto section : score.sections)
                if (section < 0 || section > std::numeric_limits<std::int32_t>::max())
                    throw std::invalid_argument("kernel section score is outside nonnegative int32");
            if (observed_shape && score.contributions.size() != contribution_sizes[i])
                throw std::invalid_argument("kernel contribution result shape changed");
        }
        if (!observed_shape) {
            for (std::size_t i = 0; i != bounds.size(); ++i)
                contribution_sizes[i] = value.scores[i].contributions.size();
            observed_shape = true;
        }
    }
    std::size_t leaf_extra_bytes(const KernelEvaluation &value, std::size_t depth) const {
        std::size_t count = 0;
        for (const auto &score : value.scores)
            count += score.contributions.size();
        // Both witnesses, sample moments, public sample means, and exact sums.
        return 2 * bounds.size() * depth * sizeof(std::uint32_t) +
               count *
                   (2 * sizeof(std::int64_t) + sizeof(double) + sizeof(double) + sizeof(cpp_rational) + 256) +
               bounds.size() * 1024;
    }
    void witnesses(const std::vector<PathScore> &scores, const std::vector<std::uint32_t> &choices) {
        for (std::size_t i = 0; i != bounds.size(); ++i) {
            auto &rank = result[i];
            auto &minimum = rank.minimum;
            auto &maximum = rank.maximum;
            if (!minimum.witness || scores[i].total < minimum.witness->total) {
                minimum.witness = scores[i];
                minimum.choices = choices;
                minimum.upper = scores[i].total;
            }
            if (!maximum.witness || scores[i].total > maximum.witness->total) {
                maximum.witness = scores[i];
                maximum.choices = choices;
                maximum.lower = scores[i].total;
            }
            minimum.status = minimum.lower == minimum.upper ? "exact" : "bounded";
            maximum.status = maximum.lower == maximum.upper ? "exact" : "bounded";
        }
    }
    bool accumulate_leaf(const KernelEvaluation &evaluation, const std::vector<std::uint32_t> &choices,
                         const cpp_rational &probability) {
        const auto extra = leaf_extra_bytes(evaluation, choices.size());
        // Sum denominators may grow: each rational addition has at most the
        // combined operand bit lengths plus 65 numerator bits for int64.
        const auto growth =
            (rational_bits(probability) / 8 + 32) *
            (bounds.size() * 5 + 1 +
             std::accumulate(contribution_sizes.begin(), contribution_sizes.end(), std::size_t{0}));
        if (extra + growth > options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes) ||
            extra + growth > options.exact_memory_limit_bytes -
                                 std::min(exact_bytes(), options.exact_memory_limit_bytes)) {
            exact_memory_limited = true;
            return false;
        }
        auto next_exact = exact;
        cpp_rational next_mass = mass + probability;
        std::size_t retained_growth = 0;
        for (std::size_t i = 0; i != bounds.size(); ++i) {
            next_exact[i].add(evaluation.scores[i], probability);
            retained_growth +=
                next_exact[i].bytes() > exact[i].bytes() ? next_exact[i].bytes() - exact[i].bytes() : 0;
        }
        retained_growth += rational_bytes(next_mass) > rational_bytes(mass)
                               ? rational_bytes(next_mass) - rational_bytes(mass)
                               : 0;
        if (retained_growth + extra >
                options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes) ||
            retained_growth > options.exact_memory_limit_bytes -
                                  std::min(exact_bytes(), options.exact_memory_limit_bytes)) {
            exact_memory_limited = true;
            return false;
        }
        witnesses(evaluation.scores, choices);
        exact.swap(next_exact);
        mass.swap(next_mass);
        ++leaves;
        return true;
    }
    void mark_complete() {
        if (mass != 1)
            throw std::logic_error("exhaustive search did not cover probability one");
        complete = true;
        for (auto &rank : result) {
            rank.minimum.lower = rank.minimum.upper;
            rank.maximum.upper = rank.maximum.lower;
            rank.minimum.status = rank.maximum.status = "exact";
        }
    }
    bool enumerate_dag() {
        if (dag.empty() || exact_memory_limited)
            return false;
        // Strictly increasing kernel progress defines a topological order.
        // Every predecessor is expanded before a state's first evaluation, so
        // its probability mass is final and its conditional result is computed
        // only once. Full key bytes, not a hash, decide state equivalence.
        auto current = dag.begin();
        auto &node = current->second;
        if (!node.evaluation) {
            auto evaluation = state_kernel.evaluate(node.state);
            ++evaluations;
            validate(evaluation, node.choices);
            if (!evaluation.decision_weights.empty() && node.max_depth >= options.max_decisions)
                throw std::invalid_argument("random DAG path exceeds max_decisions");
            std::size_t added = evaluation.decision_weights.capacity() * sizeof(std::uint64_t) +
                                evaluation.scores.capacity() * sizeof(PathScore) + 2 * allocation_allowance;
            for (const auto &score : evaluation.scores)
                added += path_bytes(score);
            if (added > options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes) ||
                added > options.exact_memory_limit_bytes -
                            std::min(exact_bytes(), options.exact_memory_limit_bytes)) {
                exact_memory_limited = true;
                return true;
            }
            node.evaluation = std::move(evaluation);
            if (node.evaluation->decision_weights.empty()) {
                if (accumulate_leaf(*node.evaluation, node.choices, node.probability)) {
                    dag.erase(current);
                    if (dag.empty())
                        mark_complete();
                }
            }
            return true;
        }
        const auto &weights = node.evaluation->decision_weights;
        if (weights.empty())
            throw std::logic_error("unprocessed terminal DAG state");
        while (node.branch_cursor < weights.size() && !weights[node.branch_cursor])
            ++node.branch_cursor;
        if (node.branch_cursor == weights.size())
            throw std::logic_error("DAG branch cursor out of range");
        const auto choice = static_cast<std::uint32_t>(node.branch_cursor);
        auto child_state = state_kernel.transition(node.state, choice);
        ++evaluations;
        if (!child_state.opaque || child_state.key.empty())
            throw std::invalid_argument("state kernel returned an empty state/key");
        if (child_state.progress <= node.state.progress)
            throw std::invalid_argument("DAG progress must strictly increase");
        cpp_rational branch_probability =
            node.probability * cpp_rational(cpp_int(weights[choice]), cpp_int(weight_sum(weights)));
        DagIdentity identity{child_state.progress, child_state.key};
        auto found = dag.find(identity);
        if (found == dag.end()) {
            DagNode child;
            child.state = std::move(child_state);
            child.probability = std::move(branch_probability);
            child.choices = node.choices;
            child.choices.push_back(choice);
            child.max_depth = node.max_depth + 1;
            const auto added = child.bytes();
            if (added > options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes) ||
                added > options.exact_memory_limit_bytes -
                            std::min(exact_bytes(), options.exact_memory_limit_bytes)) {
                exact_memory_limited = true;
                return true;
            }
            dag.emplace(std::move(identity), std::move(child));
        } else {
            if (found->second.evaluation)
                throw std::logic_error("incoming mass reached an already evaluated DAG state");
            cpp_rational merged = found->second.probability + branch_probability;
            const auto old_bytes = rational_bytes(found->second.probability);
            const auto new_bytes = rational_bytes(merged);
            const auto added = new_bytes > old_bytes ? new_bytes - old_bytes : 0;
            if (added > options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes) ||
                added > options.exact_memory_limit_bytes -
                            std::min(exact_bytes(), options.exact_memory_limit_bytes)) {
                exact_memory_limited = true;
                return true;
            }
            found->second.probability.swap(merged);
            found->second.max_depth = std::max(found->second.max_depth, node.max_depth + 1);
            ++merged_states;
        }
        // Cursor advances only after the entire new mass was committed. On a
        // budget/memory stop the original unexpanded branch remains available.
        ++node.branch_cursor;
        while (node.branch_cursor < weights.size() && !weights[node.branch_cursor])
            ++node.branch_cursor;
        if (node.branch_cursor == weights.size())
            dag.erase(current);
        return true;
    }
    bool enumerate() {
        if (state_kernel.initial)
            return enumerate_dag();
        if (pending.empty() || exact_memory_limited)
            return false;
        auto evaluation = kernel(pending.back().choices);
        ++evaluations;
        validate(evaluation, pending.back().choices);
        const auto &node = pending.back();
        if (!evaluation.decision_weights.empty()) {
            const auto total_weight = weight_sum(evaluation.decision_weights);
            const auto child_count = static_cast<std::size_t>(
                std::count_if(evaluation.decision_weights.begin(), evaluation.decision_weights.end(),
                              [](auto value) { return value != 0; }));
            const auto depth = node.choices.size() + 1;
            const auto child_bytes = sizeof(PendingNode) + depth * sizeof(std::uint32_t) +
                                     rational_bytes(node.probability) * 2 + 256 +
                                     (bounder ? bounds.size() * sizeof(ScoreBound) : 0);
            const auto free_exact =
                options.exact_memory_limit_bytes - std::min(exact_bytes(), options.exact_memory_limit_bytes);
            const auto free_all = options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes);
            if (child_count > free_exact / child_bytes || child_count > free_all / child_bytes) {
                exact_memory_limited = true;
                return true;
            }
            std::vector<PendingNode> children;
            children.reserve(child_count);
            // Construct transactionally: neither an allocation failure nor an
            // underestimated limb capacity may lose unresolved probability mass.
            for (std::size_t i = evaluation.decision_weights.size(); i-- > 0;) {
                const auto weight = evaluation.decision_weights[i];
                if (!weight)
                    continue;
                PendingNode child;
                child.choices.reserve(depth);
                child.choices.assign(node.choices.begin(), node.choices.end());
                child.choices.push_back(static_cast<std::uint32_t>(i));
                child.probability = node.probability * cpp_rational(cpp_int(weight), cpp_int(total_weight));
                if (bounder)
                    child.bounds = checked_bounds(child.choices, node.bounds);
                children.push_back(std::move(child));
            }
            std::size_t added = child_count * sizeof(PendingNode);
            for (const auto &child : children)
                added += child.bytes();
            if (added > free_exact || added > free_all) {
                exact_memory_limited = true;
                return true;
            }
            // reserve can throw; do it before removing the original node.
            pending.reserve(pending.size() - 1 + child_count);
            pending.pop_back();
            for (auto &child : children)
                pending.push_back(std::move(child));
        } else {
            if (bounder) {
                for (std::size_t i = 0; i != bounds.size(); ++i)
                    if (evaluation.scores[i].total < node.bounds[i].lower ||
                        evaluation.scores[i].total > node.bounds[i].upper)
                        throw std::invalid_argument("leaf score violates a proven prefix bound");
            }
            if (accumulate_leaf(evaluation, node.choices, node.probability)) {
                pending.pop_back();
                if (pending.empty())
                    mark_complete();
            }
        }
        return true;
    }
    void confidence_checkpoint() {
        ++checkpoint;
        // Sum_{k>=1} 1/(k(k+1)) = 1. Union over rank outputs and all
        // predetermined checkpoints therefore spends at most 1-confidence.
        const cpp_rational confidence =
            options.confidence == 0.95 ? cpp_rational(95, 100) : exact_double(options.confidence);
        const cpp_rational failure =
            (1 - confidence) / bounds.size() / cpp_int(checkpoint) / (cpp_int(checkpoint) + 1);
        const cpp_rational log_term = logarithm_upper(4 / failure);
        for (std::size_t i = 0; i != bounds.size(); ++i) {
            auto &output = result[i].expectation;
            const auto &stats = sample[i];
            const cpp_rational mean = stats.total_mean();
            const cpp_rational variance = stats.total_variance();
            const cpp_rational range = bounds[i].upper - bounds[i].lower;
            const cpp_rational width = square_root_upper(2 * variance * log_term / cpp_int(samples)) +
                                       7 * range * log_term / (3 * cpp_int(samples - 1));
            const cpp_rational lower = mean - width;
            const cpp_rational upper = mean + width;
            output.lower =
                directed_double(lower < bounds[i].lower ? cpp_rational(bounds[i].lower) : lower, false);
            output.upper =
                directed_double(upper > bounds[i].upper ? cpp_rational(bounds[i].upper) : upper, true);
            output.value = stats.mean();
            output.status = "estimated";
            output.confidence_samples = samples;
            const cpp_rational relative_error = options.relative_error == 0.001
                                                    ? cpp_rational(1, 1000)
                                                    : exact_double(options.relative_error);
            const cpp_rational target = std::max(cpp_rational(1), cpp_rational(abs(mean) * relative_error));
            output.precision_reached =
                (exact_double(output.upper) - exact_double(output.lower)) / 2 <= target;
        }
        next_checkpoint = samples > std::numeric_limits<std::uint64_t>::max() / 2
                              ? std::numeric_limits<std::uint64_t>::max()
                              : samples * 2;
    }
    bool sample_step() {
        if (!options.enable_sampling || memory_limited)
            return false;
        KernelEvaluation evaluation;
        if (sampler) {
            auto path = sampler(samples);
            if (path.choices.size() > options.max_decisions)
                throw std::invalid_argument("sample path exceeds max_decisions");
            if (path.choices.capacity() * sizeof(std::uint32_t) >
                options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes)) {
                ++evaluations;
                memory_limited = true;
                return true;
            }
            sample_choices = std::move(path.choices);
            evaluation.scores = std::move(path.scores);
        } else
            evaluation = kernel(sample_choices);
        ++evaluations;
        validate(evaluation, sample_choices);
        if (!evaluation.decision_weights.empty()) {
            if (bytes() + (sample_choices.capacity() + 1) * sizeof(std::uint32_t) + allocation_allowance >
                options.memory_limit_bytes) {
                memory_limited = true;
                return true;
            }
            sample_choices.push_back(draw(random, evaluation.decision_weights));
        } else {
            if (leaf_extra_bytes(evaluation, sample_choices.size()) >
                options.memory_limit_bytes - std::min(bytes(), options.memory_limit_bytes)) {
                memory_limited = true;
                return true;
            }
            witnesses(evaluation.scores, sample_choices);
            if (samples == UINT64_MAX)
                throw std::overflow_error("sample count exhausted");
            ++samples;
            for (std::size_t i = 0; i != bounds.size(); ++i)
                sample[i].add(evaluation.scores[i], samples);
            sample_choices.clear();
            if (samples == next_checkpoint)
                confidence_checkpoint();
            else if (samples < options.min_samples) {
                for (std::size_t i = 0; i != bounds.size(); ++i) {
                    result[i].expectation.value = sample[i].mean();
                    result[i].expectation.status = "estimated";
                }
            }
        }
        return true;
    }
};

Analyzer::Analyzer(AnalysisKernel kernel, std::vector<ScoreBound> bounds, AnalysisOptions options,
                   SampleKernel sampler, NodeBounder bounder, StateKernel states)
    : impl_(std::make_unique<Impl>(std::move(kernel), std::move(bounds), options, std::move(sampler),
                                   std::move(bounder), std::move(states))) {}
Analyzer::~Analyzer() = default;
Analyzer::Analyzer(Analyzer &&) noexcept = default;
Analyzer &Analyzer::operator=(Analyzer &&) noexcept = default;
void Analyzer::cancel() noexcept {
    impl_->cancelled.store(true, std::memory_order_relaxed);
}
void Analyzer::resume() noexcept {
    impl_->cancelled.store(false, std::memory_order_relaxed);
}
void Analyzer::advance(WorkBudget budget) {
    auto &state = *impl_;
    if (state.failed)
        throw std::runtime_error("analysis session has failed: " + state.error);
    const auto started = std::chrono::steady_clock::now();
    const auto initial_evaluations = state.evaluations;
    try {
        for (;;) {
            if (state.cancelled.load(std::memory_order_relaxed)) {
                state.stop_reason = "cancelled";
                break;
            }
            if (state.complete) {
                state.stop_reason = "exact";
                break;
            }
            if (state.evaluations - initial_evaluations >= budget.max_evaluations) {
                state.stop_reason = "work_budget";
                break;
            }
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                      started)
                    .count() >= static_cast<double>(budget.time_ms)) {
                state.stop_reason = "time_budget";
                break;
            }
            // Fixed deterministic schedule: three enumeration evaluations, then one
            // sampling evaluation. Scheduling is independent of wall-clock slices.
            const bool prefer_sample = (++state.scheduler % 4 == 0);
            bool progressed = prefer_sample ? state.sample_step() : state.enumerate();
            if (!progressed)
                progressed = prefer_sample ? state.enumerate() : state.sample_step();
            if (!progressed) {
                state.stop_reason = "memory_limit";
                break;
            }
        }
    } catch (const std::exception &exception) {
        state.failed = true;
        state.error = exception.what();
        state.stop_reason = "failed";
        throw;
    } catch (...) {
        state.failed = true;
        state.error = "unknown kernel failure";
        state.stop_reason = "failed";
        throw;
    }
}
AnalysisSnapshot Analyzer::snapshot() const {
    const auto &state = *impl_;
    AnalysisSnapshot result;
    result.ranks = state.result;
    result.evaluations = state.evaluations;
    result.enumerated_leaves = state.leaves;
    result.samples = state.samples;
    result.pending_nodes = state.pending.size() + state.dag.size();
    result.probability_dag = bool(state.state_kernel.initial);
    result.merged_states = state.merged_states;
    result.memory_bytes = state.bytes();
    result.explored_probability = as_value(state.mass);
    result.complete = state.complete;
    result.cancelled = state.cancelled.load(std::memory_order_relaxed);
    result.exact_memory_limited = state.exact_memory_limited;
    result.stop_reason = state.stop_reason;
    result.failed = state.failed;
    result.error = state.error;
    if (result.cancelled && !result.complete && !result.failed)
        result.stop_reason = "cancelled";
    if (!state.complete && state.bounder && !state.failed) {
        for (std::size_t i = 0; i != result.ranks.size(); ++i) {
            auto &rank = result.ranks[i];
            auto lower = rank.minimum.witness ? rank.minimum.witness->total : state.bounds[i].upper;
            auto upper = rank.maximum.witness ? rank.maximum.witness->total : state.bounds[i].lower;
            for (const auto &node : state.pending) {
                lower = std::min(lower, node.bounds[i].lower);
                upper = std::max(upper, node.bounds[i].upper);
            }
            rank.minimum.lower = lower;
            rank.maximum.upper = upper;
            if (rank.minimum.witness)
                rank.minimum.status = rank.minimum.lower == rank.minimum.upper ? "exact" : "bounded";
            if (rank.maximum.witness)
                rank.maximum.status = rank.maximum.lower == rank.maximum.upper ? "exact" : "bounded";
        }
    }
    if (state.complete) {
        for (std::size_t i = 0; i != result.ranks.size(); ++i) {
            auto &output = result.ranks[i].expectation;
            output.status = "exact";
            output.exact = state.exact[i].exact();
            output.value = state.exact[i].mean();
            output.lower = directed_double(state.exact[i].total, false);
            output.upper = directed_double(state.exact[i].total, true);
            output.precision_reached = true;
        }
    }
    return result;
}
} // namespace mns
