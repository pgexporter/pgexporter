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

#include <pgexporter.h>
#include <queries.h>

/**
 * Extension Query Alternatives, or ext_query_alts, are alternatives of the same
 * extension query with different minimum requirements of extension version for
 * them to work.
 *
 * eg. An ext_query_alt may ask both column A and B from a server with extension
 * version X, just A if extension has version Y (given column B is not supported
 * by version Y).
 *
 * This allows sending that query to the server that has the highest extension
 * version support.
 *
 * To support insert as well as fetch (finding lower bound) of query to send
 * to server, ext_query_alts is an AVL tree by design using semantic versioning.
 */

/**
 * @struct ext_query_alts
 * A node in an AVL tree for extension-specific query alternatives.
 * Contains semantic version information and inherits common query fields.
 */
struct ext_query_alts
{
   struct version ext_version;  /**< Extension semantic version */
   struct query_alts_base node; /**< Inherit base fields */

   /* AVL Tree */
   unsigned int height;          /**< Node's height, 1 if leaf, 0 if NULL */
   struct ext_query_alts* left;  /**< Left child node */
   struct ext_query_alts* right; /**< Right child node */
} __attribute__((aligned(64)));

/**
 * @brief Get the extension query alternative for a given extension version
 * @param root Root of the AVL tree
 * @param ext_version Extension's semantic version
 * @return ext_query_alts* NULL if not supported, otherwise a valid pointer
 */
struct ext_query_alts*
pgexporter_get_extension_query_alt(struct ext_query_alts* root, struct version* ext_version);

/**
 * @brief Insert a node `new_node` into the extension AVL tree `root`
 * @param root Root of the AVL tree
 * @param new_node New node to add (Memory is free'd if node is not used)
 * @return ext_query_alts* Returns root of AVL Tree. Can ignore.
 */
struct ext_query_alts*
pgexporter_insert_extension_node_avl(struct ext_query_alts* root, struct ext_query_alts** new_node);

/**
 * @brief Copy extension query alternative from `src` to `dst`
 * @param src Source
 * @param dst Destination
 */
void
pgexporter_copy_extension_query_alts(struct ext_query_alts* src, struct ext_query_alts** dst);

/**
 * @brief Free the Extension Query Alternatives for all extension metrics
 * @param config The configuration struct
 */
void
pgexporter_free_extension_query_alts(struct configuration* config);

/**
 * @brief Free allocated memory for an extension AVL Tree Node given its root
 * @param root Root of the AVL tree
 */
void
pgexporter_free_extension_node_avl(struct ext_query_alts** root);