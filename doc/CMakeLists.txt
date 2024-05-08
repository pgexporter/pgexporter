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

# pgexporter.1
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_SRC_FILE} ${PGEXPORTER_DST_FILE}
  OUTPUTS ${PGEXPORTER_DST_FILE}
)

# pgexporter-cli.1
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_CLI_SRC_FILE} ${PGEXPORTER_CLI_DST_FILE}
  OUTPUTS ${PGEXPORTER_CLI_DST_FILE}
)

# pgexporter-admin.1
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_ADMIN_SRC_FILE} ${PGEXPORTER_ADMIN_DST_FILE}
  OUTPUTS ${PGEXPORTER_ADMIN_DST_FILE}
)

# pgexporter.conf.5
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGEXPORTER_CONF_SRC_FILE} ${PGEXPORTER_CONF_DST_FILE}
  OUTPUTS ${PGEXPORTER_CONF_DST_FILE}
)

# man pages
add_custom_command(
  TARGET man
  DEPENDS ${PGEXPORTER_DST_FILE} ${PGEXPORTER_CLI_DST_FILE} ${PGEXPORTER_ADMIN_DST_FILE} ${PGEXPORTER_CONF_DST_FILE}
)

#
# Install configuration and documentation
#
install(FILES etc/pgexporter.conf DESTINATION share/doc/pgexporter/etc)

install(DIRECTORY . DESTINATION share/doc/pgexporter FILES_MATCHING PATTERN "*.md" PATTERN "etc" EXCLUDE PATTERN "man" EXCLUDE)

install(FILES ${PGEXPORTER_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGEXPORTER_CLI_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGEXPORTER_ADMIN_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGEXPORTER_CONF_DST_FILE} DESTINATION share/man/man5)

#
# Generate manual
#
if(generation)
  # Files
  FILE(GLOB PREDS "manual/0?-*.md")
  FILE(GLOB POSTDS "manual/9?-*.md")
  FILE(GLOB TDS "tutorial/??_*.md")
  FILE(GLOB UMDS_PRE "manual/user-0?-*.md")
  FILE(GLOB UMDS_POST "manual/user-1?-*.md")
  FILE(GLOB DMDS "manual/dev-??-*.md")

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
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgexporter-user-guide.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgexporter-user-guide.pdf --from markdown --template eisvogel --listings -N --toc ${PREDS} ${UMDS_PRE} ${TDS} ${UMDS_POST} ${POSTDS}
    DEPENDS ${UMDS}
    COMMENT "Generating User Guide PDF documentation"
  )
  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgexporter-dev-guide.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgexporter-dev-guide.pdf --from markdown --template eisvogel --listings -N --toc ${PREDS} ${DMDS} ${POSTDS}
    DEPENDS ${DMDS}
    COMMENT "Generating Developer Guide PDF documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgexporter-user-guide.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgexporter-user-guide.html -s -f markdown-smart -N --toc -t html5 ${PREDS} ${UMDS_PRE} ${TDS} ${UMDS_POST} ${POSTDS}
    DEPENDS ${UMDS}
    COMMENT "Generating User Guide HTML documentation"
  )
  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgexporter-dev-guide.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgexporter-dev-guide.html -s -f markdown-smart -N --toc -t html5 ${PREDS} ${DMDS} ${POSTDS}
    DEPENDS ${DMDS}
    COMMENT "Generating Developer Guide HTML documentation"
  )

  add_custom_target(
    all_docs ALL
    DEPENDS copy_images ${MANUAL_OUTPUT_DIR}/pgexporter-user-guide.pdf ${MANUAL_OUTPUT_DIR}/pgexporter-user-guide.html ${MANUAL_OUTPUT_DIR}/pgexporter-dev-guide.pdf ${MANUAL_OUTPUT_DIR}/pgexporter-dev-guide.html
  )
endif()
