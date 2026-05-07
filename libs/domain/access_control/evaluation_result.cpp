#include "evaluation_result.h"

#include <sstream>

namespace sea::application::access_control {

std::string EvaluationResult::format_for_log() const
{
    std::ostringstream oss;
    oss << (allowed ? "ALLOW" : "DENY")
        << " (predicates_evaluated=" << predicates_evaluated << ")";

    if (reason.has_value()) {
        oss << " reason=" << *reason;
    }

    if (!traces.empty()) {
        oss << "\n  trace:";
        for (const auto& trace : traces) {
            oss << "\n    " << (trace.result ? "✓" : "✗")
                << " " << trace.description
                << " [left=" << trace.left_resolved
                << " right=" << trace.right_resolved << "]";
            if (trace.error.has_value()) {
                oss << " ERROR: " << *trace.error;
            }
        }
    }

    return oss.str();
}

} // namespace sea::application::access_control