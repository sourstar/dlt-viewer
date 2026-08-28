# DLT Viewer reproducible build container.
#
# Provides two toolchains in one image:
#   * native Linux x86_64  (gcc + Fedora Qt6)
#   * Windows x86_64 cross (mingw-w64 gcc + Fedora's mingw64 Qt6 packages)
#
# Windows ARM64 is NOT built here: it needs the MSVC ARM64 toolchain and a
# Qt6 ARM64 build, neither of which can be cross-hosted from Linux. That
# target is produced by .github/workflows/BuildPR.yml on a windows-11-arm
# runner. See docker/README.md.

FROM fedora:42

ARG QT6_MINGW_VERSION=6.10.2

RUN dnf -y --setopt=install_weak_deps=False install \
        cmake ninja-build make git which file findutils tar xz p7zip p7zip-plugins \
        gcc gcc-c++ \
        qt6-qtbase-devel qt6-qtserialport-devel qt6-qttools-devel \
        gtest-devel \
        mingw64-gcc mingw64-gcc-c++ mingw64-binutils mingw64-filesystem \
        mingw64-winpthreads mingw64-winpthreads-static \
        mingw64-qt6-qtbase mingw64-qt6-qtserialport mingw64-qt6-qtsvg mingw64-qt6-qttools \
    && dnf clean all && rm -rf /var/cache/dnf

# Host Qt6 tools (moc/uic/rcc) used by the cross build.
ENV QT_HOST_PATH=/usr

WORKDIR /src
COPY build-in-container.sh /usr/local/bin/build-in-container.sh
RUN chmod +x /usr/local/bin/build-in-container.sh
ENTRYPOINT ["/usr/local/bin/build-in-container.sh"]
CMD ["windows-x64"]
