cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
unset(ENV{NEURA_RUN_OPTIONS})
set(hello "${SOURCE_DIR}/examples/hello-world/hello-world.nra")
set(tests "${SOURCE_DIR}/tests/backend")

function(invoke prefix)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    foreach(field IN ITEMS status stdout stderr)
        set(${prefix}_${field} "${${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

function(success)
    invoke(result ${ARGN})
    if(NOT result_status STREQUAL "0")
        message(FATAL_ERROR "${ARGN}: status ${result_status}\n${result_stdout}${result_stderr}")
    endif()
endfunction()

function(equal_files first second)
    success("${CMAKE_COMMAND}" -E compare_files "${first}" "${second}")
endfunction()

function(compare_program name)
    set(image "${TEST_DIR}/${name}.nvr")
    set(executable "${TEST_DIR}/${name}")
    success("${NEURA}" build ${ARGN} -o "${image}")
    success("${NEURA}" build --native ${policy_options} "${image}" -o "${executable}")
    success("${NEURA}" inspect "${executable}" -o extracted.nvr)
    equal_files("${image}" "${TEST_DIR}/extracted.nvr")
    invoke(cpu "${NEURA}" run --no-gpu ${policy_options} "${image}")
    invoke(native "${executable}")
    string(REPLACE "${image}: trap:" "<program>: trap:" cpu_error "${cpu_stderr}")
    string(REPLACE "${executable}: trap:" "<program>: trap:" native_error "${native_stderr}")
    if(NOT native_status STREQUAL cpu_status OR NOT native_stdout STREQUAL cpu_stdout OR
       NOT native_error STREQUAL cpu_error)
        message(FATAL_ERROR "${name}: CPU status ${cpu_status}, launcher status ${native_status}\n"
                            "CPU stdout: ${cpu_stdout}\n${cpu_error}\nLauncher stdout: ${native_stdout}\n${native_error}")
    endif()
endfunction()

if(SUITE STREQUAL "build")
    success("${NEURA}" emit-ir "${hello}" -o hello.nir)
    success("${NEURA}" build "${hello}" -o hello.nvr)
    success("${NEURA}" build --native "${hello}" -o source)
    success("${NEURA}" build --native hello.nir -o from-nir)
    success("${NEURA}" build --native hello.nvr -o from-nvr)
    success("${NEURA}" build --native "${hello}" -o again)
    foreach(other IN ITEMS from-nir from-nvr again)
        equal_files("${TEST_DIR}/source" "${TEST_DIR}/${other}")
    endforeach()
    # N8: the same source label gives the same bytes from another working
    # directory and from a copy at another absolute path.
    file(MAKE_DIRECTORY "${TEST_DIR}/other-cwd" "${TEST_DIR}/copies/deeper/still")
    file(COPY "${hello}" DESTINATION "${TEST_DIR}/copies/deeper/still")
    execute_process(COMMAND "${NEURA}" build --native "${hello}" -o "${TEST_DIR}/from-other-cwd"
        WORKING_DIRECTORY "${TEST_DIR}/other-cwd" RESULT_VARIABLE other_status ERROR_VARIABLE other_error)
    if(NOT other_status STREQUAL "0")
        message(FATAL_ERROR "build from another working directory failed\n${other_error}")
    endif()
    equal_files("${TEST_DIR}/source" "${TEST_DIR}/from-other-cwd")
    success("${NEURA}" build --native "${TEST_DIR}/copies/deeper/still/hello-world.nra" -o from-copy)
    equal_files("${TEST_DIR}/source" "${TEST_DIR}/from-copy")
    # The default policy holds no prefixes. Its encoded size, including the
    # five task settings of container version 2 (36 bytes) and the image
    # length word, is 72 bytes. Compare both unmodified regions.
    file(SIZE "${LAUNCHER}" launcher_size)
    file(SIZE "${TEST_DIR}/hello.nvr" image_size)
    math(EXPR image_offset "${launcher_size} + 72")
    file(READ "${LAUNCHER}" launcher_hex HEX)
    file(READ "${TEST_DIR}/source" prefix LIMIT "${launcher_size}" HEX)
    file(READ "${TEST_DIR}/source" payload OFFSET "${image_offset}" LIMIT "${image_size}" HEX)
    file(READ "${TEST_DIR}/hello.nvr" expected HEX)
    if(NOT launcher_hex STREQUAL prefix OR NOT payload STREQUAL expected)
        message(FATAL_ERROR "packaging changed the launcher prefix or NVR image")
    endif()
    invoke(mode stat -c "%a" "${TEST_DIR}/source")
    if(NOT mode_status STREQUAL "0" OR NOT mode_stdout STREQUAL "755\n")
        message(FATAL_ERROR "expected native mode 0755, got ${mode_stdout}${mode_stderr}")
    endif()
    # Launchers stay CPU-only: no Vulkan library is needed or named.
    find_program(READELF NAMES readelf llvm-readelf)
    if(NOT READELF)
        message(FATAL_ERROR "readelf or llvm-readelf is required to check that the launcher is CPU-only")
    endif()
    invoke(elf "${READELF}" -d "${LAUNCHER}")
    if(NOT elf_status STREQUAL "0" OR NOT elf_stdout MATCHES "NEEDED" OR elf_stdout MATCHES "[Vv]ulkan")
        message(FATAL_ERROR "the default launcher must list its dependencies and none may be Vulkan\n${elf_stdout}${elf_stderr}")
    endif()
    foreach(spelling IN ITEMS "76756c6b616e" "56756c6b616e")
        string(FIND "${launcher_hex}" "${spelling}" vulkan_offset)
        if(NOT vulkan_offset EQUAL -1)
            message(FATAL_ERROR "the default launcher contains the text 'ulkan' at hex offset ${vulkan_offset}")
        endif()
    endforeach()
    function(build_error text)
        invoke(rejected "${NEURA}" build ${ARGN} -o rejected)
        if(NOT rejected_status STREQUAL "1" OR NOT rejected_stderr STREQUAL "neura: error: ${text}\n" OR
           NOT rejected_stdout STREQUAL "" OR EXISTS "${TEST_DIR}/rejected")
            message(FATAL_ERROR "expected ${text}\n${rejected_stdout}${rejected_stderr}")
        endif()
    endfunction()
    build_error("build takes sources or one .nir file; packaging a .nvr needs --native" hello.nvr)
    foreach(flag IN ITEMS --no-optimize --no-source-map)
        build_error("${flag} cannot be applied to an image that is already built" --native "${flag}" hello.nvr)
    endforeach()
    configure_file("${TEST_DIR}/hello.nvr" "${TEST_DIR}/newer.nvr" COPYONLY)
    string(ASCII 6 newer_minor)
    file(WRITE "${TEST_DIR}/minor" "${newer_minor}")
    execute_process(COMMAND dd of=newer.nvr bs=1 seek=6 conv=notrunc
                    INPUT_FILE "${TEST_DIR}/minor" WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE patched
                    ERROR_QUIET)
    if(NOT patched STREQUAL "0")
        message(FATAL_ERROR "could not write the newer image version fixture")
    endif()
    build_error("--native: the image is NVR 1.6; this launcher runs up to 1.5" --native newer.nvr)
    success("${NEURA}" assemble "${tests}/scoped-tasks.nvra" -o tasks.nvr)
    success("${NEURA}" build --native tasks.nvr -o tasks)
    success("${NEURA}" inspect tasks -o extracted-tasks.nvr)
    equal_files("${TEST_DIR}/tasks.nvr" "${TEST_DIR}/extracted-tasks.nvr")
    invoke(tasks_default "${TEST_DIR}/tasks")
    # The quantum changes the interleaving, and with it the output order, so
    # the variable rejects it. neura run still accepts it and shows the change.
    invoke(tasks_quantum "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--no-gpu --gpu=off --task-quantum 1"
        "${TEST_DIR}/tasks")
    invoke(tasks_cpu_quantum "${NEURA}" run --no-gpu --task-quantum 1 tasks.nvr)
    invoke(tasks_cpu_default "${NEURA}" run --no-gpu tasks.nvr)
    invoke(tasks_allowed "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--no-gpu --gpu=off --max-tasks 65536"
        "${TEST_DIR}/tasks")
    invoke(tasks_limit "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--max-tasks 1" "${TEST_DIR}/tasks")
    if(NOT tasks_default_status STREQUAL "0" OR NOT tasks_default_stdout STREQUAL "root\nchild\n" OR
       NOT tasks_default_stderr STREQUAL "" OR
       NOT tasks_quantum_status STREQUAL "1" OR NOT tasks_quantum_stdout STREQUAL "" OR
       NOT tasks_quantum_stderr MATCHES ": error: NEURA_RUN_OPTIONS: --task-quantum is not accepted by a native executable\n$" OR
       NOT tasks_cpu_quantum_status STREQUAL "0" OR NOT tasks_cpu_quantum_stdout STREQUAL "child\nroot\n" OR
       NOT tasks_cpu_default_status STREQUAL "0" OR NOT tasks_cpu_default_stdout STREQUAL "root\nchild\n" OR
       NOT tasks_allowed_status STREQUAL "0" OR NOT tasks_allowed_stdout STREQUAL "root\nchild\n" OR
       NOT tasks_allowed_stderr STREQUAL "" OR
       NOT tasks_limit_status STREQUAL "137" OR NOT tasks_limit_stdout STREQUAL "" OR
       NOT tasks_limit_stderr MATCHES "trap: task limit exceeded")
        message(FATAL_ERROR "unexpected scheduler launcher result\n"
            "default: ${tasks_default_status}\n${tasks_default_stdout}${tasks_default_stderr}\n"
            "quantum: ${tasks_quantum_status}\n${tasks_quantum_stdout}${tasks_quantum_stderr}\n"
            "neura run quantum: ${tasks_cpu_quantum_status}\n${tasks_cpu_quantum_stdout}${tasks_cpu_quantum_stderr}\n"
            "neura run default: ${tasks_cpu_default_stdout}${tasks_cpu_default_stderr}\n"
            "max-tasks: ${tasks_allowed_status}\n${tasks_allowed_stdout}${tasks_allowed_stderr}\n"
            "limit: ${tasks_limit_status}\n${tasks_limit_stdout}${tasks_limit_stderr}")
    endif()
    # The policy records the task settings (container version 2). A baked
    # quantum of 1 gives the interleaving neura run --task-quantum 1 gives,
    # and inspect reads every setting back.
    success("${NEURA}" build --native --task-quantum 1 --max-tasks 8 --max-queued-messages 16
        --max-pending-requests 4 --parallel-workers 3 tasks.nvr -o tasks-baked)
    invoke(baked "${TEST_DIR}/tasks-baked")
    invoke(baked_inspect "${NEURA}" inspect tasks-baked)
    # The variable lowers a baked limit and never raises one.
    invoke(baked_raised "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--max-tasks 1000 --max-queued-messages 1000 --max-pending-requests 1000 --parallel-workers 1000 --stats"
        "${TEST_DIR}/tasks-baked")
    invoke(baked_lowered "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--max-tasks 1" "${TEST_DIR}/tasks-baked")
    success("${NEURA}" build --native --max-tasks 1 tasks.nvr -o tasks-one)
    invoke(baked_one "${TEST_DIR}/tasks-one")
    success("${NEURA}" build --native --sealed --max-tasks 1 tasks.nvr -o tasks-one-sealed)
    invoke(baked_sealed "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--max-tasks 1000" "${TEST_DIR}/tasks-one-sealed")
    if(NOT baked_status STREQUAL "0" OR NOT baked_stdout STREQUAL "child\nroot\n" OR NOT baked_stderr STREQUAL "" OR
       NOT baked_inspect_stdout MATCHES "\ntasks: at most 8, quantum 1, queued messages 16, pending requests 4, parallel workers 3\n" OR
       NOT baked_raised_status STREQUAL "0" OR NOT baked_raised_stdout STREQUAL "child\nroot\n" OR
       NOT baked_lowered_status STREQUAL "137" OR NOT baked_lowered_stderr MATCHES "trap: task limit exceeded" OR
       NOT baked_one_status STREQUAL "137" OR NOT baked_one_stderr MATCHES "trap: task limit exceeded" OR
       NOT baked_sealed_status STREQUAL "137" OR NOT baked_sealed_stderr MATCHES "trap: task limit exceeded")
        message(FATAL_ERROR "unexpected baked task settings\n"
            "baked: ${baked_status}\n${baked_stdout}${baked_stderr}\ninspect: ${baked_inspect_stdout}\n"
            "raised: ${baked_raised_status}\n${baked_raised_stdout}${baked_raised_stderr}\n"
            "lowered: ${baked_lowered_status}\n${baked_lowered_stderr}\n"
            "one: ${baked_one_status}\n${baked_one_stderr}\nsealed: ${baked_sealed_status}\n${baked_sealed_stderr}")
    endif()
    # The observing options: a task dump on a trap and scheduler events.
    invoke(observed "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--max-tasks 1 --task-dump=json" "${TEST_DIR}/tasks")
    invoke(traced "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--trace-events launcher.events --trace-wall-time"
        "${TEST_DIR}/tasks")
    file(READ "${TEST_DIR}/launcher.events" launcher_events)
    if(NOT observed_status STREQUAL "137" OR NOT observed_stderr MATCHES "^{\"tasks\":\\[{\"id\":0," OR
       NOT traced_status STREQUAL "0" OR NOT traced_stdout STREQUAL "root\nchild\n" OR
       NOT launcher_events MATCHES "^{\"event\":\"spawn\",[^\n]*\"wall_ns\":[0-9]+}\n")
        message(FATAL_ERROR "unexpected observed launcher run (status ${observed_status}, ${traced_status})\n${observed_stderr}\n${launcher_events}")
    endif()
    invoke(events_value "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--trace-events" "${TEST_DIR}/tasks")
    invoke(dump_form "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--task-dump=xml" "${TEST_DIR}/tasks")
    invoke(virtual_refused "${CMAKE_COMMAND}" -E env "NEURA_RUN_OPTIONS=--virtual-time" "${TEST_DIR}/tasks")
    if(NOT events_value_status STREQUAL "1" OR
       NOT events_value_stderr MATCHES "NEURA_RUN_OPTIONS: expected a value after --trace-events" OR
       NOT dump_form_status STREQUAL "1" OR
       NOT dump_form_stderr MATCHES "NEURA_RUN_OPTIONS: --task-dump=xml is not accepted by a native executable" OR
       NOT virtual_refused_status STREQUAL "1" OR
       NOT virtual_refused_stderr MATCHES "NEURA_RUN_OPTIONS: --virtual-time is not accepted by a native executable")
        message(FATAL_ERROR "unexpected option refusals\n${events_value_stderr}\n${dump_form_stderr}\n${virtual_refused_stderr}")
    endif()
    build_error("--task-quantum applies only with --native" --task-quantum 4 "${hello}")
    build_error("--max-tasks applies only with --native" --max-tasks 4 "${hello}")
    build_error("expected a positive integer after --task-quantum" --native --task-quantum 0 tasks.nvr)
    build_error("--parallel-workers is at most 1024" --native --parallel-workers 1025 tasks.nvr)
    success(truncate -s 4294967296 "${TEST_DIR}/oversize.nvr")
    build_error("native executable: image must be smaller than 4 GiB" --native oversize.nvr)
    file(REMOVE "${TEST_DIR}/oversize.nvr")
    get_filename_component(launcher_directory "${LAUNCHER}" DIRECTORY)
    get_filename_component(triple "${launcher_directory}" NAME)
    set(isolated "${TEST_DIR}/no-install")
    set(missing_directory "${isolated}/lib/neura/launchers/${triple}")
    file(MAKE_DIRECTORY "${isolated}")
    file(COPY "${NEURA}" DESTINATION "${isolated}")
    set(original_neura "${NEURA}")
    set(NEURA "${isolated}/neura")
    build_error("--native: no launcher for ${triple} in ${missing_directory}; pass --launcher or install one" --native hello.nvr)
    file(MAKE_DIRECTORY "${missing_directory}")
    file(WRITE "${missing_directory}/neura-launcher" "not a launcher")
    build_error("--native: ${missing_directory}/neura-launcher is not a Neura launcher" --native hello.nvr)
    set(NEURA "${original_neura}")
    build_error("--target applies only with --native" --target "${triple}" "${hello}")
    build_error("--launcher applies only with --native" --launcher "${LAUNCHER}" "${hello}")
    build_error("--static applies only with --native" --static "${hello}")
    build_error("conflicting --target values" --native --target "${triple}" --target other-linux-gnu hello.nvr)
    build_error("conflicting --launcher values" --native --launcher "${LAUNCHER}" --launcher different hello.nvr)
    build_error("--target: launcher is for ${triple}, expected other-linux-gnu" --native
                --target other-linux-gnu --launcher "${LAUNCHER}" hello.nvr)
    build_error("--static: ${LAUNCHER} is a default launcher" --native --static --launcher "${LAUNCHER}" hello.nvr)
    build_error("a .nvr input must be the only input file" --native hello.nvr hello.nvr)
    build_error("-I applies only to source inputs" --native -I "${TEST_DIR}" hello.nvr)
    foreach(bad IN ITEMS "a/b" ".." "x y")
        build_error("--target expects an ELF target triple" --native --target "${bad}" hello.nvr)
    endforeach()
    build_error("--max-frames is too large" --native --max-frames 4294967296 hello.nvr)
    build_error("--allow-fs= expects a comma-separated list of path prefixes" --native --allow-fs= hello.nvr)
    build_error("--allow-fs= expects a comma-separated list of path prefixes" --native --allow-fs=a,,b hello.nvr)
    build_error("expected a positive integer after --max-steps" --native --max-steps 0 hello.nvr)
    invoke(missing "${NEURA}" build --native hello.nvr)
    if(NOT missing_status STREQUAL "1" OR NOT missing_stderr STREQUAL "neura: error: build --native requires -o <file>\n")
        message(FATAL_ERROR "expected the missing -o error\n${missing_stderr}")
    endif()
    invoke(missing "${NEURA}" build --native hello.nvr -o rejected --target)
    if(NOT missing_status STREQUAL "1" OR NOT missing_stderr STREQUAL "neura: error: expected a value after --target\n" OR
       EXISTS "${TEST_DIR}/rejected")
        message(FATAL_ERROR "expected the missing value error\n${missing_stderr}")
    endif()
    invoke(misplaced "${NEURA}" run --native hello.nvr)
    if(NOT misplaced_status STREQUAL "1" OR NOT misplaced_stderr STREQUAL "neura: error: --native applies only to build\n")
        message(FATAL_ERROR "expected --native to be refused outside build\n${misplaced_stderr}")
    endif()
    success("${NEURA}" build --native --target "${triple}" --target "${triple}"
            --launcher "${LAUNCHER}" --launcher "${LAUNCHER}" hello.nvr -o explicit)
    equal_files("${TEST_DIR}/explicit" "${TEST_DIR}/again")
    file(WRITE "${TEST_DIR}/source" "replace me")
    success("${NEURA}" build --native hello.nvr -o source)
    equal_files("${TEST_DIR}/source" "${TEST_DIR}/again")
    # A symbolic link at -o is replaced, not followed, and no temporary file stays behind.
    file(WRITE "${TEST_DIR}/link-victim" "victim\n")
    file(CREATE_LINK "${TEST_DIR}/link-victim" "${TEST_DIR}/linked-output" SYMBOLIC)
    success("${NEURA}" build --native hello.nvr -o linked-output)
    file(READ "${TEST_DIR}/link-victim" link_victim)
    if(NOT link_victim STREQUAL "victim\n" OR IS_SYMLINK "${TEST_DIR}/linked-output")
        message(FATAL_ERROR "the output link should be replaced and its target left alone")
    endif()
    equal_files("${TEST_DIR}/linked-output" "${TEST_DIR}/again")
    file(GLOB temporaries "${TEST_DIR}/.neura-native-*")
    if(temporaries)
        message(FATAL_ERROR "temporary output files remain: ${temporaries}")
    endif()
    invoke(inspect "${NEURA}" inspect source -o extracted.nvr)
    if(NOT inspect_status STREQUAL "0" OR NOT inspect_stderr STREQUAL "" OR
       NOT inspect_stdout MATCHES "^native executable: source\nlauncher: neura development, [^,]+, default, runs NVR up to 1[.]5\ncapabilities: console, process exit, files, network, environment, clock, entropy, native\nlimits: steps 100000000, frames 10000, heap cells 67108864\ntasks: at most 65536, quantum 1024, queued messages 1048576, pending requests 4096, parallel workers 1\nrun options: accepted\nimage: ${image_size} bytes, NVR 1[.]2, [0-9]+ graphs, source map present, native library absent\n$")
        message(FATAL_ERROR "unexpected inspect output\n${inspect_stdout}${inspect_stderr}")
    endif()
    equal_files("${TEST_DIR}/hello.nvr" "${TEST_DIR}/extracted.nvr")
    # Files that are not native executables, and a launcher without a program.
    file(WRITE "${TEST_DIR}/empty-file" "")
    file(WRITE "${TEST_DIR}/tiny-file" "NEURA")
    foreach(other IN ITEMS hello.nvr hello.nir empty-file tiny-file "${SOURCE_DIR}/README.md" "${hello}")
        invoke(foreign "${NEURA}" inspect "${other}")
        if(NOT foreign_status STREQUAL "1" OR NOT foreign_stdout STREQUAL "" OR
           NOT foreign_stderr MATCHES "^neura: error: [^\n]*: not a native executable [(]no NEURAEXE footer[)]\n$")
            message(FATAL_ERROR "inspect ${other} should say it is not a native executable\n${foreign_stdout}${foreign_stderr}")
        endif()
    endforeach()
    invoke(foreign "${NEURA}" inspect "${LAUNCHER}")
    if(NOT foreign_status STREQUAL "1" OR NOT foreign_stdout STREQUAL "" OR NOT foreign_stderr STREQUAL
       "neura: error: this is a Neura launcher with no program; build one with neura build --native\n")
        message(FATAL_ERROR "inspect of a bare launcher\n${foreign_stdout}${foreign_stderr}")
    endif()
    foreach(command IN ITEMS disassemble decompile)
        invoke(raw "${NEURA}" "${command}" hello.nvr)
        invoke(packaged "${NEURA}" "${command}" source)
        if(NOT raw_status STREQUAL "0" OR NOT packaged_status STREQUAL "0" OR
           NOT raw_stdout STREQUAL packaged_stdout OR NOT raw_stderr STREQUAL packaged_stderr)
            message(FATAL_ERROR "${command} changed when the image was packaged")
        endif()
    endforeach()
    success("${NEURA}" build --native --max-steps 1 hello.nvr -o limited)
    invoke(packaged "${TEST_DIR}/limited")
    invoke(extracted "${NEURA}" run --no-gpu limited)
    if(NOT packaged_status STREQUAL "152" OR NOT extracted_status STREQUAL "0" OR
       NOT extracted_stdout STREQUAL "Hello, World!\n" OR NOT extracted_stderr STREQUAL "")
        message(FATAL_ERROR "neura run must apply its own policy to an embedded image")
    endif()
    build_error("--sealed applies only with --native" --sealed "${hello}")
    build_error("--allow-env applies only with --native" --allow-env "${hello}")
    build_error("--deny-env applies only with --native" --deny-env "${hello}")
    build_error("--deny-all applies only with --native" --deny-all "${hello}")
    build_error("--max-steps applies only with --native" --max-steps 8 "${hello}")

elseif(SUITE STREQUAL "run")
    foreach(source IN ITEMS hello-world/hello-world modules/module-interface modules/multi-file/main types/overloads foreign/native)
        string(REPLACE "/" "-" name "${source}")
        compare_program("${name}" "${SOURCE_DIR}/examples/${source}.nra")
    endforeach()
    set(compared 5)
    file(GLOB drivers "${tests}/examples/*-driver.nra")
    if(NOT drivers)
        message(FATAL_ERROR "no driver programs found in ${tests}/examples")
    endif()
    list(LENGTH drivers driver_count)
    foreach(source IN LISTS drivers)
        get_filename_component(name "${source}" NAME_WE)
        if(name STREQUAL "resources-driver")
            compare_program("${name}" "${SOURCE_DIR}/examples/guide/resources.nra" "${source}")
        else()
            compare_program("${name}" -I "${SOURCE_DIR}/examples/guide" -I "${SOURCE_DIR}/examples/types" "${source}")
        endif()
    endforeach()
    file(GLOB traps "${tests}/traps/*.nra")
    if(NOT traps)
        message(FATAL_ERROR "no trap programs found in ${tests}/traps")
    endif()
    list(LENGTH traps trap_count)
    foreach(source IN LISTS traps)
        get_filename_component(name "${source}" NAME_WE)
        file(STRINGS "${source}" policy_options REGEX "^// run-options: ")
        string(REGEX REPLACE "^// run-options: " "" policy_options "${policy_options}")
        separate_arguments(policy_options NATIVE_COMMAND "${policy_options}")
        compare_program("trap-${name}" "${source}")
    endforeach()
    math(EXPR compared "${compared} + ${driver_count} + ${trap_count}")
    message(STATUS "packaged and compared ${compared} programs: 5 examples, ${driver_count} drivers, ${trap_count} traps")
    success("${NEURA}" build --native "${tests}/arguments/first-argument.nra" -o arguments)
    foreach(argument IN ITEMS --help -- --max-steps)
        invoke(result "${TEST_DIR}/arguments" "${argument}")
        if(NOT result_status STREQUAL "0" OR NOT result_stdout STREQUAL "first: ${argument}\n" OR
           NOT result_stderr STREQUAL "")
            message(FATAL_ERROR "argument '${argument}' did not reach main\n${result_stdout}${result_stderr}")
        endif()
    endforeach()

    # Several arguments at once, including an empty one, one with spaces, and
    # options that look like the runner's: the program receives them all.
    success("${NEURA}" build "${SOURCE_DIR}/tests/native/print-arguments.nra" -o print-arguments.nvr)
    success("${NEURA}" build --native print-arguments.nvr -o print-arguments)
    # execute_process keeps the quoted empty argument; a list expansion would drop it.
    set(expected_arguments "<first>\n<>\n<two words>\n<-->\n<--help>\n<--max-steps>\n<1>\n< padded >\n<--task-quantum=1>\n")
    execute_process(COMMAND "${TEST_DIR}/print-arguments"
        first "" "two words" -- --help --max-steps 1 " padded " --task-quantum=1
        WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE forwarded_native_status
        OUTPUT_VARIABLE forwarded_native_stdout ERROR_VARIABLE forwarded_native_stderr)
    execute_process(COMMAND "${NEURA}" run --no-gpu print-arguments.nvr --
        first "" "two words" -- --help --max-steps 1 " padded " --task-quantum=1
        WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE forwarded_cpu_status
        OUTPUT_VARIABLE forwarded_cpu_stdout ERROR_VARIABLE forwarded_cpu_stderr)
    if(NOT forwarded_native_status STREQUAL "0" OR NOT forwarded_native_stdout STREQUAL expected_arguments OR
       NOT forwarded_native_stderr STREQUAL "" OR NOT forwarded_cpu_stdout STREQUAL expected_arguments)
        message(FATAL_ERROR "arguments were not forwarded unchanged\n${forwarded_native_stdout}${forwarded_native_stderr}\n"
                            "neura run: ${forwarded_cpu_stdout}${forwarded_cpu_stderr}")
    endif()

elseif(SUITE STREQUAL "container")
    success("${NEURA}" assemble "${tests}/legacy/legacy-hello.nvra" -o hello.nvr)
    success("${NEURA}" build --native hello.nvr -o hello)
    success("${CONTAINER_TESTS}" "${TEST_DIR}/hello" "${LAUNCHER}" "${STRIP}" "${TEST_DIR}")
else()
    message(FATAL_ERROR "unknown native test suite: ${SUITE}")
endif()
