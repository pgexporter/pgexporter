/*
 * Copyright (C) 2023 Red Hat
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
#include <query_alts.h>
#include <shmem.h>

// Get height of AVL Tree Node
static int height(query_alts_t* A);

// Get balance of AVL Tree Node
static int get_node_balance(query_alts_t* A);

// Right rotate a node, and left child, such that left child is new root, and root is new right child
static query_alts_t* node_right_rotate(query_alts_t* root);

// Right rotate a node, and right child, such that right child is new root, and root is new left child
static query_alts_t* node_left_rotate(query_alts_t* root);

void
pgexporter_copy_query_alts(query_alts_t** dst, query_alts_t* src)
{

   if (!src)
   {
      return;
   }

   void* new_query_alt = NULL;
   pgexporter_create_shared_memory(sizeof(query_alts_t), HUGEPAGE_OFF, &new_query_alt);
   *dst = (query_alts_t*) new_query_alt;

   (*dst)->height = src->height;
   (*dst)->is_histogram = src->is_histogram;
   (*dst)->n_columns = src->n_columns;
   (*dst)->version = src->version;

   memcpy((*dst)->query, src->query, MAX_QUERY_LENGTH);
   memcpy((*dst)->columns, src->columns, MAX_NUMBER_OF_COLUMNS * sizeof(struct column));

   pgexporter_copy_query_alts(&(*dst)->left, src->left);
   pgexporter_copy_query_alts(&(*dst)->right, src->right);
}

static int
height(query_alts_t* A)
{
   return A ? A->height : 0;
}

static int
get_node_balance(query_alts_t* A)
{
   return A ? height(A->left) - height(A->right) : 0;
}

static query_alts_t*
node_right_rotate(query_alts_t* root)
{

   if (!root || !root->left)
   {
      return root;
   }

   query_alts_t* A, * B;

   A = root, B = root->left;

   A->left = B->right;
   B->right = A;

   A->height = MAX(height(A->left), height(A->right)) + 1;
   B->height = MAX(height(B->left), height(B->right)) + 1;

   return B;
}

static query_alts_t*
node_left_rotate(query_alts_t* root)
{

   if (!root || !root->right)
   {
      return root;
   }

   query_alts_t* A, * B;

   A = root, B = root->right;

   A->right = B->left;
   B->left = A;

   A->height = MAX(height(A->left), height(A->right)) + 1;
   B->height = MAX(height(B->left), height(B->right)) + 1;

   return B;
}

query_alts_t*
pgexporter_insert_node_avl (query_alts_t* root, query_alts_t** new_node)
{
   if (!root)
   {
      return (*new_node);
   }
   else if (root->version == (*new_node)->version)
   {
      // Free New Node, as no need to insert it
      pgexporter_free_node_avl(new_node);
      return root;
   }
   else if (root->version > (*new_node)->version)
   {
      root->left = pgexporter_insert_node_avl(root->left, new_node);
   }
   else
   {
      root->right = pgexporter_insert_node_avl(root->right, new_node);
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

query_alts_t*
pgexporter_get_query_alt(query_alts_t* root, int server)
{
   struct configuration* config = NULL;
   query_alts_t* temp = root;
   query_alts_t* last = NULL;
   int ver;

   config = (struct configuration*)shmem;
   ver = config->servers[server].version;

   // Traversing the AVL tree
   while (temp)
   {
      if (temp->version <= ver &&
          (!last || temp->version > last->version))
      {
         last = temp;
      }

      if (temp->version > ver)
      {
         temp = temp->left;
      }
      else
      {
         temp = temp->right;
      }
   }

   if (!last || last->version > ver)
   {
      return NULL;
   }
   else
   {
      return last;
   }

}

void
pgexporter_free_query_alts(struct configuration* config)
{
   for (int i = 0; i < config->number_of_metrics; i++)
   {
      pgexporter_free_node_avl(&config->prometheus[i].root);
   }
}

void
pgexporter_free_node_avl(query_alts_t** root)
{

   if (!root || !(*root))
   {
      return;
   }

   pgexporter_free_node_avl(&(*root)->left);
   pgexporter_free_node_avl(&(*root)->right);

   pgexporter_destroy_shared_memory(&root, sizeof(query_alts_t*));
}