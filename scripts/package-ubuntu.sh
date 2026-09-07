#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build/package-ubuntu"

for command_name in cmake cpack dpkg-shlibdeps ninja sha256sum; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "Missing required command: ${command_name}" >&2
    echo "Install: build-essential cmake ninja-build qt6-base-dev dpkg-dev libgl1-mesa-dev libglx-dev libopengl-dev" >&2
    exit 1
  fi
done

cmake -S "${project_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "${build_dir}" --parallel
cpack --config "${build_dir}/CPackConfig.cmake" -G DEB

(
  cd "${project_dir}/dist/ubuntu"
  sha256sum ./*.deb > SHA256SUMS.txt
)

echo "DEB package created in: ${project_dir}/dist/ubuntu"
