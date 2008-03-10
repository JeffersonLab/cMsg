/***************************************************************************
 *cr
 *cr            (C) Copyright 1995-2007 The Board of Trustees of the
 *cr                        University of Illinois
 *cr                         All Rights Reserved
 *cr
 ***************************************************************************/

/***************************************************************************
 * RCS INFORMATION:
 *
 *      Revision: 1.9       Date: 2007/01/12 20:08:36
 *
 ***************************************************************************
 * DESCRIPTION:
 *   A simple hash table implementation for strings, contributed by John Stone,
 *   derived from his ray tracer code.
 ***************************************************************************/
#ifndef HASH_H
#define HASH_H

/** This structure represents a hash table. */
typedef struct hash_t {
  struct hash_node_t **bucket; /**< array of hash nodes. */
  int size;                    /**< size of the array. */
  int entries;                 /**< number of entries in table. */
  int downshift;               /**< shift cound, used in hash function. */
  int mask;                    /**< used to select bits for hashing. */
} hash_t;

#define HASH_FAIL -1

#ifdef __cplusplus
extern "C" {
#endif

void hashInit(hash_t *, int);

int hashLookup (const hash_t *, const char *, void **);

int hashInsert (hash_t *, const char *, void *, void **);

int hashRemove (hash_t *, const char *, void **);

void hashClear(hash_t *, void ***, int *);

void hashDestroy(hash_t *, void ***, int *);

int hashSize(hash_t *tptr);

char *hashStats (hash_t *);

#ifdef __cplusplus
}
#endif

#endif

