#ifndef MNS_ANALYSIS_HPP
#define MNS_ANALYSIS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mns {

// A complete legal random path, for one externally specified rank condition.
// Scores are checked against the caller's proven bounds by the analyzer.
struct PathScore {
    std::int64_t total = 0;
    std::int64_t solo = 0;
    std::array<std::int64_t, 3> sections{};
    std::vector<std::int64_t> contributions;
};

// Exactly one member must be nonempty. A decision lists integer probability
// weights (zero-weight outcomes are never explored). A leaf contains one score
// for each rank condition. Replaying the same prefix must be deterministic.
struct KernelEvaluation {
    std::vector<std::uint64_t> decision_weights;
    std::vector<PathScore> scores;
};
using AnalysisKernel = std::function<KernelEvaluation(std::span<const std::uint32_t>)>;
struct SamplePath {
    std::vector<PathScore> scores;
    std::vector<std::uint32_t> choices;
};
// Optional efficient sampler: one complete independent legal path per index.
// The provider owns probability-correct, cross-platform reproducible sampling.
using SampleKernel = std::function<SamplePath(std::uint64_t sample_index)>;

struct ScoreBound {
    std::int64_t lower = 0;
    std::int64_t upper = std::numeric_limits<std::int32_t>::max();
};

// Optional proven bounds for every completion of a prefix. Returned bounds
// must nest within parent bounds and have the same rank count. Returning the
// inherited/global bounds is always valid. Bounds alone cannot remove a node
// needed for exact expectation; the analyzer preserves its probability mass.
using NodeBounder = std::function<std::vector<ScoreBound>(std::span<const std::uint32_t>)>;

// Immutable, copyable kernel state parked at a random decision or terminal.
// key contains every field affecting future outputs AND accumulated scores;
// equal keys at equal progress must denote identical conditional final outputs.
// Raw pointer addresses/padding and approximate floating equality are forbidden.
struct AnalysisState {
    std::shared_ptr<const void> opaque;
    std::vector<std::uint8_t> key;
    std::uint64_t progress = 0;     // Strictly increases on every transition.
    std::size_t retained_bytes = 0; // State allocation, excluding key/this struct.
};
struct StateKernel {
    std::function<AnalysisState()> initial;
    std::function<KernelEvaluation(const AnalysisState &)> evaluate;
    std::function<AnalysisState(const AnalysisState &, std::uint32_t)> transition;
};

struct AnalysisOptions {
    std::uint64_t seed = 0;
    std::size_t memory_limit_bytes = 256ULL * 1024 * 1024;
    std::size_t exact_memory_limit_bytes = 64ULL * 1024 * 1024;
    std::size_t max_decisions = 65536;
    std::uint64_t min_samples = 32;
    double confidence = 0.95;
    double relative_error = 0.001;
    bool enable_sampling = true;
};

struct WorkBudget {
    // A zero budget performs no work. Limits apply to this advance() call.
    std::uint64_t time_ms = 30000;
    std::uint64_t max_evaluations = std::numeric_limits<std::uint64_t>::max();
};

struct RationalValue {
    std::string numerator = "0";
    std::string denominator = "1";
};
struct MeanScore {
    double total = 0;
    double solo = 0;
    std::array<double, 3> sections{};
    std::vector<double> contributions;
};
struct ExactMeanScore {
    RationalValue total;
    RationalValue solo;
    std::array<RationalValue, 3> sections;
    std::vector<RationalValue> contributions;
};

struct ExtremeResult {
    // pending: no witness; bounded: witness and bounds; exact: proof complete.
    std::string status = "pending";
    std::int64_t lower = 0;
    std::int64_t upper = 0;
    std::optional<PathScore> witness;
    std::vector<std::uint32_t> choices;
};
struct ExpectationResult {
    // estimated values use only independent complete samples; enumeration
    // leaves never enter the sample mean. Exact values use all probability mass.
    std::string status = "pending";
    std::optional<MeanScore> value;
    std::optional<ExactMeanScore> exact;
    double lower = 0;
    double upper = 0;
    // Number of samples at the fixed checkpoint supporting the current CI.
    std::uint64_t confidence_samples = 0;
    bool precision_reached = false;
};
struct RankAnalysis {
    ExtremeResult minimum;
    ExtremeResult maximum;
    ExpectationResult expectation;
};
struct AnalysisSnapshot {
    std::vector<RankAnalysis> ranks;
    std::uint64_t evaluations = 0;
    std::uint64_t enumerated_leaves = 0;
    std::uint64_t samples = 0;
    std::size_t pending_nodes = 0;
    std::size_t memory_bytes = 0;
    RationalValue explored_probability;
    bool complete = false;
    bool cancelled = false;
    bool exact_memory_limited = false;
    bool failed = false;
    bool probability_dag = false;
    std::uint64_t merged_states = 0;
    std::string error;
    // ready, time_budget, work_budget, cancelled, memory_limit, failed, or exact.
    std::string stop_reason = "ready";
};

// Single-writer session. cancel() may be called concurrently with advance();
// all other operations require the caller to serialize access. A kernel error
// makes the session terminally failed; snapshot remains available for diagnosis.
// The callback is
// invoked between cancellation checks; it must itself be bounded/cooperative.
// The memory budget covers retained analysis state, not prepared/kernel data,
// allocator overhead outside this session, or the process's resident memory.
class Analyzer {
  public:
    Analyzer(AnalysisKernel kernel, std::vector<ScoreBound> bounds, AnalysisOptions options = {},
             SampleKernel sampler = {}, NodeBounder bounder = {}, StateKernel state_kernel = {});
    ~Analyzer();
    Analyzer(Analyzer &&) noexcept;
    Analyzer &operator=(Analyzer &&) noexcept;
    Analyzer(const Analyzer &) = delete;
    Analyzer &operator=(const Analyzer &) = delete;

    void advance(WorkBudget budget = {});
    void cancel() noexcept;
    void resume() noexcept;
    AnalysisSnapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mns
#endif
