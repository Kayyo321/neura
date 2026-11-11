#!/bin/sh
# Installs neura from an unpacked release archive.
#
# Run this from the directory the archive unpacked to, the one that holds
# bin/, share/, lib/, install.sh, and VERSION. It never uses sudo and never
# edits shell startup files.

set -eu

usage() {
    cat <<'EOF'
usage: ./install.sh [--force] [--help]

Installs the neura in this unpacked release archive for the current user.

  <root>/versions/<version>/   a copy of bin/, share/, and lib/
  <root>/current               a symbolic link to the newest installed version
  <root>/neura-install.txt     a marker that "neura update" and "neura uninstall" read
  <bin dir>/neura              a symbolic link to <root>/current/bin/neura

Options:
  --force   replace an installed copy of the same version
  --help    print this text

Environment variables (read by this script only):
  NEURA_INSTALL_ROOT  the install root (default: ${XDG_DATA_HOME:-$HOME/.local/share}/neura)
  NEURA_BIN_DIR       the directory for the neura link (default: $HOME/.local/bin)

The script refuses to replace a file in the bin directory that it did not
create, and it refuses to touch an install that a package manager owns.
If the bin directory is not on PATH, the script prints the line to add to
your shell startup file. It does not add the line itself.
EOF
}

die() {
    printf 'install.sh: %s\n' "$*" >&2
    exit 1
}

force=0
for argument in "$@"; do
    case $argument in
    --force) force=1 ;;
    --help | -h)
        usage
        exit 0
        ;;
    *)
        usage >&2
        die "unknown argument: $argument"
        ;;
    esac
done

source_dir=$(cd "$(dirname "$0")" && pwd -P) || die "cannot find the directory of this script"

[ -f "$source_dir/VERSION" ] || die "$source_dir/VERSION is missing; run install.sh from an unpacked release archive"
IFS= read -r version <"$source_dir/VERSION" || die "$source_dir/VERSION is empty"
if ! printf '%s\n' "$version" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$'; then
    die "VERSION does not hold a semantic version: $version"
fi
[ -x "$source_dir/bin/neura" ] || die "$source_dir/bin/neura is missing or not executable"
[ -d "$source_dir/share/neura/stdlib/std" ] || die "$source_dir/share/neura/stdlib/std is missing"

no_newline() {
    case $1 in
    *'
'*) die "$2 must not contain a newline" ;;
    esac
}

absolute_directory() {
    # $1 = path, $2 = name for messages. Creates it and prints its real path.
    case $1 in
    /*) ;;
    *) die "$2 must be an absolute path, not $1" ;;
    esac
    no_newline "$1" "$2"
    mkdir -p "$1" || die "cannot create $1"
    (cd "$1" && pwd -P) || die "cannot enter $1"
}

if [ -n "${NEURA_INSTALL_ROOT:-}" ]; then
    root_request=$NEURA_INSTALL_ROOT
elif [ -n "${XDG_DATA_HOME:-}" ]; then
    root_request=$XDG_DATA_HOME/neura
elif [ -n "${HOME:-}" ]; then
    root_request=$HOME/.local/share/neura
else
    die "HOME is not set; set NEURA_INSTALL_ROOT"
fi
if [ -n "${NEURA_BIN_DIR:-}" ]; then
    bin_request=$NEURA_BIN_DIR
elif [ -n "${HOME:-}" ]; then
    bin_request=$HOME/.local/bin
else
    die "HOME is not set; set NEURA_BIN_DIR"
fi

# Everything is checked before the first change.
marker_versions=""
if [ -e "$root_request/neura-install.txt" ]; then
    marker=$root_request/neura-install.txt
    [ -f "$marker" ] || die "$marker is not a regular file"
    [ "$(sed -n '1p' "$marker")" = "neura-install 1" ] || die "$marker is not a neura install marker"
    method=$(sed -n 's/^method //p' "$marker")
    [ "$method" = "archive" ] || die "$root_request is owned by $method; use $method to manage neura"
    marker_versions=$(sed -n 's/^version //p' "$marker")
fi
root=$(absolute_directory "$root_request" "the install root")
bin_dir=$(absolute_directory "$bin_request" "the bin directory")
link=$bin_dir/neura
target=$root/current/bin/neura

if [ -e "$root/versions/$version" ] || [ -L "$root/versions/$version" ]; then
    if [ "$force" -ne 1 ]; then
        die "neura $version is already installed in $root/versions/$version; pass --force to replace it"
    fi
    if [ -L "$root/versions/$version" ] || [ ! -d "$root/versions/$version" ]; then
        die "$root/versions/$version is not a directory"
    fi
fi

link_exists=0
if [ -L "$link" ]; then
    current_target=$(readlink "$link")
    case $current_target in
    "$root"/*) link_exists=1 ;;
    *) die "$link is a symbolic link to $current_target, outside $root; remove it or choose another NEURA_BIN_DIR" ;;
    esac
elif [ -e "$link" ]; then
    die "$link already exists and is not a link made by this installer; remove it or choose another NEURA_BIN_DIR"
fi

mkdir -p "$root/versions"
staging=$root/.install-$$
cleanup() {
    rm -rf "$staging" "$root/.current-$$" "$root/.marker-$$" "$bin_dir/.neura-link-$$"
}
trap cleanup EXIT
mkdir "$staging" || die "cannot create $staging"
for part in bin share lib; do
    if [ -d "$source_dir/$part" ]; then
        cp -R "$source_dir/$part" "$staging/$part" || die "cannot copy $part"
    fi
done
[ -x "$staging/bin/neura" ] || die "the copy has no bin/neura"

if [ "$force" -eq 1 ] && [ -d "$root/versions/$version" ]; then
    rm -rf "${root:?}/versions/$version"
fi
mv "$staging" "$root/versions/$version" || die "cannot move the copy to $root/versions/$version"

# Point current at the new version. mv -T (GNU) and mv -h (BSD) replace the
# link itself; where neither exists the fallback is not atomic.
point_link() {
    # $1 = what the link says, $2 = the link
    pending=$3
    rm -f "$pending"
    ln -s "$1" "$pending" || die "cannot create a symbolic link in $(dirname "$pending")"
    if [ ! -L "$2" ] && [ ! -e "$2" ]; then
        mv -f "$pending" "$2"
    elif mv -fT "$pending" "$2" 2>/dev/null; then
        :
    elif mv -fh "$pending" "$2" 2>/dev/null; then
        :
    else
        rm -f "$pending"
        ln -sfn "$1" "$2" || die "cannot update $2"
    fi
}
point_link "versions/$version" "$root/current" "$root/.current-$$"

if [ "$link_exists" -eq 0 ]; then
    point_link "$target" "$link" "$bin_dir/.neura-link-$$"
fi

versions=$version
for old in $marker_versions; do
    if [ "$old" != "$version" ] && [ -d "$root/versions/$old" ]; then
        versions="$old
$versions"
    fi
done
{
    printf 'neura-install 1\nmethod archive\nlink %s\n' "$link"
    printf '%s\n' "$versions" | sort -u | while IFS= read -r each; do
        printf 'version %s\n' "$each"
    done
} >"$root/.marker-$$"
mv -f "$root/.marker-$$" "$root/neura-install.txt"

printf 'Installed neura %s\n' "$version"
printf '  root:   %s\n' "$root"
printf '  files:  %s/versions/%s\n' "$root" "$version"
printf '  link:   %s -> %s\n' "$link" "$target"
case ":${PATH:-}:" in
*":$bin_dir:"*) ;;
*)
    printf '\n%s is not on your PATH. To use neura, add this line to your shell startup file:\n\n' "$bin_dir"
    # shellcheck disable=SC2016
    printf '    export PATH="%s:$PATH"\n\n' "$bin_dir"
    ;;
esac
printf 'Run "neura doctor" to check the install.\n'
