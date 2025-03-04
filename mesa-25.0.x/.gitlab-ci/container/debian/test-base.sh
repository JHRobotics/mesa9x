#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_BASE_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

uncollapsed_section_start debian_setup "Base Debian system setup"

export DEBIAN_FRONTEND=noninteractive

apt-get install -y curl ca-certificates gnupg2 software-properties-common

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list.d/*

echo "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" | tee /etc/apt/sources.list.d/gfx-ci_.list

: "${LLVM_VERSION:?llvm version not set!}"

. .gitlab-ci/container/debian/maybe-add-llvm-repo.sh

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
    autoconf
    automake
    bc
    bison
    bzip2
    ccache
    cmake
    "clang-${LLVM_VERSION}"
    dpkg-dev
    flex
    glslang-tools
    g++
    libasound2-dev
    libcap-dev
    "libclang-cpp${LLVM_VERSION}-dev"
    "libclang-rt-${LLVM_VERSION}-dev"
    libdrm-dev
    libegl-dev
    libelf-dev
    libepoxy-dev
    libgbm-dev
    libpciaccess-dev
    libssl-dev
    libvulkan-dev
    libwayland-dev
    libx11-xcb-dev
    libxext-dev
    "llvm-${LLVM_VERSION}-dev"
    make
    meson
    openssh-server
    patch
    pkgconf
    protobuf-compiler
    python3-dev
    python3-pip
    python3-setuptools
    python3-wheel
    spirv-tools
    wayland-protocols
    xz-utils
)

DEPS=(
    apt-utils
    curl
    git
    git-lfs
    inetutils-syslogd
    iptables
    jq
    libasan8
    libdrm2
    libexpat1
    "libllvm${LLVM_VERSION}"
    liblz4-1
    libpng16-16
    libpython3.11
    libubsan1
    libvulkan1
    libwayland-client0
    libwayland-server0
    libxcb-ewmh2
    libxcb-randr0
    libxcb-xfixes0
    libxkbcommon0
    libxrandr2
    libxrender1
    python3-mako
    python3-numpy
    python3-packaging
    python3-pil
    python3-requests
    python3-six
    python3-yaml
    socat
    vulkan-tools
    waffle-utils
    xauth
    xvfb
    zlib1g
    zstd
)

apt-get update
apt-get dist-upgrade -y

apt-get install --purge -y \
      sysvinit-core libelogind0

apt-get install -y --no-remove "${DEPS[@]}"

apt-get install -y --no-install-recommends "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_pre_build.sh

# Needed for ci-fairy, this revision is able to upload files to MinIO
# and doesn't depend on git
pip3 install --break-system-packages git+http://gitlab.freedesktop.org/freedesktop/ci-templates@ffe4d1b10aab7534489f0c4bbc4c5899df17d3f2

# Needed for manipulation with traces yaml files.
pip3 install --break-system-packages yq

section_end debian_setup

############### Download prebuilt kernel

if [ "$DEBIAN_ARCH" = amd64 ]; then
  uncollapsed_section_start kernel "Downloading kernel"
  export KERNEL_IMAGE_NAME=bzImage
  mkdir -p /lava-files/
  . .gitlab-ci/container/download-prebuilt-kernel.sh
  section_end kernel
fi

############### Build mold

. .gitlab-ci/container/build-mold.sh

############### Build LLVM-SPIRV translator

. .gitlab-ci/container/build-llvm-spirv.sh

############### Build libclc

. .gitlab-ci/container/build-libclc.sh

############### Build Wayland

. .gitlab-ci/container/build-wayland.sh

############### Install Rust toolchain

. .gitlab-ci/container/build-rust.sh

############### Build Crosvm

. .gitlab-ci/container/build-crosvm.sh

############### Build dEQP runner

. .gitlab-ci/container/build-deqp-runner.sh

############### Uninstall the build software

uncollapsed_section_switch debian_cleanup "Cleaning up base Debian system"

apt-get purge -y "${EPHEMERAL[@]}"

rm -rf /root/.rustup

. .gitlab-ci/container/container_post_build.sh

section_end debian_cleanup
