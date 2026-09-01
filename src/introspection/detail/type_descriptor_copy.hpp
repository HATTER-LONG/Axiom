#pragma once

#include <axiom/foundation/type_descriptor.hpp>

namespace axiom::introspection::detail {

/** @brief Copies a TypeDescriptor graph without retaining source nodes. */
[[nodiscard]] TypeDescriptor copyTypeDescriptor(const TypeDescriptor& source);

} // namespace axiom::introspection::detail
