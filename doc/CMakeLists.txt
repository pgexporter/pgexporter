if (RST2MAN_FOUND)
  # man target
  add_custom_target(man ALL)

  # man page definitions
  set(PGEXPORTER_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgexporter.1.rst")
  set(PGEXPORTER_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgexporter.1")
  set(PGEXPORTER_CLI_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgexporter-cli.1.rst")
  set(PGEXPORTER_CLI_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgexporter-cli.1")
  set(PGEXPORTER_ADMIN_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgexporter-admin.1.rst")
  set(PGEXPORTER_ADMIN_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgexporter-admin.1")
  set(PGEXPORTER_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgexporter.conf.5.rst")
  set(PGEXPORTER_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgexporter.conf.5")

  # Generate man pages for pgexporter.1
  add_custom_command(
    OUTPUT ${PGEXPORTER_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_SRC_FILE} ${PGEXPORTER_DST_FILE}
    DEPENDS ${PGEXPORTER_SRC_FILE}
    COMMENT "Generating man page: pgexporter.1"
  )

  # Generate man pages for pgexporter-cli.1
  add_custom_command(
    OUTPUT ${PGEXPORTER_CLI_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_CLI_SRC_FILE} ${PGEXPORTER_CLI_DST_FILE}
    DEPENDS ${PGEXPORTER_CLI_SRC_FILE}
    COMMENT "Generating man page: pgexporter-cli.1"
  )

  # Generate man pages for pgexporter-admin.1
  add_custom_command(
    OUTPUT ${PGEXPORTER_ADMIN_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_ADMIN_SRC_FILE} ${PGEXPORTER_ADMIN_DST_FILE}
    DEPENDS ${PGEXPORTER_ADMIN_SRC_FILE}
    COMMENT "Generating man page: pgexporter-admin.1"
  )

  # Generate man pages for pgexporter.conf.5
  add_custom_command(
    OUTPUT ${PGEXPORTER_CONF_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_CONF_SRC_FILE} ${PGEXPORTER_CONF_DST_FILE}
    DEPENDS ${PGEXPORTER_CONF_SRC_FILE}
    COMMENT "Generating man page: pgexporter.conf.5"
  )

  # Define the target to group all man pages
  add_custom_target(manpages ALL
    DEPENDS ${PGEXPORTER_DST_FILE} ${PGEXPORTER_CLI_DST_FILE} ${PGEXPORTER_ADMIN_DST_FILE} ${PGEXPORTER_CONF_DST_FILE}
  )

  # Add the 'man' target as a dependency to 'manpages'
  add_dependencies(man manpages)

  #
  # Install configuration and documentation
  #
  install(FILES etc/pgexporter.conf DESTINATION share/doc/pgexporter/etc)

  install(DIRECTORY . DESTINATION share/doc/pgexporter FILES_MATCHING PATTERN "*.md" PATTERN "etc" EXCLUDE PATTERN "man" EXCLUDE PATTERN "manual" EXCLUDE)

  install(FILES ${PGEXPORTER_DST_FILE} DESTINATION share/man/man1)
  install(FILES ${PGEXPORTER_CLI_DST_FILE} DESTINATION share/man/man1)
  install(FILES ${PGEXPORTER_ADMIN_DST_FILE} DESTINATION share/man/man1)
  install(FILES ${PGEXPORTER_CONF_DST_FILE} DESTINATION share/man/man5)
endif()

#
# Generate manual
#
if(DOCS)
  # Files
  FILE(GLOB EN "manual/en/??-*.md")

  # Manuals definitions
  set(MANUAL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/doc")
  get_filename_component(PARENT_DIR ${CMAKE_BINARY_DIR} DIRECTORY)
  set(IMAGE_DIR "${PARENT_DIR}/doc/images")

  add_custom_target(
    copy_images
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${IMAGE_DIR} ${CMAKE_BINARY_DIR}/images
    COMMENT "Copy images"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgexporter-en.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgexporter-en.pdf --from markdown --template eisvogel --listings -N --toc ${EN}
    DEPENDS ${UMDS}
    COMMENT "Generating English PDF documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgexporter-en.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgexporter-en.html -s -f markdown-smart -N --toc -t html5 ${EN}
    DEPENDS ${UMDS}
    COMMENT "Generating English Guide HTML documentation"
  )

  add_custom_target(
    all_docs ALL
    DEPENDS copy_images ${MANUAL_OUTPUT_DIR}/pgexporter-en.pdf ${MANUAL_OUTPUT_DIR}/pgexporter-en.html

  )

  install(FILES ${MANUAL_OUTPUT_DIR}/pgexporter-en.pdf DESTINATION share/doc/pgexporter/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgexporter-en.html DESTINATION share/doc/pgexporter/manual)

  #
  # Generate API docs
  #
  if (DOXYGEN_FOUND)
    add_custom_target(api ALL)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_MAN NO)
    set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
    set(DOXYGEN_EXCLUDE_PATTERNS "internal.h")
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.doxygen)
    doxygen_add_docs(doxygen ${PROJECT_SOURCE_DIR}/src/include/)
    if (${DOXYGEN_VERSION} STREQUAL "1.10.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.11.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.12.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.13.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.13.1" OR
        ${DOXYGEN_VERSION} STREQUAL "1.13.2" OR
        ${DOXYGEN_VERSION} STREQUAL "1.14.0")
      add_custom_command(
        TARGET api
        POST_BUILD
        COMMAND ${DOXYGEN_EXECUTABLE} -q ${DOXYFILE_OUT}
        COMMENT "Generating API documentation"
      )
    else()
      add_custom_command(
        TARGET api
        POST_BUILD
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        COMMENT "Generating API documentation"
      )
    endif()
  endif()
endif()
