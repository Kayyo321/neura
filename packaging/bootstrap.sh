#!/bin/sh
# Downloads, verifies, and installs a Neura release. Each release publishes
# this file as install.sh, so one command installs Neura:
#
#   curl --proto '=https' --tlsv1.2 -fsSL \
#     https://github.com/Kayyo321/neura/releases/latest/download/install.sh | sh
#
# Options, after "sh -s --" when the script is piped:
#
#   --version VERSION  install this version, such as 0.1.0 or 0.1.0-rc.1,
#                      in place of the latest release
#   --source WHERE     read the release from an https:// URL or a local
#                      directory that holds neura-release.txt and the archives
#   --force            replace an installed copy of the same version
#   --help             print this text
#
# What it does: finds this machine's target, reads the release's
# neura-release.txt, downloads the archive for the target, checks the
# archive's size and SHA-256 against that file, checks its build provenance
# when the GitHub CLI (gh) is installed, unpacks it into a temporary
# directory, and runs the install.sh inside the archive. That installer
# copies Neura under ${XDG_DATA_HOME:-$HOME/.local/share}/neura and links
# $HOME/.local/bin/neura. NEURA_INSTALL_ROOT and NEURA_BIN_DIR change the two
# places. Nothing is written anywhere else, sudo is never used, and no shell
# startup file is edited.
#
# It needs sh, curl, tar, and sha256sum or shasum.
#
# The whole script is one function that the last line calls, so a download
# that stops partway runs nothing.

main() {
    set -eu

    repository=Kayyo321/neura
    version=""
    source=""
    force=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --version) [ $# -ge 2 ] || die "--version needs a value"; version=$2; shift 2 ;;
            --source) [ $# -ge 2 ] || die "--source needs a value"; source=$2; shift 2 ;;
            --force) force=--force; shift ;;
            --help) usage; exit 0 ;;
            *) die "unknown option: $1 (try --help)" ;;
        esac
    done

    if [ -n "$version" ]; then
        printf '%s\n' "$version" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$' ||
            die "not a version: $version"
    fi
    if [ -z "$source" ]; then
        if [ -n "$version" ]; then
            source="https://github.com/$repository/releases/download/v$version"
        else
            source="https://github.com/$repository/releases/latest/download"
        fi
    fi
    case "$source" in
        https://*) remote=1 ;;
        http://*) die "an http:// source is not accepted; use https:// or a local directory" ;;
        *) remote=0; [ -d "$source" ] || die "$source is not a directory" ;;
    esac

    # The target triple of this machine.
    system=$(uname -s)
    machine=$(uname -m)
    case "$machine" in
        x86_64 | amd64) architecture=x86_64 ;;
        aarch64 | arm64) architecture=aarch64 ;;
        *) die "Neura has no release for the processor $machine" ;;
    esac
    case "$system" in
        Linux)
            triple=$architecture-linux-gnu
            # The archives need the GNU C library.
            if ! (ldd --version 2>&1 || true) | grep -Eqi 'glibc|gnu libc|gnu c library'; then
                die "this system does not use the GNU C library (glibc), which the Linux archives need. Build Neura from source: https://github.com/$repository/blob/main/CONTRIBUTING.md"
            fi
            ;;
        Darwin) triple=$architecture-apple-darwin ;;
        *) die "Neura has no release for $system. On Windows, run this command inside a WSL 2 distribution" ;;
    esac

    need() { command -v "$1" > /dev/null 2>&1 || die "$1 is required and was not found"; }
    need tar
    if [ "$remote" = 1 ]; then need curl; fi
    if command -v sha256sum > /dev/null 2>&1; then
        digest() { sha256sum "$1" | cut -d ' ' -f 1; }
    elif command -v shasum > /dev/null 2>&1; then
        digest() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
    else
        die "sha256sum or shasum is required and neither was found"
    fi

    work=$(mktemp -d "${TMPDIR:-/tmp}/neura-install.XXXXXX")
    trap 'rm -rf "$work"' EXIT
    trap 'exit 1' INT TERM HUP

    # fetch NAME: copies one file of the release into $work.
    fetch() {
        if [ "$remote" = 1 ]; then
            curl --proto '=https' --proto-redir '=https' --tlsv1.2 --fail --location --silent --show-error \
                --output "$work/$1" "$source/$1" || die "cannot download $source/$1"
        else
            [ -f "$source/$1" ] || die "$source/$1 does not exist"
            cp "$source/$1" "$work/$1"
        fi
    }

    fetch neura-release.txt
    [ "$(sed -n 1p "$work/neura-release.txt")" = "neura-release 1" ] ||
        die "$source/neura-release.txt is not release metadata this script can read"
    released=$(sed -n 's/^version \([0-9A-Za-z.-]*\)$/\1/p' "$work/neura-release.txt")
    [ -n "$released" ] || die "the release metadata names no version"
    if [ -n "$version" ] && [ "$released" != "$version" ]; then
        die "asked for version $version, and the release metadata is for $released"
    fi

    asset=$(awk -v triple="$triple" '$1 == "asset" && $2 == triple && NF == 5 { print $3, $4, $5 }' "$work/neura-release.txt")
    if [ -z "$asset" ]; then
        die "Neura $released has no archive for $triple. Supported platforms: https://github.com/$repository/blob/main/docs/Install.md#supported-platforms"
    fi
    [ "$(printf '%s\n' "$asset" | wc -l | tr -d ' ')" = 1 ] || die "the release metadata lists $triple more than once"
    file=$(printf '%s' "$asset" | cut -d ' ' -f 1)
    expected=$(printf '%s' "$asset" | cut -d ' ' -f 2)
    size=$(printf '%s' "$asset" | cut -d ' ' -f 3)
    [ "$file" = "neura-$released-$triple.tar.gz" ] || die "the release metadata names an unexpected file: $file"

    echo "Downloading Neura $released for $triple"
    fetch "$file"
    actual_size=$(wc -c < "$work/$file" | tr -d ' ')
    [ "$actual_size" = "$size" ] || die "$file is $actual_size bytes; the release metadata says $size"
    actual=$(digest "$work/$file")
    [ "$actual" = "$expected" ] || die "SHA-256 mismatch for $file: got $actual, the release metadata says $expected"
    echo "Verified: size and SHA-256 of $file against the release metadata"

    if [ "$remote" = 1 ] && command -v gh > /dev/null 2>&1; then
        if gh attestation verify "$work/$file" --repo "$repository" > "$work/attestation.log" 2>&1; then
            echo "Verified: build provenance with gh attestation verify --repo $repository"
        else
            sed 's/^/  /' "$work/attestation.log" >&2
            die "gh attestation verify did not confirm the build provenance of $file. Nothing was installed"
        fi
    else
        echo "Not verified: build provenance (gh is not installed, or the source is a local directory)"
    fi

    # Every member must be inside the one expected directory.
    top="neura-$released-$triple"
    if tar -tzf "$work/$file" | grep -Ev "^$top(/|\$)" | grep -q .; then
        die "$file holds a member outside $top/. Nothing was installed"
    fi
    if tar -tzf "$work/$file" | grep -Eq '(^|/)\.\.(/|$)'; then
        die "$file holds a member with a .. component. Nothing was installed"
    fi
    mkdir "$work/unpack"
    tar -xzf "$work/$file" -C "$work/unpack"
    [ -f "$work/unpack/$top/install.sh" ] || die "$file has no install.sh"

    (cd "$work/unpack/$top" && sh ./install.sh $force)
}

die() {
    printf 'neura install: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat << 'TEXT'
Installs Neura from a release.

  --version VERSION  install this version, such as 0.1.0 or 0.1.0-rc.1,
                     in place of the latest release
  --source WHERE     read the release from an https:// URL or a local
                     directory that holds neura-release.txt and the archives
  --force            replace an installed copy of the same version
  --help             print this text

NEURA_INSTALL_ROOT sets where Neura is copied (default
${XDG_DATA_HOME:-$HOME/.local/share}/neura) and NEURA_BIN_DIR where the
neura command is linked (default $HOME/.local/bin).
TEXT
}

main "$@"
