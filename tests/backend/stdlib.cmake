file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/package/bin")
file(COPY "${NEURA}" DESTINATION "${TEST_DIR}/package/bin")
get_filename_component(binary "${NEURA}" NAME)
set(tool "${TEST_DIR}/package/bin/${binary}")
set(library "${TEST_DIR}/package/share/neura/stdlib/std")
file(MAKE_DIRECTORY "${library}/internal" "${TEST_DIR}/shadow/std")

file(WRITE "${library}/program.nra" [=[
export let Program = module (
    export let ExitCode = enum (Ok, Failure, Status(int));
    export let Entry = module (
        export let main = func(args: string[]) -> ExitCode;
    );
);
]=])
file(WRITE "${library}/internal/helper.nra" [=[
export let Helper = module (
    export let name = func() -> string { return "HOME"; };
);
]=])
file(WRITE "${library}/services.nra" [=[
let helper = import:Std.Internal.Helper;

export let Services = module (
    export let Outcome = enum (Ok, Error(uint32));
    let Lookup = enum (Ok([bool, string]), Error(uint32));
    export let write = func(message: string) -> Outcome host("neura.console.write_line", 1);
    // The test runs the image with --deny-env, so the call takes its
    // error continuation with code 14 (CapabilityDenied).
    let try_environment = func(name: string) -> Lookup host("neura.env.get", 1);
    let unused = func() -> Outcome host("neura.unavailable.service", 1);

    export let verify_failure = func() -> bool {
        return match (try_environment(helper.name())) {
            Lookup.Ok(_) => false,
            Lookup.Error(code) => code == 14,
        };
    };
);
]=])
file(WRITE "${TEST_DIR}/shadow/std/program.nra" "invalid source from an untrusted root")
file(WRITE "${TEST_DIR}/main.nra" [=[
let program = import:Std.Program;
let services = import:Std.Services;

export let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        if (args.length == 1) {
            return program.ExitCode.Failure;
        }
        if (args.length == 2) {
            return program.ExitCode.Status(256);
        }
        if (!services.verify_failure()) {
            return program.ExitCode.Failure;
        }
        match (services.write("bound host success")) {
            services.Outcome.Ok => { return program.ExitCode.Status(42); },
            services.Outcome.Error(_) => { return program.ExitCode.Failure; },
        }
    };
);
]=])

function(expect status out error)
    execute_process(COMMAND "${tool}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT "${result}" STREQUAL "${status}" OR NOT "${stdout}" STREQUAL "${out}")
        message(FATAL_ERROR "${ARGN} returned ${result}: [${stdout}][${stderr}]")
    endif()
    if(NOT "${stderr}" MATCHES "${error}")
        message(FATAL_ERROR "${ARGN} lacked diagnostic '${error}': [${stderr}]")
    endif()
endfunction()

expect(0 "" "" emit-ir -I "${TEST_DIR}/shadow" "${TEST_DIR}/main.nra" -o "${TEST_DIR}/main.nir")
foreach(mode IN ITEMS optimized unoptimized)
    if(mode STREQUAL "optimized")
        set(flags)
    else()
        set(flags --no-optimize)
    endif()
    expect(0 "" "" build ${flags} "${TEST_DIR}/main.nir" -o "${TEST_DIR}/main.nvr")
    expect(42 "bound host success\n" "" run --deny-env "${TEST_DIR}/main.nvr")
    expect(1 "" "" run "${TEST_DIR}/main.nvr" -- failure)
    expect(134 "" "invalid argument: exit status" run "${TEST_DIR}/main.nvr" -- invalid status)
endforeach()

file(WRITE "${TEST_DIR}/internal.nra" "let helper = import:Std.Internal.Helper;")
expect(1 "" "cannot import internal standard library" check "${TEST_DIR}/internal.nra")
file(WRITE "${TEST_DIR}/reserved.nra" "export let Std = module ();")
expect(1 "" "Std module namespace is reserved" check "${TEST_DIR}/reserved.nra")
file(WRITE "${TEST_DIR}/forged.nra" [=[
export let Forged = module (
    let Outcome = enum (Ok, Error(uint32));
    let host_call = func() -> Outcome host("neura.unavailable.service", 1);
);
]=])
expect(1 "" "host bindings are only allowed in standard library modules" check "${TEST_DIR}/forged.nra")
# The built-in SysIo and Test modules are retired: importing either fails and
# names its standard library replacement.
file(WRITE "${TEST_DIR}/legacy.nra" [=[
let io = import:SysIo;
export let Legacy = module:io.Entry (
    export let.override main = func(args: string[]) -> Entry.ExitCode {
        return Entry.ExitCode.Ok;
    };
);
]=])
expect(1 "" "cannot resolve import 'SysIo': the built-in SysIo module was removed; use Std.Io.Console and Std.Program"
       check "${TEST_DIR}/legacy.nra")
file(WRITE "${TEST_DIR}/legacy-test.nra" [=[
let test = import:Test;
export let LegacyTests = module (
    export let test_one = func() -> void {
        test.assert(true);
    };
);
]=])
expect(1 "" "cannot resolve import 'Test': the built-in Test module was removed; use Std.Test"
       test "${TEST_DIR}/legacy-test.nra")
file(WRITE "${TEST_DIR}/second.nra" [=[
let program = import:Std.Program;
export let Second = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        return program.ExitCode.Ok;
    };
);
]=])
expect(1 "" "more than one entry point" build "${TEST_DIR}/main.nra" "${TEST_DIR}/second.nra" -o "${TEST_DIR}/two.nvr")

file(WRITE "${library}/wrong-signature.nra" [=[
export let WrongSignature = module (
    export let Outcome = enum (Ok, Error(uint32));
    export let write = func(message: int) -> Outcome host("neura.console.write_line", 1);
);
]=])
file(WRITE "${TEST_DIR}/bad-binding.nra" [=[
let program = import:Std.Program;
let wrong = import:Std.WrongSignature;
export let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        wrong.write(1);
        return program.ExitCode.Ok;
    };
);
]=])
expect(0 "" "" build "${TEST_DIR}/bad-binding.nra" -o "${TEST_DIR}/bad-binding.nvr")
expect(1 "" "does not match the signature of the registered service" run "${TEST_DIR}/bad-binding.nvr")

# Kernel bindings (docs/runtime/Kernels.md, "Binding a kernel"): a trusted
# module binds kernels of each result shape, a program calls them, and the
# image runs with every capability denied, since a kernel is not a host
# request. A kernel's trap status is the program trap of its message.
file(WRITE "${library}/kernel-bindings.nra" [=[
export let KernelBindings = module (
    export let crc32 = func(bytes: uint8[list]) -> uint32 kernel("neura.digest.crc32", 1);
    export let hex = func(bytes: uint8[list]) -> string kernel("neura.encoding.hex_encode", 1);
    export let unhex = func(text: string) -> [uint32, int64, uint8[list]] kernel("neura.encoding.hex_decode", 1);
    export let sort = func(items: int64[list]) kernel("neura.sort.i64", 1);
    export let format = func(value: int64, radix: int64) -> string kernel("neura.integer.format_i64", 1);
    export let compare = func(a: string, b: string) -> int32 kernel("neura.text.compare", 1);
);
]=])
file(WRITE "${TEST_DIR}/kernels.nra" [=[
let program = import:Std.Program;
let services = import:Std.Services;
let kernels = import:Std.KernelBindings;

export let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        services.write(kernels.hex([0, 15, 255]));
        services.write(kernels.crc32([49, 50, 51, 52, 53, 54, 55, 56, 57]).to_string());
        let items: int64[list] = [5, -3, 9, 0];
        kernels.sort(items);
        services.write(items.to_string());
        let bad = kernels.unhex("0g");
        services.write(bad[0].to_string() + " " + bad[1].to_string() + " " + bad[2].length.to_string());
        let good = kernels.unhex("00ff");
        services.write(good[0].to_string() + " " + good[1].to_string() + " " + good[2].to_string());
        services.write(kernels.compare("a", "b").to_string() + " " + kernels.format(-255, 16));
        if (args.length == 1) {
            services.write(kernels.format(1, 99));
        }
        return program.ExitCode.Status(42);
    };
);
]=])
set(kernel_output "000fff\n3421780262\n[-3, 0, 5, 9]\n23 1 0\n0 0 [0, 255]\n-1 -ff\n")
foreach(flags IN ITEMS "" "--no-optimize")
    expect(0 "" "" build ${flags} "${TEST_DIR}/kernels.nra" -o "${TEST_DIR}/kernels.nvr")
    expect(42 "${kernel_output}" "" run --deny-all "${TEST_DIR}/kernels.nvr")
    expect(134 "${kernel_output}" "explicit program failure \\(trap reason 3\\): invalid argument: radix outside 2 through 36"
           run "${TEST_DIR}/kernels.nvr" -- radix)
endforeach()
# Recording traces only the console writes: the kernel calls are not requests.
expect(42 "${kernel_output}" "" run --record "${TEST_DIR}/kernels.trace" "${TEST_DIR}/kernels.nvr")
file(READ "${TEST_DIR}/kernels.trace" kernel_trace)
string(REGEX MATCHALL "\nrequest " kernel_requests "${kernel_trace}")
list(LENGTH kernel_requests kernel_request_count)
if(NOT kernel_request_count EQUAL 6 OR kernel_trace MATCHES "kernel")
    message(FATAL_ERROR "a kernel call reached the trace:\n${kernel_trace}")
endif()
expect(42 "${kernel_output}" "" run --replay "${TEST_DIR}/kernels.trace" "${TEST_DIR}/kernels.nvr")
# The image round-trips through the assembler, and the decompiler names the
# kernel as an opaque operation.
expect(0 "" "" disassemble "${TEST_DIR}/kernels.nvr" -o "${TEST_DIR}/kernels.nvra")
expect(0 "" "" assemble "${TEST_DIR}/kernels.nvra" -o "${TEST_DIR}/kernels-again.nvr")
file(SHA256 "${TEST_DIR}/kernels.nvr" kernel_image)
file(SHA256 "${TEST_DIR}/kernels-again.nvr" kernel_reassembled)
if(NOT kernel_image STREQUAL kernel_reassembled)
    message(FATAL_ERROR "a kernel image did not round-trip through disassemble and assemble")
endif()
expect(0 "" "" decompile "${TEST_DIR}/kernels.nvr" -o "${TEST_DIR}/kernels.decompiled")
file(READ "${TEST_DIR}/kernels.decompiled" kernel_decompiled)
if(NOT kernel_decompiled MATCHES "kernel \"neura.sort.i64\" v1\\([^)]*\\) \\[opaque kernel\\]")
    message(FATAL_ERROR "the decompiler did not name the kernel call:\n${kernel_decompiled}")
endif()

# Tags and kernel trailers together (docs/lang/Tags.md): an #Internal module
# of #Hidden, bodyless kernel bindings serves another trusted module, and a
# program outside the library cannot import it (E0911).
file(WRITE "${library}/tagged-kernels.nra" [=[
/// Kernel bindings for the library's own use.
#Internal
export let TaggedKernels = module (
    /// The CRC-32 of the bytes.
    #[Hidden, Since("0.2.0")]
    export let crc32 = func(bytes: uint8[list]) -> uint32 kernel("neura.digest.crc32", 1);

    #Hidden
    #NoDiscard
    export let hex = func(bytes: uint8[list]) -> string kernel("neura.encoding.hex_encode", 1);
);
]=])
file(WRITE "${library}/kernel-facade.nra" [=[
let kernels = import:Std.TaggedKernels;

export let KernelFacade = module (
    export let checksum = func(bytes: uint8[list]) -> string {
        return kernels.hex([0, 15]) + " " + kernels.crc32(bytes).to_string();
    };
);
]=])
file(WRITE "${TEST_DIR}/tagged-kernels.nra" [=[
let program = import:Std.Program;
let services = import:Std.Services;
let facade = import:Std.KernelFacade;

export let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        services.write(facade.checksum([49, 50, 51, 52, 53, 54, 55, 56, 57]));
        return program.ExitCode.Ok;
    };
);
]=])
foreach(flags IN ITEMS "" "--no-optimize")
    expect(0 "" "" build ${flags} "${TEST_DIR}/tagged-kernels.nra" -o "${TEST_DIR}/tagged-kernels.nvr")
    expect(0 "000f 3421780262\n" "" run --deny-all "${TEST_DIR}/tagged-kernels.nvr")
endforeach()
file(WRITE "${TEST_DIR}/internal-kernels.nra" [=[
let kernels = import:Std.TaggedKernels;

let checksum = func(bytes: uint8[list]) -> uint32 {
    return kernels.crc32(bytes);
};
]=])
expect(1 "" "error\\[E0911\\]: module 'TaggedKernels' is internal to its library" check "${TEST_DIR}/internal-kernels.nra")

# A binding's signature must be the one its kernel's kinds give (E0781), and
# the kernel must exist at that ABI version (E0782).
foreach(case IN ITEMS
        "wrong|uint8[list]) -> uint64 kernel(\"neura.digest.crc32\", 1)|E0781.*needs the signature func\\(bytes: uint8\\[list\\]\\) -> uint32"
        "unknown|uint8[list]) -> uint32 kernel(\"neura.digest.md5\", 1)|E0782.*no kernel is named 'neura.digest.md5'"
        "version|uint8[list]) -> uint32 kernel(\"neura.digest.crc32\", 9)|E0782.*has no version 9 of kernel 'neura.digest.crc32'")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 name)
    list(GET parts 1 signature)
    list(GET parts 2 diagnostic)
    file(WRITE "${library}/kernel${name}.nra"
        "export let Kernel${name} = module (\n    export let sum = func(bytes: ${signature};\n);\n")
    file(WRITE "${TEST_DIR}/kernel-${name}.nra" "let bad = import:Std.Kernel${name};\n")
    expect(1 "" "${diagnostic}" check --error-format=human "${TEST_DIR}/kernel-${name}.nra")
endforeach()
