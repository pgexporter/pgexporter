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
#ifndef PGEXPORTER_JSONCONFIGURATION_H
#define PGEXPORTER_JSONCONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <json.h>

/**
 * Read the custom configuration from JSON file provided by user.
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_json_metrics_configuration(void* shmem);

/**
 * Read and load JSON configuration from file pointer.
 * @param prometheus The array of data structures where the JSON configuration is loaded
 * @param prometheus_idx The index of the data structure in the array
 * @param number_of_metrics The number of metrics the configuration has. This value will be set by the function.
 * @param file File pointer
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_json_from_file_pointer(struct prometheus* prometheus, int prometheus_idx, int* number_of_metrics, FILE* file);

/**
 * Read and parse a single JSON file into the prometheus metrics structure
 * @param prometheus The prometheus metrics array
 * @param prometheus_idx Starting index in the prometheus array
 * @param filename Path to the JSON file
 * @param number_of_metrics Number of metrics read (output parameter)
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_json(struct prometheus* prometheus, int prometheus_idx, char* filename, int* number_of_metrics);

/**
 * Get all JSON files from a directory
 * @param base Base directory path
 * @param number_of_json_files Number of JSON files found (output parameter)
 * @param files Array of filenames (output parameter)
 * @return 0 upon success, otherwise 1
 */
int
get_json_files(char* base, int* number_of_json_files, char*** files);

/**
 * Check if given filename has a JSON extension
 * @param filename The filename to check
 * @return true if file has .json extension, false otherwise
 */
bool
is_json_file(char* filename);

#ifdef __cplusplus
}
#endif

#endif