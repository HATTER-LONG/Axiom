#pragma once

#include <string>

#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

/**
 * @brief Caller-supplied metadata propagated with every action invocation.
 *
 * Purely informational: the runtime never branches on its content.
 */
struct InvocationContext {
    std::string m_requestId;
    std::string m_traceId;
    std::string m_caller;
    Value::Object m_metadata;
};

} // namespace axiom::runtime
