# Exercise both forms of native COFF inputs and distinguish them from DLL
# import libraries. The common c-import suite checks the default toolchain,
# larger mathops ABI surface, byte reproducibility and native capability.
foreach(variable NEURA SOURCE_DIR TEST_DIR C_COMPILER COFF_ARCHIVER IMPORT_LIBRARY PYTHON)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, TEST_DIR, C_COMPILER, COFF_ARCHIVER, IMPORT_LIBRARY, and PYTHON")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_DIR}")
set(work "${TEST_DIR}/C imports-é")
file(MAKE_DIRECTORY "${work}")
set(fixtures "${SOURCE_DIR}/tests/c-import")
file(COPY "${fixtures}/coff-widths.c" "${fixtures}/coff-widths.h" "${fixtures}/coff-widths.nra" DESTINATION "${work}")
file(COPY_FILE "${IMPORT_LIBRARY}" "${work}/external.dll.lib")

function(invoke label)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${work}" ENCODING UTF-8
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${label}: status ${status}\n${stdout}${stderr}")
    endif()
    set(output "${stdout}" PARENT_SCOPE)
endfunction()

function(run_program label image expected)
    invoke("${label}" "${NEURA}" run --allow-native "${image}")
    if(NOT output STREQUAL expected)
        message(FATAL_ERROR "${label}: expected\n${expected}actual\n${output}")
    endif()
endfunction()

function(reject_link label library pattern)
    file(WRITE "${work}/${label}.nra"
        "let widths = import:c(\"coff-widths.h\", \"${library}\");\n${entry}")
    execute_process(COMMAND "${NEURA}" build --cc "${C_COMPILER}" "${label}.nra" -o "${label}.nvr"
        WORKING_DIRECTORY "${work}" ENCODING UTF-8
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "1" OR NOT stderr MATCHES "${pattern}" OR EXISTS "${work}/${label}.nvr")
        message(FATAL_ERROR "${label}: expected '${pattern}' without an image, got status ${status}\n${stdout}${stderr}")
    endif()
endfunction()

invoke("compile the x64 COFF object" "${C_COMPILER}" /nologo /c /MT /utf-8 /Focoff-widths.obj coff-widths.c)
invoke("archive the x64 COFF object" "${COFF_ARCHIVER}" /nologo /OUT:coff-widths.lib coff-widths.obj)
invoke("generate LLP64 bindings" "${NEURA}" c-bindings --linked coff-widths.h)
foreach(signature IN ITEMS "coff_signed_long = func(value: int32) -> int32"
                           "coff_unsigned_long = func(value: uint32) -> uint32"
                           "coff_wide_char = func(value: uint16) -> uint16"
                           "coff_size = func(value: uint64) -> uint64")
    string(FIND "${output}" "export let ${signature} {" declaration)
    if(declaration EQUAL -1)
        message(FATAL_ERROR "expected an LLP64 binding for ${signature}\n${output}")
    endif()
endforeach()

file(READ "${work}/coff-widths.nra" source)
string(REPLACE "coff-widths.lib" "coff-widths.obj" object_source "${source}")
file(WRITE "${work}/coff-object.nra" "${object_source}")
set(linked_import "let widths = import:c(\"coff-widths.h\", \"coff-widths.lib\");")
set(header_only_import "let header_only = import:c(\"coff-widths.h\");")
string(REPLACE "widths.coff_size(size)" "header_only.coff_size(size)" repeated_source "${source}")
file(WRITE "${work}/coff-repeated.nra" "${header_only_import}\n${repeated_source}")
string(REPLACE "${linked_import}" "${linked_import}\n${header_only_import}" linked_first_source "${repeated_source}")
file(WRITE "${work}/coff-repeated-linked-first.nra" "${linked_first_source}")

# The generated module is parsed before the bridge's dependency supplies the
# archive. The entry still calls through its original header-only import.
string(REPLACE "${linked_import}"
    "let widths = import:c(\"coff-widths.h\");\nlet bridge = import:CoffPromotionBridge;" late_source "${source}")
string(REPLACE "widths.coff_signed_long(signed_value)" "bridge.signed_long(signed_value)" late_source "${late_source}")
file(WRITE "${work}/coff-late.nra" "${late_source}")
file(WRITE "${work}/coff-promotion-bridge.nra" [=[let linked = import:CoffPromotionInput;

export let CoffPromotionBridge = module (
    export let signed_long = func(value: int32) -> int32 {
        return linked.signed_long(value);
    };
);
]=])
file(WRITE "${work}/coff-promotion-input.nra" [=[let widths = import:c("coff-widths.h", "coff-widths.lib");

export let CoffPromotionInput = module (
    export let signed_long = func(value: int32) -> int32 {
        return widths.coff_signed_long(value);
    };
);
]=])
set(expected "-2147483647\n4294967295\n55357\n4294967297\n")
foreach(test IN ITEMS coff-widths coff-object coff-repeated coff-repeated-linked-first coff-late)
    invoke("link ${test}" "${NEURA}" build --cc "${C_COMPILER}" "${test}.nra" -o "${test}.nvr")
    run_program("run ${test}" "${test}.nvr" "${expected}")
    invoke("inspect the embedded DLL" "${NEURA}" disassemble "${test}.nvr" -o "${test}.nvra")
    file(STRINGS "${work}/${test}.nvra" native_library REGEX "^native-library " LIMIT_COUNT 1)
    if(NOT native_library MATCHES "^native-library \"x86_64-windows-msvc\" 4[dD]5[aA]")
        message(FATAL_ERROR "${test}: expected Win64 PE bytes in NATV")
    endif()
endforeach()
invoke("verify PE export and CRT dependencies" "${PYTHON}" "${fixtures}/check-pe.py"
    "${work}/coff-widths.nvra" coff_signed_long coff_unsigned_long coff_wide_char coff_size)

# A header-only shim exports its wrapper. Its external declaration keeps
# using the default library lookup even though the image embeds a DLL.
file(WRITE "${work}/inline.h" "int abs(int value);\nstatic inline long coff_inline(long value) { return value + 7; }\n")
file(WRITE "${work}/coff-inline.nra" [=[let console = import:Std.Io.Console;
let program = import:Std.Program;
let header = import:c("inline.h");

export let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(header.coff_inline(35).to_string());
        console.print_line(header.abs(-9).to_string());
        return program.ExitCode.Ok;
    };
);
]=])
invoke("link the header-only shim" "${NEURA}" build --cc "${C_COMPILER}" coff-inline.nra -o coff-inline.nvr)
run_program("run the header-only shim" coff-inline.nvr "42\n9\n")
invoke("inspect the shim exports" "${NEURA}" disassemble coff-inline.nvr -o coff-inline.nvra)
file(STRINGS "${work}/coff-inline.nvra" inline_exports REGEX "^native-symbol ")
if(NOT inline_exports STREQUAL "native-symbol \"neura_inline_coff_inline\"")
    message(FATAL_ERROR "expected only the generated wrapper in NATV, got ${inline_exports}")
endif()
invoke("verify the shim PE export and CRT dependencies" "${PYTHON}" "${fixtures}/check-pe.py"
    "${work}/coff-inline.nvra" neura_inline_coff_inline)

# External-only headers still need only libclang at build time.
file(WRITE "${work}/external.h" "int abs(int value);\n")
file(READ "${work}/coff-inline.nra" external_source)
string(REPLACE "inline.h" "external.h" external_source "${external_source}")
string(REPLACE "        console.print_line(header.coff_inline(35).to_string());\n" "" external_source "${external_source}")
file(WRITE "${work}/coff-external.nra" "${external_source}")
invoke("build external-only import without a compiler" "${NEURA}"
    build --cc compiler-must-not-be-used coff-external.nra -o coff-external.nvr)
run_program("run the external-only import" coff-external.nvr "9\n")

# Both positive inputs above supply definitions for exactly this header. A
# similarly named DLL import library may only describe another binary.
set(entry [=[let program = import:Std.Program;
export let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        widths.coff_signed_long(1);
        return program.ExitCode.Ok;
    };
);
]=])
reject_link(import-archive external.dll.lib "DLL import")
file(WRITE "${work}/readme.obj" "This is not COFF.\n")
reject_link(wrong-object-format readme.obj "expected an x64 COFF object or static archive")
file(WRITE "${work}/incomplete.c" "int a_different_symbol(void) { return 0; }\n")
invoke("compile the unrelated x64 COFF object" "${C_COMPILER}" /nologo /c /MT /utf-8 /Foincomplete.obj incomplete.c)
reject_link(undefined-export incomplete.obj "linking the native library of the C imports failed")

message(STATUS "COFF object, static archive, LLP64 signatures, PE payload, shim exports, and rejected link inputs checked")
