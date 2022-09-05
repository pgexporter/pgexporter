/*
 * Copyright (C) 2022 Red Hat
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

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <message.h>

#include <stdlib.h>

/** @struct
 * Defines the signal structure
 */
struct signal_info
{
   struct ev_signal signal; /**< The libev base type */
   int slot;                /**< The slot */
};

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
 * Read an int32
 * @param data Pointer to the data
 * @return The int32
 */
int32_t
pgexporter_read_int32(void* data);

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
 * Write an int32
 * @param data Pointer to the data
 * @param i The int32
 */
void
pgexporter_write_int32(void* data, int32_t i);

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
 * @return 0 if success, otherwise 1
 */
int
pgexporter_base64_encode(char* raw, int raw_length, char** encoded);

/**
 * BASE64 decode a string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @param raw The raw string
 * @param raw_length The length of the raw string
 * @return 0 if success, otherwise 1
 */
int
pgexporter_base64_decode(char* encoded, size_t encoded_length, char** raw, int* raw_length);

/**
 * Set process title
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

#ifdef DEBUG

/**
 * Generate a backtrace in the log
 * @return 0 if success, otherwise 1
 */
int
pgexporter_backtrace(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
