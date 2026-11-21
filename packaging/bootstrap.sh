#!/bin/sh
# Downloads, verifies, and installs a Neura release. Each release publishes
# this file as install.sh, so one command installs Neura:
#
#   curl --proto '=https' --tlsv1.2 -fsSL \
#     https://github.com/Kayyo321/neura/releases/latest/download/install.sh | sh
#
# Options, after "sh -s --" when the script is piped:
#
#   --components SET   sdk (the default), runtime, or sdk,service
#   --version VERSION  install this version, such as 0.1.0 or 0.1.0-rc.1,
#                      in place of the latest release
#   --source WHERE     read the release from an https:// URL or a local
#                      directory that holds the release metadata and archives
#   --force            install again a generation that is already installed
#   --help             print this text
#
# What it does: finds this machine's target, reads the release's
# neura-components.txt, downloads the one or two archives the set of
# components needs (Neura-sdk, Neura-runtime, or both), checks each
# archive's size and SHA-256 against that file, checks its build provenance
# when the GitHub CLI (gh) is installed, unpacks it into a temporary
# directory, and runs the install.sh inside the first archive, with --add
# for the second. That installer copies Neura under
# ${XDG_DATA_HOME:-$HOME/.local/share}/neura and links each tool in
# $HOME/.local/bin. NEURA_INSTALL_ROOT and NEURA_BIN_DIR change the two
# places. Nothing is written anywhere else, sudo is never used, and no shell
# startup file is edited. A release without neura-components.txt (0.2 and
# earlier) is installed from its neura-release.txt, for the SDK only.
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
    components=sdk
    while [ $# -gt 0 ]; do
        case "$1" in
            --components) [ $# -ge 2 ] || die "--components needs a value"; components=$2; shift 2 ;;
            --version) [ $# -ge 2 ] || die "--version needs a value"; version=$2; shift 2 ;;
            --source) [ $# -ge 2 ] || die "--source needs a value"; source=$2; shift 2 ;;
            --force) force=--force; shift ;;
            --help) usage; exit 0 ;;
            *) die "unknown option: $1 (try --help)" ;;
        esac
    done

    case "$components" in
        sdk) products=sdk ;;
        runtime) products=runtime ;;
        sdk,service | service,sdk) products="sdk runtime" ;;
        *) die "--components takes sdk, runtime or sdk,service, not $components" ;;
    esac
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
        MINGW* | MSYS* | CYGWIN*)
            # Git Bash, MSYS2 and Cygwin on Windows: the Windows release
            # installs from PowerShell (docs/design/Windows030.md, W24).
            die "on Windows, install Neura from PowerShell: irm https://github.com/$repository/releases/latest/download/install.ps1 | iex"
            ;;
        *) die "Neura has no release for $system" ;;
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

    # check_archive FILE SHA256 SIZE: the size and digest the metadata gives.
    check_archive() {
        actual_size=$(wc -c < "$work/$1" | tr -d ' ')
        [ "$actual_size" = "$3" ] || die "$1 is $actual_size bytes; the release metadata says $3"
        actual=$(digest "$work/$1")
        [ "$actual" = "$2" ] || die "SHA-256 mismatch for $1: got $actual, the release metadata says $2"
        echo "Verified: size and SHA-256 of $1 against the release metadata"
        # gh can verify nothing until it is signed in. That is a missing tool, not
        # a failed verification: only a check that ran and failed stops the install.
        if [ "$remote" != 1 ]; then
            echo "Not verified: build provenance (the source is a local directory)"
        elif ! command -v gh > /dev/null 2>&1; then
            echo "Not verified: build provenance (gh, the GitHub CLI, is not installed)"
        elif ! gh auth status > /dev/null 2>&1; then
            echo "Not verified: build provenance (gh is installed but not signed in; run \"gh auth login\" to enable the check)"
        elif gh attestation verify "$work/$1" --repo "$repository" > "$work/attestation.log" 2>&1; then
            echo "Verified: build provenance with gh attestation verify --repo $repository"
        else
            sed 's/^/  /' "$work/attestation.log" >&2
            die "gh attestation verify did not confirm the build provenance of $1. Nothing was installed"
        fi
    }

    # unpack FILE TOP: every member must be inside the one expected directory.
    unpack() {
        if tar -tzf "$work/$1" | grep -Ev "^$2(/|\$)" | grep -q .; then
            die "$1 holds a member outside $2/. Nothing was installed"
        fi
        if tar -tzf "$work/$1" | grep -Eq '(^|/)\.\.(/|$)'; then
            die "$1 holds a member with a .. component. Nothing was installed"
        fi
        mkdir -p "$work/unpack"
        tar -xzf "$work/$1" -C "$work/unpack"
        [ -f "$work/unpack/$2/install.sh" ] || die "$1 has no install.sh"
    }

    if [ "$remote" = 1 ]; then
        if ! curl --proto '=https' --proto-redir '=https' --tlsv1.2 --fail --location --silent --show-error \
            --output "$work/neura-components.txt" "$source/neura-components.txt" 2> /dev/null; then
            rm -f "$work/neura-components.txt"
        fi
    elif [ -f "$source/neura-components.txt" ]; then
        cp "$source/neura-components.txt" "$work/neura-components.txt"
    fi

    if [ ! -f "$work/neura-components.txt" ]; then
        # A 0.2 release: one archive, named by neura-release.txt.
        [ "$components" = sdk ] || die "the release has no component metadata (neura-components.txt), so it has no $components archive"
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
        [ "$file" = "neura-$released-$triple.tar.gz" ] || die "the release metadata names an unexpected file: $file"
        echo "Downloading Neura $released for $triple"
        fetch "$file"
        check_archive "$file" "$(printf '%s' "$asset" | cut -d ' ' -f 2)" "$(printf '%s' "$asset" | cut -d ' ' -f 3)"
        unpack "$file" "neura-$released-$triple"
        (cd "$work/unpack/neura-$released-$triple" && sh ./install.sh $force)
        return
    fi

    [ "$(sed -n 1p "$work/neura-components.txt")" = "neura-components 1" ] ||
        die "$source/neura-components.txt is not component metadata this script can read"
    released=$(sed -n 's/^version \([0-9A-Za-z.-]*\)$/\1/p' "$work/neura-components.txt")
    [ -n "$released" ] || die "the component metadata names no version"
    if [ -n "$version" ] && [ "$released" != "$version" ]; then
        die "asked for version $version, and the release metadata is for $released"
    fi
    tops=""
    for product in $products; do
        entry=$(awk -v product="$product" -v triple="$triple" '$1 == "archive" && $2 == product && $3 == triple && NF == 6 { print $4, $5, $6 }' \
            "$work/neura-components.txt")
        if [ -z "$entry" ]; then
            die "Neura $released has no $product archive for $triple. Supported platforms: https://github.com/$repository/blob/main/docs/Install.md#supported-platforms"
        fi
        [ "$(printf '%s\n' "$entry" | wc -l | tr -d ' ')" = 1 ] || die "the component metadata lists the $product archive for $triple more than once"
        file=$(printf '%s' "$entry" | cut -d ' ' -f 1)
        [ "$file" = "neura-$product-$released-$triple.tar.gz" ] || die "the component metadata names an unexpected file: $file"
        echo "Downloading Neura-$product $released for $triple"
        fetch "$file"
        check_archive "$file" "$(printf '%s' "$entry" | cut -d ' ' -f 2)" "$(printf '%s' "$entry" | cut -d ' ' -f 3)"
        unpack "$file" "neura-$product-$released-$triple"
        tops="$tops neura-$product-$released-$triple"
    done
    set -- $tops
    if [ $# -eq 2 ]; then
        (cd "$work/unpack/$1" && sh ./install.sh --add "$work/unpack/$2" $force)
    else
        (cd "$work/unpack/$1" && sh ./install.sh $force)
    fi
}

die() {
    printf 'neura install: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat << 'TEXT'
Installs Neura from a release.

  --components SET   sdk (the default: the development tools, without the
                     service), runtime (the execution core and the service),
                     or sdk,service (both)
  --version VERSION  install this version, such as 0.1.0 or 0.1.0-rc.1,
                     in place of the latest release
  --source WHERE     read the release from an https:// URL or a local
                     directory that holds the release metadata and archives
  --force            install again a generation that is already installed
  --help             print this text

NEURA_INSTALL_ROOT sets where Neura is copied (default
${XDG_DATA_HOME:-$HOME/.local/share}/neura) and NEURA_BIN_DIR where each
tool is linked (default $HOME/.local/bin).
TEXT
}

main "$@"
