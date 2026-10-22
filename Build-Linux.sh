#!/usr/bin/env bash
set -euo pipefail
guardian_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
guardian_mode=${1:-Release}
case "$guardian_mode" in
    Debug|Release) guardian_config=$guardian_mode; guardian_sanitize=OFF ;;
    Sanitize) guardian_config=Debug; guardian_sanitize=ON ;;
    *) echo "Uso: ./Build-Linux.sh [Debug|Release|Sanitize]" >&2; exit 2 ;;
esac
guardian_cache=${XDG_CACHE_HOME:-$HOME/.cache}/guardian-native-deps
guardian_cmake=${CMAKE:-$(command -v cmake || true)}
if [[ -z "$guardian_cmake" && -x "$guardian_cache/python/cmake/data/bin/cmake" ]]; then
    guardian_cmake=$guardian_cache/python/cmake/data/bin/cmake
fi
if [[ -z "$guardian_cmake" ]]; then
    echo "Instale CMake 3.22+, compilador C++20, make, libssl-dev e Python 3.9+." >&2
    exit 2
fi
guardian_ctest=$(dirname -- "$guardian_cmake")/ctest
if [[ ! -x "$guardian_ctest" ]]; then guardian_ctest=$(command -v ctest); fi
guardian_build=${GUARDIAN_LINUX_BUILD_DIR:-$guardian_root/build-linux/$guardian_mode}
guardian_options=()
if [[ -n "${GUARDIAN_OPENSSL_ROOT:-}" ]]; then
    guardian_options+=("-DOPENSSL_ROOT_DIR=$GUARDIAN_OPENSSL_ROOT")
elif [[ -f "$guardian_cache/openssl/usr/include/openssl/evp.h" ]]; then
    guardian_options+=("-DOPENSSL_ROOT_DIR=$guardian_cache/openssl/usr")
fi
"$guardian_cmake" -S "$guardian_root" -B "$guardian_build" -G "Unix Makefiles" \
    "-DCMAKE_BUILD_TYPE=$guardian_config" -DBUILD_TESTING=ON \
    "-DGUARDIAN_ENABLE_SANITIZERS=$guardian_sanitize" \
    "-DGUARDIAN_TEST_KVM=${GUARDIAN_TEST_KVM:-OFF}" \
    "-DGUARDIAN_KVM_GUEST_DIR=${GUARDIAN_KVM_GUEST_DIR:-}" "${guardian_options[@]}"
"$guardian_cmake" --build "$guardian_build" --parallel "${GUARDIAN_BUILD_JOBS:-2}"
"$guardian_ctest" --test-dir "$guardian_build" --output-on-failure --output-junit "$guardian_build/tests.xml"
echo "Executável Linux: $guardian_build/guardianav"
