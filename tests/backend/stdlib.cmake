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
        export let main = func(string[] args) -> ExitCode;
    );
);
]=])
file(WRITE "${library}/internal/helper.nra" [=[
export let Helper = module (
    export let name = func() -> string { return "HOME"; }
);
]=])
file(WRITE "${library}/services.nra" [=[
let helper = import:Std.Internal.Helper;

export let Services = module (
    export let Outcome = enum (Ok, Error(uint32));
    let Lookup = enum (Ok([bool, string]), Error(uint32));
    export let write = func(string message) -> Outcome host("neura.console.write_line", 1);
    // Environment access is not granted by default, so the call takes its
    // error continuation with code 14 (CapabilityDenied).
    let try_environment = func(string name) -> Lookup host("neura.env.get", 1);
    let unused = func() -> Outcome host("neura.unavailable.service", 1);

    export let verify_failure = func() -> bool {
        return match (try_environment(helper.name())) {
            Lookup.Ok(_) => false,
            Lookup.Error(code) => code == 14,
        };
    }
);
]=])
file(WRITE "${TEST_DIR}/shadow/std/program.nra" "invalid source from an untrusted root")
file(WRITE "${TEST_DIR}/main.nra" [=[
let program = import:Std.Program;
let services = import:Std.Services;

export let App = module:program.Entry (
    export let.override main = func(string[] args) -> program.ExitCode {
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
    }
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
    expect(42 "bound host success\n" "" run "${TEST_DIR}/main.nvr")
    expect(1 "" "" run "${TEST_DIR}/main.nvr" -- failure)
    expect(70 "" "invalid argument: exit status" run "${TEST_DIR}/main.nvr" -- invalid status)
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
    export let.override main = func(string[] args) -> Entry.ExitCode {
        return Entry.ExitCode.Ok;
    }
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
    export let.override main = func(string[] args) -> program.ExitCode {
        return program.ExitCode.Ok;
    }
);
]=])
expect(1 "" "more than one entry point" build "${TEST_DIR}/main.nra" "${TEST_DIR}/second.nra" -o "${TEST_DIR}/two.nvr")

file(WRITE "${library}/wrong-signature.nra" [=[
export let WrongSignature = module (
    export let Outcome = enum (Ok, Error(uint32));
    export let write = func(int message) -> Outcome host("neura.console.write_line", 1);
);
]=])
file(WRITE "${TEST_DIR}/bad-binding.nra" [=[
let program = import:Std.Program;
let wrong = import:Std.WrongSignature;
export let App = module:program.Entry (
    export let.override main = func(string[] args) -> program.ExitCode {
        wrong.write(1);
        return program.ExitCode.Ok;
    }
);
]=])
expect(0 "" "" build "${TEST_DIR}/bad-binding.nra" -o "${TEST_DIR}/bad-binding.nvr")
expect(1 "" "does not match the signature of the registered service" run "${TEST_DIR}/bad-binding.nvr")
