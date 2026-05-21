cmake_minimum_required(VERSION 3.14)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "9")
set(VERSION_PATCH "0")
set(VERSION_STRING ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

option(ENABLE_COVERAGE "Enable code coverage" OFF)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

#
# Avoid source tree pollution
#
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

If(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not permitted. Make a separate folder for building:\nmkdir build; cd build; cmake ..\nBefore that, remove the files already created:\nrm -rf CMakeCache.txt CMakeFiles")
endif(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)

project(pgexporter VERSION ${VERSION_STRING} LANGUAGES  C)

set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
  "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_SOURCE_IGNORE_FILES
  "/build/;/test/;/.cache/;/.git/;/.github/;/*.patch;/.bundle/;/_site/;/vendor/;~$;${CPACK_SOURCE_IGNORE_FILES}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
include(CPack)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "pgexporter ${VERSION_STRING}")

if (NOT DEFINED DOCS)
  set(DOCS TRUE)
endif()
set(check TRUE)

#
# Enable LLVM coverage when running tests with Clang
#
if (check AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
  message(STATUS "LLVM coverage enabled")

  add_compile_options(
    -fprofile-instr-generate
    -fcoverage-mapping
    -O0
  )

  add_link_options(
    -fprofile-instr-generate
    -fcoverage-mapping
  )

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
endif()


include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(FindPackageHandleStandardArgs)
include(GNUInstallDirs)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif ()

message(STATUS "Build type is ${CMAKE_BUILD_TYPE}")
message(STATUS "System is ${CMAKE_SYSTEM_NAME}")

set(SUPPORTED_COMPILERS "GNU" "Clang" "AppleClang")

# Check for a supported compiler
if (NOT CMAKE_C_COMPILER_ID IN_LIST SUPPORTED_COMPILERS)
   message(FATAL_ERROR "Unsupported compiler ${CMAKE_C_COMPILER_ID}. Supported compilers are: ${SUPPORTED_COMPILERS}")
endif ()

CHECK_C_COMPILER_FLAG("-std=c17" COMPILER_SUPPORTS_C17)
if(NOT COMPILER_SUPPORTS_C17)
  message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} has no C17 support. Please use a different C compiler.")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(HOMEBREW_PREFIX "/opt/homebrew")
  else()
    set(HOMEBREW_PREFIX "/usr/local")
  endif()
  
  message(STATUS "Detected macOS with Homebrew prefix: ${HOMEBREW_PREFIX}")
  
  set(CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}" ${CMAKE_PREFIX_PATH})
  include_directories("${HOMEBREW_PREFIX}/include")
  link_directories("${HOMEBREW_PREFIX}/lib")
  
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DARWIN_C_SOURCE -I${HOMEBREW_PREFIX}/include")
endif()


find_package(ZLIB)
if (ZLIB_FOUND)
  message(STATUS "zlib found")
else ()
  message(FATAL_ERROR "zlib needed")
endif()

find_package(BZip2)
if (BZIP2_FOUND)
  message(STATUS "bzip2 found")
else ()
  message(FATAL_ERROR "bzip2 needed")
endif()

find_package(Zstd)
if (ZSTD_FOUND)
  message(STATUS "zstd found")
else ()
  message(FATAL_ERROR "zstd needed")
endif()

find_package(Lz4)
if (LZ4_FOUND)
  message(STATUS "lz4 found")
else ()
  message(FATAL_ERROR "lz4 needed")
endif()

#
# Event backend detection (io_uring/epoll/kqueue); custom layer in src/include/ev.h
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  # Check for io_uring support (liburing >= 2.5)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBURING liburing>=2.5)
  
  if (LIBURING_FOUND)
    set(HAVE_IO_URING 1 CACHE BOOL "io_uring support available")
    message(STATUS "io_uring found (liburing ${LIBURING_VERSION})")
    
    # Check for required io_uring constants
    set(CMAKE_REQUIRED_INCLUDES "${LIBURING_INCLUDE_DIRS}")
    set(HAVE_IO_URING_CONSTANTS ON)
    set(MISSING_IO_URING_CONSTANTS "")
    
    foreach(constant
            IORING_SETUP_CQSIZE
            IORING_SETUP_DEFER_TASKRUN
            IORING_SETUP_SINGLE_ISSUER
            IORING_TIMEOUT_MULTISHOT
            IORING_SQ_CQ_OVERFLOW
            IORING_CQE_BUFFER_SHIFT
            IORING_CQE_F_MORE)
      set(IO_URING_CONSTANTS_TEST "
#include <liburing.h>
int main(void) {
  (void)${constant};
  return 0;
}
")
      set(const_var "HAVE_${constant}")
      check_c_source_compiles("${IO_URING_CONSTANTS_TEST}" ${const_var})
      if(${const_var})
        message(STATUS "io_uring constant check: ${constant}=usable")
      else()
        message(STATUS "io_uring constant check: ${constant}=not-usable")
        set(HAVE_IO_URING_CONSTANTS OFF)
        list(APPEND MISSING_IO_URING_CONSTANTS "${constant}")
      endif()
    endforeach()
    set(CMAKE_REQUIRED_INCLUDES "")
    
    if (HAVE_IO_URING_CONSTANTS)
      add_compile_options(-DHAVE_IO_URING)
    else()
      message(STATUS "io_uring constants missing; falling back to epoll")
      message(STATUS "Missing io_uring constants: ${MISSING_IO_URING_CONSTANTS}")
      set(HAVE_IO_URING 0)
    endif()
  else ()
    set(HAVE_IO_URING 0 CACHE BOOL "io_uring support available")
    message(STATUS "io_uring not found (liburing >= 2.5 required)")
  endif ()
  
  # Check for epoll support
  include(CheckSymbolExists)
  check_symbol_exists(epoll_create1 "sys/epoll.h" HAVE_EPOLL)
  if (HAVE_EPOLL)
    message(STATUS "epoll found")
  else ()
    message(STATUS "epoll not available")
  endif ()
  
  # Check for epoll_pwait2 (Linux 5.11+)
  check_symbol_exists(epoll_pwait2 "sys/epoll.h" HAVE_EPOLL_PWAIT2)
  if (HAVE_EPOLL_PWAIT2)
    message(STATUS "epoll_pwait2 found")
  endif ()
  
  set(HAVE_LINUX 1 CACHE BOOL "Linux system")
  
  # Check Linux kernel version (simplified check)
  execute_process(
    COMMAND uname -r
    OUTPUT_VARIABLE KERNEL_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE KERNEL_RESULT
  )
  if (KERNEL_RESULT EQUAL 0)
    message(STATUS "Linux kernel version: ${KERNEL_VERSION}")
    # Basic version check - io_uring requires 5.1+
    if (KERNEL_VERSION VERSION_GREATER_EQUAL "5.1")
      message(STATUS "Kernel supports io_uring (5.1+)")
    else ()
      message(STATUS "Kernel may not support io_uring (requires 5.1+)")
    endif ()
  endif ()
  
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin" OR ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
  # Check for kqueue support (BSD/macOS)
  check_symbol_exists(kqueue "sys/event.h" HAVE_KQUEUE)
  if (HAVE_KQUEUE)
    message(STATUS "kqueue found")
  else ()
    message(FATAL_ERROR "kqueue not available")
  endif ()
endif ()

find_package(OpenSSL)
if (OPENSSL_FOUND)
  message(STATUS "OpenSSL found")
else ()
  message(FATAL_ERROR "OpenSSL needed")
endif()

find_package(Rst2man)
if (RST2MAN_FOUND)
  message(STATUS "rst2man found")
else ()
  message(FATAL_ERROR "rst2man needed")
endif()

find_package(THREAD)
if (THREAD_FOUND)
  message(STATUS "pthread found")
else ()
  message(FATAL_ERROR "pthread needed")
endif()

if (DOCS)
  find_package(Pandoc)
  if (PANDOC_FOUND)
    message(STATUS "pandoc found")
  else ()
    set(DOCS FALSE)
    message(STATUS "pandoc needed. The documentation will be skipped.")
  endif()

  find_package(Xelatex)
  if (XELATEX_FOUND)
    message(STATUS "xelatex found")
  else ()
    set(DOCS FALSE)
    message(STATUS "xelatex needed. The documentation will be skipped.")
  endif()

  find_package(Doxygen)

  if (DOXYGEN_FOUND)
    message(status "Doxygen found: ${DOXYGEN_EXECUTABLE}")
  endif()
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  find_package(Libatomic)
  if (LIBATOMIC_FOUND)
    message(STATUS "libatomic found")
  else ()
    message(FATAL_ERROR "libatomic needed")
  endif()

  find_package(Systemd)
  if (SYSTEMD_FOUND)
    message(STATUS "systemd found")
  else ()
    message(STATUS "systemd not found; building without systemd support")
  endif()
endif()

find_package(LibYAML)
if (LIBYAML_FOUND)
  message(STATUS "LibYAML found")
else ()
  message(FATAL_ERROR "LibYAML needed")
endif()

find_package(Python3 COMPONENTS Interpreter)
if (Python3_FOUND)
  message(STATUS "Python3 found: ${Python3_EXECUTABLE}")
else()
  message(STATUS "Python3 not found; contrib file generation will be skipped")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/")

add_subdirectory(doc)
add_subdirectory(src)
add_subdirectory(extensions)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
  add_subdirectory(test)
else()
  message(STATUS "Test directory not found, skipping tests")
endif()

#
# Custom target to regenerate contrib files
#
if(Python3_FOUND)
  set(YAML_JSON_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_yaml_json.py")
  set(DASHBOARD_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_dashboards.py")
  set(INTERNAL_H "${CMAKE_CURRENT_SOURCE_DIR}/src/include/internal.h")
  set(DASHBOARD_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/contrib/grafana/postgresql_dashboard.json")

  add_custom_target(
    generate-yaml-json
    COMMAND ${Python3_EXECUTABLE} ${YAML_JSON_SCRIPT}
            --internal-h ${INTERNAL_H}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating contrib YAML/JSON files from internal.h"
  )

  add_custom_target(
    generate-dashboards
    COMMAND ${Python3_EXECUTABLE} ${DASHBOARD_SCRIPT}
            --template ${DASHBOARD_TEMPLATE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating Grafana dashboard files from template"
  )

  add_custom_target(
    generate-contrib
    DEPENDS generate-yaml-json generate-dashboards
  )
endif()
