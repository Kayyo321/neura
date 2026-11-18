#!/bin/sh
# Packs install components into a release archive (docs/Install.md).
#
# usage: make-archive.sh --product sdk|runtime|legacy --core DIR [--sdk DIR]
#                        [--service DIR] [--build COMMIT]
#                        <output-dir> <full-version> <triple>
#
# Each DIR is a prefix that "cmake --install build --component <name>
# --prefix DIR" made, holding one component. The products are:
#
#   sdk      neura-sdk-<version>-<triple>.tar.gz      core and sdk
#   runtime  neura-runtime-<version>-<triple>.tar.gz  core and service
#   legacy   neura-<version>-<triple>.tar.gz          core and sdk, the tree of
#            the SDK archive under the name and top directory that 0.2
#            updaters require
#
# The single top directory holds the component files, a manifest for each
# component in share/neura/components/<name>.txt, this repository's
# install.sh, and a VERSION file with the full version. A manifest lists
# the component's version, build, target, requirements, public tools (every
# file in its bin/) and every file with its mode, SHA-256 and size. The
# service component may be empty: a build without neurad still writes its
# manifest, with no tool and no file. --build defaults to the commit that
# the core's bin/neura --version reports.
#
# The archive is reproducible: names are sorted, owners are 0, the time of
# every member is SOURCE_DATE_EPOCH (default 0), and gzip records no name or
# time, so the core files are byte-identical in the three archives of one
# build. It needs GNU tar; on macOS install it as gtar (brew install gnu-tar).

set -eu

die() {
    printf 'make-archive.sh: %s\n' "$*" >&2
    exit 1
}

usage="usage: make-archive.sh --product sdk|runtime|legacy --core DIR [--sdk DIR] [--service DIR] [--build COMMIT] <output-dir> <full-version> <triple>"
product=""
core_dir=""
sdk_dir=""
service_dir=""
build=""
while [ "$#" -gt 0 ]; do
    case $1 in
    --product) [ "$#" -ge 2 ] || die "$usage"; product=$2; shift 2 ;;
    --core) [ "$#" -ge 2 ] || die "$usage"; core_dir=$2; shift 2 ;;
    --sdk) [ "$#" -ge 2 ] || die "$usage"; sdk_dir=$2; shift 2 ;;
    --service) [ "$#" -ge 2 ] || die "$usage"; service_dir=$2; shift 2 ;;
    --build) [ "$#" -ge 2 ] || die "$usage"; build=$2; shift 2 ;;
    --*) die "unknown option: $1 ($usage)" ;;
    *) break ;;
    esac
done
[ "$#" -eq 3 ] || die "$usage"
output_dir=$1
version=$2
triple=$3

printf '%s\n' "$version" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$' ||
    die "not a semantic version: $version"
printf '%s\n' "$triple" | grep -Eq '^[0-9A-Za-z_.-]+$' || die "not a target triple: $triple"
case $product in
sdk | legacy) components="core sdk" ;;
runtime) components="core service" ;;
*) die "--product must be sdk, runtime or legacy ($usage)" ;;
esac
[ -n "$core_dir" ] || die "--core is required"
case $components in
*sdk*) [ -n "$sdk_dir" ] || die "--sdk is required for the $product product" ;;
*) [ -z "$sdk_dir" ] || die "the $product product has no sdk component" ;;
esac
case $components in
*service*) ;;
*) [ -z "$service_dir" ] || die "the $product product has no service component" ;;
esac

# The tools of each component, as src/cli/tool_registry.cpp lists them.
core_tools="neura-run neura-inspect neura-doctor neura-update neura-uninstall neura-server-host"
sdk_tools="neurac neura-test neura-doc neura-cbind neura-decompile neura-asm neura-disasm neura-explain neura-tags neura-debug neura-lsp neura-fmt"
service_tools="neurad"

[ -d "$core_dir" ] || die "$core_dir is not a directory"
for tool in neura $core_tools; do
    [ -x "$core_dir/bin/$tool" ] || die "$core_dir/bin/$tool is missing; install the core component into $core_dir"
done
ls "$core_dir"/lib/neura/libneura-core-* > /dev/null 2>&1 ||
    die "the core library lib/neura/libneura-core-<version> is missing from $core_dir"
[ -f "$core_dir/share/doc/neura/LICENSE" ] || die "$core_dir/share/doc/neura/LICENSE is missing"
for tool in $sdk_tools $service_tools; do
    [ ! -e "$core_dir/bin/$tool" ] || die "$core_dir/bin/$tool is not a core tool; give each component its own prefix"
done
[ ! -e "$core_dir/share/neura/stdlib" ] || die "$core_dir holds the standard library, which is the sdk component's"
if [ -n "$sdk_dir" ]; then
    [ -d "$sdk_dir" ] || die "$sdk_dir is not a directory"
    for tool in $sdk_tools; do
        [ -x "$sdk_dir/bin/$tool" ] || die "$sdk_dir/bin/$tool is missing; install the sdk component into $sdk_dir"
    done
    ls "$sdk_dir"/lib/neura/libneura-sdk-* > /dev/null 2>&1 ||
        die "the SDK library lib/neura/libneura-sdk-<version> is missing from $sdk_dir"
    [ -d "$sdk_dir/share/neura/stdlib/std" ] || die "$sdk_dir/share/neura/stdlib/std is missing"
    for tool in neura $core_tools $service_tools; do
        [ ! -e "$sdk_dir/bin/$tool" ] || die "$sdk_dir/bin/$tool is not an sdk tool; give each component its own prefix"
    done
fi
if [ -n "$service_dir" ]; then
    [ -d "$service_dir" ] || die "$service_dir is not a directory"
    for tool in neura $core_tools $sdk_tools; do
        [ ! -e "$service_dir/bin/$tool" ] || die "$service_dir/bin/$tool is not the service's; give each component its own prefix"
    done
fi
here=$(cd "$(dirname "$0")" && pwd -P)
[ -f "$here/install.sh" ] || die "$here/install.sh is missing"

if [ -z "$build" ]; then
    build=$("$core_dir/bin/neura" --version 2>/dev/null | sed -n 's/^neura .* (.*, commit \([0-9a-z]*\))$/\1/p')
    [ -n "$build" ] || die "cannot read the build commit from $core_dir/bin/neura --version; pass --build"
fi
printf '%s\n' "$build" | grep -Eq '^[0-9a-z]{1,64}$' || die "not a build commit: $build"

if tar --version 2>/dev/null | grep -q 'GNU tar'; then
    tar_program=tar
elif command -v gtar >/dev/null 2>&1 && gtar --version 2>/dev/null | grep -q 'GNU tar'; then
    tar_program=gtar
else
    die "GNU tar is required (on macOS: brew install gnu-tar, which provides gtar)"
fi
if command -v sha256sum >/dev/null 2>&1; then
    sha256_program='sha256sum "$@"'
elif command -v shasum >/dev/null 2>&1; then
    sha256_program='shasum -a 256 "$@"'
else
    die "sha256sum or shasum is required"
fi

epoch=${SOURCE_DATE_EPOCH:-0}
printf '%s\n' "$epoch" | grep -Eq '^[0-9]+$' || die "SOURCE_DATE_EPOCH is not a number: $epoch"

mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd -P)
case $product in
legacy) top=neura-$version-$triple ;;
*) top=neura-$product-$version-$triple ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/neura-archive.XXXXXX")
trap 'rm -rf "$work"' EXIT

# component_tree NAME DIR: copies a component into $work/parts/NAME with
# modes that do not depend on the umask of the build.
component_tree() {
    mkdir -p "$work/parts/$1"
    if [ -n "$2" ]; then
        cp -R "$2/." "$work/parts/$1/"
    fi
    if [ -n "$(find "$work/parts/$1" -type l)" ]; then
        die "the $1 component contains symbolic links, which a release archive must not hold"
    fi
    if [ -n "$(find "$work/parts/$1" ! -type f ! -type d)" ]; then
        die "the $1 component holds something that is not a regular file or a directory"
    fi
    find "$work/parts/$1" -type d -exec chmod 755 {} +
    find "$work/parts/$1" -type f -perm -u+x -exec chmod 755 {} +
    find "$work/parts/$1" -type f ! -perm -u+x -exec chmod 644 {} +
}

# manifest NAME: writes $work/manifests/NAME.txt for $work/parts/NAME.
manifest() {
    part=$work/parts/$1
    out=$work/manifests/$1.txt
    (cd "$part" && find . -type f | sed 's|^\./||' | LC_ALL=C sort) >"$work/$1.files"
    if grep -Ev '^[0-9A-Za-z._+/-]+$' "$work/$1.files" | grep -q .; then
        die "the $1 component has a path with a character other than letters, digits, '.', '_', '+', '-' and '/': $(grep -Ev '^[0-9A-Za-z._+/-]+$' "$work/$1.files" | head -n 1)"
    fi
    if grep -q '^share/neura/components/' "$work/$1.files"; then
        die "the $1 component already holds share/neura/components"
    fi
    {
        printf 'neura-component 1\nname %s\nversion %s\nbuild %s\ntarget %s\n' "$1" "$version" "$build" "$triple"
        [ "$1" = core ] || printf 'requires core\n'
        [ "$1" != service ] || printf 'protocol service 1.0\n'
        grep '^bin/' "$work/$1.files" | sed 's|^bin/||' | LC_ALL=C sort | sed 's/^/tool /'
        if [ -s "$work/$1.files" ]; then
            # One sha256sum run for every file, in the sorted order.
            (cd "$part" && tr '\n' '\0' <"$work/$1.files" | xargs -0 sh -c 'for f; do
                if [ -x "$f" ]; then m=755; else m=644; fi
                s=$(wc -c <"$f" | tr -d " ")
                printf "%s %s %s\n" "$m" "$s" "$f"
            done' sh) >"$work/$1.modes"
            (cd "$part" && tr '\n' '\0' <"$work/$1.files" | xargs -0 sh -c "$sha256_program" sh) |
                sed 's/^\([0-9a-f]*\) [ *]\(.*\)$/\2 \1/' >"$work/$1.sums"
            # Join on the path: "file <mode> <sha256> <size> <path>".
            awk 'NR == FNR { sum[$1] = $2; next } { print "file " $1 " " sum[$3] " " $2 " " $3 }' "$work/$1.sums" "$work/$1.modes"
        fi
    } >"$out"
}

mkdir "$work/parts" "$work/manifests"
for component in $components; do
    case $component in
    core) component_tree core "$core_dir" ;;
    sdk) component_tree sdk "$sdk_dir" ;;
    service) component_tree service "$service_dir" ;;
    esac
    manifest "$component"
done

# One tree: a file belongs to one component.
mkdir "$work/$top"
for component in $components; do
    while IFS= read -r path; do
        [ ! -e "$work/$top/$path" ] || die "$path is in two components"
    done <"$work/$component.files"
    cp -R "$work/parts/$component/." "$work/$top/"
done
mkdir -p "$work/$top/share/neura/components"
for component in $components; do
    cp "$work/manifests/$component.txt" "$work/$top/share/neura/components/$component.txt"
done
cp "$here/install.sh" "$work/$top/install.sh"
printf '%s\n' "$version" >"$work/$top/VERSION"
find "$work/$top" -type d -exec chmod 755 {} +
chmod 644 "$work/$top/share/neura/components/"*.txt "$work/$top/VERSION"
chmod 755 "$work/$top/install.sh"

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
