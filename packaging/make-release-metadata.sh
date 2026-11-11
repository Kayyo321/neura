#!/bin/sh
# Writes SHA256SUMS and neura-release.txt for the archives in a directory.
#
# usage: make-release-metadata.sh <dir-with-archives> <full-version>
#
# It reads every neura-<full-version>-<triple>.tar.gz in the directory and
# writes, next to them:
#
#   SHA256SUMS         "<sha256>  <file>" lines in sha256sum format, sorted by name
#   neura-release.txt  neura-release 1
#                      version <full version>
#                      asset <triple> <file> <sha256> <size in bytes>
#
# "neura update" reads neura-release.txt.

set -eu

die() {
    printf 'make-release-metadata.sh: %s\n' "$*" >&2
    exit 1
}

[ "$#" -eq 2 ] || die "usage: make-release-metadata.sh <dir-with-archives> <full-version>"
directory=$1
version=$2
printf '%s\n' "$version" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$' ||
    die "not a semantic version: $version"
[ -d "$directory" ] || die "$directory is not a directory"

if command -v sha256sum >/dev/null 2>&1; then
    digest() { sha256sum "$1" | cut -d ' ' -f 1; }
elif command -v shasum >/dev/null 2>&1; then
    digest() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
else
    die "sha256sum or shasum is required"
fi

cd "$directory"
prefix=neura-$version-
suffix=.tar.gz
files=$(find . -maxdepth 1 -type f -name 'neura-*.tar.gz' | sed 's|^\./||' | LC_ALL=C sort)
[ -n "$files" ] || die "no neura-*.tar.gz archives in $directory"

sums=SHA256SUMS.tmp
release=neura-release.txt.tmp
: >"$sums"
printf 'neura-release 1\nversion %s\n' "$version" >"$release"
assets=""
for file in $files; do
    case $file in
    "$prefix"*"$suffix") ;;
    *) die "$file does not match neura-$version-<triple>$suffix" ;;
    esac
    triple=${file#"$prefix"}
    triple=${triple%"$suffix"}
    printf '%s\n' "$triple" | grep -Eq '^[0-9A-Za-z_.-]+$' || die "not a target triple in $file: $triple"
    sum=$(digest "$file")
    size=$(wc -c <"$file" | tr -d ' ')
    printf '%s  %s\n' "$sum" "$file" >>"$sums"
    assets="$assets$triple $file $sum $size
"
done
printf '%s' "$assets" | LC_ALL=C sort | while IFS=' ' read -r triple file sum size; do
    printf 'asset %s %s %s %s\n' "$triple" "$file" "$sum" "$size"
done >>"$release"
mv -f "$sums" SHA256SUMS
mv -f "$release" neura-release.txt
printf '%s/SHA256SUMS\n%s/neura-release.txt\n' "$directory" "$directory"
