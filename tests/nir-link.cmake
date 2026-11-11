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
    # The short form is the stable one-line form (Tooling.md, Diagnostics).
    execute_process(COMMAND "${NEURA}" ${ARGN} --error-format=short
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
    };

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
        };

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
    };
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
    };
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
    };
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
reject("missing-module.nra:1:[0-9]+: error(\\[[EW][0-9]+\\])?:.*does not declare module 'Missing'"
    check "${artifacts}/missing-module.nra")
file(WRITE "${artifacts}/broken.nir" "invalid bytes")
file(WRITE "${artifacts}/bad-file.nra" [=[
let invalid = import:"broken.nir";
]=])
reject("bad-file.nra:1:[0-9]+: error(\\[[EW][0-9]+\\])?:.*not a NeuraIR file" check "${artifacts}/bad-file.nra")
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

# Accessors (Types.md, Accessors) survive a .nir round trip: the importing
# program reaches the fields through the hidden accessor methods, a generic
# class's accessors specialize, a write to a get-only field is still
# rejected, and the image is the one built from source.
set(accessor_sources "${TEST_DIR}/accessors")
file(MAKE_DIRECTORY "${accessor_sources}")
file(WRITE "${accessor_sources}/gadget.nra" [=[
let console = import:Std.Io.Console;

let Gadget = module (
    export let Meter = class (
        export let reading: int = 0;
            | get: {
                console.print_line("get reading");
                return reading;
            };
            | set: {
                console.print_line("set reading " + value.to_string());
                reading = value;
            };
        export let serial = "G-1";
            | get: serial;
    );

    export let Cell = class<T> (
        export let item: T;
            | get: item;
            | set: item = value;

        export let init = func(item: T) {
            instance.item = item;
        };
    );
);
]=])
set(accessor_app [=[
let gadget = import:GADGET;
let console = import:Std.Io.Console;
let program = import:Std.Program;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let meter = new gadget.Meter();
        meter.reading = 4;
        meter.reading += 3;
        console.print_line(meter.serial + " " + meter.reading.to_string());
        let cell = new gadget.Cell<int>(1);
        cell.item++;
        console.print_line(cell.item.to_string());
        return program.ExitCode.Ok;
    };
);
]=])
string(REPLACE "GADGET" "Gadget" source_app "${accessor_app}")
file(WRITE "${accessor_sources}/app.nra" "${source_app}")
neura(emit-ir "${accessor_sources}/gadget.nra" -o "${artifacts}/gadget.nir")
string(REPLACE "GADGET" "\"gadget.nir\"" artifact_app "${accessor_app}")
file(WRITE "${artifacts}/accessor-app.nra" "${artifact_app}")
set(expected "set reading 4\nget reading\nset reading 7\nget reading\nG-1 7\n2\n")
neura(build --no-source-map "${artifacts}/accessor-app.nra" -o "${TEST_DIR}/accessor-artifact.nvr")
neura(run "${TEST_DIR}/accessor-artifact.nvr")
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "accessors through .nir produced:\n${output}")
endif()
neura(build --no-source-map "${accessor_sources}/app.nra" -o "${TEST_DIR}/accessor-source.nvr")
file(SHA256 "${TEST_DIR}/accessor-artifact.nvr" from_artifact)
file(SHA256 "${TEST_DIR}/accessor-source.nvr" from_source)
if(NOT from_artifact STREQUAL from_source)
    message(FATAL_ERROR "the image built through gadget.nir differs from the one built from source")
endif()
file(WRITE "${artifacts}/accessor-read-only.nra" [=[
let gadget = import:"gadget.nir";
let meter = new gadget.Meter();
meter.serial = "G-2";
]=])
reject("accessor-read-only.nra:3:1: error\\[E0510\\]: 'serial' is read-only outside its class"
    check "${artifacts}/accessor-read-only.nra")

# An anonymous class with accessors is a class of its own; a program that
# imports it from a .nir file reaches its fields through the accessors too.
file(WRITE "${source}/knobs.nra" [=[
let console = import:Std.Io.Console;

let Knobs = module (
    export let settings: class (
        export let level = 1;
            | get: level;
            | set: {
                console.print_line("set level " + value.to_string());
                level = value;
            };
        export let name = "knobs";
            | get: name;
    ) = new;
);
]=])
neura(emit-ir "${source}/knobs.nra" -o "${artifacts}/knobs.nir")
file(WRITE "${artifacts}/knobs-app.nra" [=[
let knobs = import:"knobs.nir".Knobs;
let console = import:Std.Io.Console;
let program = import:Std.Program;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        knobs.settings.level = 3;
        knobs.settings.level += 1;
        console.print_line(knobs.settings.name + " " + knobs.settings.level.to_string());
        return program.ExitCode.Ok;
    };
);
]=])
neura(build "${artifacts}/knobs-app.nra" -o "${TEST_DIR}/knobs-app.nvr")
neura(run "${TEST_DIR}/knobs-app.nvr")
if(NOT output STREQUAL "set level 3\nset level 4\nknobs 4\n")
    message(FATAL_ERROR "anonymous class accessors through .nir produced:\n${output}")
endif()
file(WRITE "${artifacts}/knobs-read-only.nra" [=[
let knobs = import:"knobs.nir".Knobs;
let rename = func() { knobs.settings.name = "x"; };
]=])
reject("knobs-read-only.nra:2:23: error\\[E0510\\]: 'name' is read-only outside its class"
    check "${artifacts}/knobs-read-only.nra")

# A fields-only anonymous class is one class across a module and the program
# that links its artifact, and across modules compiled separately that write
# the same fields (Types.md, Anonymous classes): each declares the class in
# the same generated file, which linking coalesces.
file(WRITE "${source}/points.nra" [=[
let Points = module (
    export let make = func(x: int) -> class (export let x: int; export let label: string;) {
        let made: class (export let x: int = 0; export let label: string = "point";) = new;
        made.x = x;
        return made;
    };
    export let show = func(p: class (export let x: int; export let label: string;)) -> string {
        return p.label + ":" + p.x.to_string();
    };
);
]=])
file(WRITE "${source}/other-points.nra" [=[
let OtherPoints = module (
    export let make = func() -> class (export let x: int; export let label: string;) {
        let made: class (export let x: int = 1; export let label: string = "other";) = new;
        return made;
    };
);
]=])
neura(emit-ir "${source}/points.nra" -o "${artifacts}/points.nir")
neura(emit-ir "${source}/other-points.nra" -o "${artifacts}/other-points.nir")
file(WRITE "${artifacts}/points-app.nra" [=[
let points = import:"points.nir".Points;
let console = import:Std.Io.Console;
let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let mine: class (export let x: int = 5; export let label: string = "mine";) = new;
        console.print_line(points.show(mine));
        let back: class (export let x: int; export let label: string;) = points.make(6);
        console.print_line(points.show(back) + " " + back.to_string());
        return program.ExitCode.Ok;
    };
);
]=])
foreach(mode IN ITEMS optimized plain)
    set(flags)
    if(mode STREQUAL "plain")
        set(flags --no-optimize)
    endif()
    neura(build ${flags} "${artifacts}/points-app.nra" -o "${TEST_DIR}/points-${mode}.nvr")
    neura(run "${TEST_DIR}/points-${mode}.nvr")
    if(NOT output STREQUAL "mine:5\npoint:6 class(x: int32, label: string)#1\n")
        message(FATAL_ERROR "${mode} anonymous class link produced:\n${output}")
    endif()
endforeach()
neura(emit-ir "${artifacts}/points-app.nra" -o "${TEST_DIR}/points-combined.nir")
neura(build "${TEST_DIR}/points-combined.nir" -o "${TEST_DIR}/points-round-trip.nvr")
file(SHA256 "${TEST_DIR}/points-optimized.nvr" original)
file(SHA256 "${TEST_DIR}/points-round-trip.nvr" rebuilt)
if(NOT original STREQUAL rebuilt)
    message(FATAL_ERROR "linked anonymous class round trip changed the executable")
endif()
file(WRITE "${artifacts}/points-mixed.nra" [=[
let points = import:"points.nir".Points;
let other = import:"other-points.nir".OtherPoints;
let console = import:Std.Io.Console;
let program = import:Std.Program;
let Mixed = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(points.show(other.make()));
        let mine: class (export let x: int = 2; export let label: string = "mine";) = new;
        console.print_line(points.show(mine));
        return program.ExitCode.Ok;
    };
);
]=])
neura(build "${artifacts}/points-mixed.nra" -o "${TEST_DIR}/points-mixed.nvr")
neura(run "${TEST_DIR}/points-mixed.nvr")
if(NOT output STREQUAL "other:1\nmine:2\n")
    message(FATAL_ERROR "separately compiled anonymous classes produced:\n${output}")
endif()
neura(emit-ir "${artifacts}/points-mixed.nra" -o "${TEST_DIR}/points-mixed.nir")
neura(build "${TEST_DIR}/points-mixed.nir" -o "${TEST_DIR}/points-mixed-round-trip.nvr")
file(SHA256 "${TEST_DIR}/points-mixed.nvr" original)
file(SHA256 "${TEST_DIR}/points-mixed-round-trip.nvr" rebuilt)
if(NOT original STREQUAL rebuilt)
    message(FATAL_ERROR "linked anonymous classes from two modules changed in the round trip")
endif()

# A source file compiled into two artifacts declares the classes its bodies
# write in the same way in both, whichever file of an artifact wrote them first.
file(WRITE "${source}/shape-user.nra" [=[
let points = import:Points;
let ShapeUser = module (
    let mine: class (export let x: int = 4; export let label: string = "user";) = new;
    export let go = func() -> string => points.show(mine);
);
]=])
file(WRITE "${source}/shape-other.nra" [=[
let points = import:Points;
let ShapeOther = module (
    export let go = func() -> string => "other";
);
]=])
neura(emit-ir -I "${source}" "${source}/shape-user.nra" -o "${artifacts}/shape-user.nir")
neura(emit-ir -I "${source}" "${source}/shape-other.nra" -o "${artifacts}/shape-other.nir")
file(WRITE "${artifacts}/shape-app.nra" [=[
let user = import:"shape-user.nir".ShapeUser;
let other = import:"shape-other.nir".ShapeOther;
let console = import:Std.Io.Console;
let program = import:Std.Program;
let ShapeApp = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(user.go() + " " + other.go());
        return program.ExitCode.Ok;
    };
);
]=])
neura(build "${artifacts}/shape-app.nra" -o "${TEST_DIR}/shape-app.nvr")
neura(run "${TEST_DIR}/shape-app.nvr")
if(NOT output STREQUAL "user:4 other\n")
    message(FATAL_ERROR "a shared source in two artifacts produced:\n${output}")
endif()
