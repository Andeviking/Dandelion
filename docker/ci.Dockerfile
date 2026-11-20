# syntax=docker/dockerfile:1.7
# Build and publish the Linux x86-64 image from the repository root.
#   image=ghcr.io/andeviking/dandelion-ci:toolchain-v1
#   docker build --platform linux/amd64 -t "$image" - < docker/ci.Dockerfile
#   docker push "$image"
# Publish it with public read access for fork PRs. Set the repository Actions
# variable DANDELION_CI_IMAGE to the published image digest to pin CI runs.

# Keep the downloaded archives out of the final image.
FROM scratch AS archives
WORKDIR /archives

ADD --checksum=sha256:81f0d5b0960fb6adc8197030ffc827fba937640d51e73e39e0e9e40d88c66f6f \
    https://github.com/Kitware/CMake/releases/download/v4.5.0/cmake-4.5.0-linux-x86_64.tar.gz \
    cmake-4.5.0-linux-x86_64.tar.gz
ADD --checksum=sha256:5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6 \
    https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip \
    ninja-linux.zip
ADD --checksum=sha256:f72edb0b36580ece7f52e23858f00c18fad4e87395ea7ad05d7e55c39f8fc78a \
    https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.5/LLVM-23.1.5-Linux-X64.tar.xz \
    LLVM-23.1.5-Linux-X64.tar.xz
ADD --checksum=sha256:75419ef4f446fc1c24ef12514b574e66e898ee6f527c6ae2ad84f91a905823c2 \
    https://github.com/doxygen/doxygen/releases/download/Release_1_17_0/doxygen-1.17.0.linux.bin.tar.gz \
    doxygen-1.17.0.linux.bin.tar.gz

# Match the commits, archive checksums, and provenance files in the CMake helpers.
ADD --checksum=sha256:623fe05d283020f37a579a8985ba338091ac7229da62b5591ff40acdf05b95e2 \
    https://codeload.github.com/google/googletest/tar.gz/063de7e9578f82b369302001269680b4b1553359 \
    googletest-1.18.0.tar.gz
ADD --checksum=sha256:1e34f52437882bfe010f955580e77a19b4fb0a702ef678609b509a834296075c \
    https://codeload.github.com/google/benchmark/tar.gz/a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c \
    benchmark-1.10.0.tar.gz

FROM gcc:16.2.0@sha256:531d94a9eb84cb69df20050ddde80d308bf8650c83f4cb0accf4ef39bb543ed4 AS ci

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       ca-certificates git graphviz libgmp-dev pkg-config python3 unzip xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN --mount=type=bind,from=archives,source=/archives,target=/tmp/ci-archives <<'SH'
set -eu
cd /tmp/ci-archives
mkdir -p /opt/dandelion /opt/llvm /opt/doxygen
tar -xf cmake-4.5.0-linux-x86_64.tar.gz --strip-components=1 -C /opt/dandelion
unzip -p ninja-linux.zip ninja > /opt/dandelion/bin/ninja
chmod 0755 /opt/dandelion/bin/ninja
tar -xf LLVM-23.1.5-Linux-X64.tar.xz --strip-components=1 -C /opt/llvm
tar -xf doxygen-1.17.0.linux.bin.tar.gz --strip-components=1 -C /opt/doxygen

# Use the same GCC installation and libstdc++ modules for both C++ compilers.
printf '%s\n' --gcc-toolchain=/usr/local -stdlib=libstdc++ > /opt/llvm/bin/clang++.cfg

gtest=/opt/dandelion/subprojects/googletest-1.18.0
benchmark=/opt/dandelion/subprojects/benchmark-1.10.0
mkdir -p "$gtest" "$benchmark"
tar -xf googletest-1.18.0.tar.gz --strip-components=1 -C "$gtest"
tar -xf benchmark-1.10.0.tar.gz --strip-components=1 -C "$benchmark"
printf '%s\n' 063de7e9578f82b369302001269680b4b1553359 > "$gtest/.dandelion-googletest-commit"
printf '%s\n' a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c > "$benchmark/.dandelion-benchmark-commit"
SH

ENV PATH="/opt/dandelion/bin:/opt/llvm/bin:/opt/doxygen/bin:${PATH}"

RUN <<'SH'
set -eu
cmake --version | grep -Fx 'cmake version 4.5.0'
test "$(ninja --version)" = 1.13.2
test "$(gcc -dumpfullversion)" = 16.2.0
test "$(g++ -dumpfullversion)" = 16.2.0
for tool in clang clang++ clang-scan-deps clang-format clang-tidy; do
    "$tool" --version | grep -E 'version 23\.1\.5($|[[:space:]])'
done
metadata=$(g++ -print-file-name=libstdc++.modules.json)
test -f "$metadata"
test "$metadata" -ef "$(clang++ -print-file-name=libstdc++.modules.json)"
test "$(pkg-config --modversion gmp)" = 6.3.0
doxygen --version | grep -E '^1\.17\.0([[:space:]]|$)'
dot -V
python3 --version
SH
