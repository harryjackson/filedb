#ifndef FDB_H
#define FDB_H
#include "sqlite3.h"
#include <apr-1/apr_queue.h>
#include <apr-1/apr_pools.h>
#include <apr-1/apr_thread_pool.h>
#include <stdio.h>

#define FDB_THREADS 0

typedef void *(*fdb_thread_start_t)(apr_thread_t *, void *);

typedef struct fdb_worker {
  apr_queue_t        * queue;
  apr_thread_pool_t  * apt_pool;
  sqlite3            * db;
  fdb_thread_start_t * start;
  int                  owner;
} fdb_worker;

fdb_worker * fdb_start_workers(apr_allocator_t * apr_alloc, const unsigned int threads);


typedef struct filework {
  char         * full_path;
  size_t         pathlen;
  off_t          fsize;
  sqlite3      * db;
} filework;


int walk_recur(const char *const dname, fdb_worker * worker);


#endif /* FDB_H */
