# Exercises optimized and unoptimized emission through the command line.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(MAKE_DIRECTORY "${TEST_DIR}")

function(run_success output)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${ARGN} failed with status ${status}\n${stderr}")
    endif()
    set("${output}" "${stdout}" PARENT_SCOPE)
endfunction()

function(run_failure name pattern)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status MATCHES "^[1-9][0-9]*$" OR NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR "Expected ${name} to fail with '${pattern}', got ${status}\n${stdout}\n${stderr}")
    endif()
endfunction()

# The symbol table precedes the first body and ends at the first line that is
# a lone closing brace.
function(symbol_section output listing)
    string(FIND "${listing}" "\n}\n" end)
    if(end EQUAL -1)
        message(FATAL_ERROR "IR listing has no symbol section\n${listing}")
    endif()
    string(SUBSTRING "${listing}" 0 ${end} section)
    set("${output}" "${section}" PARENT_SCOPE)
endfunction()

# Emits `ARGN` twice with and twice without optimization. Each mode must be
# reproducible. Optimization must not change the symbol table; the default
# listing is also specialized, which appends generic instances and retypes
# uses of generic types. It also replaces structural closures with concrete
# receiver layouts. Check for the corresponding output in those programs.
function(check_both_modes label)
    run_success(optimized emit-ir ${ARGN})
    run_success(optimized_again emit-ir ${ARGN})
    run_success(unoptimized emit-ir --no-optimize ${ARGN})
    run_success(unoptimized_again emit-ir --no-optimize ${ARGN})
    if(NOT optimized STREQUAL optimized_again)
        message(FATAL_ERROR "${label}: repeated optimized emission differs")
    endif()
    if(NOT unoptimized STREQUAL unoptimized_again)
        message(FATAL_ERROR "${label}: repeated unoptimized emission differs")
    endif()
    if(NOT optimized MATCHES "^neura[.]ir\nsymbols {\n" OR NOT unoptimized MATCHES "^neura[.]ir\nsymbols {\n")
        message(FATAL_ERROR "${label}: expected a NeuraIR listing")
    endif()
    symbol_section(optimized_symbols "${optimized}")
    symbol_section(unoptimized_symbols "${unoptimized}")
    # Generic declarations of the program itself, as opposed to those of
    # imported standard library modules, which a program may leave unused.
    string(REGEX MATCHALL "[^\n]* generics=\\[[^\n]*" generic_lines "${unoptimized_symbols}")
    set(own_generics FALSE)
    foreach(line IN LISTS generic_lines)
        if(NOT line MATCHES "/stdlib/std/")
            set(own_generics TRUE)
        endif()
    endforeach()
    if(own_generics)
        if(NOT optimized_symbols MATCHES "\"[A-Za-z_][A-Za-z0-9_]*<[^\"]+>\"")
            message(FATAL_ERROR "${label}: specialization added no instances")
        endif()
    elseif(unoptimized MATCHES "make_closure")
        if(NOT optimized_symbols MATCHES "class \"[$]signature[0-9]+" OR optimized MATCHES "make_closure")
            message(FATAL_ERROR "${label}: specialization left a structural closure")
        endif()
    elseif(NOT optimized_symbols STREQUAL unoptimized_symbols AND NOT unoptimized_symbols MATCHES " generics=\\[")
        # Without generic declarations, only specialization could change
        # the symbol table, and it has nothing to do.
        message(FATAL_ERROR "${label}: optimization changed the symbol table")
    endif()
endfunction()

file(GLOB_RECURSE examples "${SOURCE_DIR}/examples/*.nra")
list(SORT examples)
list(LENGTH examples example_count)
if(example_count EQUAL 0)
    message(FATAL_ERROR "No Neura examples were found")
endif()
message(STATUS "Emitting ${example_count} examples with and without optimization")

check_both_modes("all examples" ${examples})
set(roots -I "${SOURCE_DIR}/examples/guide" -I "${SOURCE_DIR}/examples/modules/multi-file")
foreach(example IN LISTS examples)
    file(RELATIVE_PATH name "${SOURCE_DIR}" "${example}")
    check_both_modes("${name}" ${roots} "${example}")
endforeach()

# A fixture with rewrites shows the difference between the two modes. The
# option may appear anywhere before `--`.
set(folding "${SOURCE_DIR}/tests/ir-optimization/fixtures/folding.nra")
run_success(folded emit-ir "${folding}")
run_success(unfolded emit-ir "${folding}" --no-optimize)
if(NOT unfolded MATCHES "binary \"[+]\" : int32[^\n]*\n +operands {\n +integer \"2\" : int32[^\n]*\n +integer \"3\"")
    message(FATAL_ERROR "Unoptimized emission should keep 2 + 3\n${unfolded}")
endif()
if(folded MATCHES "integer \"2\" : int32[^\n]*\n +integer \"3\"" OR NOT folded MATCHES "integer \"5\" : int32")
    message(FATAL_ERROR "Optimized emission should fold 2 + 3 to 5\n${folded}")
endif()
check_both_modes("folding fixture" "${folding}")

run_failure(check-option "--no-optimize applies only to emit-ir" check --no-optimize "${folding}")
run_failure(unknown-option "unknown option: --optimize-harder" emit-ir --optimize-harder "${folding}")
run_success(help --help)
if(NOT help MATCHES "emit-ir [^\n]*--no-optimize")
    message(FATAL_ERROR "Usage should document --no-optimize\n${help}")
endif()
