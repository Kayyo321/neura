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
    # Inlining appends a local to the caller for each inlined parameter and
    # local; every other symbol keeps its number and listing.
    string(REGEX REPLACE "\n  %[0-9]+ local \"[$]inline[.][^\n]*" "" optimized_symbols "${optimized_symbols}")
    if(unoptimized MATCHES "reference_place" AND optimized MATCHES "reference_place")
        message(FATAL_ERROR "${label}: the executable IR still has a provisional reference binding")
    endif()
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
    elseif(NOT optimized_symbols STREQUAL unoptimized_symbols AND
           NOT unoptimized_symbols MATCHES " generics=\\[" AND NOT unoptimized MATCHES "reference_place")
        # Specialization and reference storage preparation can change symbols
        # before the optimizer runs.
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

# A program that spawns a task cannot also use a native library (decision C15
# of docs/lang/Concurrency.md), so the combined program leaves the concurrency
# examples out. Each one is still emitted on its own below.
set(combined ${examples})
list(FILTER combined EXCLUDE REGEX "/examples/concurrency/")
check_both_modes("all examples" ${combined})
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

# Optimized and unoptimized images of the same program must behave alike on
# the NVM: the same stdout, exit status, and trap diagnostic. The diagnostic
# is compared without the image path and the function and block numbers,
# which name the blocks the converter built. The optimized run may consume
# no more steps or peak heap cells than the unoptimized one
# (docs/IrOptimization.md, "Resources").
function(normalized_diagnostic output text)
    string(REGEX REPLACE "(^|\n)[^\n]*: trap: " "\\1trap: " text "${text}")
    string(REGEX REPLACE " in function [0-9]+ block [0-9]+" "" text "${text}")
    string(REGEX REPLACE "(^|\n)(stats|gpu): [^\n]*" "" text "${text}")
    set("${output}" "${text}" PARENT_SCOPE)
endfunction()

function(run_image prefix image)
    execute_process(
        COMMAND "${NEURA}" run --stats "${image}" -- ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    string(REGEX MATCH "stats: steps ([0-9]+), peak heap cells ([0-9]+)" stats "${stderr}")
    if(NOT stats)
        message(FATAL_ERROR "${image} ${ARGN}: no statistics, status ${status}\n${stderr}")
    endif()
    normalized_diagnostic(diagnostic "${stderr}")
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stdout" "${stdout}" PARENT_SCOPE)
    set("${prefix}_diagnostic" "${diagnostic}" PARENT_SCOPE)
    set("${prefix}_steps" "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set("${prefix}_heap" "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

# Builds `source` both ways and compares a run per argument list in ARGN;
# each list separates its arguments with commas, and "-" stands for none.
set(compared_runs 0)
set(cheaper_runs 0)
set(trapped_runs 0)
function(compare_builds source)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(directory "${source}" DIRECTORY)
    get_filename_component(directory "${directory}" NAME)
    set(stem "${TEST_DIR}/run-${directory}-${name}")
    run_success(ignored build "${source}" -o "${stem}.nvr")
    run_success(ignored build --no-optimize "${source}" -o "${stem}-unoptimized.nvr")
    foreach(arguments IN LISTS ARGN)
        if(arguments STREQUAL "-")
            set(arguments "")
        endif()
        string(REPLACE "," ";" arguments "${arguments}")
        set(label "${directory}/${name} [${arguments}]")
        run_image(optimized "${stem}.nvr" ${arguments})
        run_image(unoptimized "${stem}-unoptimized.nvr" ${arguments})
        foreach(part IN ITEMS status stdout diagnostic)
            if(NOT optimized_${part} STREQUAL unoptimized_${part})
                message(FATAL_ERROR "${label}: the builds differ in ${part}\n"
                                    "optimized:\n${optimized_${part}}\nunoptimized:\n${unoptimized_${part}}")
            endif()
        endforeach()
        if(optimized_steps GREATER unoptimized_steps OR optimized_heap GREATER unoptimized_heap)
            message(FATAL_ERROR "${label}: optimization raised a resource: steps ${unoptimized_steps} -> "
                                "${optimized_steps}, peak heap cells ${unoptimized_heap} -> ${optimized_heap}")
        endif()
        math(EXPR compared_runs "${compared_runs} + 1")
        if(optimized_steps LESS unoptimized_steps)
            math(EXPR cheaper_runs "${cheaper_runs} + 1")
        endif()
        if(optimized_diagnostic MATCHES "trap: ")
            math(EXPR trapped_runs "${trapped_runs} + 1")
        endif()
    endforeach()
    set(compared_runs "${compared_runs}" PARENT_SCOPE)
    set(cheaper_runs "${cheaper_runs}" PARENT_SCOPE)
    set(trapped_runs "${trapped_runs}" PARENT_SCOPE)
endfunction()

set(programs "${SOURCE_DIR}/tests/ir-optimization/programs")
compare_builds("${programs}/values.nra" - "a,b,c" "a,b,c,d,e,f")
compare_builds("${programs}/inlining.nra" - "a1b2-c3" "zz9,x" "0123456789abc")
# Argument counts 1, 2, 3, 6, 7, and 8 select each case with operands that
# fit and that overflow.
compare_builds("${programs}/traps.nra" a "a,b" "a,b,c" "a,b,c,d,e,f" "a,b,c,d,e,f,g" "a,b,c,d,e,f,g,h")
file(GLOB backend_programs "${SOURCE_DIR}/tests/backend/programs/*.nra")
list(SORT backend_programs)
foreach(source IN LISTS backend_programs)
    compare_builds("${source}" -)
endforeach()
# The cost benchmarks at their first size: standard library code throughout.
file(GLOB benchmarks "${SOURCE_DIR}/tests/benchmarks/*.nra")
list(SORT benchmarks)
foreach(source IN LISTS benchmarks)
    file(STRINGS "${source}" sizes_line REGEX "^// sizes: ")
    string(REGEX MATCH "[0-9]+" size "${sizes_line}")
    compare_builds("${source}" "${size}")
endforeach()
if(trapped_runs LESS 3 OR cheaper_runs EQUAL 0)
    message(FATAL_ERROR "expected traps and cheaper optimized runs: ${trapped_runs} trapped, ${cheaper_runs} cheaper")
endif()
message(STATUS "Compared ${compared_runs} runs of optimized and unoptimized images: ${trapped_runs} trapped, "
               "${cheaper_runs} took fewer steps optimized, none took more steps or peak heap cells")

run_failure(check-option "--no-optimize applies only to emit-ir" check --no-optimize "${folding}")
run_failure(unknown-option "unknown option: --optimize-harder" emit-ir --optimize-harder "${folding}")
run_success(help --help)
if(NOT help MATCHES "emit-ir [^\n]*--no-optimize")
    message(FATAL_ERROR "Usage should document --no-optimize\n${help}")
endif()

# #Inline and #NoInline (docs/lang/Tags.md) set the inlining directive that
# inline-calls reads. The optimized and unoptimized programs print the same
# output; inlining #Inline(always: true) removes calls, so the optimized run
# takes fewer steps.
set(tags_example "${SOURCE_DIR}/examples/tags/tags.nra")
run_success(unused build "${tags_example}" -o tags-optimized.nvr)
run_success(unused build --no-optimize "${tags_example}" -o tags-unoptimized.nvr)
function(run_stats output steps image)
    execute_process(COMMAND "${NEURA}" run --stats "${image}" WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "0" OR NOT stderr MATCHES "stats: steps ([0-9]+),")
        message(FATAL_ERROR "run --stats ${image} failed (${status})\n${stderr}")
    endif()
    set("${output}" "${stdout}" PARENT_SCOPE)
    set("${steps}" "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
run_stats(optimized_output optimized_steps tags-optimized.nvr)
run_stats(unoptimized_output unoptimized_steps tags-unoptimized.nvr)
message(STATUS "tags example: ${optimized_steps} steps optimized, ${unoptimized_steps} unoptimized")
if(NOT optimized_output STREQUAL unoptimized_output OR NOT optimized_output STREQUAL "16\n9\n25 25\n")
    message(FATAL_ERROR "The tags example prints differently with and without optimization\n"
                        "${optimized_output}\n---\n${unoptimized_output}")
endif()
if(NOT optimized_steps LESS unoptimized_steps)
    message(FATAL_ERROR "Expected inlining to save steps: ${optimized_steps} optimized, ${unoptimized_steps} not")
endif()

# W0004: the pass refuses #Inline(always: true) on a recursive function and
# says why, once per tag, when it optimizes; #Deny(inline) makes it an error.
file(WRITE "${TEST_DIR}/inline-refused.nra" [=[
let Math = module (
    #Inline(always: true)
    export let factorial = func(value: int64) -> int64 {
        if (value <= 1) {
            return 1;
        }
        return value * factorial(value - 1);
    };
);
let value: int64 = Math.factorial(5);
]=])
execute_process(COMMAND "${NEURA}" emit-ir --error-format=short inline-refused.nra WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE unused ERROR_VARIABLE stderr)
string(REGEX MATCHALL "warning\\[W0004\\]" refusals "${stderr}")
list(LENGTH refusals refusal_count)
if(NOT status STREQUAL "0" OR NOT refusal_count EQUAL 1 OR
   NOT stderr MATCHES "inline-refused[.]nra:2:5: warning\\[W0004\\]: cannot inline 'factorial': it is recursive")
    message(FATAL_ERROR "Expected one W0004 for the recursive function (${status})\n${stderr}")
endif()
execute_process(COMMAND "${NEURA}" emit-ir --no-optimize --error-format=short inline-refused.nra
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status OUTPUT_VARIABLE unused ERROR_VARIABLE stderr)
if(NOT status STREQUAL "0" OR stderr MATCHES "W0004")
    message(FATAL_ERROR "Expected no W0004 without optimization (${status})\n${stderr}")
endif()
file(READ "${TEST_DIR}/inline-refused.nra" refused_source)
string(REPLACE "#Inline(always: true)" "#Inline(always: true)\n    #Deny(inline)" denied_source "${refused_source}")
file(WRITE "${TEST_DIR}/inline-denied.nra" "${denied_source}")
run_failure(inline-denied "inline-denied[.]nra:2:5: error\\[W0004\\]" emit-ir --error-format=short inline-denied.nra)
