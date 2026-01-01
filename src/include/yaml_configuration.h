/*
 * Copyright (C) 2026 The pgexporter community
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

#ifndef PGEXPORTER_YAMLCONFIGURATION_H
#define PGEXPORTER_YAMLCONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

#include <yaml.h>

/**
 * Read the custome configuration from YAML file provided by user.
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_metrics_configuration(void* shmem);

/**
 * @brief Read the internal YAML configuration `INTERNAL_YAML` and load the
 * metrics in the config.
 *
 * @param config The configuration where it will be loaded
 * @param start true if it will reset the `number_of_metrics` in `config` to 0 and start counting from there
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_internal_yaml_metrics(struct configuration* config, bool start);

/**
 * Read and load YAML configuration from file pointer.
 * @param prometheus The array of data structures where the YAML configuration is loaded
 * @param prometheus_idx The index of the data structure in the array
 * @param number_of_metrics The number of metrics the configuration has. This value will be set by the function.
 * @param file File pointer
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_yaml_from_file_pointer(struct prometheus* prometheus, int prometheus_idx, int* number_of_metrics, FILE* file);

/**
 * Find and load a specific extension's YAML file
 * @param extensions_path The base extensions directory path
 * @param extension_name The name of the extension to find
 * @param config The configuration to load into
 * @return 0 on success, 1 on error (file not found or parse error)
 */
int
pgexporter_load_single_extension_yaml(char* extensions_path, char* extension_name, struct configuration* config);

#ifdef __cplusplus
}
#endif

#endif
