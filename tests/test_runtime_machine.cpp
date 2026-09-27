#include "runtime_fixture.hpp"
#include "runtime_machine.hpp"

namespace {
std::vector<int64_t> signature(const RuntimeResult &r) {
    std::vector<int64_t> value;
    for (auto collection : {&r.shared, &r.primary})
        for (const auto &f : *collection)
            value.insert(value.end(),
                         {f.time_ms, f.owner, f.combo_raw, f.note_raw, f.judgement, f.judgement_raw, f.luck});
    for (const auto &n : r.notes)
        value.insert(value.end(), {n.id, n.time_ms, n.judgement, n.solo_judgement, n.life});
    for (const auto &s : r.sections) {
        value.insert(value.end(), {s.start_ms, s.end_ms, s.mission, s.mission_value});
        for (auto [t, c] : s.combo)
            value.insert(value.end(), {t, c});
    }
    return value;
}
} // namespace
int main() {
    auto p = prepared();
    p.master.tables["MasterGekisouSkillEffect"].push_back(effect(1, 11003, 1, 2, 10000, 0));
    p.master.tables["MasterLiveGekisouLuckBonusLot"] = Json::array();
    for (int kind = 0; kind < 5; ++kind)
        for (int result : {0, 3})
            p.master.tables["MasterLiveGekisouLuckBonusLot"].push_back(
                Json{{"_chanceLotType", kind}, {"_lotResult", result}, {"_weight", 1}});
    Json d = deck(), request{{"mode", "battle"}};
    RuntimeMachine initial(p, d, request);
    initial.advance_until_decision();
    assert(initial.weights().size() == 120 && initial.committed_events() == 0);
    auto root_key = initial.canonical_key();
    RuntimeMachine twin(p, d, request);
    twin.advance_until_decision();
    assert(twin.canonical_key() == root_key);
    RuntimeMachine selected = initial;
    selected.choose(0);
    selected.advance_until_decision();
    assert(initial.canonical_key() == root_key);
    assert(selected.committed_events() > 0 && !selected.finished());
    assert(selected.weights().size() == 2);
    const auto committed = selected.committed_events();
    const auto key = selected.canonical_key();
    // Branching during consume's cached-next draw retries this event only.
    RuntimeMachine left = selected, right = selected;
    left.choose(0);
    right.choose(1);
    left.advance_until_decision();
    right.advance_until_decision();
    assert(selected.canonical_key() == key);
    assert(left.committed_events() == committed && right.committed_events() == committed);
    assert(left.canonical_key() != right.canonical_key());
    assert(left.progress() > selected.progress());
    for (uint32_t first : {0u, 1u}) {
        RuntimeMachine machine = selected;
        std::vector<uint32_t> choices{0, first};
        machine.choose(first);
        machine.advance_until_decision();
        while (!machine.finished()) {
            auto old = machine.progress();
            machine.choose(0);
            choices.push_back(0);
            machine.advance_until_decision();
            assert(machine.progress() > old);
        }
        BranchRandom random(choices);
        auto reference = run_runtime(p, d, request, random);
        assert(signature(machine.result()) == signature(reference));
        assert(random.consumed() == choices.size());
        auto terminal_key = machine.canonical_key();
        RuntimeMachine copy = machine;
        copy.advance_until_decision();
        assert(copy.canonical_key() == terminal_key);
        assert(machine.retained_bytes() > sizeof(RuntimeMachine));
    }
    bool rejected = false;
    try {
        initial.choose(120);
    } catch (const Error &) {
        rejected = true;
    }
    assert(rejected && initial.canonical_key() == root_key);
    std::cout << "runtime machine: event transaction, clone isolation, strict progress, completed prefix "
                 "reuse passed\n";
}
