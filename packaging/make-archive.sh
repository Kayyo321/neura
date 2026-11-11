#!/bin/sh
# Packs an install prefix into a release archive.
#
# usage: make-archive.sh <install-prefix> <output-dir> <full-version> <triple>
#
# <install-prefix> is a directory made by "cmake --install --prefix". The
# output is <output-dir>/neura-<full-version>-<triple>.tar.gz, whose single
# top-level directory neura-<full-version>-<triple>/ holds the prefix, this
# repository's install.sh, and a VERSION file with the full version.
#
# The archive is reproducible: names are sorted, owners are 0, the time of
# every member is SOURCE_DATE_EPOCH (default 0), and gzip records no name or
# time. It needs GNU tar; on macOS install it as gtar (brew install gnu-tar).

set -eu

die() {
    printf 'make-archive.sh: %s\n' "$*" >&2
    exit 1
}

[ "$#" -eq 4 ] || die "usage: make-archive.sh <install-prefix> <output-dir> <full-version> <triple>"
prefix=$1
output_dir=$2
version=$3
triple=$4

printf '%s\n' "$version" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$' ||
    die "not a semantic version: $version"
printf '%s\n' "$triple" | grep -Eq '^[0-9A-Za-z_.-]+$' || die "not a target triple: $triple"
[ -d "$prefix" ] || die "$prefix is not a directory"
[ -x "$prefix/bin/neura" ] || die "$prefix/bin/neura is missing; install into the prefix first"
here=$(cd "$(dirname "$0")" && pwd -P)
[ -f "$here/install.sh" ] || die "$here/install.sh is missing"

if tar --version 2>/dev/null | grep -q 'GNU tar'; then
    tar_program=tar
elif command -v gtar >/dev/null 2>&1 && gtar --version 2>/dev/null | grep -q 'GNU tar'; then
    tar_program=gtar
else
    die "GNU tar is required (on macOS: brew install gnu-tar, which provides gtar)"
fi

epoch=${SOURCE_DATE_EPOCH:-0}
printf '%s\n' "$epoch" | grep -Eq '^[0-9]+$' || die "SOURCE_DATE_EPOCH is not a number: $epoch"

mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd -P)
top=neura-$version-$triple
work=$(mktemp -d "${TMPDIR:-/tmp}/neura-archive.XXXXXX")
trap 'rm -rf "$work"' EXIT

mkdir "$work/$top"
cp -R "$prefix/." "$work/$top/"
cp "$here/install.sh" "$work/$top/install.sh"
printf '%s\n' "$version" >"$work/$top/VERSION"

if [ -n "$(find "$work/$top" -type l)" ]; then
    die "the prefix contains symbolic links, which a release archive must not hold"
fi
# Modes that do not depend on the umask of the build.
find "$work/$top" -type d -exec chmod 755 {} +
find "$work/$top" -type f -perm -u+x -exec chmod 755 {} +
find "$work/$top" -type f ! -perm -u+x -exec chmod 644 {} +

(cd "$work" && find "$top" -print | LC_ALL=C sort >"$work.list")
trap 'rm -rf "$work" "$work.list" "$work.tar"' EXIT
archive=$output_dir/$top.tar.gz
(
    cd "$work"
    LC_ALL=C "$tar_program" --create --format=gnu --no-recursion --files-from="$work.list" \
        --owner=0 --group=0 --numeric-owner --mtime="@$epoch" --file="$work.tar"
)
gzip -n -9 <"$work.tar" >"$archive.tmp"
mv -f "$archive.tmp" "$archive"
printf '%s\n' "$archive"
