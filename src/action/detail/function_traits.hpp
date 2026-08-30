#pragma once

/**
 * @file function_traits.hpp
 * @brief Private compatibility include for Action implementation templates.
 *
 * Callable traits are defined with ModuleBuilder because its public templates
 * require them at consumer compile time. This private header deliberately
 * reuses that single definition rather than creating a second trait family.
 */

#include <axiom/action/module_builder.hpp>
