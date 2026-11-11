#!/bin/sh
# Checks the kernel library (docs/runtime/Kernels.md):
#
#   1. The archive libneura_kernels.a calls no libm function: none of its
#      undefined symbols is a C math function or a symbol the host's libm
#      defines. The release binaries may depend only on libc, libm, and the
#      loader, and the kernels must add nothing to that.
#   2. Its other undefined symbols are only the memory functions a compiler
#      emits for copies (memcpy, memmove, memset, memcmp), so no kernel calls
#      a locale-dependent or otherwise stateful libc function.
#   3. The generated table and reference are up to date
#      (python3 scripts/gen-kernels.py --check).
#
# Usage: scripts/check-kernels.sh [build-directory]   (default: build)
# Build the archive first: cmake --build <build-directory> --target neura_kernels
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-"$root/build"}
archive="$build/libneura_kernels.a"
if [ ! -f "$archive" ]; then
    echo "check-kernels: $archive does not exist; build the neura_kernels target first" >&2
    exit 2
fi

status=0
case "$(uname -s)" in
    Darwin) prefix=_ ;;
    *) prefix= ;;
esac

# The archive's undefined symbols, one per line, without the platform's
# symbol prefix or a version suffix.
undefined=$(nm -u "$archive" | awk '
    /:$/ || NF == 0 { next }
    { print $NF }' | sed -e "s/^$prefix//" -e 's/@.*//' | sort -u)

# C99 and POSIX <math.h> functions in every precision, and glibc's internal
# entry points, in case the host's libm cannot be read.
math_names=$(
    for f in acos asin atan atan2 cos sin tan sincos acosh asinh atanh cosh sinh tanh exp exp2 exp10 expm1 \
        frexp ilogb ldexp log log10 log1p log2 logb modf scalb scalbn scalbln cbrt fabs hypot pow pow10 sqrt \
        erf erfc lgamma tgamma gamma j0 j1 jn y0 y1 yn ceil floor nearbyint rint lrint llrint round lround \
        llround roundeven trunc fmod remainder drem remquo copysign nan nextafter nexttoward nextup nextdown \
        fdim fmax fmin fma fmaximum fminimum significand; do
        echo "$f"
        echo "${f}f"
        echo "${f}l"
        echo "__${f}_finite"
    done
    echo __fpclassify
    echo __signbit
    echo __isnan
    echo __isinf
    echo __finite
)
host_libm=""
if [ "$prefix" = "" ]; then
    libm=$(cc -print-file-name=libm.so.6 2>/dev/null || true)
    if [ -n "$libm" ] && [ -f "$libm" ]; then
        host_libm=$(nm -D --defined-only "$libm" 2>/dev/null | awk '{ print $NF }' | sed 's/@.*//')
    fi
fi

libm_symbols=$(printf '%s\n%s\n' "$math_names" "$host_libm" | sort -u)
from_libm=""
for symbol in $undefined; do
    if printf '%s\n' "$libm_symbols" | grep -Fqx "$symbol"; then
        from_libm="$from_libm $symbol"
    fi
done
if [ -n "$from_libm" ]; then
    echo "check-kernels: the kernels call libm:$from_libm" >&2
    status=1
fi

others=""
for symbol in $undefined; do
    case "$symbol" in
        memcpy | memmove | memset | memcmp) ;;
        *)
            case " $from_libm " in
                *" $symbol "*) ;;
                *) others="$others $symbol" ;;
            esac
            ;;
    esac
done
if [ -n "$others" ]; then
    echo "check-kernels: the kernels call functions outside memcpy, memmove, memset, and memcmp:$others" >&2
    status=1
fi

if ! python3 "$root/scripts/gen-kernels.py" --check --root "$root"; then
    status=1
fi

if [ "$status" -eq 0 ]; then
    count=$(printf '%s\n' "$undefined" | grep -c . || true)
    echo "check-kernels: no libm symbols; $count undefined symbols, all memory builtins"
fi
exit "$status"
