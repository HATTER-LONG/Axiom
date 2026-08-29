#pragma once

#include <axiom/core/logging/log_level.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace axiom::core::logging {

/** @brief Query parameters shared by record-retaining sinks. */
struct LogQuery {
    LogLevel minimum_level{LogLevel::Trace};
    std::vector<std::string> category_prefixes;
    std::size_t limit{0};
};

} // namespace axiom::core::logging
