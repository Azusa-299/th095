#pragma once
// Classified diagnostics shared by the ECL and ANM virtual machines.
//
// The retail engines never stop on an unknown opcode: the caller keeps
// running and the unsupported operation is simply a no-op. The port records
// the event with an explicit category so coverage can be measured instead of
// collapsing every situation into a single fatal flag.

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace th95::vm {

enum class Fault {
    None,
    MissingResource,       // script, archive member or index does not exist
    MissingService,        // engine callback or service is not implemented
    UnsupportedInstruction,// opcode outside the implemented subset
    UnsupportedVariable,   // variable id outside the implemented subset
    InvalidOperand,        // operand outside the legal range for the opcode
    UnresolvedJump,        // branch target is not present in the script
    BudgetExhausted        // instruction budget for one tick was exhausted
};

inline const char *faultName(Fault fault)
{
    switch (fault) {
    case Fault::None: return "none";
    case Fault::MissingResource: return "missing-resource";
    case Fault::MissingService: return "missing-service";
    case Fault::UnsupportedInstruction: return "unsupported-instruction";
    case Fault::UnsupportedVariable: return "unsupported-variable";
    case Fault::InvalidOperand: return "invalid-operand";
    case Fault::UnresolvedJump: return "unresolved-jump";
    case Fault::BudgetExhausted: return "budget-exhausted";
    }
    return "unknown";
}

inline bool fatal(Fault fault)
{
    // Only missing resources make the containing scene unusable. Everything
    // else is recoverable and must keep the frame advancing.
    return fault == Fault::MissingResource;
}

struct Diagnostic {
    Fault kind = Fault::None;
    uint16_t opcode = 0;
    uint32_t offset = 0;
    int script = -1;
    int detail = 0;
    unsigned count = 1;
};

// Aggregate sink: identical (kind, machine, script, opcode, offset, detail)
// tuples collapse into one entry with a hit count. This keeps logs bounded
// while still showing which script triggered an unsupported operation.
class DiagnosticSink {
public:
    void reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }
    void record(Fault kind, int machine, int script, uint16_t opcode,
                uint32_t offset, int detail = 0)
    {
        if (kind == Fault::None) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        const Key key{kind, machine, script, opcode, offset, detail};
        auto &entry = m_entries[key];
        if (entry.count++ == 0) {
            entry.kind = kind;
            entry.script = script;
            entry.opcode = opcode;
            entry.offset = offset;
            entry.detail = detail;
        }
    }
    std::vector<Diagnostic> snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<Diagnostic> result;
        result.reserve(m_entries.size());
        for (const auto &pair : m_entries) result.push_back(pair.second);
        return result;
    }
    unsigned count(Fault kind) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        unsigned total = 0;
        for (const auto &pair : m_entries)
            if (pair.second.kind == kind) total += pair.second.count;
        return total;
    }
    unsigned totalCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        unsigned total = 0;
        for (const auto &pair : m_entries) total += pair.second.count;
        return total;
    }

private:
    using Key = std::tuple<Fault, int, int, uint16_t, uint32_t, int>;
    mutable std::mutex m_mutex;
    std::map<Key, Diagnostic> m_entries;
};

enum class Machine { Ecl = 0, Anm = 1 };

inline DiagnosticSink &sink()
{
    static DiagnosticSink instance;
    return instance;
}

} // namespace th95::vm
