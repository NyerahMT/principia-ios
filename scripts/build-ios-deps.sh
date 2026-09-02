#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${ROOT}/build-ios-deps"
WORK="${ROOT}/build-ios-deps-src"

SDK="${IOS_SDK:-iphonesimulator}"
ARCH="${IOS_ARCH:-arm64}"
DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}"

COMMON_CMAKE_ARGS=(
    -G Ninja
    -DCMAKE_SYSTEM_NAME=iOS
    -DCMAKE_SYSTEM_PROCESSOR="${ARCH}"
    -DCMAKE_OSX_SYSROOT="${SDK}"
    -DCMAKE_OSX_ARCHITECTURES="${ARCH}"
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="${PREFIX}"
    -DBUILD_SHARED_LIBS=OFF
)

rm -rf "${PREFIX}" "${WORK}"
mkdir -p "${PREFIX}" "${WORK}"

echo "========================================"
echo " Building Principia iOS dependencies"
echo "========================================"
echo "SDK: ${SDK}"
echo "Architecture: ${ARCH}"
echo "Deployment target: ${DEPLOYMENT_TARGET}"
echo "Prefix: ${PREFIX}"
echo

cd "${WORK}"

echo "===== zlib ====="

git clone \
    --depth 1 \
    --branch v1.3.1 \
    https://github.com/madler/zlib.git

cmake \
    -S zlib \
    -B zlib-build \
    "${COMMON_CMAKE_ARGS[@]}"

cmake --build zlib-build
cmake --install zlib-build


echo "===== libpng ====="

git clone \
    --depth 1 \
    --branch v1.6.44 \
    https://github.com/pnggroup/libpng.git

cmake \
    -S libpng \
    -B libpng-build \
    "${COMMON_CMAKE_ARGS[@]}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}" \
    -DPNG_SHARED=OFF \
    -DPNG_STATIC=ON \
    -DPNG_TESTS=OFF \
    -DPNG_TOOLS=OFF

cmake --build libpng-build
cmake --install libpng-build


echo "===== libjpeg-turbo ====="

git clone \
    --depth 1 \
    --branch 3.1.2 \
    https://github.com/libjpeg-turbo/libjpeg-turbo.git

cmake \
    -S libjpeg-turbo \
    -B libjpeg-turbo-build \
    "${COMMON_CMAKE_ARGS[@]}" \
    -DENABLE_SHARED=OFF \
    -DENABLE_STATIC=ON \
    -DWITH_TURBOJPEG=OFF \
    -DWITH_TOOLS=OFF \
    -DWITH_TESTS=OFF

cmake --build libjpeg-turbo-build
cmake --install libjpeg-turbo-build


echo "===== FreeType ====="

git clone \
    --depth 1 \
    --branch VER-2-13-3 \
    https://github.com/freetype/freetype.git

cmake \
    -S freetype \
    -B freetype-build \
    "${COMMON_CMAKE_ARGS[@]}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}" \
    -DFT_DISABLE_BZIP2=ON \
    -DFT_DISABLE_BROTLI=ON \
    -DFT_DISABLE_HARFBUZZ=ON

cmake --build freetype-build
cmake --install freetype-build


echo "===== curl ====="

git clone \
    --depth 1 \
    --branch curl-8_12_1 \
    https://github.com/curl/curl.git

cmake \
    -S curl \
    -B curl-build \
    "${COMMON_CMAKE_ARGS[@]}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}" \
    -DBUILD_CURL_EXE=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_LIBCURL_DOCS=OFF \
    -DBUILD_MISC_DOCS=OFF \
    -DBUILD_TESTING=OFF \
    -DCURL_USE_SECTRANSP=ON \
    -DCURL_USE_LIBPSL=OFF \
    -DCURL_USE_LIBSSH2=OFF \
    -DCURL_ZLIB=ON

cmake --build curl-build
cmake --install curl-build

echo
echo "========================================"
echo " iOS dependencies built successfully"
echo "========================================"

find "${PREFIX}" -maxdepth 2 -type f | sort
