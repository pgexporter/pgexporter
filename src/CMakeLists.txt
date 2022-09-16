#
# Add files for libpgexporter
#
FILE(GLOB SOURCE_FILES "libpgexporter/*.c")
FILE(GLOB HEADER_FILES "include/*.h")

set(SOURCES ${SOURCE_FILES} ${HEADER_FILES})

#
# OS
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

  add_compile_options(-DHAVE_LINUX)
  add_compile_options(-D_POSIX_C_SOURCE=200809L)

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBEV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${SYSTEMD_INCLUDE_DIRS}
    ${LIBYAML_INCLUDE_DIRS}
  )

  #
  # Library directories
  #
  link_libraries(
    ${LIBEV_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${SYSTEMD_LIBRARIES}
    ${LIBYAML_LIBRARIES}
  )

  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

else()

  add_compile_options(-D_XOPEN_SOURCE=700)
  add_compile_options(-D_BSD_SOURCE)
  add_compile_options(-D_DEFAULT_SOURCE)
  add_compile_options(-D__BSD_VISIBLE)

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBEV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${LIBYAML_INCLUDE_DIRS}
  )

  #
  # Library directories
  #
  link_libraries(
    ${LIBEV_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${LIBYAML_LIBRARIES}
  )

  if (${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    add_compile_options(-DHAVE_OPENBSD)
  elseif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    add_compile_options(-DHAVE_FREEBSD)
  endif()

endif()

#
# Compile options
#
add_compile_options(-g)
add_compile_options(-Wall)
add_compile_options(-std=c17)
add_compile_options(-D__USE_ISOC11)
add_compile_options(-D_GNU_SOURCE)
add_compile_options(-Wno-deprecated)
add_compile_options(-Wno-deprecated-declarations)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wstrict-prototypes)
endif()

# Debug
if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_compile_options(-O0)
  add_compile_options(-DDEBUG)

  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
      add_compile_options(-fsanitize=address)

      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address")
    endif()
  endif()

  check_c_compiler_flag(-fno-omit-frame-pointer HAS_NO_OMIT_FRAME_POINTER)
  if (HAS_NO_OMIT_FRAME_POINTER)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer")
  endif()
endif()

# Release
if (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  add_compile_options(-O2)
  add_compile_options(-DNDEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)

check_c_compiler_flag(-Wformat HAS_FORMAT)
if (HAS_FORMAT)
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat")
endif()

check_c_compiler_flag(-Wformat-security HAS_FORMAT_SECURITY)
if (HAS_FORMAT_SECURITY)
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat-security")
endif()

check_c_compiler_flag(-fstack-protector-strong HAS_STACKPROTECTOR_STRONG)
if (HAS_STACKPROTECTOR_STRONG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
else()
  check_c_compiler_flag(-fstack-protector HAS_STACKPROTECTOR)
  if (HAS_STACKPROTECTOR)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector")
  endif()
endif()

check_c_compiler_flag(-rdynamic HAS_DYNAMIC)
if (HAS_DYNAMIC)
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
endif()

check_c_compiler_flag(-fPIC HAS_PIC)
if (HAS_PIC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  include(CheckPIESupported)
  check_pie_supported()
endif()

check_c_compiler_flag(-Wl,-z,relro HAS_RELRO)
if (HAS_RELRO)
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
endif()

check_c_compiler_flag(-Wl,-z,now HAS_NOW)
if (HAS_NOW)
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,now")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
endif()

#
# Build libpgexporter
#
add_library(pgexporter SHARED ${SOURCES})
set_target_properties(pgexporter PROPERTIES LINKER_LANGUAGE C VERSION ${VERSION_STRING}
                               SOVERSION ${VERSION_MAJOR})
target_link_libraries(pgexporter PUBLIC)

install(TARGETS pgexporter DESTINATION ${CMAKE_INSTALL_LIBDIR}/)

#
# Build pgexporter
#
add_executable(pgexporter-bin main.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgexporter-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgexporter POSITION_INDEPENDENT_CODE TRUE)
else()
  set_target_properties(pgexporter-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgexporter POSITION_INDEPENDENT_CODE FALSE)
endif()
target_link_libraries(pgexporter-bin pgexporter)

install(TARGETS pgexporter-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgexporter-cli
#
add_executable(pgexporter-cli-bin cli.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgexporter-cli-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgexporter-cli POSITION_INDEPENDENT_CODE TRUE)
else()
  set_target_properties(pgexporter-cli-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgexporter-cli POSITION_INDEPENDENT_CODE FALSE)
endif()
target_link_libraries(pgexporter-cli-bin pgexporter)

install(TARGETS pgexporter-cli-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgexporter-admin
#
add_executable(pgexporter-admin-bin admin.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgexporter-admin-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgexporter-admin POSITION_INDEPENDENT_CODE TRUE)
else()
  set_target_properties(pgexporter-admin-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgexporter-admin POSITION_INDEPENDENT_CODE FALSE)
endif()
target_link_libraries(pgexporter-admin-bin pgexporter)

install(TARGETS pgexporter-admin-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
