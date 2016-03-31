#include "fdb.h"
#include "sqlite3.h"
#include <apr-1/apr_pools.h>
#include <pthread.h>
#include <apr-1/apr_thread_pool.h>
#include <apr-1/apr_thread_proc.h>
#include <apr-1/apr_thread_mutex.h>
#include <apr-1/apr_queue.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/dir.h>
#include <string.h>
#include <unistd.h>
#if defined(__APPLE__)
#  define COMMON_DIGEST_FOR_OPENSSL
#  include <CommonCrypto/CommonDigest.h>
//#  define SHA1 CC_SHA1
#else
#  include <openssl/md5.h>
#endif

//static size_t io_file_size(const char *fname);
//static int    io_file_exists(const char *filename);

static size_t get_file_size(const char *filename);
static int    sql_file_exists(filework *fw);
static int    sql_insert_file(filework *fw);
static void * do_work(apr_thread_t *th, void *data);


static const char   hexchars[]  = "0123456789abcdef";
//static char       * file_exists = "select fname from fdb_files where fname = ?;";
static const char * sql_file_insert = "insert into fdb_files(fname, fsize, md5_full) values(?,?,?);";

//@TODO make these thread safe
static size_t file_count        = 0;
static size_t file_insert_count = 0;

static void * do_work(apr_thread_t *th, void *data) {
  filework *fw = data;
  assert(fw->db);
  printf("Working on file... %s\n", fw->full_path);
  file_count++;
  int exists = sql_file_exists(fw);
  if(!exists) {
    sql_insert_file(fw);
    file_insert_count++;
  }
  if(file_count % 10000 == 0) {
    pthread_t ptid = pthread_self();
    //apr_os_thread_t apr_id;
    //printf("thread id== %d\n",ptid);
    //printf("ptid=%u done=%zu in=%zu\n", (int)ptid, file_count, file_insert_count);
  }
  return NULL;
}

fdb_worker * fdb_start_workers(apr_allocator_t * apr_alloc, const unsigned int threads) {
  fdb_worker *work    = NULL;
  apr_pool_t         * pool;
  apr_pool_create(&pool, NULL);
  work = apr_pcalloc(pool, sizeof(*work));

  work->start    = NULL;
  work->apt_pool = NULL;
  work->owner    = 1;

  unsigned int init_threads = threads;
  unsigned int max_threads  = threads;
  int rv = apr_queue_create(&work->queue, max_threads, pool);
  assert(rv == APR_SUCCESS);

  rv = apr_thread_pool_create(&work->apt_pool, init_threads, max_threads, pool);
  if (rv != APR_SUCCESS) {
    //apr_thread_create(&thread[t], NULL, ap_thread_stream, (void *)stream, pool) != APR_SUCCESS) {
    printf("ERROR; return code from apr_thread_pool_create(...) is %d\n",rv);
    exit(-1);
  }
  return work;
}

static int io_md5(const char *filename, char *out) {
  unsigned char c[MD5_DIGEST_LENGTH];
  int i;
  FILE *fp = fopen(filename, "rb");
  if(!fp) {
    printf("%s err = %s", filename, strerror(errno));
    //assert(fp);
    return -1;
  }
  MD5_CTX mdContext;
  size_t bytes;
  unsigned char data[8192];
  MD5_Init (&mdContext);
  while ((bytes = fread(data, 1, 8192, fp)) != 0) {
    MD5_Update (&mdContext, data, bytes);
  }
  MD5_Final(c,&mdContext);
  int n = 0;
  for(i = 0, n = 0; i < MD5_DIGEST_LENGTH; i++, n++) {
    int nib = c[i];
    out[n++] = hexchars[nib >> 4];
    out[n  ] = hexchars[nib & 0xF];
  }
  out[32] = '\0';
  fclose (fp);
  return 0;
}




static int sql_file_exists(filework *fw) {
  assert(fw->db);

  int res = 0;

  size_t l = strlen(fw->full_path);
  const char *exists_sql = "select fname from fdb_files where fname = ?;";
  //printf("full_path=%s pathle=%d == %d\n", fw->full_path, fw->pathlen, l);
  sqlite3_stmt *ex_stmt;
  int rc = sqlite3_prepare_v2(fw->db,
                              exists_sql,
                              -1,
                              &ex_stmt,
                              NULL);
  if(rc != SQLITE_OK){
    fprintf(stderr, "Error: : %s\n", sqlite3_errmsg(fw->db));
    //int ecode = sqlite3_extended_errcode(db);
    //printf("%d\n", ecode);
    //fprintf(stderr, "Error: %s\n", sqlite3_errstr(ecode));
    assert(NULL);
  }

  rc = sqlite3_bind_text(ex_stmt,1,fw->full_path, (int)fw->pathlen, NULL);

  if(rc == SQLITE_NOMEM){
    printf("%d\n", rc);
    fprintf(stderr, "Error: SQLITE_NOMEM: %s\n", sqlite3_errmsg(fw->db));
    int ecode = sqlite3_extended_errcode(fw->db);
    printf("%d\n", ecode);
    fprintf(stderr, "Bind Error: %s\n", sqlite3_errstr(ecode));
    assert(NULL);
  }
  else if(rc == SQLITE_MISUSE ){
    printf("%d\n", rc);
    fprintf(stderr, "Error: SQLITE_MISUSE: %s\n", sqlite3_errmsg(fw->db));
    int ecode = sqlite3_extended_errcode(fw->db);
    printf("%d\n", ecode);
    fprintf(stderr, "Bind Error: %s\n", sqlite3_errstr(ecode));
    assert(NULL);
  }
  else if(rc != SQLITE_OK ){
    printf(">%d<\n", rc);
    fprintf(stderr, "Error database: %s\n", sqlite3_errmsg(fw->db));
    int ecode = sqlite3_extended_errcode(fw->db);
    //printf("%d\n", ecode);
    fprintf(stderr, "Bind Error: %s\n", sqlite3_errstr(ecode));
    assert(NULL);
  }
  rc = sqlite3_step(ex_stmt);
  if(rc == SQLITE_DONE || rc == SQLITE_ROW) {
    if(sqlite3_column_text(ex_stmt, 0) == NULL) {
      res = 0;
    }
    else {
      //printf("Found %s\n", sqlite3_column_text(ex_stmt, 0));
      res = 1;
    }
    //return 0;
  }
  else if (rc != SQLITE_OK ){
    printf("%d\n", rc);
    fprintf(stderr, "Can't step: %s\n", sqlite3_errmsg(fw->db));
    //int ecode = sqlite3_extended_errcode(db);
    //printf("%d\n", ecode);
    //fprintf(stderr, "Can't open database: %s\n", sqlite3_errstr(ecode));
    assert(NULL);
  }
  else if(rc == SQLITE_ROW) {
    //printf("Found %s\n", sqlite3_column_text(ex_stmt, 0));2
    res = 1;
  }
  sqlite3_reset(ex_stmt);
  sqlite3_finalize(ex_stmt);
  return res;
}

static int sql_insert_file(filework *fw) {
  assert(fw->db);
  sqlite3_stmt *in_stmt;
  sqlite3_prepare_v2(fw->db, sql_file_insert, -1, &in_stmt, NULL);

  int res = 0;
  //off_t       fsize = io_file_size(full_path);
  char buf[40];
  int rc = io_md5(fw->full_path, buf);
  if(rc == -1) {
    return 0;
  }
  rc = sqlite3_bind_text(in_stmt,1,fw->full_path,  (int)fw->pathlen, NULL);
  if(rc != SQLITE_OK ){
    printf("id=%d %s\n",rc, fw->full_path);
    assert(NULL);
  }
  rc = sqlite3_bind_int64(in_stmt,2, fw->fsize);
  if(rc != SQLITE_OK ){
    assert(NULL);
  }
  rc = sqlite3_bind_text(in_stmt,3,buf,32,NULL);
  if(rc != SQLITE_OK ){
    assert(NULL);
  }
  rc = sqlite3_step(in_stmt);
  if(SQLITE_DONE) {
    goto reset_ret;
  }
  if (rc != SQLITE_OK ){
    printf("%d\n", rc);
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(fw->db));
    int ecode = sqlite3_extended_errcode(fw->db);
    printf("%d\n", ecode);
    fprintf(stderr, "Error: %s\n", sqlite3_errstr(ecode));
    int ex_ecode = sqlite3_extended_errcode(fw->db);
    fprintf(stderr, "Error: %s\n", sqlite3_errstr(ex_ecode));
    assert(NULL);
  }
reset_ret:
  sqlite3_reset(in_stmt);

  sqlite3_finalize(in_stmt);
  return res;
}


int walk_recur(const char *const dname, fdb_worker * worker) {
  //printf("walking=%s\n", dname);

  DIR *dir;
  struct dirent *dent;
  char next_dir[FILENAME_MAX];
  size_t len = strlen(dname);
  assert(len <= FILENAME_MAX);

  strcpy(next_dir, dname);
  errno = 0;
  if (!(dir = opendir(dname))) {
    printf("can't open directory: %s\n", strerror(errno));
    return 0;
  }
  errno = 0;
  while ((dent = readdir(dir))) {
    if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
      continue;
    }
    if(dent->d_type != DT_REG && dent->d_type  != DT_DIR) {
      continue;
    }
    //printf("done=%zu in=%zu\n", file_count, file_insert_count);

    char full_path[8192];
    snprintf(full_path, sizeof(full_path)-1, "%s/%s", dname, dent->d_name);
    //int pathlen = strlen(full_path);
    struct stat stat_s;
    //strncpy(fn + len, dent->d_name, FILENAME_MAX - len);
    if (stat(full_path, &stat_s) == -1) {
      printf("Can't stat %s", next_dir);
      continue;
    }

    if(dent->d_type == DT_REG) {

      filework     * fw = malloc(sizeof(filework));
      fw->pathlen       = strlen(full_path);//pathlen;
      fw->full_path     = full_path;
      fw->fsize         = stat_s.st_size;
      fw->db            = worker->db;
      assert(fw->db);
      int rv = apr_thread_pool_push(worker->apt_pool, &do_work, (void *)fw, 1, &worker->owner);
      if (rv != APR_SUCCESS) {
        printf ("ERROR; return code from apr_thread_pool_push(...) is %d\n", rv);
        exit(-1);
      }
      apr_size_t s =  apr_thread_pool_busy_count(worker->apt_pool);
      //printf ("busy==%zu\n", s);
    }
    else if(dent->d_type == DT_DIR) {
     if(S_ISDIR(stat_s.st_mode)) {
        walk_recur(full_path, worker);
      }
    }
  }
  if (dir) {
    closedir(dir);
  }
  return 0;
}

/*
int main(int argc, const char* const argv[]) {
  apr_initialize();
  struct passwd *pw;
  uid_t uid;

  uid = geteuid ();
  pw = getpwuid (uid);
  if (pw) {
    printf("username=%s\n",pw->pw_name);
  }
  else {
    fprintf (stderr,"cannot find username for UID %u\n", (unsigned) uid);
    exit(EXIT_FAILURE);
  }
  assert(sqlite3_threadsafe() > 0);
  printf("command     == %s\n", argv[0]);
  printf("database    == %s\n", argv[1]);
  printf("search  dir == %s\n", argv[2]);

  int res = access(argv[2], F_OK);
  assert(res == 0);
  //printf("%s\n", strerror(res));
  if( argc!=3 ){
    fprintf(stderr, "Usage: %s DATABASE start_directory\n", argv[0]);
    return(1);
  }
  int rc = sqlite3_open(argv[1], &db);
  if( rc != SQLITE_OK){
    //int ecode = sqlite3_errcode(db);
    //printf("Can't open database: %s\n", argv[1]);
    //fprintf(stderr, "ecode: %s\n", sqlite3_errstr(ecode));
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return(1);
  }

  apr_allocator_t * apr_alloc = NULL;
  apr_status_t st = apr_allocator_create(&apr_alloc);
  if(st != APR_SUCCESS) {
    assert(NULL);
  }

  fdb_worker * worker = fdb_start_workers(apr_alloc);

  assert(db != NULL);
  sqlite3_extended_result_codes(db, 1);
  rc = walk_recur(argv[2], worker);
  printf("canceling threads\n");
  printf("Busy Count %lu\n", apr_thread_pool_busy_count(worker->apt_pool));
  int rv = apr_thread_pool_tasks_cancel(worker->apt_pool, &worker->owner);
  if (rv != APR_SUCCESS) {
    printf ("ERROR; return code from pthread_create() is %d\n", rv);
    //exit(-1);
  }
  rv = apr_thread_pool_destroy(worker->apt_pool);
  if (rv != APR_SUCCESS) {
    printf ("ERROR; return code from pthread_create() is %d\n", rv);
    //exit(-1);
  }
  sqlite3_close(db);
  return rc;
}
*/

//static size_t io_file_size(const char *fname) {
//  assert(fname != NULL);
//  return get_file_size(fname);
//}
//
//static int io_file_exists(const char *filename) {
//  struct stat buffer;
//  return (stat (filename, &buffer) == 0);
//}
//
//
//static size_t get_file_size(const char *filename) {
//  struct stat stbuf;
//  int fd = open(filename, O_RDONLY);
//  if (fd == -1) {
//    close(fd);
//    return -1;
//  }
//  if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
//    close(fd);
//    return -1;
//  }
//  close(fd);
//  return stbuf.st_size;
//}
//
//
//static struct stat * io_stat(const char *filename, struct stat *stbuf) {
//  int fd = open(filename, O_RDONLY);
//  if (fd == -1) {
//    close(fd);
//    return NULL;
//  }
//  if ((fstat(fd, stbuf) != 0) || (!S_ISREG(stbuf->st_mode))) {
//    close(fd);
//    return NULL;
//  }
//  close(fd);
//  return stbuf;
//}
