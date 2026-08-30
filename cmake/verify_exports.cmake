cmake_minimum_required(VERSION 3.25)

# Inspect only defined dynamic symbols. Apple nm uses different selection flags.
if (AXIOM_APPLE)
    set(options -gU)
else ()
    set(options -D --defined-only)
endif ()
execute_process(COMMAND "${AXIOM_NM}" ${options} "${AXIOM_LIBRARY}"
                OUTPUT_VARIABLE symbols COMMAND_ERROR_IS_FATAL ANY)
if (symbols MATCHES "spdlog|[0-9]fmt")
    message(FATAL_ERROR "Core exports private spdlog/fmt implementation symbols")
endif ()
