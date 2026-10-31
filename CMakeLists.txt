cmake_minimum_required(VERSION 3.16)
project(chitralipi VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CHITRALIPI_BUILD_CLI "Build the chitralipi command line tool" ON)
option(CHITRALIPI_BUILD_TESTS "Build the test suite" ON)
option(CHITRALIPI_FETCH_STB "Download stb_image so the CLI reads/writes PNG, JPEG, BMP, TGA, GIF" OFF)
option(CHITRALIPI_JNI "Put the Java (JNI) glue inside the shared library" OFF)
set(CHITRALIPI_STB_DIR "" CACHE PATH "Folder with stb_image.h and stb_image_write.h")

# Android NDK cross-build (an app's .so). A native Termux build also reports
# ANDROID, but it has no ANDROID_ABI, so it builds like normal Linux.
if(ANDROID AND ANDROID_ABI)
  set(CHITRALIPI_JNI ON)
  set(CHITRALIPI_BUILD_CLI OFF)
  set(CHITRALIPI_BUILD_TESTS OFF)
endif()

set(CHL_CORE_SOURCES
  src/api.cpp
  src/codec.cpp
  src/text.cpp
  src/transform.cpp
  src/util.cpp)
if(CHITRALIPI_JNI)
  list(APPEND CHL_CORE_SOURCES bindings/java/jni/chitralipi_jni.cpp)
endif()

add_library(chitralipi_obj OBJECT ${CHL_CORE_SOURCES})
target_include_directories(chitralipi_obj PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE src)
target_compile_definitions(chitralipi_obj PRIVATE CHITRALIPI_BUILDING)
set_target_properties(chitralipi_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
  target_compile_options(chitralipi_obj PRIVATE /W4 /utf-8)
else()
  target_compile_options(chitralipi_obj PRIVATE -Wall -Wextra)
endif()
if(CHITRALIPI_JNI AND NOT ANDROID)
  find_package(JNI REQUIRED)
  target_include_directories(chitralipi_obj PRIVATE ${JNI_INCLUDE_DIRS})
endif()

# ---- WebAssembly (emcmake cmake ...) ----------------------------------------
if(EMSCRIPTEN)
  add_executable(chitralipi_wasm $<TARGET_OBJECTS:chitralipi_obj>)
  set_target_properties(chitralipi_wasm PROPERTIES OUTPUT_NAME chitralipi SUFFIX ".mjs")
  target_link_options(chitralipi_wasm PRIVATE
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createChitralipi
    -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,worker,node
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_chl_encode','_chl_decode','_chl_pack','_chl_unpack','_chl_options_default','_chl_status_string','_chl_free','_chl_version']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','getValue','setValue','UTF8ToString','stringToUTF8','lengthBytesUTF8']")
  return()
endif()

# ---- shared + static library --------------------------------------------------
add_library(chitralipi SHARED $<TARGET_OBJECTS:chitralipi_obj>)
target_include_directories(chitralipi PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_definitions(chitralipi INTERFACE CHITRALIPI_SHARED)
set_target_properties(chitralipi PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
if(ANDROID)
  # Android 15+ devices with 16 KB memory pages.
  target_link_options(chitralipi PRIVATE "-Wl,-z,max-page-size=16384")
endif()

add_library(chitralipi_static STATIC $<TARGET_OBJECTS:chitralipi_obj>)
target_include_directories(chitralipi_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

include(GNUInstallDirs)
install(TARGETS chitralipi chitralipi_static EXPORT chitralipiTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/chitralipi/chitralipi.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/chitralipi)

# find_package(chitralipi) -> chitralipi::chitralipi (shared) and chitralipi::chitralipi_static
install(EXPORT chitralipiTargets
  NAMESPACE chitralipi::
  FILE chitralipiConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chitralipi)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/chitralipiConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/chitralipiConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chitralipi)

# pkg-config: clang app.c $(pkg-config --cflags --libs chitralipi)
file(RELATIVE_PATH CHL_PC_TO_PREFIX "/r/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/r")  # relocatable
configure_file(cmake/chitralipi.pc.in ${CMAKE_CURRENT_BINARY_DIR}/chitralipi.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/chitralipi.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# Python module next to the other files, so installers can copy it.
install(FILES bindings/python/chitralipi.py DESTINATION ${CMAKE_INSTALL_DATADIR}/chitralipi/python)

# ---- command line tool --------------------------------------------------------
if(CHITRALIPI_BUILD_CLI)
  add_executable(chitralipi_cli tools/cli.cpp)
  set_target_properties(chitralipi_cli PROPERTIES OUTPUT_NAME chitralipi)
  target_link_libraries(chitralipi_cli PRIVATE chitralipi_static)

  set(CHL_STB_INCLUDE "")
  if(CHITRALIPI_STB_DIR)
    set(CHL_STB_INCLUDE ${CHITRALIPI_STB_DIR})
  elseif(CHITRALIPI_FETCH_STB)
    include(FetchContent)
    FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(stb)  # stb has no CMakeLists, so this only downloads it
    set(CHL_STB_INCLUDE ${stb_SOURCE_DIR})
  endif()
  if(CHL_STB_INCLUDE)
    target_include_directories(chitralipi_cli PRIVATE ${CHL_STB_INCLUDE})
    target_compile_definitions(chitralipi_cli PRIVATE CHL_HAVE_STB=1)
    message(STATUS "Chitralipi CLI: PNG/JPEG support ON")
  else()
    message(STATUS "Chitralipi CLI: PNG/JPEG support OFF (PPM/PGM/PAM only). Use -DCHITRALIPI_FETCH_STB=ON")
  endif()
  if(MSVC)
    target_compile_options(chitralipi_cli PRIVATE /utf-8)
  endif()
  install(TARGETS chitralipi_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- tests --------------------------------------------------------------------
if(CHITRALIPI_BUILD_TESTS)
  enable_testing()
  add_executable(chitralipi_tests tests/test_main.cpp)
  target_include_directories(chitralipi_tests PRIVATE src)
  target_link_libraries(chitralipi_tests PRIVATE chitralipi_static)
  if(MSVC)
    target_compile_options(chitralipi_tests PRIVATE /utf-8)
  endif()
  add_test(NAME core COMMAND chitralipi_tests)

  add_executable(chitralipi_c_test tests/test_c_api.c)
  target_link_libraries(chitralipi_c_test PRIVATE chitralipi_static)
  set_target_properties(chitralipi_c_test PROPERTIES LINKER_LANGUAGE CXX)
  add_test(NAME c_api COMMAND chitralipi_c_test)
endif()
