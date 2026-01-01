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
#include <extension.h>
#include <ext_query_alts.h>
#include <shmem.h>

// Get height of extension AVL Tree Node
static int ext_height(struct ext_query_alts* node);

// Get balance of extension AVL Tree Node
static int ext_get_node_balance(struct ext_query_alts* node);

// Right rotate extension node
static struct ext_query_alts* ext_node_right_rotate(struct ext_query_alts* root);

// Left rotate extension node
static struct ext_query_alts* ext_node_left_rotate(struct ext_query_alts* root);

void
pgexporter_copy_extension_query_alts(struct ext_query_alts* src, struct ext_query_alts** dst)
{
   void* new_query_alt = NULL;
   struct ext_query_alts* local_dst = NULL;

   if (!src)
   {
      return;
   }

   pgexporter_create_shared_memory(sizeof(struct ext_query_alts), HUGEPAGE_OFF, &new_query_alt);
   local_dst = (struct ext_query_alts*)new_query_alt;

   local_dst->height = src->height;
   local_dst->ext_version = src->ext_version;
   local_dst->node.is_histogram = src->node.is_histogram;
   local_dst->node.n_columns = src->node.n_columns;

   memcpy(local_dst->node.query, src->node.query, MAX_QUERY_LENGTH);
   memcpy(local_dst->node.columns, src->node.columns, MAX_NUMBER_OF_COLUMNS * sizeof(struct column));

   pgexporter_copy_extension_query_alts(src->left, &local_dst->left);
   pgexporter_copy_extension_query_alts(src->right, &local_dst->right);

   *dst = local_dst;
}

static int
ext_height(struct ext_query_alts* node)
{
   return node ? node->height : 0;
}

static int
ext_get_node_balance(struct ext_query_alts* node)
{
   return node ? ext_height(node->left) - ext_height(node->right) : 0;
}

static struct ext_query_alts*
ext_node_right_rotate(struct ext_query_alts* root)
{
   struct ext_query_alts* current_root = NULL;
   struct ext_query_alts* new_root = NULL;

   if (!root || !root->left)
   {
      return root;
   }

   current_root = root;
   new_root = root->left;

   current_root->left = new_root->right;
   new_root->right = current_root;

   current_root->height = MAX(ext_height(current_root->left), ext_height(current_root->right)) + 1;
   new_root->height = MAX(ext_height(new_root->left), ext_height(new_root->right)) + 1;

   return new_root;
}

static struct ext_query_alts*
ext_node_left_rotate(struct ext_query_alts* root)
{
   struct ext_query_alts* current_root = NULL;
   struct ext_query_alts* new_root = NULL;

   if (!root || !root->right)
   {
      return root;
   }

   current_root = root;
   new_root = root->right;

   current_root->right = new_root->left;
   new_root->left = current_root;

   current_root->height = MAX(ext_height(current_root->left), ext_height(current_root->right)) + 1;
   new_root->height = MAX(ext_height(new_root->left), ext_height(new_root->right)) + 1;

   return new_root;
}

struct ext_query_alts*
pgexporter_insert_extension_node_avl(struct ext_query_alts* root, struct ext_query_alts** new_node)
{
   int cmp = 0;

   if (!root)
   {
      return (*new_node);
   }

   cmp = pgexporter_compare_extension_versions(&root->ext_version, &(*new_node)->ext_version);

   if (cmp == VERSION_EQUAL)
   {
      // Free new node, as no need to insert it
      pgexporter_free_extension_node_avl(new_node);
      return root;
   }
   else if (cmp == VERSION_GREATER)
   {
      root->left = pgexporter_insert_extension_node_avl(root->left, new_node);
   }
   else
   {
      root->right = pgexporter_insert_extension_node_avl(root->right, new_node);
   }

   root->height = MAX(ext_height(root->left), ext_height(root->right)) + 1;

   /* AVL Rotations */
   if (ext_get_node_balance(root) > 1)
   {
      if (ext_get_node_balance(root->left) == -1)
      {
         root->left = ext_node_left_rotate(root->left);
      }
      return ext_node_right_rotate(root);
   }
   else if (ext_get_node_balance(root) < -1)
   {
      if (ext_get_node_balance(root->right) == 1)
      {
         root->right = ext_node_right_rotate(root->right);
      }
      if (ext_get_node_balance(root) != 0)
      {
         return ext_node_left_rotate(root);
      }
   }

   return root;
}

struct ext_query_alts*
pgexporter_get_extension_query_alt(struct ext_query_alts* root, struct version* ext_version)
{
   struct ext_query_alts* temp = root;
   struct ext_query_alts* last = NULL;

   // Traversing the AVL tree to find highest compatible version
   while (temp)
   {
      int cmp = pgexporter_compare_extension_versions(&temp->ext_version, ext_version);

      if (cmp <= VERSION_EQUAL &&
          (!last || pgexporter_compare_extension_versions(&temp->ext_version, &last->ext_version) == VERSION_GREATER))
      {
         last = temp;
      }

      if (cmp == VERSION_GREATER)
      {
         temp = temp->left;
      }
      else
      {
         temp = temp->right;
      }
   }

   if (!last || pgexporter_compare_extension_versions(&last->ext_version, ext_version) == VERSION_GREATER)
   {
      return NULL;
   }
   else
   {
      return last;
   }
}

void
pgexporter_free_extension_query_alts(struct configuration* config)
{
   for (int i = 0; i < config->number_of_extensions; i++)
   {
      for (int j = 0; j < config->extensions[i].number_of_metrics; j++)
      {
         if (config->extensions[i].metrics[j].ext_root)
         {
            pgexporter_free_extension_node_avl(&config->extensions[i].metrics[j].ext_root);
         }
      }
   }
}

void
pgexporter_free_extension_node_avl(struct ext_query_alts** root)
{
   if (!root || !(*root))
   {
      return;
   }

   pgexporter_free_extension_node_avl(&(*root)->left);
   pgexporter_free_extension_node_avl(&(*root)->right);

   pgexporter_destroy_shared_memory(&root, sizeof(struct ext_query_alts*));
}
