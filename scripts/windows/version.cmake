# Run for a Windows *target*, not just a Windows host, so that the Linux
# mingw container on the perf/mingw branch can cross-compile the Windows build.
#
# CMake processes the top-level CMakeLists twice on a fresh configure; on the
# second pass WIN32 and CMAKE_SYSTEM_NAME are not yet populated, so the
# decision is latched into the cache on the first pass.
if(NOT WIN32 AND NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Windows" AND NOT "$ENV{DLT_TARGET_IS_WINDOWS}")
    return()
endif()
set(DLT_TARGET_IS_WINDOWS ON CACHE INTERNAL "DLT Viewer: build target is Windows")
message(STATUS "windows/version.cmake ${CMAKE_CURRENT_SOURCE_DIR}")

# RC is enabled here on purpose: Qt6 enables it later for Windows targets,
# and a late CMAKE_RC_COMPILER cache write makes CMake delete the cache and
# re-run configure without the toolchain, which breaks the container cross build.
project(GenerateVersion CXX RC)

set(QT5_MIN_VERSION_REQ "5.5.1")
set(QT6_MIN_VERSION_REQ "6.2.0")

# try to find QT6
find_package(Qt6 "6" COMPONENTS Core Network PrintSupport SerialPort Widgets Concurrent)
if(Qt6_FOUND)
    set(QT_PREFIX Qt6)
    message(STATUS "Found Qt6 version: ${Qt6Core_VERSION}")
    if(${QT_PREFIX}Core_VERSION VERSION_LESS ${QT6_MIN_VERSION_REQ})
        # Presumably Qt6Core implies all dependent libs too
        message(FATAL_ERROR "Due to SerialPort QT6 minimum version required: ${QT6_MIN_VERSION_REQ}")
    endif()
else()
    find_package(Qt5 "5" REQUIRED COMPONENTS Core Network PrintSupport SerialPort Widgets Concurrent)
    if(Qt5_FOUND)
        set(QT_PREFIX Qt5)
        message(STATUS "Found Qt5 version: ${Qt5Core_VERSION}")
        if(${QT_PREFIX}Core_VERSION VERSION_LESS ${QT5_MIN_VERSION_REQ})
            # Presumably Qt5Core implies all dependent libs too
            message(FATAL_ERROR "QT5 minimum version required: ${QT5_MIN_VERSION_REQ}")
        endif()
    endif()
endif()

set(DLT_QT_VERSION "${${QT_PREFIX}Core_VERSION}" CACHE STRING "DLT_QT_VERSION")
get_target_property(DLT_QT_LIBRARY_PATH ${QT_PREFIX}::Core LOCATION)
get_filename_component(DLT_QT_LIB_DIR ${DLT_QT_LIBRARY_PATH} DIRECTORY)

find_package(Git REQUIRED)
execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count --no-merges HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_PATCH_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}\\scripts\\windows\\parse_version.bat" "${CMAKE_CURRENT_SOURCE_DIR}\\src\\version.h" PACKAGE_MAJOR_VERSION
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/windows"
    OUTPUT_VARIABLE DLT_PROJECT_VERSION_MAJOR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}\\scripts\\windows\\parse_version.bat" "${CMAKE_CURRENT_SOURCE_DIR}\\src\\version.h" PACKAGE_MINOR_VERSION
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/windows"
    OUTPUT_VARIABLE DLT_PROJECT_VERSION_MINOR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}\\scripts\\windows\\parse_version.bat" "${CMAKE_CURRENT_SOURCE_DIR}\\src\\version.h" PACKAGE_PATCH_LEVEL
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/windows"
    OUTPUT_VARIABLE DLT_PROJECT_VERSION_PATCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# The parse_version.bat helper above only runs on a Windows host. When this
# Windows build is produced by cross-compiling from Linux, fall
# back to reading src/version.h directly with CMake.
if(NOT CMAKE_HOST_WIN32)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" DLT_VERSION_HEADER_CONTENT)
    foreach(part MAJOR_VERSION MINOR_VERSION PATCH_LEVEL)
        if("${part}" STREQUAL "MAJOR_VERSION")
            set(outvar DLT_PROJECT_VERSION_MAJOR)
        elseif("${part}" STREQUAL "MINOR_VERSION")
            set(outvar DLT_PROJECT_VERSION_MINOR)
        else()
            set(outvar DLT_PROJECT_VERSION_PATCH)
        endif()
        if(DLT_VERSION_HEADER_CONTENT MATCHES "#define[ \t]+PACKAGE_${part}[ \t]+([0-9]+)")
            set(${outvar} "${CMAKE_MATCH_1}")
        else()
            set(${outvar} 0)
        endif()
    endforeach()
    message(STATUS "Cross build: parsed version ${DLT_PROJECT_VERSION_MAJOR}.${DLT_PROJECT_VERSION_MINOR}.${DLT_PROJECT_VERSION_PATCH} from src/version.h")
endif()

if(MSVC)
    set(VS_VERSION ${MSVC_TOOLSET_VERSION})
    string(REPLACE "140" "msvc2015" VS_VERSION ${VS_VERSION})
    string(REPLACE "141" "msvc2017" VS_VERSION ${VS_VERSION})
    string(REPLACE "142" "msvc2019" VS_VERSION ${VS_VERSION})
    string(REPLACE "143" "msvc2022" VS_VERSION ${VS_VERSION})
endif()
set(DLT_VERSION_SUFFIX "STABLE-qt${DLT_QT_VERSION}-r${GIT_PATCH_VERSION}_${VS_VERSION}_${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
