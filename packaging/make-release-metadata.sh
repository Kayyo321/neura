#!/bin/sh
# Writes SHA256SUMS, neura-release.txt and neura-components.txt for the
# archives of one release in a directory (docs/Install.md).
#
# usage: make-release-metadata.sh <dir-with-archives> <full-version>
#
# The directory holds, for each target triple, the three archives that
# make-archive.sh makes:
#
#   neura-<version>-<triple>.tar.gz          the legacy archive (0.2 clients)
#   neura-sdk-<version>-<triple>.tar.gz      Neura-sdk
#   neura-runtime-<version>-<triple>.tar.gz  Neura-runtime
#
# and nothing else named neura-*.tar.gz. It writes, next to them:
#
#   SHA256SUMS            "<sha256>  <file>" for every archive, sorted by name
#   neura-release.txt     schema 1, which 0.2 clients read: the version and one
#                         asset line for each target, naming the legacy archive
#   neura-components.txt  neura-components 1, the version, the build that the
#                         archives' core manifests name, and one sorted line
#                         "archive <product> <triple> <file> <sha256> <size>"
#                         for each SDK and runtime archive

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
suffix=.tar.gz
files=$(find . -maxdepth 1 -type f -name 'neura-*.tar.gz' | sed 's|^\./||' | LC_ALL=C sort)
[ -n "$files" ] || die "no neura-*.tar.gz archives in $directory"

work=$(mktemp -d "${TMPDIR:-/tmp}/neura-metadata.XXXXXX")
trap 'rm -rf "$work"' EXIT
: >"$work/legacy"
: >"$work/products"
: >"$work/sums"
build=""
for file in $files; do
    case $file in
    "neura-sdk-$version-"*"$suffix") product=sdk ;;
    "neura-runtime-$version-"*"$suffix") product=runtime ;;
    "neura-$version-"*"$suffix") product=legacy ;;
    *) die "$file is not neura-[sdk-|runtime-]$version-<triple>$suffix" ;;
    esac
    case $product in
    legacy) triple=${file#"neura-$version-"} ;;
    *) triple=${file#"neura-$product-$version-"} ;;
    esac
    triple=${triple%"$suffix"}
    printf '%s\n' "$triple" | grep -Eq '^[0-9A-Za-z_.-]+$' || die "not a target triple in $file: $triple"
    sum=$(digest "$file")
    size=$(wc -c <"$file" | tr -d ' ')
    [ "$size" -gt 0 ] || die "$file is empty"
    printf '%s  %s\n' "$sum" "$file" >>"$work/sums"
    if [ "$product" = legacy ]; then
        printf '%s %s %s %s\n' "$triple" "$file" "$sum" "$size" >>"$work/legacy"
        continue
    fi
    printf '%s %s %s %s %s\n' "$product" "$triple" "$file" "$sum" "$size" >>"$work/products"
    # Every archive names the same version and build in its core manifest.
    top=${file%"$suffix"}
    manifest=$(tar -xzf "$file" -O "$top/share/neura/components/core.txt" 2>/dev/null) ||
        die "$file has no $top/share/neura/components/core.txt"
    [ "$(printf '%s\n' "$manifest" | sed -n 's/^version //p')" = "$version" ] || die "the core manifest of $file is not for version $version"
    [ "$(printf '%s\n' "$manifest" | sed -n 's/^target //p')" = "$triple" ] || die "the core manifest of $file is not for $triple"
    this_build=$(printf '%s\n' "$manifest" | sed -n 's/^build //p')
    [ -n "$this_build" ] || die "the core manifest of $file names no build"
    if [ -z "$build" ]; then
        build=$this_build
    elif [ "$build" != "$this_build" ]; then
        die "$file is build $this_build; another archive is build $build"
    fi
done
# Each target has all three archives.
for triple in $(cut -d ' ' -f 1 "$work/legacy") $(cut -d ' ' -f 2 "$work/products"); do
    grep -q "^$triple " "$work/legacy" || die "there is no neura-$version-$triple$suffix"
    for product in sdk runtime; do
        grep -q "^$product $triple " "$work/products" || die "there is no neura-$product-$version-$triple$suffix"
    done
done

{
    printf 'neura-release 1\nversion %s\n' "$version"
    LC_ALL=C sort "$work/legacy" | while IFS=' ' read -r triple file sum size; do
        printf 'asset %s %s %s %s\n' "$triple" "$file" "$sum" "$size"
    done
} >"$work/release"
{
    printf 'neura-components 1\nversion %s\nbuild %s\n' "$version" "$build"
    LC_ALL=C sort "$work/products" | while IFS=' ' read -r product triple file sum size; do
        printf 'archive %s %s %s %s %s\n' "$product" "$triple" "$file" "$sum" "$size"
    done
} >"$work/components"
LC_ALL=C sort -k 2 "$work/sums" >SHA256SUMS.tmp
cp "$work/release" neura-release.txt.tmp
cp "$work/components" neura-components.txt.tmp
mv -f SHA256SUMS.tmp SHA256SUMS
mv -f neura-release.txt.tmp neura-release.txt
mv -f neura-components.txt.tmp neura-components.txt
printf '%s/SHA256SUMS\n%s/neura-release.txt\n%s/neura-components.txt\n' "$directory" "$directory" "$directory"
