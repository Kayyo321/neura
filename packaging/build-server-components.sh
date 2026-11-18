#!/bin/sh
# Builds the server components that the release archives link statically:
# OpenSSL (TLS), nghttp2 (HTTP/2) and PostgreSQL's libpq (the PostgreSQL
# provider of Std.Data.Sql), from the sources that
# packaging/server-components.lock.toml pins.
#
# usage: build-server-components.sh [--downloads DIR] [--jobs N] PREFIX
#
#   --downloads DIR  where the source archives are kept; an archive already
#                    there is used when its SHA-256 matches the lock, and a
#                    missing or different one is downloaded again (default:
#                    PREFIX/downloads)
#   --jobs N         parallel compile jobs (default 2)
#
# Every archive is checked against the lock's SHA-256 before it is unpacked,
# and every licence text against the lock's license_sha256. The release
# workflow, the Tier 1 job and a local release build run this one script, so
# their libraries are built the same way. It writes:
#
#   PREFIX/openssl      include/, lib/libssl.a, lib/libcrypto.a, bin/openssl
#   PREFIX/nghttp2      include/, lib/libnghttp2.a
#   PREFIX/postgresql   the libpq headers and src/interfaces/libpq/libpq.a,
#                       src/common/libpgcommon_shlib.a, src/port/libpgport_shlib.a
#                       at the paths NEURA_LIBPQ_SOURCE and NEURA_LIBPQ_BUILD use
#   PREFIX/licenses     the three licence texts, for the archives' notices
#   PREFIX/components.txt  the versions and source digests that were built
#
# Configure Neura with:
#   -DNEURA_ENABLE_SERVER_TLS=ON -DOPENSSL_ROOT_DIR=PREFIX/openssl
#   -DNEURA_ENABLE_SERVER_HTTP2=ON -DNEURA_NGHTTP2_ROOT=PREFIX/nghttp2
#   -DNEURA_ENABLE_POSTGRES=ON -DNEURA_LIBPQ_SOURCE=PREFIX/postgresql
#   -DNEURA_LIBPQ_BUILD=PREFIX/postgresql -DNEURA_COMPONENT_LICENSES=PREFIX/licenses
#
# The libraries are static, position independent (they are linked into
# libneura-core), and compiled with hidden visibility, so libneura-core does
# not export their C functions. Paths compiled into them are fixed, never the
# build machine's: OpenSSL looks for trust anchors and openssl.cnf under
# /etc/ssl (SSL_CERT_FILE, SSL_CERT_DIR and OPENSSL_CONF override it), and
# libpq for pg_service.conf under /usr/local/pgsql/etc (PGSYSCONFDIR).
# It needs sh, curl, a tar that reads gzip, xz and bzip2, perl, make and a
# C compiler ($CC, or cc).
set -eu

die() {
    printf 'build-server-components.sh: %s\n' "$*" >&2
    exit 1
}

usage="usage: build-server-components.sh [--downloads DIR] [--jobs N] PREFIX"
downloads=""
jobs=2
while [ "$#" -gt 0 ]; do
    case $1 in
    --downloads) [ "$#" -ge 2 ] || die "$usage"; downloads=$2; shift 2 ;;
    --jobs) [ "$#" -ge 2 ] || die "$usage"; jobs=$2; shift 2 ;;
    --*) die "unknown option: $1 ($usage)" ;;
    *) break ;;
    esac
done
[ "$#" -eq 1 ] || die "$usage"
printf '%s\n' "$jobs" | grep -Eq '^[1-9][0-9]*$' || die "--jobs needs a positive number"

here=$(cd "$(dirname "$0")" && pwd -P)
lock=$here/server-components.lock.toml
[ -f "$lock" ] || die "$lock is missing"
mkdir -p "$1"
prefix=$(cd "$1" && pwd -P)
[ -n "$downloads" ] || downloads=$prefix/downloads
mkdir -p "$downloads"
downloads=$(cd "$downloads" && pwd -P)

if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d ' ' -f 1; }
elif command -v shasum >/dev/null 2>&1; then
    sha256() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
else
    die "sha256sum or shasum is required"
fi

# lock_value SECTION KEY: the string value of KEY in [providers.SECTION].
lock_value() {
    value=$(awk -v section="[providers.$1]" -v key="$2" '
        $0 == section { inside = 1; next }
        /^\[/ { inside = 0 }
        inside && $1 == key {
            sub(/^[^=]*= *"/, ""); sub(/"[ \t]*$/, ""); print; exit
        }' "$lock")
    [ -n "$value" ] || die "the lock has no $2 for $1"
    printf '%s\n' "$value"
}

# fetch SECTION: the verified source archive of SECTION, downloaded if needed.
fetch() {
    url=$(lock_value "$1" source)
    expected=$(lock_value "$1" sha256)
    file=$downloads/$(basename "$url")
    if [ -f "$file" ] && [ "$(sha256 "$file")" = "$expected" ]; then
        printf '%s: %s is verified (SHA-256 %s)\n' "$1" "$(basename "$file")" "$expected" >&2
    else
        rm -f "$file"
        printf '%s: downloading %s\n' "$1" "$url" >&2
        curl --proto '=https' --tlsv1.2 -fsSL --retry 3 -o "$file.part" "$url" || die "cannot download $url"
        actual=$(sha256 "$file.part")
        [ "$actual" = "$expected" ] || die "$url has SHA-256 $actual; the lock pins $expected"
        mv "$file.part" "$file"
        printf '%s: %s is verified (SHA-256 %s)\n' "$1" "$(basename "$file")" "$expected" >&2
    fi
    printf '%s\n' "$file"
}

# licence SECTION DESTINATION: copy the licence the lock names out of the
# unpacked source, after checking its digest.
licence() {
    path=$work/$(lock_value "$1" license_path)
    expected=$(lock_value "$1" license_sha256)
    [ -f "$path" ] || die "$path is missing"
    actual=$(sha256 "$path")
    [ "$actual" = "$expected" ] || die "$path has SHA-256 $actual; the lock pins $expected"
    cp "$path" "$prefix/licenses/$2"
}

cc=${CC:-cc}
case $(uname -s) in
Darwin)
    # The archives support the macOS release that docs/Install.md names.
    export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-15.0}
    ;;
esac
flags="-fPIC -fvisibility=hidden -O2"
# OpenSSL adds -fPIC and its own optimisation.
openssl_flags="-fvisibility=hidden"

work=$prefix/work
rm -rf "$work" "$prefix/openssl" "$prefix/nghttp2" "$prefix/postgresql" "$prefix/licenses" "$prefix/components.txt"
mkdir -p "$work" "$prefix/licenses"

openssl_version=$(lock_value openssl version)
nghttp2_version=$(lock_value nghttp2 version)
postgresql_version=$(lock_value postgresql version)
openssl_archive=$(fetch openssl)
nghttp2_archive=$(fetch nghttp2)
postgresql_archive=$(fetch postgresql)

# OpenSSL: static libraries and the command-line tool (the TLS tests make
# their certificates with it), no loadable modules or engines, no tests or
# documentation. The prefix is fixed so no build path is compiled in; the
# files are installed under PREFIX/openssl through DESTDIR.
printf 'openssl: building %s\n' "$openssl_version" >&2
tar -xzf "$openssl_archive" -C "$work"
(
    cd "$work/openssl-$openssl_version"
    CC=$cc ./Configure --prefix=/opt/neura-openssl --libdir=lib --openssldir=/etc/ssl \
        no-shared no-module no-engine no-tests no-docs $openssl_flags > "$work/openssl-configure.log" 2>&1 ||
        { tail -n 40 "$work/openssl-configure.log" >&2; exit 1; }
    make -j "$jobs" > "$work/openssl-build.log" 2>&1 || { tail -n 60 "$work/openssl-build.log" >&2; exit 1; }
    make install_sw DESTDIR="$work/openssl-root" > "$work/openssl-install.log" 2>&1 ||
        { tail -n 40 "$work/openssl-install.log" >&2; exit 1; }
)
mv "$work/openssl-root/opt/neura-openssl" "$prefix/openssl"
built=$("$prefix/openssl/bin/openssl" version | cut -d ' ' -f 2)
[ "$built" = "$openssl_version" ] || die "the OpenSSL build reports $built, not $openssl_version"
licence openssl openssl-LICENSE.txt

# nghttp2: the library only. NGHTTP2_STATICLIB drops the default visibility
# that its header gives the API when it builds a shared library.
printf 'nghttp2: building %s\n' "$nghttp2_version" >&2
tar -xf "$nghttp2_archive" -C "$work"
(
    cd "$work/nghttp2-$nghttp2_version"
    CC=$cc CFLAGS="$flags -DNGHTTP2_STATICLIB" ./configure --prefix="$prefix/nghttp2" --enable-lib-only --enable-static --disable-shared \
        --with-pic --disable-dependency-tracking > "$work/nghttp2-configure.log" 2>&1 ||
        { tail -n 40 "$work/nghttp2-configure.log" >&2; exit 1; }
    make -j "$jobs" > "$work/nghttp2-build.log" 2>&1 || { tail -n 60 "$work/nghttp2-build.log" >&2; exit 1; }
    make install > "$work/nghttp2-install.log" 2>&1 || { tail -n 40 "$work/nghttp2-install.log" >&2; exit 1; }
)
rm -rf "$prefix/nghttp2/share" "$prefix/nghttp2/bin" "$prefix/nghttp2/lib/libnghttp2.la"
grep -q "^#define NGHTTP2_VERSION \"$nghttp2_version\"" "$prefix/nghttp2/include/nghttp2/nghttp2ver.h" ||
    die "the nghttp2 build is not $nghttp2_version"
licence nghttp2 nghttp2-COPYING

# libpq: the static archive and the two support archives it needs, over the
# OpenSSL above. Upstream's shared libpq is not built: its symbol check
# rejects a libpq with OpenSSL linked in, and Neura links the static one.
printf 'postgresql: building libpq %s\n' "$postgresql_version" >&2
tar -xf "$postgresql_archive" -C "$work"
(
    cd "$work/postgresql-$postgresql_version"
    CC=$cc CFLAGS="$flags" ./configure --with-ssl=openssl --with-includes="$prefix/openssl/include" \
        --with-libraries="$prefix/openssl/lib" --without-icu --without-readline --without-zlib \
        > "$work/postgresql-configure.log" 2>&1 || { tail -n 40 "$work/postgresql-configure.log" >&2; exit 1; }
    {
        make -C src/backend generated-headers &&
            make -C src/port -j "$jobs" libpgport_shlib.a &&
            make -C src/common -j "$jobs" libpgcommon_shlib.a &&
            make -C src/interfaces/libpq -j "$jobs" all-static-lib
    } > "$work/postgresql-build.log" 2>&1 || { tail -n 60 "$work/postgresql-build.log" >&2; exit 1; }
)
source=$work/postgresql-$postgresql_version
target=$prefix/postgresql
mkdir -p "$target/src/interfaces/libpq" "$target/src/common" "$target/src/port"
cp "$source/src/interfaces/libpq/libpq.a" "$source/src/interfaces/libpq/libpq-fe.h" \
    "$source/src/interfaces/libpq/libpq-events.h" "$target/src/interfaces/libpq/"
cp "$source/src/common/libpgcommon_shlib.a" "$target/src/common/"
cp "$source/src/port/libpgport_shlib.a" "$target/src/port/"
# The headers of src/include, with the ones configure generated.
(cd "$source" && find src/include -name '*.h' -print) | while read -r header; do
    mkdir -p "$target/$(dirname "$header")"
    cp "$source/$header" "$target/$header"
done
licence postgresql postgresql-COPYRIGHT

{
    printf 'openssl %s %s\n' "$openssl_version" "$(lock_value openssl sha256)"
    printf 'nghttp2 %s %s\n' "$nghttp2_version" "$(lock_value nghttp2 sha256)"
    printf 'postgresql %s %s\n' "$postgresql_version" "$(lock_value postgresql sha256)"
    printf 'flags %s\n' "$flags"
    printf 'cc %s\n' "$("$cc" --version 2>/dev/null | head -n 1)"
} > "$prefix/components.txt"
rm -rf "$work"
cat "$prefix/components.txt"
