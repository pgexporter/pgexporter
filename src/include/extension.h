/*
 * Copyright (C) 2025 The pgexporter community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGEXPORTER_EXTENSION_H
#define PGEXPORTER_EXTENSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

/**
 * Setup extensions path based on build type (development vs installation)
 * @param config The configuration struct to update
 * @param argv0 The raw argv[0] from main
 * @param bin_path Output parameter for the resolved binary path
 * @return 0 on success, 1 on error
 */
int
pgexporter_setup_extensions_path(struct configuration* config, const char* argv0, char** bin_path);

/**
 * Parse a semantic version string (e.g., "1.8.2" or "2.1") into version struct
 * @param version_str The version string to parse
 * @param version Output version struct
 * @return 0 on success, 1 on error
 */
int
pgexporter_parse_extension_version(char* version_str, struct version* version);

/**
 * Compare two semantic versions
 * @param v1 First version
 * @param v2 Second version
 * @return VERSION_GREATER if v1 > v2
 *         VERSION_EQUAL if v1 == v2
 *         VERSION_LESS if v1 < v2
 *         VERSION_ERROR on error
 */
int
pgexporter_compare_extension_versions(struct version* v1, struct version* v2);

/**
 * Convert a version struct to a string representation
 * @param version The version struct to convert
 * @param buffer Output buffer to write the version string
 * @param buffer_size Size of the output buffer
 * @return 0 on success, 1 on error
 */
int
pgexporter_version_to_string(struct version* version, char* buffer, size_t buffer_size);

/**
 * Load extension YAML files for all servers based on detected extensions
 * @param config The configuration struct
 * @return 0 on success, 1 on error
 */
int
pgexporter_load_extension_yamls(struct configuration* config);

/**
 * Find and load a specific extension's YAML file
 * @param extensions_path The base extensions directory path
 * @param extension_name The name of the extension to find
 * @param config The configuration to load into
 * @return 0 on success, 1 on error (file not found or parse error)
 */
int
pgexporter_load_single_extension_yaml(char* extensions_path, char* extension_name, struct configuration* config);

/**
 * Determine if an extension should be enabled based on configuration
 * @param config The configuration struct
 * @param server The server index
 * @param extension_name The extension name to check
 * @return true if extension should be enabled, false otherwise
 */
bool
pgexporter_extension_is_enabled(struct configuration* config, int server, char* extension_name);

#ifdef __cplusplus
}
#endif

#endif
