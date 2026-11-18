#!/bin/sh
# Runs the release qualification checks on the three archives of a target.
#
# usage: qualify.sh --archive FILE --sdk-archive FILE --runtime-archive FILE
#                   --version VERSION --triple TRIPLE --examples DIR
#                   [--previous FILE] [--require-previous]
#                   [--bridge FILE]... [--require-bridge]
#                   [--size-reference FILE]
#
#   --archive          the legacy archive neura-<version>-<triple>.tar.gz
#   --sdk-archive      neura-sdk-<version>-<triple>.tar.gz
#   --runtime-archive  neura-runtime-<version>-<triple>.tar.gz
#   --version          the full version the archives must report
#   --triple           the target triple they must report
#   --examples         the repository's examples directory
#   --previous         an archive of the previous release, for the upgrade
#                      check: a 0.2 archive updates with its own 0.2 client and
#                      is then migrated; a 0.3 SDK archive updates with the 0.3
#                      client
#   --require-previous fail when --previous is not given
#   --bridge           a 0.2 archive (repeatable) for the 0.2 bridge: its own
#                      neura update, then neura update --migrate-install
#                      (docs/design/Lifecycle030.md, "The 0.2 bridge")
#   --require-bridge   fail when a bridge check does not run
#   --size-reference   the archive the size budget is measured against
#                      (rc.3's of this target): the SDK and legacy archives may
#                      be at most 55% larger, and the runtime archive must be
#                      smaller than the SDK archive
#
# The script installs into new temporary HOMEs, with a PATH that holds only
# the system directories, so nothing from a development checkout can take
# part. It needs sh, tar, cmp, and the usual POSIX utilities, and nothing
# else.
#
# Each check prints one line: "PASS n name", "FAIL n name: reason", or
# "NOT RUN n name: reason". The exit status is 0 only when no check failed
# and, with --require-previous or --require-bridge, those checks ran.
set -eu

archive=""
sdk_archive=""
runtime_archive=""
version=""
triple=""
examples=""
previous=""
require_previous=0
bridges=""
require_bridge=0
size_reference=""
while [ $# -gt 0 ]; do
    case "$1" in
        --archive) archive=$2; shift 2 ;;
        --sdk-archive) sdk_archive=$2; shift 2 ;;
        --runtime-archive) runtime_archive=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --triple) triple=$2; shift 2 ;;
        --examples) examples=$2; shift 2 ;;
        --previous) previous=$2; shift 2 ;;
        --require-previous) require_previous=1; shift ;;
        --bridge) bridges="$bridges $(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; shift 2 ;;
        --require-bridge) require_bridge=1; shift ;;
        --size-reference) size_reference=$2; shift 2 ;;
        *) echo "qualify.sh: unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$archive" ] || [ -z "$sdk_archive" ] || [ -z "$runtime_archive" ] || [ -z "$version" ] || [ -z "$triple" ] || [ -z "$examples" ]; then
    echo "qualify.sh: --archive, --sdk-archive, --runtime-archive, --version, --triple, and --examples are required" >&2
    exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/neura-qualify.XXXXXX")
trap 'chmod -R u+w "$work" 2>/dev/null || true; rm -rf "$work"' EXIT
absolute() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
archive=$(absolute "$archive")
sdk_archive=$(absolute "$sdk_archive")
runtime_archive=$(absolute "$runtime_archive")
examples=$(cd "$examples" && pwd)
if [ -n "$previous" ]; then
    previous=$(absolute "$previous")
fi
if [ -n "$size_reference" ]; then
    size_reference=$(absolute "$size_reference")
fi
# The tools of each component (docs/design/Products030.md), as
# src/cli/tool_registry.cpp lists them.
core_tools="neura-run neura-inspect neura-doctor neura-update neura-uninstall neura-server-host"
sdk_tools="neurac neura-test neura-doc neura-cbind neura-decompile neura-asm neura-disasm neura-explain neura-tags neura-debug neura-lsp neura-fmt"

failed=0
not_run=0
pass() { echo "PASS $1 $2"; }
fail() { echo "FAIL $1 $2: $3"; failed=$((failed + 1)); }
skip() { echo "NOT RUN $1 $2: $3"; not_run=$((not_run + 1)); }

# Runs a command with an empty environment: a new HOME and the system PATH.
clean() {
    # Not "home": the checks keep their own HOME in that variable.
    clean_home=$1
    shift
    env -i HOME="$clean_home" PATH="$clean_home/.local/bin:/usr/bin:/bin" LC_ALL=C "$@"
}

# install_archive HOME ARCHIVE [ADD-ARCHIVE]: unpacks the archives and runs
# the first one's installer, with --add for the second.
install_archive() {
    mkdir -p "$1/unpack"
    tar -xzf "$2" -C "$1/unpack"
    top=$(tar -tzf "$2" | head -n 1 | cut -d / -f 1)
    if [ -n "${3:-}" ]; then
        tar -xzf "$3" -C "$1/unpack"
        other=$(tar -tzf "$3" | head -n 1 | cut -d / -f 1)
        (cd "$1/unpack/$top" && clean "$1" sh ./install.sh --add "$1/unpack/$other") > "$1/install.log" 2>&1
    else
        (cd "$1/unpack/$top" && clean "$1" sh ./install.sh) > "$1/install.log" 2>&1
    fi
}

size_of() { wc -c < "$1" | tr -d ' '; }
digest_of() {
    if command -v sha256sum > /dev/null 2>&1; then sha256sum "$1" | cut -d ' ' -f 1; else shasum -a 256 "$1" | cut -d ' ' -f 1; fi
}

name="neura-$version-$triple"
sdk_name="neura-sdk-$version-$triple"
runtime_name="neura-runtime-$version-$triple"
home=$work/home
mkdir -p "$home"

# 1. The three archives: one directory each, the expected contents, the
# legacy archive is the SDK archive's tree, and the core is identical.
mkdir "$work/list" "$work/list-sdk" "$work/list-runtime"
reason=""
if ! tar -xzf "$archive" -C "$work/list" 2> "$work/tar.log" || [ "$(ls "$work/list")" != "$name" ]; then
    reason="expected one directory $name in the legacy archive"
elif ! tar -xzf "$sdk_archive" -C "$work/list-sdk" 2>> "$work/tar.log" || [ "$(ls "$work/list-sdk")" != "$sdk_name" ]; then
    reason="expected one directory $sdk_name in the SDK archive"
elif ! tar -xzf "$runtime_archive" -C "$work/list-runtime" 2>> "$work/tar.log" || [ "$(ls "$work/list-runtime")" != "$runtime_name" ]; then
    reason="expected one directory $runtime_name in the runtime archive"
else
    top=$work/list/$name
    for path in bin/neura install.sh share/doc/neura/LICENSE share/neura/stdlib/std share/neura/components/core.txt share/neura/components/sdk.txt; do
        [ -e "$top/$path" ] || reason="$reason $path is missing;"
    done
    [ "$(cat "$top/VERSION" 2> /dev/null)" = "$version" ] || reason="$reason VERSION is not $version;"
    for tool in $core_tools $sdk_tools; do
        [ -x "$top/bin/$tool" ] || reason="$reason bin/$tool is missing;"
    done
    for library in core sdk; do
        ls "$top/lib/neura/libneura-$library-$version".* > /dev/null 2>&1 || reason="$reason libneura-$library-$version is missing;"
    done
    diff -r "$top" "$work/list-sdk/$sdk_name" > "$work/diff.log" 2>&1 || reason="$reason the legacy and SDK trees differ;"
    while read -r kind mode sum size path; do
        [ "$kind" = file ] || continue
        cmp -s "$top/$path" "$work/list-runtime/$runtime_name/$path" || reason="$reason $path differs between the SDK and runtime archives;"
    done < "$top/share/neura/components/core.txt"
    cmp -s "$top/share/neura/components/core.txt" "$work/list-runtime/$runtime_name/share/neura/components/core.txt" ||
        reason="$reason the core manifests differ;"
    if [ -n "$(find "$work/list" "$work/list-sdk" "$work/list-runtime" ! -type f ! -type d)" ]; then
        reason="$reason an archive holds something that is not a regular file or a directory;"
    fi
fi
if [ -z "$reason" ]; then
    pass 1 "archive contents"
else
    fail 1 "archive contents" "$reason"
fi

# 2. The SDK installs into a new home directory with a link for every tool.
reason=""
if install_archive "$home" "$sdk_archive"; then
    for tool in neura $core_tools $sdk_tools; do
        [ -L "$home/.local/bin/$tool" ] || reason="$reason no link $tool;"
    done
    [ ! -e "$home/.local/bin/neurad" ] || reason="$reason a neurad link;"
    [ "$(sed -n 1p "$home/.local/share/neura/neura-install.txt" 2> /dev/null)" = "neura-install 2" ] || reason="$reason no schema 2 marker;"
else
    reason="$(tail -n 5 "$home/install.log" 2> /dev/null | tr '\n' ' ')"
fi
if [ -z "$reason" ]; then
    pass 2 "install"
else
    fail 2 "install" "$reason"
fi

# 3. The installed command reports the version and target of the release.
reported=$(clean "$home" neura --version 2>&1 || true)
case "$reported" in
    "neura $version ($triple, commit "*) pass 3 "version report" ;;
    *) fail 3 "version report" "printed '$reported'" ;;
esac

# 4. The installation's self-check finds no failure.
if clean "$home" neura doctor > "$work/doctor.log" 2>&1; then
    pass 4 "doctor"
else
    fail 4 "doctor" "$(grep FAIL "$work/doctor.log" | tr '\n' ' ')"
fi

# 5. Hello World builds and runs, through the dispatcher and the direct tools.
mkdir "$work/hello"
if (cd "$work/hello" \
    && clean "$home" neura build "$examples/hello-world/hello-world.nra" -o hello.nvr \
    && [ "$(clean "$home" neura run hello.nvr)" = "Hello, World!" ] \
    && clean "$home" neurac "$examples/hello-world/hello-world.nra" -o direct.nvr \
    && [ "$(clean "$home" neura-run direct.nvr)" = "Hello, World!" ]) > "$work/hello.log" 2>&1; then
    pass 5 "build and run"
else
    fail 5 "build and run" "$(tail -n 3 "$work/hello.log" | tr '\n' ' ')"
fi

# 6. The test runner passes the example test module.
if clean "$home" neura test -I "$examples/guide" "$examples/testing/math-tests.nra" > "$work/test.log" 2>&1; then
    pass 6 "test runner"
else
    fail 6 "test runner" "$(tail -n 3 "$work/test.log" | tr '\n' ' ')"
fi

# 7. A native executable builds and runs where the release has a launcher.
case "$triple" in
    *-linux-gnu)
        if (cd "$work/hello" \
            && clean "$home" neura build --native "$examples/hello-world/hello-world.nra" -o hello \
            && [ "$(env -i ./hello)" = "Hello, World!" ]) > "$work/native.log" 2>&1; then
            pass 7 "native executable"
        else
            fail 7 "native executable" "$(tail -n 3 "$work/native.log" | tr '\n' ' ')"
        fi
        ;;
    *)
        if (cd "$work/hello" \
            && ! clean "$home" neura build --native "$examples/hello-world/hello-world.nra" -o hello) > "$work/native.log" 2>&1; then
            pass 7 "native executable is refused on a target without a launcher"
        else
            fail 7 "native executable" "build --native succeeded on $triple, which has no launcher"
        fi
        ;;
esac

# 8. The update check finds this release up to date. The release directory
# holds the three archives and the two metadata files that
# make-release-metadata.sh writes.
mkdir "$work/release"
cp "$archive" "$sdk_archive" "$runtime_archive" "$work/release/"
build=$(sed -n 's/^build //p' "$work/list-sdk/$sdk_name/share/neura/components/core.txt")
printf 'neura-release 1\nversion %s\nasset %s %s %s %s\n' \
    "$version" "$triple" "$(basename "$archive")" "$(digest_of "$archive")" "$(size_of "$archive")" > "$work/release/neura-release.txt"
printf 'neura-components 1\nversion %s\nbuild %s\narchive runtime %s %s %s %s\narchive sdk %s %s %s %s\n' "$version" "$build" \
    "$triple" "$(basename "$runtime_archive")" "$(digest_of "$runtime_archive")" "$(size_of "$runtime_archive")" \
    "$triple" "$(basename "$sdk_archive")" "$(digest_of "$sdk_archive")" "$(size_of "$sdk_archive")" > "$work/release/neura-components.txt"
if clean "$home" neura update --check --source "$work/release" > "$work/check.log" 2>&1; then
    pass 8 "update check"
else
    fail 8 "update check" "$(tail -n 3 "$work/check.log" | tr '\n' ' ')"
fi

# bridge N HOME OLD-ARCHIVE: a 0.2 installation updates with its own client
# into this release, works unmigrated, and migrates.
bridge() {
    check=$1
    old=$2
    old_archive=$3
    label=$(basename "$old_archive" .tar.gz)
    mkdir -p "$old"
    if ! install_archive "$old" "$old_archive"; then
        fail "$check" "bridge from $label" "the old archive did not install: $(tail -n 3 "$old/install.log" | tr '\n' ' ')"
        return
    fi
    before=$(clean "$old" neura --version 2>&1 || true)
    if ! clean "$old" neura update --source "$work/release" > "$old/update.log" 2>&1; then
        fail "$check" "bridge from $label" "the old neura update failed: $(tail -n 3 "$old/update.log" | tr '\n' ' ')"
        return
    fi
    if grep -q "is up to date" "$old/update.log"; then
        skip "$check" "bridge from $label" "$version is not newer than $(echo "$before" | cut -d ' ' -f 2)"
        return
    fi
    reason=""
    case "$(clean "$old" neura --version 2>&1)" in
        "neura $version ("*) ;;
        *) reason="$reason the old update did not activate $version;" ;;
    esac
    [ ! -e "$old/.local/share/neura/leases" ] || reason="$reason a leases/ directory before the migration;"
    [ ! -e "$old/.local/bin/neurac" ] || reason="$reason neurac is on PATH before the migration;"
    (cd "$work/hello" && clean "$old" neura build "$examples/hello-world/hello-world.nra" -o "$old/bridged.nvr" \
        && [ "$(clean "$old" neura run "$old/bridged.nvr")" = "Hello, World!" ] \
        && clean "$old" neura test -I "$examples/guide" "$examples/testing/math-tests.nra") > "$old/run.log" 2>&1 ||
        reason="$reason build, run or test failed unmigrated: $(tail -n 2 "$old/run.log" | tr '\n' ' ');"
    if clean "$old" neura doctor > "$old/doctor.log" 2>&1 || ! grep -q "FAIL  migration" "$old/doctor.log"; then
        reason="$reason doctor did not fail on migration;"
    fi
    if ! clean "$old" neura update --migrate-install > "$old/migrate.log" 2>&1; then
        reason="$reason --migrate-install failed: $(tail -n 2 "$old/migrate.log" | tr '\n' ' ');"
    else
        [ "$(sed -n 1p "$old/.local/share/neura/neura-install.txt")" = "neura-install 2" ] || reason="$reason no schema 2 marker;"
        for tool in neura $core_tools $sdk_tools; do
            [ -L "$old/.local/bin/$tool" ] || reason="$reason no link $tool;"
        done
        clean "$old" neurac --version > /dev/null 2>&1 || reason="$reason neurac does not run from PATH;"
        clean "$old" neura doctor > "$old/doctor2.log" 2>&1 || reason="$reason doctor fails after the migration: $(grep FAIL "$old/doctor2.log" | tr '\n' ' ');"
        clean "$old" neura update --migrate-install 2>&1 | grep -q "already migrated" || reason="$reason a second --migrate-install;"
    fi
    if [ -z "$reason" ]; then
        pass "$check" "bridge from $(echo "$before" | cut -d ' ' -f 2)"
    else
        fail "$check" "bridge from $label" "$reason"
    fi
}

# 9. An installation of the previous release updates to this one.
if [ -n "$previous" ]; then
    case "$(basename "$previous")" in
        neura-sdk-*)
            old=$work/old
            mkdir -p "$old"
            if install_archive "$old" "$previous" \
                && before=$(clean "$old" neura --version) \
                && clean "$old" neura update --source "$work/release" > "$work/update.log" 2>&1 \
                && after=$(clean "$old" neura --version) \
                && [ "$before" != "$after" ] \
                && case "$after" in "neura $version ("*) true ;; *) false ;; esac \
                && clean "$old" neura doctor > "$work/update-doctor.log" 2>&1 \
                && (cd "$work/hello" && clean "$old" neura build "$examples/hello-world/hello-world.nra" -o upgraded.nvr \
                    && [ "$(clean "$old" neura run upgraded.nvr)" = "Hello, World!" ]) >> "$work/update.log" 2>&1 \
                && clean "$old" neura update --rollback > "$work/rollback.log" 2>&1 \
                && [ "$(clean "$old" neura --version)" = "$before" ] \
                && clean "$old" neura update --rollback >> "$work/rollback.log" 2>&1 \
                && clean "$old" neura uninstall --yes > "$work/old-uninstall.log" 2>&1; then
                pass 9 "upgrade from $(echo "$before" | cut -d ' ' -f 2), rollback, and uninstall"
            else
                fail 9 "upgrade" "$(tail -n 5 "$work/update.log" "$work/rollback.log" 2>/dev/null | tr '\n' ' ')"
            fi
            ;;
        *) bridge 9 "$work/old" "$previous" ;;
    esac
elif [ "$require_previous" = 1 ]; then
    fail 9 "upgrade" "no previous archive was given and one is required"
else
    skip 9 "upgrade" "no previous archive was given"
fi

# 10. Uninstalling removes the installation, and installing again works.
if clean "$home" neura uninstall --yes > "$work/uninstall.log" 2>&1 \
    && [ ! -e "$home/.local/share/neura" ] \
    && [ -z "$(find "$home/.local/bin" -type l 2> /dev/null)" ]; then
    rm -rf "$home/unpack"
    if install_archive "$home" "$sdk_archive" && clean "$home" neura --version > /dev/null 2>&1; then
        pass 10 "uninstall and reinstall"
    else
        fail 10 "uninstall and reinstall" "installing again failed: $(tail -n 3 "$home/install.log" | tr '\n' ' ')"
    fi
else
    fail 10 "uninstall and reinstall" "$(tail -n 5 "$work/uninstall.log" | tr '\n' ' ') (files remain under $home/.local)"
fi

# service_runs HOME: one launch of hello-world through the installed
# runtime service, then the service's shutdown.
service_runs() {
    out=$(clean "$1" NEURA_GPU=off neura run --runtime=required "$work/hello/hello.nvr" 2>> "$work/service.log") || true
    clean "$1" neura runtime shutdown >> "$work/service.log" 2>&1 || true
    [ "$out" = "Hello, World!" ]
}

# 11. The runtime archive installs alone: no compiler or standard library,
# the service neurad, images run directly and (on Linux) through the
# service, source commands name Neura-sdk, and doctor is healthy.
runtime_home=$work/runtime-home
reason=""
# install_archive sets top, so the runtime archive's listing has a name
# of its own.
runtime_top=$work/list-runtime/$runtime_name
for tool in $sdk_tools; do
    [ ! -e "$runtime_top/bin/$tool" ] || reason="$reason bin/$tool is an SDK tool;"
done
[ ! -e "$runtime_top/share/neura/stdlib" ] || reason="$reason it holds the standard library;"
! ls "$runtime_top"/lib/neura/libneura-sdk-* > /dev/null 2>&1 || reason="$reason it holds the SDK library;"
[ -x "$runtime_top/bin/neurad" ] || reason="$reason it does not hold the service, bin/neurad;"
if [ -z "$reason" ]; then
    mkdir -p "$runtime_home"
    if ! install_archive "$runtime_home" "$runtime_archive"; then
        reason="the runtime archive did not install: $(tail -n 3 "$runtime_home/install.log" | tr '\n' ' ')"
    elif [ -e "$runtime_home/.local/bin/neurac" ]; then
        reason="a neurac link"
    elif ! [ "$(clean "$runtime_home" neura run "$work/hello/hello.nvr" 2>> "$work/runtime.log")" = "Hello, World!" ] \
        || ! [ "$(clean "$runtime_home" neura-run "$work/hello/hello.nvr" 2>> "$work/runtime.log")" = "Hello, World!" ]; then
        reason="the image did not run: $(tail -n 2 "$work/runtime.log" | tr '\n' ' ')"
    elif clean "$runtime_home" neura build "$examples/hello-world/hello-world.nra" -o "$work/runtime-x.nvr" > "$work/runtime-build.log" 2>&1 \
        || ! grep -q 'needs Neura-sdk, which is not installed here' "$work/runtime-build.log"; then
        reason="neura build did not name Neura-sdk: $(tr '\n' ' ' < "$work/runtime-build.log")"
    elif ! clean "$runtime_home" neura doctor > "$work/runtime-doctor.log" 2>&1; then
        reason="doctor failed: $(grep FAIL "$work/runtime-doctor.log" | tr '\n' ' ')"
    elif [ ! -L "$runtime_home/.local/bin/neurad" ]; then
        reason="no neurad link"
    elif [ "$(uname -s)" = Linux ] && ! service_runs "$runtime_home"; then
        reason="the service did not run the image: $(tail -n 3 "$work/service.log" | tr '\n' ' ')"
    fi
fi
if [ -z "$reason" ]; then
    pass 11 "runtime archive"
else
    fail 11 "runtime archive" "$reason"
fi

# 12. The SDK and runtime archives install together as one generation.
both=$work/both-home
mkdir -p "$both"
if install_archive "$both" "$sdk_archive" "$runtime_archive" \
    && [ "$(ls "$both/.local/share/neura/versions" | wc -l | tr -d ' ')" = 1 ] \
    && grep -q '^components core sdk service$' "$both/.local/share/neura/neura-install.txt" \
    && clean "$both" neura doctor > "$work/both-doctor.log" 2>&1 \
    && [ -L "$both/.local/bin/neurad" ]; then
    pass 12 "SDK and runtime together"
else
    fail 12 "SDK and runtime together" "$(tail -n 3 "$both/install.log" 2> /dev/null | tr '\n' ' ') $(grep FAIL "$work/both-doctor.log" 2> /dev/null | tr '\n' ' ')"
fi

# 13. The 0.2 bridge from each given 0.2 archive.
count=0
bridged=0
for old_archive in $bridges; do
    count=$((count + 1))
    before_failed=$failed
    before_skipped=$not_run
    bridge "13.$count" "$work/bridge-$count" "$old_archive"
    if [ "$failed" = "$before_failed" ] && [ "$not_run" = "$before_skipped" ]; then
        bridged=$((bridged + 1))
    fi
done
if [ "$require_bridge" = 1 ] && [ "$bridged" -lt 2 ]; then
    fail 13 "bridge" "the bridge must run from v0.2.1-rc.3 and v0.2.0; $bridged ran"
elif [ -z "$bridges" ]; then
    skip 13 "bridge" "no 0.2 archive was given"
fi

# 14. The size report and budget.
sdk_size=$(size_of "$sdk_archive")
runtime_size=$(size_of "$runtime_archive")
legacy_size=$(size_of "$archive")
echo "size: $(basename "$sdk_archive") $sdk_size bytes; $(basename "$runtime_archive") $runtime_size bytes; $(basename "$archive") $legacy_size bytes"
reason=""
[ "$runtime_size" -lt "$sdk_size" ] || reason="the runtime archive is not smaller than the SDK archive;"
if [ -n "$size_reference" ]; then
    reference=$(size_of "$size_reference")
    limit=$((reference + reference * 55 / 100))
    echo "size: the reference $(basename "$size_reference") is $reference bytes; the limit is $limit"
    [ "$sdk_size" -le "$limit" ] || reason="$reason the SDK archive is more than 55% larger than the reference;"
    [ "$legacy_size" -le "$limit" ] || reason="$reason the legacy archive is more than 55% larger than the reference;"
fi
if [ -z "$reason" ]; then
    pass 14 "archive sizes"
else
    fail 14 "archive sizes" "$reason"
fi

echo "$failed failed, $not_run not run."
[ "$failed" = 0 ]
