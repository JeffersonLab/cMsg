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
 *      Revision: 1.11       Date: 2007/01/12 20:08:36
 *      Revision: 1.2        Date: 2008/02/29 Carl Timmer
 *
 ***************************************************************************
 * DESCRIPTION:
 *   A simple hash table implementation for strings, contributed by John Stone,
 *   derived from his ray tracer code.
 *   Carl Timmer changed data from int to void pointer and changed routines
 *   signatures. Use doxygen documentation. Fix bug in hashRemove which did
 *   not decrement entry counter.
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

#define HASH_LIMIT 0.5

/** This structure represents a hash table entry (key & value). */
typedef struct hash_node_t {
  void *data;                 /**< data in hash node. */
  const char * key;           /**< key for hash lookup. */
  struct hash_node_t *next;   /**< next node in hash chain. */
} hash_node_t;


/*
 * This hash function returns a hash number for a given key.
 *
 * @param tptr Pointer to a hash table
 * @param key The key to create a hash number for
 * @return hash value
 */
static int hash(const hash_t *tptr, const char *key) {
  int i=0;
  int hashvalue;
 
  while (*key != '\0')
    i=(i<<3)+(*key++ - '0');
 
  hashvalue = (((i*1103515249)>>tptr->downshift) & tptr->mask);
  if (hashvalue < 0) {
    hashvalue = 0;
  }    

  return hashvalue;
}


/*
 * This routine initializes a new hash table.
 *
 * @param tptr Pointer to the hash table to initialize
 * @param buckets The number of initial buckets to create
 */
void hashInit(hash_t *tptr, int buckets) {

  /* make sure we allocate something */
  if (buckets==0)
    buckets=16;

  /* initialize the table */
  tptr->entries=0;
  tptr->size=2;
  tptr->mask=1;
  tptr->downshift=29;

  /* ensure buckets is a power of 2 */
  while (tptr->size<buckets) {
    tptr->size<<=1;
    tptr->mask=(tptr->mask<<1)+1;
    tptr->downshift--;
  } /* while */

  /* allocate memory for table */
  tptr->bucket=(hash_node_t **) calloc(tptr->size, sizeof(hash_node_t *));

  return;
}


/*
 * This routine creates new hash table when old one fills up.
 *
 * @param tptr Pointer to a hash table
 */
static void rebuild_table(hash_t *tptr) {
  hash_node_t **old_bucket, *old_hash, *tmp;
  int old_size, h, i;

  old_bucket=tptr->bucket;
  old_size=tptr->size;

  /* create a new table and rehash old buckets */
  hashInit(tptr, old_size<<1);
  for (i=0; i<old_size; i++) {
    old_hash=old_bucket[i];
    while(old_hash) {
      tmp=old_hash;
      old_hash=old_hash->next;
      h=hash(tptr, tmp->key);
      tmp->next=tptr->bucket[h];
      tptr->bucket[h]=tmp;
      tptr->entries++;
    } /* while */
  } /* for */

  /* free memory used by old table */
  free(old_bucket);

  return;
}


/*
 * This routines looks up an entry in the hash table and fills a pointer with it.
 * It returns 1 if it was found, else 0.
 *
 * @param tptr Pointer to the hash table
 * @param key The key to lookup
 * @param data Pointer which gets filled with entry in hash table
 * @return 1 if entry found, else 0
 */
int hashLookup(const hash_t *tptr, const char *key, void **data) {
  int h;
  hash_node_t *node;

  /* find the entry in the hash table */
  h=hash(tptr, key);
  for (node=tptr->bucket[h]; node!=NULL; node=node->next) {
    if (!strcmp(node->key, key))
      break;
  }

  /* Set pointer to the entry if it exists. Return 1 if it exists, else 0. */
  if (node != NULL) {
      if (data != NULL)
          *data = node->data;
      return (1);
  }
  return (0);
}


/*
 * This routine insert an entry into the hash table. If the entry already
 * exists, the oldData pointer (if not NULL) gets filled with the old entry,
 * the new entry is inserted and 1 is returned. Otherwise the new entry is
 * inserted and 0 is returned.
 *
 * @param tptr A pointer to the hash table
 * @param key The key to insert into the hash table
 * @param data A pointer to the data to insert into the hash table
 * @param oldData A pointer that gets filled with any existing old data for the key
 * 
 * @return 0 if the only the new entry is inserted or 1 if the new entry is
 *         replacing an existing entry
 */
int hashInsert(hash_t *tptr, const char *key, void *data, void **oldData) {
  hash_node_t *node;
  int h;
  
  /*****************************************************/
  /* check to see if the entry exists, if so update it */
  
  /* find the entry in the hash table */
  h=hash(tptr, key);
  for (node=tptr->bucket[h]; node!=NULL; node=node->next) {
    if (!strcmp(node->key, key))
      break;
  }

  /* if entry exists, update it and return */
  if (node != NULL) {
      if (oldData != NULL)
          *oldData = node->data;
      node->data = data;
      return (1);
  }
  /*****************************************************/

  /* expand the table if needed */
  while (tptr->entries>=HASH_LIMIT*tptr->size) {
   rebuild_table(tptr);
  }

  /* insert the new entry */
  node=(struct hash_node_t *) malloc(sizeof(hash_node_t));
  node->data=data;
  node->key=key;
  node->next=tptr->bucket[h];
  tptr->bucket[h]=node;
  tptr->entries++;

  return (0);
}


/*
 * This routine removes an entry from a hash table and fills a pointer
 * with it if it exists. It returns 1 if removed and 0 if it wasn't.
 *
 * @param tptr A pointer to the hash table
 * @param key The key to remove from the hash table
 * @param data A pointer that gets filled with any existing entry for the key
 * 
 * @return 1 if entry removed, else 0
*/
int hashRemove(hash_t *tptr, const char *key, void **data) {
  hash_node_t *node, *last;
  int h;

  /* find the node to remove */
  h=hash(tptr, key);
  for (node=tptr->bucket[h]; node; node=node->next) {
    if (!strcmp(node->key, key))
      break;
  }

  /* Didn't find anything */
  if (node==NULL)
    return 0;

  /* if node is at head of bucket, we have it easy */
  if (node==tptr->bucket[h])
    tptr->bucket[h]=node->next;
  else {
    /* find the node before the node we want to remove */
    for (last=tptr->bucket[h]; last && last->next; last=last->next) {
      if (last->next==node)
        break;
    }
    last->next=node->next;
  }

  /* free memory and return the data */
  if (data != NULL)
      *data=node->data;
  free(node);
  tptr->entries--;

  return (1);
}


/*
 * This routine clears the table of all entries.
 * It returns an array of all the entries and its size.
 * 
 * @param tptr A pointer to the hash table
 * @param data pointer to array of void pointers -
 *             gets filled with entries if not NULL
 * @param size pointer to int -
 *             gets filled with the number of entries if not NULL and data not NULL
 */
void hashClear(hash_t *tptr, void ***data, int *size) {
  hash_node_t *node, *last;
  int i, index=0;
  void **array=NULL;
  
  /* return data so user can free it */
  if (data != NULL) {
    if (tptr->entries > 0) {
      array = (void **) malloc(tptr->entries*sizeof(void *));
      *data = array;
      if (size != NULL)
         *size = tptr->entries;
    }
    else {
        *data = NULL;
        if (size != NULL)
            *size = 0;
    }
  }
  
  for (i=0; i<tptr->size; i++) {
    node = tptr->bucket[i];
    while (node != NULL) { 
      last = node;   
      node = node->next;
      if (array != NULL)
          array[index++] = last->data;
      free(last);
    }
  }
  
  tptr->entries = 0;
}


/*
 * This routine deletes the entire table, and all entries.
 * It returns an array of all the entries and its size.
 * 
 * @param tptr A pointer to the hash table
 * @param data pointer to array of void pointers -
 *             gets filled with entries if not NULL
 * @param size pointer to int -
 *             gets filled with the number of entries if not NULL and data not NULL
 */
void hashDestroy(hash_t *tptr, void ***data, int *size) {
  hashClear(tptr, data, size);

  /* free the entire array of buckets */
  if (tptr->bucket != NULL) {
    free(tptr->bucket);
    memset(tptr, 0, sizeof(hash_t));
  }
}


/*
 * This routine returns the number of entries.
 * 
 * @param tptr A pointer to the hash table
 * @return the number of entries
 */
int hashSize(hash_t *tptr) {
    return tptr->entries;  
}


/*
 * This routine finds the average length of search.
 *
 * @param tptr A pointer to the hash table
 * @return average length of search 
 */
static float alos(hash_t *tptr) {
  int i,j;
  float alos=0;
  hash_node_t *node;


  for (i=0; i<tptr->size; i++) {
    for (node=tptr->bucket[i], j=0; node!=NULL; node=node->next, j++);
    if (j)
      alos+=((j*(j+1))>>1);
  } /* for */

  return(tptr->entries ? alos/tptr->entries : 0);
}


/*
 * This routine returns a string with statistics about a hash table.
 *
 * @param tptr A pointer to the hash table
 * @return string with hash table statistics 
 */
char *hashStats(hash_t *tptr) {
  static char buf[1024];

  sprintf(buf, "%u slots, %u entries, and %1.2f ALOS",
    (int)tptr->size, (int)tptr->entries, alos(tptr));

  return(buf);
}



