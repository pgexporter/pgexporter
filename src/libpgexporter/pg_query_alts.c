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

#include <pgexporter.h>
#include <pg_query_alts.h>
#include <shmem.h>

// Get height of AVL Tree Node
static int height(struct pg_query_alts* A);

// Get balance of AVL Tree Node
static int get_node_balance(struct pg_query_alts* A);

// Right rotate a node, and left child, such that left child is new root, and root is new right child
static struct pg_query_alts* node_right_rotate(struct pg_query_alts* root);

// Right rotate a node, and right child, such that right child is new root, and root is new left child
static struct pg_query_alts* node_left_rotate(struct pg_query_alts* root);

void
pgexporter_copy_pg_query_alts(struct pg_query_alts** dst, struct pg_query_alts* src)
{
   if (!src)
   {
      return;
   }

   void* new_query_alt = NULL;

   pgexporter_create_shared_memory(sizeof(struct pg_query_alts), HUGEPAGE_OFF, &new_query_alt);
   *dst = (struct pg_query_alts*)new_query_alt;

   (*dst)->height = src->height;
   (*dst)->node.is_histogram = src->node.is_histogram;
   (*dst)->node.n_columns = src->node.n_columns;
   (*dst)->pg_version = src->pg_version;

   memcpy((*dst)->node.query, src->node.query, MAX_QUERY_LENGTH);
   memcpy((*dst)->node.columns, src->node.columns, MAX_NUMBER_OF_COLUMNS * sizeof(struct column));

   pgexporter_copy_pg_query_alts(&(*dst)->left, src->left);
   pgexporter_copy_pg_query_alts(&(*dst)->right, src->right);
}

static int
height(struct pg_query_alts* A)
{
   return A ? A->height : 0;
}

static int
get_node_balance(struct pg_query_alts* A)
{
   return A ? height(A->left) - height(A->right) : 0;
}

static struct pg_query_alts*
node_right_rotate(struct pg_query_alts* root)
{
   if (!root || !root->left)
   {
      return root;
   }

   struct pg_query_alts *A, *B;

   A = root, B = root->left;

   A->left = B->right;
   B->right = A;

   A->height = MAX(height(A->left), height(A->right)) + 1;
   B->height = MAX(height(B->left), height(B->right)) + 1;

   return B;
}

static struct pg_query_alts*
node_left_rotate(struct pg_query_alts* root)
{
   if (!root || !root->right)
   {
      return root;
   }

   struct pg_query_alts *A, *B;

   A = root, B = root->right;

   A->right = B->left;
   B->left = A;

   A->height = MAX(height(A->left), height(A->right)) + 1;
   B->height = MAX(height(B->left), height(B->right)) + 1;

   return B;
}

struct pg_query_alts*
pgexporter_insert_pg_node_avl(struct pg_query_alts* root, struct pg_query_alts** new_node)
{
   if (!root)
   {
      return (*new_node);
   }
   else if (root->pg_version == (*new_node)->pg_version)
   {
      // Free New Node, as no need to insert it
      pgexporter_free_pg_node_avl(new_node);
      return root;
   }
   else if (root->pg_version > (*new_node)->pg_version)
   {
      root->left = pgexporter_insert_pg_node_avl(root->left, new_node);
   }
   else
   {
      root->right = pgexporter_insert_pg_node_avl(root->right, new_node);
   }

   root->height = MAX(height(root->left), height(root->right)) + 1;

   /* AVL Rotations */
   if (get_node_balance(root) > 1)
   {
      if (get_node_balance(root->left) == -1) // L
      {
         root->left = node_left_rotate(root->left);
      }

      return node_right_rotate(root);
   }
   else if (get_node_balance(root) < -1)
   {
      if (get_node_balance(root->right) == 1) //R
      {
         root->right = node_right_rotate(root->right);
      }

      if (get_node_balance(root) != 0)
      {
         return node_left_rotate(root);
      }
   }

   return root;
}

struct pg_query_alts*
pgexporter_get_pg_query_alt(struct pg_query_alts* root, int server)
{
   struct configuration* config = NULL;
   struct pg_query_alts* temp = root;
   struct pg_query_alts* last = NULL;
   int ver;

   config = (struct configuration*)shmem;
   ver = config->servers[server].version;

   // Traversing the AVL tree
   while (temp)
   {
      if (temp->pg_version <= ver &&
          (!last || temp->pg_version > last->pg_version))
      {
         last = temp;
      }

      if (temp->pg_version > ver)
      {
         temp = temp->left;
      }
      else
      {
         temp = temp->right;
      }
   }

   if (!last || last->pg_version > ver)
   {
      return NULL;
   }
   else
   {
      return last;
   }
}

void
pgexporter_free_pg_query_alts(struct configuration* config)
{
   for (int i = 0; i < config->number_of_metrics; i++)
   {
      pgexporter_free_pg_node_avl(&config->prometheus[i].pg_root);
   }
}

void
pgexporter_free_pg_node_avl(struct pg_query_alts** root)
{
   if (!root || !(*root))
   {
      return;
   }

   pgexporter_free_pg_node_avl(&(*root)->left);
   pgexporter_free_pg_node_avl(&(*root)->right);

   pgexporter_destroy_shared_memory(&root, sizeof(struct pg_query_alts*));
}
