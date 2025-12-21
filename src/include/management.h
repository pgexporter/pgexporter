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

#ifndef PGEXPORTER_MANAGEMENT_H
#define PGEXPORTER_MANAGEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <json.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

/**
 * Management header
 */
#define MANAGEMENT_COMPRESSION_NONE  0
#define MANAGEMENT_COMPRESSION_GZIP  1
#define MANAGEMENT_COMPRESSION_ZSTD  2
#define MANAGEMENT_COMPRESSION_LZ4   3
#define MANAGEMENT_COMPRESSION_BZIP2 4

#define MANAGEMENT_ENCRYPTION_NONE   0
#define MANAGEMENT_ENCRYPTION_AES256 1
#define MANAGEMENT_ENCRYPTION_AES192 2
#define MANAGEMENT_ENCRYPTION_AES128 3

/**
 * Management commands
 */
#define MANAGEMENT_UNKNOWN             0
#define MANAGEMENT_TRANSFER_CONNECTION 1
#define MANAGEMENT_SHUTDOWN            2
#define MANAGEMENT_STATUS              3
#define MANAGEMENT_STATUS_DETAILS      4
#define MANAGEMENT_PING                5
#define MANAGEMENT_RESET               6
#define MANAGEMENT_RELOAD              7
#define MANAGEMENT_CONF_LS             8
#define MANAGEMENT_CONF_GET            9
#define MANAGEMENT_CONF_SET            10

#define MANAGEMENT_MASTER_KEY          11
#define MANAGEMENT_ADD_USER            12
#define MANAGEMENT_UPDATE_USER         13
#define MANAGEMENT_REMOVE_USER         14
#define MANAGEMENT_LIST_USERS          15

/**
 * Management categories
 */
#define MANAGEMENT_CATEGORY_HEADER   "Header"
#define MANAGEMENT_CATEGORY_REQUEST  "Request"
#define MANAGEMENT_CATEGORY_RESPONSE "Response"
#define MANAGEMENT_CATEGORY_OUTCOME  "Outcome"

/**
 * Management arguments
 */
#define MANAGEMENT_ARGUMENT_ACTIVE            "Active"
#define MANAGEMENT_ARGUMENT_CLIENT_VERSION    "ClientVersion"
#define MANAGEMENT_ARGUMENT_COMMAND           "Command"
#define MANAGEMENT_ARGUMENT_COMPRESSION       "Compression"
#define MANAGEMENT_ARGUMENT_CONFIG_KEY        "ConfigKey"
#define MANAGEMENT_ARGUMENT_CONFIG_VALUE      "ConfigValue"
#define MANAGEMENT_ARGUMENT_ENCRYPTION        "Encryption"
#define MANAGEMENT_ARGUMENT_ERROR             "Error"
#define MANAGEMENT_ARGUMENT_MAJOR_VERSION     "MajorVersion"
#define MANAGEMENT_ARGUMENT_MINOR_VERSION     "MinorVersion"
#define MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS "NumberOfServers"
#define MANAGEMENT_ARGUMENT_OUTPUT            "Output"
#define MANAGEMENT_ARGUMENT_RESTART           "Restart"
#define MANAGEMENT_ARGUMENT_SERVER            "Server"
#define MANAGEMENT_ARGUMENT_SERVERS           "Servers"
#define MANAGEMENT_ARGUMENT_SERVER_VERSION    "ServerVersion"
#define MANAGEMENT_ARGUMENT_STATUS            "Status"
#define MANAGEMENT_ARGUMENT_TIME              "Time"
#define MANAGEMENT_ARGUMENT_TIMESTAMP         "Timestamp"

/**
 * Management error
 */
#define MANAGEMENT_ERROR_BAD_PAYLOAD                        1
#define MANAGEMENT_ERROR_UNKNOWN_COMMAND                    2
#define MANAGEMENT_ERROR_ALLOCATION                         3

#define MANAGEMENT_ERROR_METRICS_NOFORK                     100
#define MANAGEMENT_ERROR_METRICS_NETWORK                    101

#define MANAGEMENT_ERROR_STATUS_NOFORK                      700
#define MANAGEMENT_ERROR_STATUS_NETWORK                     701

#define MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK              800
#define MANAGEMENT_ERROR_STATUS_DETAILS_NETWORK             801

#define MANAGEMENT_ERROR_BRIDGE_NOFORK                      900
#define MANAGEMENT_ERROR_BRIDGE_NETWORK                     901
#define MANAGEMENT_ERROR_BRIDGE_JSON_NOFORK                 902
#define MANAGEMENT_ERROR_BRIDGE_JSON_NETWORK                903

#define MANAGEMENT_ERROR_CONF_GET_NOFORK                    1000
#define MANAGEMENT_ERROR_CONF_GET_NETWORK                   1001
#define MANAGEMENT_ERROR_CONF_GET_ERROR                     1002

#define MANAGEMENT_ERROR_CONF_SET_NOFORK                    1100
#define MANAGEMENT_ERROR_CONF_SET_NOREQUEST                 1101
#define MANAGEMENT_ERROR_CONF_SET_NOCONFIG_KEY_OR_VALUE     1102
#define MANAGEMENT_ERROR_CONF_SET_NORESPONSE                1103
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY 1104
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_SERVER            1105
#define MANAGEMENT_ERROR_CONF_SET_NETWORK                   1106
#define MANAGEMENT_ERROR_CONF_SET_ERROR                     1107

/**
 * Output formats
 */
#define MANAGEMENT_OUTPUT_FORMAT_TEXT 0
#define MANAGEMENT_OUTPUT_FORMAT_JSON 1
#define MANAGEMENT_OUTPUT_FORMAT_RAW  2

/**
 * Create header for management command
 * @param command The command
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @param json The target json
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_create_header(int32_t command, uint8_t compression, uint8_t encryption, int32_t output_format, struct json** json);

/**
 * Create request for management command
 * @param json The target json
 * @param json The request json
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_create_request(struct json* json, struct json** request);

/**
 * Create success outcome for management command
 * @param json The target json
 * @param start_t The start time of the command
 * @param end_t The end time of the command
 * @param outcome The outcome json
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_create_outcome_success(struct json* json, time_t start_t, time_t end_t, struct json** outcome);

/**
 * Create success outcome for management command
 * @param json The target json
 * @param error The error code
 * @param outcome The outcome json
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_create_outcome_failure(struct json* json, int32_t error, struct json** outcome);

/**
 * Management operation: Shutdown
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Status
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Details
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Ping
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Reset
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_reset(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_conf_get(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a conf get request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param config_key The configuration key
 * @param config_value The configuration value
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Reload
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_request_reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an ok response
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param start_time The start time
 * @param end_time The end time
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The full payload
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_response_ok(SSL* ssl, int socket, time_t start_time, time_t end_time, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create an error response
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param error The error code
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The full payload
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_response_error(SSL* ssl, int socket, char* server, int32_t error, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create a response
 * @param json The JSON structure
 * @param server The server
 * @param response The response
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_create_response(struct json* json, int server, struct json** response);

/**
 * Read the management JSON
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The pointer to an integer that will store the compress method
 * @param json The JSON structure
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_read_json(SSL* ssl, int socket, uint8_t* compression, uint8_t* encryption, struct json** json);

/**
 * Write the management JSON
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param json The JSON structure
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_management_write_json(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, struct json* json);

#ifdef __cplusplus
}
#endif

#endif
