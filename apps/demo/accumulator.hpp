#pragma once

/**
 * @file accumulator.hpp
 * @brief Small application-owned state shared by the action and resource examples.
 */

namespace axiom::demo {

/** @brief An integer accumulator; callers serialize access. */
class Accumulator {
public:
    /**
     * @brief Adds a delta and returns the accumulated total.
     * @param delta Amount to add.
     * @return Updated total, initially zero before the first addition.
     * @pre The updated total is representable as an int.
     */
    [[nodiscard]] int add(const int delta) {
        total_ += delta;
        return total_;
    }

private:
    int total_{0};
};
} // namespace axiom::demo
