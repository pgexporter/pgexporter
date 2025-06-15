#
# Copyright (C) 2025 The pgexporter community
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list
# of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may
# be used to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

# Find Check framework
find_package(PkgConfig REQUIRED)
pkg_check_modules(CHECK check)

if (CHECK_FOUND)
  set(SOURCES
    tsclient.c
    testcases/pgexporter_test_1.c
    testcases/pgexporter_test_2.c
    testcases/pgexporter_test_3.c
    runner.c
  )

  add_compile_options(-O0)
  add_compile_options(-DDEBUG)
  
  if (CMAKE_BUILD_TYPE MATCHES Debug)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
      if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
        if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
          add_compile_options(-fsanitize=address)
          add_compile_options(-fsanitize=undefined)
          add_compile_options(-fno-sanitize-recover=all)
          add_compile_options(-fsanitize=float-divide-by-zero)
          add_compile_options(-fsanitize=float-cast-overflow)
          add_compile_options(-fno-sanitize=null)
          add_compile_options(-fno-sanitize=alignment)

          set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment")
        endif()
      endif()
    endif()
  endif()

  add_executable(pgexporter_test ${SOURCES})
  target_include_directories(pgexporter_test PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
  target_include_directories(pgexporter_test PRIVATE ${CMAKE_SOURCE_DIR}/test/include)
  target_include_directories(pgexporter_test PRIVATE ${CHECK_INCLUDE_DIRS})

  if(EXISTS "/etc/debian_version")
    target_link_libraries(pgexporter_test ${CHECK_LIBRARIES} subunit pthread rt m pgexporter)
  elseif(APPLE)
    target_link_libraries(pgexporter_test ${CHECK_LIBRARIES} m pgexporter)
  else()
    target_link_libraries(pgexporter_test ${CHECK_LIBRARIES} pthread rt m pgexporter)
  endif()

  target_compile_options(pgexporter_test PRIVATE ${CHECK_CFLAGS_OTHER})
  target_link_directories(pgexporter_test PRIVATE ${CHECK_LIBRARY_DIRS})

  add_custom_target(custom_clean
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o pgexporter_test
    COMMENT "Cleaning up..."
  )
else()
  message(STATUS "Check framework not found - tests will be skipped")
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/test/testsuite.sh"
  "${CMAKE_BINARY_DIR}/testsuite.sh"
  COPYONLY
)