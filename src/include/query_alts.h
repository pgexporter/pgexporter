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

/**
 * Query Alternatives, or query_alts, are alternatives of the same query with
 * different minimum requirements of PostgreSQL version for them to work.
 *
 * eg. A query_alt may ask both column A and B from a server with version X,
 * just A if server has version Y (given column B is not supporter
 * by version Y).
 *
 * This allows sending that query to the server that has the highest support.
 *
 * To support fast insert as well as fetch (finding lower bound) of query to
 * send to server, query_alts is an AVL tree by design.
 */

/**
 * @brief Get the query alternative for a given server version
 * @param root Root of the AVL tree
 * @param server Server's major version
 * @return query_alts* NULL if not supported, otherwise a valid pointer
 */
struct query_alts*
pgexporter_get_query_alt(struct query_alts* root, int server);

/**
 * @brief Insert a node `new_node` into the AVL tree `root`
 * @param root Root of the AVL tree
 * @param new_node New node to add (Memory is free'd if node is not used)
 * @return query_alts* Returns root of AVL Tree. Can ignore.
 */
struct query_alts*
pgexporter_insert_node_avl (struct query_alts* root, struct query_alts** new_node);

/**
 * @brief Copy query alternative from `src` to `dst`
 * @param dst Destination
 * @param src Source
 */
void
pgexporter_copy_query_alts(struct query_alts** dst, struct query_alts* src);

/**
 * @brief Free the Query Alternatives of a configuration
 * @param configuration The configuration
 */
void
pgexporter_free_query_alts(struct configuration* config);

/**
 * @brief Free allocated memory for an AVL Tree Node for Query Alternatives given its root
 * @param root Root of the AVL tree
 */
void
pgexporter_free_node_avl(struct query_alts** root);
