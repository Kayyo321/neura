file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/source" "${TEST_DIR}/artifacts")
set(source "${TEST_DIR}/source")
set(artifacts "${TEST_DIR}/artifacts")

function(neura)
    execute_process(COMMAND "${NEURA}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${ARGN} failed (${status})\n${error}")
    endif()
    set(output "${output}" PARENT_SCOPE)
endfunction()

function(reject diagnostic)
    execute_process(COMMAND "${NEURA}" ${ARGN}
        RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status STREQUAL "1" OR NOT error MATCHES "${diagnostic}")
        message(FATAL_ERROR "expected ${diagnostic} from ${ARGN}, got (${status})\n${error}")
    endif()
endfunction()

file(WRITE "${source}/shared.nra" [=[
let console = import:Std.Io.Console;
console.print_line("shared startup");

let Shared = module (
    let initial = func -> int {
        console.print_line("static startup");
        return 3;
    }

    export let Label = interface (
        let label = func -> string;
    );

    export let Counter = class implements Label (
        export static const Scale: int = 2 * 3;
        export static let offset: int = initial();
        let value: int = 0;

        export let init = func(seed: int) {
            value = seed;
        } or func(seed: string) {
            value = seed.byte_length;
        }

        export let label = func -> string => value.to_string();
        export let pin = func -> &int => value;
        export let.as int = func -> int => value;
    );

    export let single = singleton (
        export let value: int = 7;
    );

    export let count = func(items: ...int) -> int => items.length;
    export let count_any = func(items: ...) -> int => items.length;
    export let identity = func<T>(value: T) -> T => value;
);
]=])
file(WRITE "${source}/first.nra" [=[
let shared = import:Shared;
let First = module (
    export let make = func(seed: int) -> shared.Counter => new shared.Counter(seed)
        or func(seed: string) -> shared.Counter => new shared.Counter(seed);
    export let identity = func<T>(value: T) -> T => shared.identity<T>(value);
);
]=])
file(WRITE "${source}/second.nra" [=[
let shared = import:Shared;
let Second = module (
    export let take = func(value: shared.Counter) -> string => value.label();
    export let imported_label = func(value: shared.Label) -> string => value.label();
    export let read_offset = func -> int => shared.Counter.offset;
);
]=])
neura(emit-ir "${source}/first.nra" -o "${artifacts}/first.nir")
neura(emit-ir "${source}/second.nra" -o "${artifacts}/second.nir")
neura(emit-ir "${source}/shared.nra" -o "${artifacts}/shared.nir")

file(WRITE "${artifacts}/app.nra" [=[
let first = import:"first.nir".First;
let second = import:Second;
let shared = import:"shared.nir";
let same_first = import:"./first.nir";
let console = import:Std.Io.Console;
let program = import:Std.Program;

let Derived = class:shared.Counter (
    export let init = func(seed: string) {
        super.init(seed);
    }
);

let print = func(value: int) {
    console.print_line(value.to_string());
};

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        print(shared.Counter.Scale);
        console.print_line(second.take(first.make(4)));
        console.print_line(second.take(same_first.make("hello")));
        console.print_line(second.imported_label(new Derived("yes")));
        print(first.make(8));
        print(shared.single.value);
        print(shared.count(1, 2, 3));
        print(shared.count_any("s", 4));
        print(first.identity<int>(9));
        print(second.read_offset());
        let retained: &int = first.make(12).pin();
        retained += 1;
        print(retained);
        return program.ExitCode.Ok;
    }
);
]=])
set(expected "shared startup\n6\nstatic startup\n4\n5\n3\n8\n7\n3\n2\n9\n3\n13\n")
foreach(mode IN ITEMS optimized plain)
    set(flags)
    if(mode STREQUAL "plain")
        set(flags --no-optimize)
    endif()
    neura(build ${flags} "${artifacts}/app.nra" -o "${TEST_DIR}/${mode}.nvr")
    neura(run "${TEST_DIR}/${mode}.nvr")
    if(NOT output STREQUAL expected)
        message(FATAL_ERROR "${mode} linked run produced:\n${output}")
    endif()
endforeach()
neura(emit-ir "${artifacts}/app.nra" -o "${TEST_DIR}/combined.nir")
file(RENAME "${artifacts}/first.nir" "${artifacts}/first-hidden.nir")
neura(build "${TEST_DIR}/combined.nir" -o "${TEST_DIR}/round-trip.nvr")
file(SHA256 "${TEST_DIR}/optimized.nvr" original)
file(SHA256 "${TEST_DIR}/round-trip.nvr" rebuilt)
if(NOT original STREQUAL rebuilt)
    message(FATAL_ERROR "linked IR round trip changed the executable")
endif()
file(RENAME "${artifacts}/first-hidden.nir" "${artifacts}/first.nir")

file(WRITE "${artifacts}/bridge.nra" [=[
let first = import:"first.nir";
let Bridge = module (
    export let wrapped = func -> int => first.make(11) as int;
);
]=])
neura(emit-ir "${artifacts}/bridge.nra" -o "${artifacts}/bridge.nir")
file(WRITE "${artifacts}/bridge-app.nra" [=[
let bridge = import:"bridge.nir";
let shared = import:"shared.nir";
let console = import:Std.Io.Console;
let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(bridge.wrapped().to_string());
        console.print_line(shared.single.value.to_string());
        return program.ExitCode.Ok;
    }
);
]=])
file(RENAME "${artifacts}/first.nir" "${artifacts}/first-hidden.nir")
neura(build "${artifacts}/bridge-app.nra" -o "${TEST_DIR}/bridge.nvr")
neura(run "${TEST_DIR}/bridge.nvr")
if(NOT output STREQUAL "shared startup\nstatic startup\n11\n7\n")
    message(FATAL_ERROR "transitive linked run produced:\n${output}")
endif()
file(RENAME "${artifacts}/first-hidden.nir" "${artifacts}/first.nir")

file(WRITE "${source}/plain.nra" [=[
let Plain = module (
    export let Object = class ();
);
]=])
neura(emit-ir "${source}/plain.nra" -o "${artifacts}/plain.nir")
file(WRITE "${artifacts}/plain-app.nra" [=[
let plain = import:"plain.nir";
let value: class = new plain.Object();
let text = value.to_string();
]=])
neura(check "${artifacts}/plain-app.nra")

file(WRITE "${artifacts}/missing-module.nra" [=[
let missing = import:"first.nir".Missing;
]=])
reject("missing-module.nra:1:[0-9]+: error:.*does not declare module 'Missing'"
    check "${artifacts}/missing-module.nra")
file(WRITE "${artifacts}/broken.nir" "invalid bytes")
file(WRITE "${artifacts}/bad-file.nra" [=[
let invalid = import:"broken.nir";
]=])
reject("bad-file.nra:1:[0-9]+: error:.*not a NeuraIR file" check "${artifacts}/bad-file.nra")
file(WRITE "${artifacts}/private.nra" [=[
let shared = import:"shared.nir";
let private_value = shared.initial();
]=])
reject("unknown or inaccessible member 'initial'" check "${artifacts}/private.nra")

file(READ "${source}/shared.nra" changed)
string(REPLACE "shared startup" "other startup!" changed "${changed}")
file(WRITE "${source}/shared.nra" "${changed}")
neura(emit-ir "${source}/second.nra" -o "${artifacts}/changed.nir")
file(WRITE "${artifacts}/conflict.nra" [=[
let first = import:"first.nir";
let second = import:"changed.nir".Second;
]=])
reject("incompatible definitions of.*shared.nra" check "${artifacts}/conflict.nra")
