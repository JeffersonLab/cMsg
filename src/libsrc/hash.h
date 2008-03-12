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
} hashTable;

/** This structure represents a hash table entry (key & value). */
typedef struct hash_node_t {
    void *data;                 /**< data in hash node. */
    char *key;                  /**< key for hash lookup. */
    struct hash_node_t *next;   /**< next node in hash chain. */
} hashNode;


#ifdef __cplusplus
extern "C" {
#endif

    void  hashInit    (hashTable *, int);
    int   hashLookup  (const hashTable *, const char *, void **);
    int   hashInsert  (hashTable *, const char *, void *, void **);
    int   hashRemove  (hashTable *, const char *, void **);
    void  hashGetAll  (hashTable *, hashNode **, int *);
    void  hashClear   (hashTable *, hashNode **, int *);
    void  hashDestroy (hashTable *, hashNode **, int *);
    int   hashSize    (hashTable *);
    char *hashStats   (hashTable *);

#ifdef __cplusplus
}
#endif

#endif

