#pragma once
#include "internal.hpp"
namespace mns {
// A copy preserves the completed event prefix. An unresolved event runs as a
// transaction: retrying its local choices never re-executes completed events.
// Prepared must remain alive and immutable for every machine and its copies.
class RuntimeMachine {
  public:
    RuntimeMachine(const Prepared &, const Json &resolved_deck, const Json &request);
    RuntimeMachine(const RuntimeMachine &);
    RuntimeMachine &operator=(const RuntimeMachine &);
    RuntimeMachine(RuntimeMachine &&) noexcept;
    RuntimeMachine &operator=(RuntimeMachine &&) noexcept;
    ~RuntimeMachine();
    void advance_until_decision();
    void choose(uint32_t);
    bool finished() const;
    std::span<const uint64_t> weights() const;
    const RuntimeResult &result() const;
    // Binary, lossless mutable state in the immutable kernel's namespace.
    std::vector<uint8_t> canonical_key() const;
    uint64_t progress() const;
    size_t retained_bytes() const;
    uint64_t committed_events() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mns
