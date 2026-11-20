# syntax=docker/dockerfile:1.7
# Build and publish the Linux x86-64 image from the repository root.
#   image=ghcr.io/andeviking/dandelion-ci:toolchain-v1
#   docker build --platform linux/amd64 -t "$image" - < docker/ci.Dockerfile
#   docker push "$image"
# Publish it with public read access for fork PRs. Set the repository Actions
# variable DANDELION_CI_IMAGE to the published image digest to pin CI runs.

FROM ubuntu:24.04@sha256:77eb101c607d6e3d4f5ad0c8a02c2136c689b98aba1f359e6502ba517e5c57ec AS base

# Bootstrap TLS for the pinned Ubuntu snapshot using its CA certificate package.
ADD --checksum=sha256:f9fbacd417b515450b831032cc39d18059e9874e67525679a43c16071dabf9a0 \
    https://snapshot.ubuntu.com/ubuntu/20261119T000000Z/pool/main/c/ca-certificates/ca-certificates_20260816ubuntu0.24.04.2_all.deb \
    /tmp/ca-certificates.deb

RUN <<'SH'
set -eu
dpkg-deb -x /tmp/ca-certificates.deb /tmp/bootstrap-certs
cat /tmp/bootstrap-certs/usr/share/ca-certificates/mozilla/*.crt > /tmp/apt-ca.pem
cat > /etc/apt/sources.list.d/ubuntu.sources <<'SOURCES'
Types: deb
URIs: https://snapshot.ubuntu.com/ubuntu/20261119T000000Z/
Suites: noble noble-updates noble-security
Components: main universe
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
SOURCES
apt-get -o Acquire::https::CaInfo=/tmp/apt-ca.pem update
DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::https::CaInfo=/tmp/apt-ca.pem \
    install -y --no-install-recommends \
    binutils ca-certificates fonts-dejavu-core git graphviz libc6-dev \
    libgmp-dev libmpc3 libmpfr6 libzstd1 pkg-config python3
rm -rf /var/lib/apt/lists/* /tmp/bootstrap-certs /tmp/apt-ca.pem /tmp/ca-certificates.deb
SH

FROM base AS build
ARG BUILD_JOBS=4

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       bison build-essential flex libmpc-dev libmpfr-dev libzstd-dev unzip zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/toolchain

ADD --checksum=sha256:81f0d5b0960fb6adc8197030ffc827fba937640d51e73e39e0e9e40d88c66f6f \
    https://github.com/Kitware/CMake/releases/download/v4.5.0/cmake-4.5.0-linux-x86_64.tar.gz \
    cmake-4.5.0.tar.gz
ADD --checksum=sha256:5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6 \
    https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip \
    ninja-1.13.2.zip
RUN mkdir -p /opt/dandelion \
    && tar -xf cmake-4.5.0.tar.gz --strip-components=1 -C /opt/dandelion \
    && unzip ninja-1.13.2.zip \
    && install -m 0755 ninja /opt/dandelion/bin/ninja

ADD --checksum=sha256:e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e \
    https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz \
    gcc-16.2.0.tar.xz
RUN tar -xf gcc-16.2.0.tar.xz \
    && mkdir gcc-build \
    && cd gcc-build \
    && ../gcc-16.2.0/configure --prefix=/opt/dandelion \
       --enable-languages=c,c++ --disable-multilib --disable-bootstrap \
       --enable-checking=release --disable-nls \
    && make -j"$BUILD_JOBS" \
    && make install-strip \
    && cd .. \
    && rm -rf gcc-build gcc-16.2.0

ADD --checksum=sha256:fb50b1eac93ddeb3aa8dab4cfe324dff74f4a9c2bdee6160c9e36188f8d06a6e \
    https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.5/llvm-project-23.1.5.src.tar.xz \
    llvm-project-23.1.5.src.tar.xz
# Clang discovers GCC and libstdc++ through the shared installation prefix.
RUN tar -xf llvm-project-23.1.5.src.tar.xz \
    && /opt/dandelion/bin/cmake -S llvm-project-23.1.5.src/llvm -B llvm-build -G Ninja \
       -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/dandelion \
       -DCMAKE_MAKE_PROGRAM=/opt/dandelion/bin/ninja \
       -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra' \
       -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_PARALLEL_LINK_JOBS=1 \
       -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
       -DLLVM_DISTRIBUTION_COMPONENTS='clang;clang-resource-headers;clang-scan-deps;clang-format;clang-tidy' \
    && /opt/dandelion/bin/cmake --build llvm-build --parallel "$BUILD_JOBS" \
       --target install-distribution-stripped \
    && rm -rf llvm-build llvm-project-23.1.5.src

ADD --checksum=sha256:fa4c3dd78785abc11ccc992bc9c01e7a8c3120fe14b8a8dfd7cefa7014530814 \
    https://github.com/doxygen/doxygen/releases/download/Release_1_17_0/doxygen-1.17.0.src.tar.gz \
    doxygen-1.17.0.src.tar.gz
RUN tar -xf doxygen-1.17.0.src.tar.gz \
    && /opt/dandelion/bin/cmake -S doxygen-1.17.0 -B doxygen-build -G Ninja \
       -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/dandelion \
       -DCMAKE_MAKE_PROGRAM=/opt/dandelion/bin/ninja \
    && /opt/dandelion/bin/cmake --build doxygen-build --parallel "$BUILD_JOBS" \
    && /opt/dandelion/bin/cmake --install doxygen-build --strip \
    && rm -rf doxygen-build doxygen-1.17.0

# Match the commits, archive checksums, and provenance files in the CMake helpers.
ADD --checksum=sha256:623fe05d283020f37a579a8985ba338091ac7229da62b5591ff40acdf05b95e2 \
    https://codeload.github.com/google/googletest/tar.gz/063de7e9578f82b369302001269680b4b1553359 \
    googletest-1.18.0.tar.gz
ADD --checksum=sha256:1e34f52437882bfe010f955580e77a19b4fb0a702ef678609b509a834296075c \
    https://codeload.github.com/google/benchmark/tar.gz/a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c \
    benchmark-1.10.0.tar.gz
RUN <<'SH'
set -eu
gtest=/opt/dandelion/subprojects/googletest-1.18.0
benchmark=/opt/dandelion/subprojects/benchmark-1.10.0
mkdir -p "$gtest" "$benchmark"
tar -xf googletest-1.18.0.tar.gz --strip-components=1 -C "$gtest"
tar -xf benchmark-1.10.0.tar.gz --strip-components=1 -C "$benchmark"
printf '%s\n' 063de7e9578f82b369302001269680b4b1553359 > "$gtest/.dandelion-googletest-commit"
printf '%s\n' a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c > "$benchmark/.dandelion-benchmark-commit"
SH

FROM base AS ci
COPY --from=build /opt/dandelion /opt/dandelion
ENV PATH="/opt/dandelion/bin:${PATH}"

RUN <<'SH'
set -eu
printf '%s\n' /opt/dandelion/lib64 /opt/dandelion/lib > /etc/ld.so.conf.d/dandelion.conf
ldconfig
cmake --version | grep -Fx 'cmake version 4.5.0'
test "$(ninja --version)" = 1.13.2
test "$(g++ -dumpfullversion)" = 16.2.0
for tool in clang++ clang-scan-deps clang-format clang-tidy; do
    "$tool" --version | grep -E 'version 23\.1\.5($|[[:space:]])'
done
metadata=$(g++ -print-file-name=libstdc++.modules.json)
test -f "$metadata"
test "$metadata" -ef "$(clang++ -print-file-name=libstdc++.modules.json)"
test "$(pkg-config --modversion gmp)" = 6.3.0
test "$(doxygen --version)" = 1.17.0
dot -V
python3 --version
SH
