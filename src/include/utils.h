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

#ifndef PGEXPORTER_UTILS_H
#define PGEXPORTER_UTILS_H

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <message.h>

#include <stdlib.h>

/** @struct signal_info
 * Defines the signal structure
 */
struct signal_info
{
   struct ev_signal signal; /**< The libev base type */
   int slot;                /**< The slot */
};

/** @struct pgexporter_command
 * Defines pgexporter commands.
 * The necessary fields are marked with an ">".
 *
 * Fields:
 * > command: The primary name of the command.
 * > subcommand: The subcommand name. If there is no subcommand, it should be filled with an empty literal string.
 * > accepted_argument_count: An array defining all the number of arguments this command accepts.
 *    Each entry represents a valid count of arguments, allowing the command to support overloads.
 * - default_argument: A default value for the command argument, used when no explicit argument is provided.
 * - log_message: A template string for logging command execution, which can include placeholders for dynamic values.
 * > action: A value indicating the specific action.
 * - mode: A value specifying the mode of operation or context in which the command applies.
 * > deprecated: A flag indicating whether this command is deprecated.
 * - deprecated_by: A string naming the command that replaces the deprecated command.
 *
 * This struct is key to extending and maintaining the command processing functionality in pgexporter,
 * allowing for clear definition and handling of all supported commands.
 */
struct pgexporter_command
{
   const char* command;                            /**< The command */
   const char* subcommand;                         /**< The subcommand if there is one */
   const int accepted_argument_count[MISC_LENGTH]; /**< The argument count */

   const int action;                               /**< The specific action */
   const char* default_argument;                   /**< The default argument */
   const char* log_message;                        /**< The log message used */

   /* Deprecation information */
   bool deprecated;                                /**< Is the command deprecated */
   unsigned int deprecated_since_major;            /**< Deprecated since major version */
   unsigned int deprecated_since_minor;            /**< Deprecated since minor version */
   const char* deprecated_by;                      /**< Deprecated by this command */
};

/** @struct pgexporter_parsed_command
 * Holds parsed command data.
 *
 * Fields:
 * - cmd: A pointer to the command struct that was parsed.
 * - args: An array of pointers to the parsed arguments of the command (points to argv).
 */
struct pgexporter_parsed_command
{
   const struct pgexporter_command* cmd; /**< The command */
   char* args[MISC_LENGTH];            /**< The arguments */
};

/**
 * Utility function to parse the command line
 * and search for a command.
 *
 * The function tries to be smart, in helping to find out
 * a command with the possible subcommand.
 *
 * @param argc the command line counter
 * @param argv the command line as provided to the application
 * @param offset the position at which the next token out of `argv`
 * has to be read. This is usually the `optind` set by getopt_long().
 * @param parsed an `struct pgexporter_parsed_command` to hold the parsed
 * data. It is modified inside the function to be accessed outside.
 * @param command_table array containing one `struct pgexporter_command` for
 * every possible command.
 * @param command_count number of commands in `command_table`.
 * @return true if the parsing of the command line was succesful, false
 * otherwise
 *
 */
bool
parse_command(int argc,
              char** argv,
              int offset,
              struct pgexporter_parsed_command* parsed,
              const struct pgexporter_command command_table[],
              size_t command_count);

/**
 * Get the request identifier
 * @param msg The message
 * @return The identifier
 */
int32_t
pgexporter_get_request(struct message* msg);

/**
 * Extract the user name and database from a message
 * @param msg The message
 * @param username The resulting user name
 * @param database The resulting database
 * @param appname The resulting application_name
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_extract_username_database(struct message* msg, char** username, char** database, char** appname);

/**
 * Extract a message from a message
 * @param type The message type to be extracted
 * @param msg The message
 * @param extracted The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_extract_message(char type, struct message* msg, struct message** extracted);

/**
 * Extract a message based on an offset
 * @param offset The offset
 * @param data The data segment
 * @param extracted The resulting message
 * @return The next offset
 */
size_t
pgexporter_extract_message_offset(size_t offset, void* data, struct message** extracted);

/**
 * Extract a message based on a type
 * @param type The type
 * @param data The data segment
 * @param data_size The data size
 * @param extracted The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted);

/**
 * Has a message
 * @param type The message type to be extracted
 * @param data The data
 * @param data_size The data size
 * @return true if found, otherwise false
 */
bool
pgexporter_has_message(char type, void* data, size_t data_size);

/**
 * Read a byte
 * @param data Pointer to the data
 * @return The byte
 */
signed char
pgexporter_read_byte(void* data);

/**
 * Read an uint8
 * @param data Pointer to the data
 * @return The uint8
 */
uint8_t
pgexporter_read_uint8(void* data);

/**
 * Read an int16
 * @param data Pointer to the data
 * @return The int16
 */
int16_t
pgexporter_read_int16(void* data);

/**
 * Read an int32
 * @param data Pointer to the data
 * @return The int32
 */
int32_t
pgexporter_read_int32(void* data);

/**
 * Read an uint32
 * @param data Pointer to the data
 * @return The uint32
 */
uint32_t
pgexporter_read_uint32(void* data);

/**
 * Read an int64
 * @param data Pointer to the data
 * @return The int64
 */
int64_t
pgexporter_read_int64(void* data);

/**
 * Write a byte
 * @param data Pointer to the data
 * @param b The byte
 */
void
pgexporter_write_byte(void* data, signed char b);

/**
 * Write a uint8
 * @param data Pointer to the data
 * @param b The uint8
 */
void
pgexporter_write_uint8(void* data, uint8_t b);

/**
 * Write an int32
 * @param data Pointer to the data
 * @param i The int32
 */
void
pgexporter_write_int32(void* data, int32_t i);

/**
 * Write an uint32
 * @param data Pointer to the data
 * @param i The uint32
 */
void
pgexporter_write_uint32(void* data, uint32_t i);

/**
 * Write an int64
 * @param data Pointer to the data
 * @param i The int64
 */
void
pgexporter_write_int64(void* data, int64_t i);

/**
 * Read a string
 * @param data Pointer to the data
 * @return The string
 */
char*
pgexporter_read_string(void* data);

/**
 * Write a string
 * @param data Pointer to the data
 * @param s The string
 */
void
pgexporter_write_string(void* data, char* s);

/**
 * Is the machine big endian ?
 * @return True if big, otherwise false for little
 */
bool
pgexporter_bigendian(void);

/**
 * Swap
 * @param i The value
 * @return The swapped value
 */
unsigned int
pgexporter_swap(unsigned int i);

/**
 * Print the available libev engines
 */
void
pgexporter_libev_engines(void);

/**
 * Get the constant for a libev engine
 * @param engine The name of the engine
 * @return The constant
 */
unsigned int
pgexporter_libev(char* engine);

/**
 * Get the name for a libev engine
 * @param val The constant
 * @return The name
 */
char*
pgexporter_libev_engine(unsigned int val);

/**
 * Get the home directory
 * @return The directory
 */
char*
pgexporter_get_home_directory(void);

/**
 * Get the user name
 * @return The user name
 */
char*
pgexporter_get_user_name(void);

/**
 * Get a password from stdin
 * @return The password
 */
char*
pgexporter_get_password(void);

/**
 * BASE64 encode a string
 * @param raw The string
 * @param raw_length The length of the raw string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @return 0 if success, otherwise 1
 */
int
pgexporter_base64_encode(void* raw, size_t raw_length, char** encoded, size_t* encoded_length);

/**
 * BASE64 decode a string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @param raw The raw string
 * @param raw_length The length of the raw string
 * @return 0 if success, otherwise 1
 */
int
pgexporter_base64_decode(char* encoded, size_t encoded_length, void** raw, size_t* raw_length);

/**
 * Set process title.
 *
 * The function will autonomously check the update policy set
 * via the configuration option `update_process_title` and
 * will do nothing if the setting is `never`.
 * In the case the policy is set to `strict`, the process title
 * will not overflow the initial command line length (i.e., strlen(argv[*]))
 * otherwise it will do its best to set the title to the desired string.
 *
 * The policies `strict` and `minimal` will be honored only on Linux platforms
 * where a native call to set the process title is not available.
 *
 *
 * The resulting process title will be set to either `s1` or `s1/s2` if there
 * both strings and the length is allowed by the policy.
 *
 * @param argc The number of arguments
 * @param argv The argv pointer
 * @param s1 The first string
 * @param s2 The second string
 */
void
pgexporter_set_proc_title(int argc, char** argv, char* s1, char* s2);

/**
 * Create directories
 * @param dir The directory
 * @return 0 on success, otherwise 1
 */
int
pgexporter_mkdir(char* dir);

/**
 * Append multiple strings
 * @param orig The original string
 * @param n_str The number of strings that will be appended
 * @return The resulting string
 */
char*
pgexporter_vappend(char* orig, unsigned int n_str, ...);

/**
 * Append a string
 * @param orig The original string
 * @param s The string
 * @return The resulting string
 */
char*
pgexporter_append(char* orig, char* s);

/**
 * Append an integer
 * @param orig The original string
 * @param i The integer
 * @return The resulting string
 */
char*
pgexporter_append_int(char* orig, int i);

/**
 * Append a long
 * @param orig The original string
 * @param l The long
 * @return The resulting string
 */
char*
pgexporter_append_ulong(char* orig, unsigned long l);

/**
 * Append a bool
 * @param orig The original string
 * @param b The bool
 * @return The resulting string
 */
char*
pgexporter_append_bool(char* orig, bool b);

/**
 * Append a char
 * @param orig The original string
 * @param s The string
 * @return The resulting string
 */
char*
pgexporter_append_char(char* orig, char c);

/**
 * Indent a string
 * @param str The string
 * @param tag [Optional] The tag, which will be applied after indentation if not NULL
 * @param indent The indent
 * @return The indented string
 */
char*
pgexporter_indent(char* str, char* tag, int indent);

/**
 * Compare two strings
 * @param str1 The first string
 * @param str2 The second string
 * @return true if the strings are the same, otherwise false
 */
bool
pgexporter_compare_string(const char* str1, const char* str2);

/**
 * Calculate the directory size
 * @param directory The directory
 * @return The size in bytes
 */
unsigned long
pgexporter_directory_size(char* directory);

/**
 * Get directories
 * @param base The base directory
 * @param number_of_directories The number of directories
 * @param dirs The directories
 * @return The result
 */
int
pgexporter_get_directories(char* base, int* number_of_directories, char*** dirs);

/**
 * Remove a directory
 * @param path The directory
 * @return The result
 */
int
pgexporter_delete_directory(char* path);

/**
 * Get files
 * @param base The base directory
 * @param number_of_files The number of files
 * @param files The files
 * @return The result
 */
int
pgexporter_get_files(char* base, int* number_of_files, char*** files);

/**
 * Get the timestramp difference as a string
 * @param start_time The start time
 * @param end_time The end time
 * @param seconds The number of seconds
 * @return The timestamp string
 */
char*
pgexporter_get_timestamp_string(time_t start_time, time_t end_time, int32_t* seconds);

/**
 * Remove a file
 * @param file The file
 * @return The result
 */
int
pgexporter_delete_file(char* file);

/**
 * Copy a directory
 * @param from The from directory
 * @param to The to directory
 * @return The result
 */
int
pgexporter_copy_directory(char* from, char* to);

/**
 * Copy a file
 * @param from The from file
 * @param to The to file
 * @return The result
 */
int
pgexporter_copy_file(char* from, char* to);

/**
 * Move a file
 * @param from The from file
 * @param to The to file
 * @return The result
 */
int
pgexporter_move_file(char* from, char* to);

/**
 * Get basename of a file
 * @param s The string
 * @param basename The basename of the file
 * @return The result
 */
int
pgexporter_basename_file(char* s, char** basename);

/**
 * File/directory exists
 * @param f The file/directory
 * @return The result
 */
bool
pgexporter_exists(char* f);

/**
 * Check for file
 * @param file The file
 * @return The result
 */
bool
pgexporter_is_file(char* file);

/**
 * Check for directory
 * @param file the file
 * @return The result
 */
bool
pgexporter_is_directory(char* file);

/**
 * Compare files
 * @param f1 The first file path
 * @param f2 The second file path
 * @return The result
 */
bool
pgexporter_compare_files(char* f1, char* f2);

/**
 * Symlink files
 * @param from The from file
 * @param to The to file
 * @return The result
 */
int
pgexporter_symlink_file(char* from, char* to);

/**
 * Check for symlink
 * @param file The file
 * @return The result
 */
bool
pgexporter_is_symlink(char* file);

/**
 * Get symlink
 * @param symlink The symlink
 * @return The result
 */
char*
pgexporter_get_symlink(char* symlink);

/**
 * Copy WAL files
 * @param from The from directory
 * @param to The to directory
 * @param start The start file
 * @return The result
 */
int
pgexporter_copy_wal_files(char* from, char* to, char* start);

/**
 * Get the number of WAL files
 * @param directory The directory
 * @param from The from WAL file
 * @param to The to WAL file; can be NULL
 * @return The result
 */
int
pgexporter_number_of_wal_files(char* directory, char* from, char* to);

/**
 * Get the free space for a path
 * @param path The path
 * @return The result
 */
unsigned long
pgexporter_free_space(char* path);

/**
 * Get the total space for a path
 * @param path The path
 * @return The result
 */
unsigned long
pgexporter_total_space(char* path);

/**
 * Does a string start with another string
 * @param str The string
 * @param prefix The prefix
 * @return The result
 */
bool
pgexporter_starts_with(char* str, char* prefix);

/**
 * Does a string end with another string
 * @param str The string
 * @param suffix The suffix
 * @return The result
 */
bool
pgexporter_ends_with(char* str, char* suffix);

/**
 * Remove whitespace from a string
 * @param orig The original string
 * @return The resulting string
 */
char*
pgexporter_remove_whitespace(char* orig);

/**
 * Remove the prefix from orig
 * @param orig The original string
 * @param prefix The prefix string
 * @return The resulting string
 */
char*
pgexporter_remove_prefix(char* orig, char* prefix);

/**
 * Remove the suffix from orig, it makes a copy of orig if the suffix doesn't
 * exist
 * @param orig The original string
 * @param suffix The suffix string
 * @return The resulting string
 */
char*
pgexporter_remove_suffix(char* orig, char* suffix);

/**
 * Sort a string array
 * @param size The size of the array
 * @param array The array
 * @return The result
 */
void
pgexporter_sort(size_t size, char** array);

/**
 * Bytes to string
 * @param bytes The number of bytes
 * @return The result
 */
char*
pgexporter_bytes_to_string(uint64_t bytes);

/**
 * Read version number
 * @param directory The base directory
 * @param version The version
 * @return The result
 */
int
pgexporter_read_version(char* directory, char** version);

/**
 * Read the first WAL file name
 * @param directory The base directory
 * @param wal The WAL
 * @return The result
 */
int
pgexporter_read_wal(char* directory, char** wal);

/**
 * Escape a string
 * @param str The original string
 * @return The escaped string
 */
char*
pgexporter_escape_string(char* str);

/**
 * Is the string a number ?
 * @param str The string
 * @param base The base (10 or 16)
 * @return True if number, otherwise false
 */
bool
pgexporter_is_number(char* str, int base);

/**
 * Provide the application version number as a unique value composed of the
 * three specified parts. For example, when invoked with (1,5,0) it returns
 * 10500. Every part of the number must be between 0 and 99, and the function
 * applies a restriction on the values. For example passing 1 or 101 as one of
 * the part will produce the same result.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns a number made by (patch + minor * 100 + major * 10000 )
 */
unsigned int
pgexporter_version_as_number(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Provides the current version number of the application.
 * It relies on `pgexporter_version_as_number` and invokes it with the
 * predefined constants.
 *
 * @returns the current version number
 */
unsigned int
pgexporter_version_number(void);

/**
 * Checks if the currently running version number is
 * greater or equal than the specied one.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns true if the current version is greater or equal to the specified one
 */
bool
pgexporter_version_ge(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Resolve path.
 * The function will resolve the path by expanding environment
 * variables (e.g., $HOME) in subpaths that are either surrounded
 * by double quotes (") or not surrounded by any quotes.
 * @param orig_path The original path
 * @param new_path Reference to the resolved path
 * @return 0 if success, otherwise 1
 */
int
pgexporter_resolve_path(char* orig_path, char** new_path);

#ifdef DEBUG

/**
 * Generate a backtrace in the log
 * @return 0 if success, otherwise 1
 */
int
pgexporter_backtrace(void);

#endif

/**
 * Get the OS name and kernel version.
 *
 * @param os            Pointer to store the OS name (e.g., "Linux", "FreeBSD",
 * "OpenBSD"). Memory will be allocated internally and should be freed by the
 * caller.
 * @param kernel_major  Pointer to store the kernel major version.
 * @param kernel_minor  Pointer to store the kernel minor version.
 * @param kernel_patch  Pointer to store the kernel patch version.
 * @return              0 on success, 1 on error.
 */
int
pgexporter_os_kernel_version(char** os, int* kernel_major, int* kernel_minor, int* kernel_patch);

#ifdef __cplusplus
}
#endif

#endif
