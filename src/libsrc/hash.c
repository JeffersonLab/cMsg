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
 *   not decrement entry counter. Added some new routines.
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

#define HASH_LIMIT 0.5

/**
 * This hash function returns a hash number for a given key.
 *
 * @param tptr Pointer to a hash table
 * @param key The key to create a hash number for
 * @return hash value
 */
static int hash(const hashTable *tptr, const char *key) {
    int i=0;
    int hashvalue;

    while (*key != '\0') {
        i=(i<<3)+(*key++ - '0');
    }

    hashvalue = (((i*1103515249)>>tptr->downshift) & tptr->mask);
    if (hashvalue < 0) {
        hashvalue = 0;
    }    

    return hashvalue;
}


/**
 * This routine initializes a new hash table.
 *
 * @param tptr Pointer to the hash table to initialize
 * @param buckets The number of initial buckets to create
 */
void hashInit(hashTable *tptr, int buckets) {

    /* make sure we allocate something */
    if (buckets<=0) {
        buckets=16;
    }

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
    tptr->bucket=(hashNode **) calloc((size_t)tptr->size, sizeof(hashNode *));

    return;
}


/**
 * This routine creates new hash table when old one fills up.
 *
 * @param tptr Pointer to a hash table
 */
static void rebuild_table(hashTable *tptr) {
    hashNode **old_bucket, *old_hash, *tmp;
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


/**
 * This routines looks up an entry in the hash table and fills a pointer with it.
 * It returns 1 if it was found, else 0.
 *
 * @param tptr Pointer to the hash table
 * @param key The key to lookup
 * @param data Pointer which gets filled with entry in hash table
 * @return 1 if entry found, else 0
 */
int hashLookup(const hashTable *tptr, const char *key, void **data) {
    int h;
    hashNode *node;

    /* find the entry in the hash table */
    h=hash(tptr, key);
    for (node=tptr->bucket[h]; node!=NULL; node=node->next) {
        if (!strcmp(node->key, key)) {
          break;
        }
    }

    /* Set pointer to the entry if it exists. Return 1 if it exists, else 0. */
    if (node != NULL) {
        if (data != NULL) {
            *data = node->data;
        }
        return (1);
    }
    return (0);
}


/**
 * This routine inserts an entry into the hash table. If an entry with that
 * key already exists, nothing is inserted and 0 is returned. Otherwise the
 * new entry is inserted and 1 is returned.
 *
 * @param tptr A pointer to the hash table
 * @param key The key to insert into the hash table
 * @param data A pointer to the data to insert into the hash table
 *
 * @return 1 if the new entry is inserted or 0 if an entry
 *         already exists for that key
 */
int hashInsertTry(hashTable *tptr, const char *key, void *data) {
    hashNode *node;
    int h;

    /*****************************************************/
    /* check to see if the entry exists, if so return */

    /* find the entry in the hash table */
    h=hash(tptr, key);
    for (node=tptr->bucket[h]; node!=NULL; node=node->next) {
        if (!strcmp(node->key, key)) {
            break;
        }
    }

    /* if entry exists, return */
    if (node != NULL) {
        return (0);
    }
    /*****************************************************/

    /* expand the table if needed */
    while (tptr->entries>=HASH_LIMIT*tptr->size) {
        rebuild_table(tptr);
    }

    /* insert the new entry */
    node=(struct hash_node_t *) malloc(sizeof(hashNode));
    node->data=data;
    node->key=strdup(key); /* copy key to avoid it going out of scope */
    node->next=tptr->bucket[h];
    tptr->bucket[h]=node;
    tptr->entries++;

    return (1);
}


/**
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
int hashInsert(hashTable *tptr, const char *key, void *data, void **oldData) {
  hashNode *node;
  int h;

  /*****************************************************/
  /* check to see if the entry exists, if so update it */

  /* find the entry in the hash table */
  h=hash(tptr, key);
  for (node=tptr->bucket[h]; node!=NULL; node=node->next) {
      if (!strcmp(node->key, key)) {
          break;
      }
  }

  /* if entry exists, update it and return */
  if (node != NULL) {
    if (oldData != NULL) {
      *oldData = node->data;
    }
    node->data = data;
    return (1);
  }
  /*****************************************************/

  /* expand the table if needed */
  while (tptr->entries>=HASH_LIMIT*tptr->size) {
    rebuild_table(tptr);
  }

  /* insert the new entry */
  node=(struct hash_node_t *) malloc(sizeof(hashNode));
  node->data=data;
  node->key=strdup(key); /* copy key to avoid it going out of scope */
  node->next=tptr->bucket[h];
  tptr->bucket[h]=node;
  tptr->entries++;

  return (0);
}


/**
 * This routine removes an entry from a hash table and fills a pointer
 * with it if it exists. It returns 1 if removed and 0 if it wasn't.
 *
 * @param tptr A pointer to the hash table
 * @param key The key to remove from the hash table
 * @param data A pointer that gets filled with any existing entry for the key
 * 
 * @return 1 if entry removed, else 0
*/
int hashRemove(hashTable *tptr, const char *key, void **data) {
    hashNode *node, *last;
    int h;

    /* find the node to remove */
    h=hash(tptr, key);
    for (node=tptr->bucket[h]; node; node=node->next) {
        if (!strcmp(node->key, key)) {
            break;
        }
    }

    /* Didn't find anything */
    if (node==NULL) {
        return 0;
    }

    /* if node is at head of bucket, we have it easy */
    if (node==tptr->bucket[h]) {
        tptr->bucket[h]=node->next;
    }
    else {
        /* find the node before the node we want to remove */
        for (last=tptr->bucket[h]; last && last->next; last=last->next) {
            if (last->next==node) {
                break;
            }
        }
        last->next=node->next;
    }

    /* free memory and return the data */
    if (data != NULL) {
        *data=node->data;
    }
    free(node->key);
    free(node);
    tptr->entries--;

    return (1);
}


/**
 * This routine returns an array of all the entries and its size.
 * The caller must free the returned array to avoid a memory leak,
 * but must NOT free the keys or data. If any argument is
 * NULL, no information is returned in any argument.
 * 
 * @param tptr A pointer to the hash table
 * @param entries pointer to array of hashNode structures -
 *                gets filled with entries if not NULL and size not NULL.
 *                Gets set to NULL if no enties in table.
 * @param size pointer to int -
 *             gets filled with the number of entries if not NULL and entries not NULL
 *             Gets set to 0 if no enties in table.
 *
 * @return 1 if meaningful results returned, else 0. Means an arg is NULL
 *         or cannot allocate memory
 */
int hashGetAll(hashTable *tptr, hashNode **entries, int *size) {
    hashNode *node, *last, *array=NULL;
    int i, index=0;

    if (tptr == NULL || entries == NULL || size == NULL) {
      return 0;
    }

    /* return data so user can free it */
    if (tptr->entries > 0) {
        array = (hashNode *) malloc(tptr->entries*sizeof(hashNode));
        if (array == NULL) {
            *entries = NULL;
            *size = 0;
            return 0;
        }
        *entries = array;
        *size = tptr->entries;
    }
    else {
        *entries = NULL;
        *size = 0;
        return 1;
    }

    for (i=0; i<tptr->size; i++) {
        node = tptr->bucket[i];
        while (node != NULL) { 
            last = node;   
            node = node->next;
            
            array[index].key    = last->key;
            array[index].data   = last->data;
            array[index++].next = NULL;
        }
    }
    return 1;
}


/**
 * This routine clears the table of all entries.<p>
 * If the "entries" arg is not NULL, it returns an array of
 * all the entries and its size, and the caller must free
 * the "key" member of each hashNode to avoid a memory leak.
 * The caller must also free the returned array.
 * The caller alone knows what to do with the "data" member.
 * If the table is empty, then "entries" points to NULL and
 * size is set to zero.<p>
 * If the "entries" arg is NULL, then this routine frees all
 * the keys and all data is ignored. If data point to allocated
 * memory, it may be lost.
 * 
 * @param tptr A pointer to the hash table
 * @param entries pointer to array of hashNode structures -
 *                gets filled with entries if not NULL
 * @param size pointer to int -
 *             gets filled with the number of entries if not NULL and entries not NULL
 *
 * @return 0 if NULL first arg or cannot allocate memory, else 1
 */
int hashClear(hashTable *tptr, hashNode **entries, int *size) {
  hashNode *node, *last, *array=NULL;
  int i, index=0;

  if (tptr == NULL) {
    return 0;
  }

  /* return data so user can free it */
  if (entries != NULL) {
    if (tptr->entries > 0) {
      array = (hashNode *) malloc(tptr->entries*sizeof(hashNode));
      if (array == NULL) return 0;
      *entries = array;
      if (size != NULL) {
         *size = tptr->entries;
      }
    }
    else {
      *entries = NULL;
      if (size != NULL) {
         *size = 0;
      }
      tptr->entries = 0;
      return 1;
    }
  }

  for (i=0; i<tptr->size; i++) {
    node = tptr->bucket[i];
    tptr->bucket[i] = NULL;
    while (node != NULL) {
      last = node;
      node = node->next;
      if (array != NULL) {
        array[index].key    = last->key;
        array[index].data   = last->data;
        array[index++].next = NULL;
      }
      else {
        free(last->key);
      }
      free(last);
    }
  }

  tptr->entries = 0;
  return 1;
}

/**
 * This routine deletes the entire table, and all entries.<p>
 * If the "entries" arg is not NULL, it returns an array of
 * all the entries and its size, and the caller must free
 * the "key" member of each hashNode to avoid a memory leak.
 * The caller must also free the returned array.
 * The caller alone knows what to do with the "data" member.
 * If the table is empty, then "entries" points to NULL and
 * size is set to zero.<p>
 * If the "entries" arg is NULL, then this routine frees all
 * the keys and all data is ignored. If data point to allocated
 * memory, it may be lost.
 * 
 * @param tptr A pointer to the hash table
 * @param entries pointer to array of hashNode structures -
 *                gets filled with entries if not NULL
 * @param size pointer to int -
 *             gets filled with the number of entries if not NULL and entries not NULL
 *
 * @return 0 if NULL first arg or cannot allocate memory, else 1
 */
int hashDestroy(hashTable *tptr, hashNode **entries, int *size) {
  if (!hashClear(tptr, entries, size)) return 0;

    /* free the entire array of buckets */
    if (tptr->bucket != NULL) {
        free(tptr->bucket);
        memset(tptr, 0, sizeof(hashTable));
    }
    return 1;
}


/**
 * This routine returns the number of entries.
 * 
 * @param tptr A pointer to the hash table
 * @return the number of entries
 */
int hashSize(hashTable *tptr) {
    return tptr->entries;  
}


/**
 * This routine finds the average length of search.
 *
 * @param tptr A pointer to the hash table
 * @return average length of search 
 */
static float alos(hashTable *tptr) {
    int i,j;
    float alos=0;
    hashNode *node;

    for (i=0; i<tptr->size; i++) {
        for (node=tptr->bucket[i], j=0; node!=NULL; node=node->next, j++);
        if (j)
            alos+=((j*(j+1))>>1);
    } /* for */

    return(tptr->entries ? alos/tptr->entries : 0);
}


/**
 * This routine returns a string with statistics about a hash table.
 *
 * @param tptr A pointer to the hash table
 * @return string with hash table statistics 
 */
char *hashStats(hashTable *tptr) {
    static char buf[1024];

    sprintf(buf, "%u slots, %u entries, and %1.2f ALOS",
            tptr->size, tptr->entries, alos(tptr));

    return(buf);
}



