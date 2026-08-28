# Locate the Qt plugin directory. The Qt online-installer SDK keeps plugins in
# <lib>/../plugins, but distribution packages (e.g. Fedora's mingw64-qt6, used
# by the container cross build in docker/) use <prefix>/lib/qt6/plugins.
if(NOT DLT_QT_PLUGIN_DIR)
    foreach(dlt_plugin_candidate
            "${DLT_QT_LIB_DIR}/../plugins"
            "${DLT_QT_LIB_DIR}/../lib/qt6/plugins"
            "${DLT_QT_LIB_DIR}/../lib/qt5/plugins")
        if(IS_DIRECTORY "${dlt_plugin_candidate}/platforms")
            get_filename_component(DLT_QT_PLUGIN_DIR "${dlt_plugin_candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()
endif()
if(NOT DLT_QT_PLUGIN_DIR)
    message(FATAL_ERROR "Could not locate the Qt plugin directory near ${DLT_QT_LIB_DIR}")
endif()
message(STATUS "Qt plugin directory: ${DLT_QT_PLUGIN_DIR}")

set(QT_LIBS
  ${QT_PREFIX}::Core
    ${QT_PREFIX}::Concurrent
  ${QT_PREFIX}::Gui
  ${QT_PREFIX}::Network
  ${QT_PREFIX}::PrintSupport
  ${QT_PREFIX}::SerialPort
  ${QT_PREFIX}::Widgets)

foreach(QT_LIB IN ITEMS ${QT_LIBS})
  get_target_property(LIBRARY_PATH ${QT_LIB} LOCATION)
  install(FILES
      "${LIBRARY_PATH}"
      DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}"
      COMPONENT qt_libraries)
endforeach()

if("${QT_PREFIX}" STREQUAL "Qt5")
install(FILES
    "${DLT_QT_PLUGIN_DIR}/bearer/qgenericbearer.dll"
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}/bearer"
    COMPONENT qt_libraries)
endif()
install(FILES
    "${DLT_QT_PLUGIN_DIR}/iconengines/qsvgicon.dll"
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}/iconengines"
    COMPONENT qt_libraries)
install(FILES
    "${DLT_QT_PLUGIN_DIR}/platforms/qwindows.dll"
    "${DLT_QT_PLUGIN_DIR}/platforms/qoffscreen.dll"
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}/platforms"
    COMPONENT qt_libraries)
install(FILES
    "${DLT_QT_PLUGIN_DIR}/imageformats/qico.dll"
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}/imageformats"
    COMPONENT qt_libraries)
if("${QT_PREFIX}" STREQUAL "Qt5")
install(FILES
    "${DLT_QT_PLUGIN_DIR}/printsupport/windowsprintersupport.dll"
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}/printsupport"
    COMPONENT qt_libraries)
endif()

if("${QT_PREFIX}" STREQUAL "Qt5")
install(FILES
    "${DLT_QT_PLUGIN_DIR}/styles/qwindowsvistastyle.dll"
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}/styles"
    COMPONENT qt_libraries)
endif()

# HTTPS requests (e.g. update checker) require a TLS backend plugin at runtime.
# In dev environments this is often found via Qt install paths, but packaged
# builds must carry the plugin inside the bundle.
install(DIRECTORY
    "${DLT_QT_PLUGIN_DIR}/tls"
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}"
    COMPONENT qt_libraries
    OPTIONAL
    FILES_MATCHING PATTERN "*.dll")
 
# If Qt was built with OpenSSL backend, include runtime DLLs when present.
file(GLOB QT_OPENSSL_RUNTIME_DLLS
    "${DLT_QT_LIB_DIR}/libssl-*.dll"
    "${DLT_QT_LIB_DIR}/libcrypto-*.dll")
if(QT_OPENSSL_RUNTIME_DLLS)
install(FILES
    ${QT_OPENSSL_RUNTIME_DLLS}
    DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}"
    COMPONENT qt_libraries)
endif()

# if("${QT_PREFIX}" STREQUAL "Qt6")
# install(FILES
#     "${DLT_QT_PLUGIN_DIR}/styles/qmodernwindowsstyle.dll"
#     DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}/styles"
#     COMPONENT qt_libraries)
# endif()
	
option(INCLUDE_VC_REDIST "Add vc_redist.x64.exe cmake install command" OFF)
if(INCLUDE_VC_REDIST)
  get_filename_component(MSVC_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY )
  set(VC_REDIST_PATH "${MSVC_COMPILER_DIR}/../../../../../../Redist/MSVC/v${MSVC_TOOLSET_VERSION}/vc_redist.x64.exe")
  install(FILES
      "${VC_REDIST_PATH}"
      DESTINATION "${DLT_EXECUTABLE_INSTALLATION_PATH}"
      COMPONENT vc_redist_x64)
endif()
