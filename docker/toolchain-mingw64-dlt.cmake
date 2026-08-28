# Windows x86_64 cross toolchain for the DLT Viewer container build.
#
# Wraps Fedora's stock mingw64 toolchain, with two corrections:
#
#  * CMAKE_RC_COMPILER. Fedora's toolchain hardcodes /usr/bin/...-windres,
#    but CMake resolves the same tool through PATH to /usr/sbin/...-windres.
#    Once Qt6 enables the RC language for a Windows target CMake sees the two
#    values disagree, deletes the cache and restarts configure -- and that
#    restart runs without the toolchain, so nothing is found. Resolving the
#    path the same way CMake does keeps the values identical.
#
#  * Qt locations are pinned here rather than passed with -D so they are not
#    lost if CMake does restart configure for some other reason.
include(/usr/share/mingw/toolchain-mingw64.cmake)

find_program(DLT_MINGW_WINDRES NAMES x86_64-w64-mingw32-windres REQUIRED)
set(CMAKE_RC_COMPILER "${DLT_MINGW_WINDRES}")

set(DLT_MINGW_SYSROOT /usr/x86_64-w64-mingw32/sys-root/mingw)
set(CMAKE_PREFIX_PATH "${DLT_MINGW_SYSROOT}" CACHE PATH "" FORCE)
set(Qt6_DIR "${DLT_MINGW_SYSROOT}/lib/cmake/Qt6" CACHE PATH "" FORCE)

# Native Qt6 supplying the moc/uic/rcc host tools for the cross build.
set(QT_HOST_PATH /usr CACHE PATH "" FORCE)
set(QT_HOST_PATH_CMAKE_DIR /usr/lib64/cmake CACHE PATH "" FORCE)
