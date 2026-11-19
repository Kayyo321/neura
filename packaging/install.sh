#!/bin/sh
# Installs neura from an unpacked release archive.
#
# Run this from the directory the archive unpacked to, the one that holds
# bin/, share/, lib/, install.sh, and VERSION. It checks the archive and
# hands the installation to the archive's own bin/neura-update, which
# verifies every file against the component manifests, takes the
# installation lock, and makes the change with a journal
# (docs/Install.md). It never uses sudo and never edits shell startup files.

set -eu

usage() {
    cat <<'EOF'
usage: ./install.sh [--force] [--add <dir>] [--help]

Installs the neura components in this unpacked release archive for the
current user.

  <root>/versions/<generation>/  the components of this archive (and of --add)
  <root>/current                 a symbolic link to the active generation
  <root>/neura-install.txt       a marker that "neura update" and "neura uninstall" read
  <bin dir>/<tool>               a symbolic link to <root>/current/bin/<tool>,
                                 one for each tool

Options:
  --force      install again a generation that is already installed: a whole
               one is kept and its links repaired, a damaged one replaced
  --add <dir>  also install the components of a second unpacked archive of
               the same build, such as the runtime archive beside the SDK
  --help       print this text

Environment variables:
  NEURA_INSTALL_ROOT  the install root (default: ${XDG_DATA_HOME:-$HOME/.local/share}/neura)
  NEURA_BIN_DIR       the directory for the links (default: the installation's,
                      else $HOME/.local/bin)

The installer refuses to replace a file in the bin directory that it did not
create, listing every such path and changing nothing, and it refuses to touch
an install that a package manager owns. If the bin directory is not on PATH,
it prints the line to add to your shell startup file. It does not add the
line itself.
EOF
}

die() {
    printf 'install.sh: %s\n' "$*" >&2
    exit 1
}

force=""
add=""
while [ "$#" -gt 0 ]; do
    case $1 in
    --force)
        force=--force
        shift
        ;;
    --add)
        [ "$#" -ge 2 ] || die "--add needs a directory"
        add=$2
        shift 2
        ;;
    --help | -h)
        usage
        exit 0
        ;;
    *)
        usage >&2
        die "unknown argument: $1"
        ;;
    esac
done

source_dir=$(cd "$(dirname "$0")" && pwd -P) || die "cannot find the directory of this script"

[ -f "$source_dir/VERSION" ] || die "$source_dir/VERSION is missing; run install.sh from an unpacked release archive"
IFS= read -r version <"$source_dir/VERSION" || die "$source_dir/VERSION is empty"
if ! printf '%s\n' "$version" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$'; then
    die "VERSION does not hold a semantic version: $version"
fi
updater=$source_dir/bin/neura-update
[ -x "$updater" ] || die "$updater is missing or not executable"
line=$("$updater" --version 2>/dev/null) || die "$updater does not run on this machine"
case $line in
"neura $version "*) ;;
*) die "$updater reports \"$line\"; expected neura $version" ;;
esac
if [ -n "$add" ]; then
    add=$(cd "$add" 2>/dev/null && pwd -P) || die "--add: cannot enter the directory"
fi

if [ -n "$add" ]; then
    exec "$updater" update --install-from "$source_dir" --add "$add" $force
fi
exec "$updater" update --install-from "$source_dir" $force
