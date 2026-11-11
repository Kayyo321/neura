#!/bin/sh
# Runs the release qualification checks on one release archive.
#
# usage: qualify.sh --archive FILE --version VERSION --triple TRIPLE
#                   --examples DIR [--previous FILE] [--require-previous]
#
#   --archive   the neura-<version>-<triple>.tar.gz file to qualify
#   --version   the full version the archive must report, such as 0.1.0-rc.1
#   --triple    the target triple the archive must report
#   --examples  the repository's examples directory
#   --previous  an archive of an earlier version, for the upgrade check
#   --require-previous  fail when --previous is not given
#
# The script installs into a new temporary HOME, with a PATH that holds only
# the system directories, so nothing from a development checkout can take
# part. It needs sh, tar, and the usual POSIX utilities, and nothing else.
#
# Each check prints one line: "PASS n name", "FAIL n name: reason", or
# "NOT RUN n name: reason". The exit status is 0 only when no check failed
# and, with --require-previous, the upgrade check ran.
set -eu

archive=""
version=""
triple=""
examples=""
previous=""
require_previous=0
while [ $# -gt 0 ]; do
    case "$1" in
        --archive) archive=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --triple) triple=$2; shift 2 ;;
        --examples) examples=$2; shift 2 ;;
        --previous) previous=$2; shift 2 ;;
        --require-previous) require_previous=1; shift ;;
        *) echo "qualify.sh: unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$archive" ] || [ -z "$version" ] || [ -z "$triple" ] || [ -z "$examples" ]; then
    echo "qualify.sh: --archive, --version, --triple, and --examples are required" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'chmod -R u+w "$work" 2>/dev/null || true; rm -rf "$work"' EXIT
archive=$(cd "$(dirname "$archive")" && pwd)/$(basename "$archive")
examples=$(cd "$examples" && pwd)
if [ -n "$previous" ]; then
    previous=$(cd "$(dirname "$previous")" && pwd)/$(basename "$previous")
fi

failed=0
not_run=0
pass() { echo "PASS $1 $2"; }
fail() { echo "FAIL $1 $2: $3"; failed=$((failed + 1)); }
skip() { echo "NOT RUN $1 $2: $3"; not_run=$((not_run + 1)); }

# Runs a command with an empty environment: a new HOME and the system PATH.
clean() {
    home=$1
    shift
    env -i HOME="$home" PATH="$home/.local/bin:/usr/bin:/bin" LC_ALL=C "$@"
}

# install_archive HOME ARCHIVE: unpacks an archive and runs its installer.
install_archive() {
    mkdir -p "$1/unpack"
    tar -xzf "$2" -C "$1/unpack"
    directory=$(ls "$1/unpack")
    (cd "$1/unpack/$directory" && clean "$1" sh ./install.sh) > "$1/install.log" 2>&1
}

name="neura-$version-$triple"
home=$work/home
mkdir -p "$home"

# 1. The archive holds one directory with the expected contents.
mkdir "$work/list"
if tar -xzf "$archive" -C "$work/list" 2> "$work/tar.log" \
    && [ "$(ls "$work/list")" = "$name" ] \
    && [ -x "$work/list/$name/bin/neura" ] \
    && [ -f "$work/list/$name/install.sh" ] \
    && [ -f "$work/list/$name/share/doc/neura/LICENSE" ] \
    && [ -d "$work/list/$name/share/neura/stdlib/std" ] \
    && [ "$(cat "$work/list/$name/VERSION")" = "$version" ]; then
    pass 1 "archive contents"
else
    fail 1 "archive contents" "expected one directory $name with bin/neura, install.sh, LICENSE, the standard library, and VERSION"
fi

# 2. The installer installs into a new home directory.
if install_archive "$home" "$archive" \
    && [ -L "$home/.local/bin/neura" ] \
    && [ -f "$home/.local/share/neura/neura-install.txt" ]; then
    pass 2 "install"
else
    fail 2 "install" "$(tail -n 5 "$home/install.log" 2>/dev/null | tr '\n' ' ')"
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

# 5. Hello World builds and runs.
mkdir "$work/hello"
if (cd "$work/hello" \
    && clean "$home" neura build "$examples/hello-world/hello-world.nra" -o hello.nvr \
    && [ "$(clean "$home" neura run hello.nvr)" = "Hello, World!" ]) > "$work/hello.log" 2>&1; then
    pass 5 "build and run"
else
    fail 5 "build and run" "$(tail -n 3 "$work/hello.log" | tr '\n' ' ')"
fi

# 6. The test runner passes the example test module.
if clean "$home" neura test "$examples/testing/math-tests.nra" > "$work/test.log" 2>&1; then
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

# 8. The update check finds this release up to date.
mkdir "$work/release"
cp "$archive" "$work/release/"
size=$(wc -c < "$archive" | tr -d ' ')
if command -v sha256sum > /dev/null 2>&1; then
    digest=$(sha256sum "$archive" | cut -d ' ' -f 1)
else
    digest=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
fi
printf 'neura-release 1\nversion %s\nasset %s %s %s %s\n' \
    "$version" "$triple" "$(basename "$archive")" "$digest" "$size" > "$work/release/neura-release.txt"
if clean "$home" neura update --check --source "$work/release" > "$work/check.log" 2>&1; then
    pass 8 "update check"
else
    fail 8 "update check" "$(tail -n 3 "$work/check.log" | tr '\n' ' ')"
fi

# 9. An installation of the previous version updates to this one.
if [ -n "$previous" ]; then
    old=$work/old
    mkdir -p "$old"
    if install_archive "$old" "$previous" \
        && before=$(clean "$old" neura --version) \
        && clean "$old" neura update --source "$work/release" > "$work/update.log" 2>&1 \
        && after=$(clean "$old" neura --version) \
        && [ "$before" != "$after" ] \
        && case "$after" in "neura $version ("*) true ;; *) false ;; esac \
        && (cd "$work/hello" && clean "$old" neura build "$examples/hello-world/hello-world.nra" -o upgraded.nvr \
            && [ "$(clean "$old" neura run upgraded.nvr)" = "Hello, World!" ]) >> "$work/update.log" 2>&1; then
        pass 9 "upgrade from $(echo "$before" | cut -d ' ' -f 2)"
    else
        fail 9 "upgrade" "$(tail -n 5 "$work/update.log" 2>/dev/null | tr '\n' ' ')"
    fi
elif [ "$require_previous" = 1 ]; then
    fail 9 "upgrade" "no previous archive was given and one is required"
else
    skip 9 "upgrade" "no previous archive was given"
fi

# 10. Uninstalling removes the installation, and installing again works.
if clean "$home" neura uninstall --yes > "$work/uninstall.log" 2>&1 \
    && [ ! -e "$home/.local/share/neura" ] \
    && [ ! -e "$home/.local/bin/neura" ] && [ ! -L "$home/.local/bin/neura" ]; then
    rm -rf "$home/unpack"
    if install_archive "$home" "$archive" && clean "$home" neura --version > /dev/null 2>&1; then
        pass 10 "uninstall and reinstall"
    else
        fail 10 "uninstall and reinstall" "installing again failed: $(tail -n 3 "$home/install.log" | tr '\n' ' ')"
    fi
else
    fail 10 "uninstall and reinstall" "$(tail -n 5 "$work/uninstall.log" | tr '\n' ' ') (files remain under $home/.local)"
fi

echo "$failed failed, $not_run not run."
[ "$failed" = 0 ]
