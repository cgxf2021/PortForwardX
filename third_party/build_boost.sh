#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third_party"
SRC_DIR="${THIRD_PARTY_DIR}/src"
BUILD_DIR="${THIRD_PARTY_DIR}/build/boost"
INSTALL_DIR="${THIRD_PARTY_DIR}/install/boost"

BOOST_VERSION="${BOOST_VERSION:-1.86.0}"
BOOST_VERSION_U="${BOOST_VERSION//./_}"
BOOST_ARCHIVE="boost_${BOOST_VERSION_U}.tar.gz"
BOOST_URL="https://archives.boost.io/release/${BOOST_VERSION}/source/${BOOST_ARCHIVE}"
BOOST_SRC_DIR="${SRC_DIR}/boost_${BOOST_VERSION_U}"
LINK_TYPE="${1:-static}"

if [[ "${LINK_TYPE}" != "static" && "${LINK_TYPE}" != "shared" ]]; then
  echo "Usage: $0 [static|shared]"
  exit 1
fi

mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${INSTALL_DIR}"

if [[ ! -f "${SRC_DIR}/${BOOST_ARCHIVE}" ]]; then
  echo "[boost] downloading ${BOOST_ARCHIVE}"
  curl -L --retry 3 -o "${SRC_DIR}/${BOOST_ARCHIVE}" "${BOOST_URL}"
fi

if [[ ! -d "${BOOST_SRC_DIR}" ]]; then
  echo "[boost] extracting sources"
  tar -xzf "${SRC_DIR}/${BOOST_ARCHIVE}" -C "${SRC_DIR}"
fi

echo "[boost] bootstrapping b2"
pushd "${BOOST_SRC_DIR}" >/dev/null
./bootstrap.sh

if [[ "${LINK_TYPE}" == "static" ]]; then
  LINK_ARG="static"
  RUNTIME_LINK="static"
else
  LINK_ARG="shared"
  RUNTIME_LINK="shared"
fi

echo "[boost] building and installing to ${INSTALL_DIR}"
./b2 \
  --build-dir="${BUILD_DIR}" \
  --prefix="${INSTALL_DIR}" \
  link="${LINK_ARG}" \
  runtime-link="${RUNTIME_LINK}" \
  variant=release \
  threading=multi \
  cxxstd=17 \
  --layout=system \
  --with-program_options \
  install
popd >/dev/null

echo "[boost] done"
echo "BOOST_ROOT=${INSTALL_DIR}"
