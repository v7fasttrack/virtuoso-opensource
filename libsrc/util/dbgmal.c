/*
 *  dbgmal.c
 *
 *  $Id$
 *
 *  Debugging malloc package
 *  
 *  This file is part of the OpenLink Software Virtuoso Open-Source (VOS)
 *  project.
 *  
 *  Copyright (C) 1998-2014 OpenLink Software
 *  
 *  This project is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; only version 2 of the License, dated June 1991.
 *  
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *  
 */

#ifdef MALLOC_DEBUG
#define MALLOC_DEBUG_2
#endif

#undef MALLOC_DEBUG

#include "libutil.h"
#include "util/dyntab.h"
#include "util/dbgmal.h"

#if 1 /* switch to 0 for permanently slow debug */
#define SLOW_MALLOC_DEBUG slow_malloc_debug
#define SLOW_MALLOC_DEBUG_PERMANENT 0
#else
#define SLOW_MALLOC_DEBUG 1
#define SLOW_MALLOC_DEBUG_PERMANENT 1
#endif

long slow_malloc_debug = SLOW_MALLOC_DEBUG_PERMANENT;
int slow_malloc_debug_uses = 0;


#ifdef USE_KILL_RINGBUF
#define KILL_RINGBUF_SIZE 0x1FF0
void **kill_ringbuf[KILL_RINGBUF_SIZE];
void **kill_ringbuf_curr = kill_ringbuf;
#define FREE_WITH_DELAY(ptr) \
  do \
    { \
      if (NULL != kill_ringbuf_curr[0]) \
        free (kill_ringbuf_curr[0]); \
      kill_ringbuf_curr[0] = ptr; \
      if (kill_ringbuf_curr == kill_ringbuf) \
        kill_ringbuf_curr = kill_ringbuf + KILL_RINGBUF_SIZE - 1; \
      else \
        kill_ringbuf_curr--; \
    } while (0)
#else
#define FREE_WITH_DELAY(ptr) free(ptr)
#endif

#ifdef MALLOC_DEBUG_2
#define AAAL_BUCKETS_COUNT 43 /* should be a prime */
#endif

#define MALREC_FNAME_BUFLEN 32
struct malrec_s
  {
    char fname[MALREC_FNAME_BUFLEN];
    u_int linenum;
    long numalloc;
    long prevalloc;
    long numfree;
    long prevfree;
#ifdef MALLOC_DEBUG_2
    void *aaal_malhdrs[AAAL_BUCKETS_COUNT];
    long aaal_count;
#endif
    size_t totalsize;
    size_t prevsize;
  };
typedef struct malrec_s malrec_t;

struct malhdr_s
  {
    uint32 magic;
#ifdef MALLOC_DEBUG_2
    void *next_malhdr;
#endif
    malrec_t *origin;
    size_t size;
    void *pool;
  };
typedef struct malhdr_s malhdr_t;

dk_mutex_t *		_dbgmal_mtx;
static dyntable_t	_dbgtab;
static size_t		_totalmem;
static uint32		_free_nulls;
static uint32		_free_invalid;
static int		_dbgmal_enabled;

void memdbg_abort (void);

#ifdef MALLOC_STRESS
static size_t dbg_malloc_hard_memlimit = ~((size_t)0);
static const char *hit_name = "";
static malrec_t *hit_rec = NULL;
static size_t hit_totalsize = 0;

void dbg_malloc_set_hard_memlimit (size_t consumption)
{
  dbg_malloc_hard_memlimit = consumption;
}

extern void dbg_malloc_hit_impl (const char *file, u_int line, const char *name)
{
  if (NULL != hit_rec)
    return;
  if (strcmp (name, hit_name))
    return;
  hit_rec = mal_register (file, line);
}

extern void dbg_malloc_set_hit_memlimit (const char *name, size_t consumption)
{
  if (strcmp (name, hit_name))
    {
      hit_name = name;
      hit_rec = NULL;
    }
  hit_totalsize = consumption;
}
#endif

/* #define USE_KILL_RINGBUF */

#ifdef USE_KILL_RINGBUF
#define KILL_RINGBUF_SIZE 0x1FF0
void **kill_ringbuf[KILL_RINGBUF_SIZE];
void **kill_ringbuf_curr = kill_ringbuf;
#define FREE_WITH_DELAY(ptr) \
  do \
    { \
      if (NULL != kill_ringbuf_curr[0]) \
        free (kill_ringbuf_curr[0]); \
      kill_ringbuf_curr[0] = ptr; \
      if (kill_ringbuf_curr == kill_ringbuf) \
        kill_ringbuf_curr = kill_ringbuf + KILL_RINGBUF_SIZE - 1; \
      else \
        kill_ringbuf_curr--; \
    } while (0)
#else
#define FREE_WITH_DELAY(ptr) free(ptr)
#endif

size_t dbg_malloc_get_current_total (void) { return _totalmem; }


static void
mal_printall (htrecord_t record, void *arg)
{
  malrec_t *rec = (malrec_t *) record;
  char buf[200];
  size_t buflen;
  char *localname;
  /* The path to file should not be printed */
  if (NULL != (localname = strrchr (rec->fname, '/')))
    localname++;
  else if (NULL != (localname = strrchr (rec->fname, '\\')))
    localname++;
  else localname = rec->fname;
  /* Printing aligned filename and line number */
  if (rec->linenum == -1)
    snprintf(buf, sizeof (buf), "%s (mark)", localname);
  else
    snprintf(buf, sizeof (buf), "%s (%04d)", localname, rec->linenum);
  buflen = strlen(buf);
  if (buflen < 20)
    {
      memset (buf+buflen, ' ', 20-buflen);
      buf[20] = '\0';
    }
  /* Printing statistics */
  fprintf ((FILE *) arg,
    "%s %7ld uses = %7ld - %7ld | %7ld + %7ld = %7ld b\n",
    buf,
    rec->numalloc - rec->numfree,
    rec->numalloc,
    rec->numfree,
    (long)(rec->prevsize),
    (long)(rec->totalsize - rec->prevsize),
    (long)(rec->totalsize) );
  rec->prevalloc = rec->numalloc;
  rec->prevfree = rec->numfree;
  rec->prevsize = rec->totalsize;
}


static void
mal_printnew (htrecord_t record, void *arg)
{
  malrec_t *rec = (malrec_t *) record;
  if (rec->totalsize != rec->prevsize)
    mal_printall (record, arg);
  else
    {
      rec->prevalloc = rec->numalloc;
      rec->prevfree = rec->numfree;
      rec->prevsize = rec->totalsize;
    }
}


static void
mal_printoneleak (htrecord_t record, void *arg)
{
  malrec_t *rec = (malrec_t *) record;
  char buf[200];
  size_t buflen;
  char *localname;
  if ((rec->totalsize <= rec->prevsize) &&
    ((rec->numalloc - rec->prevalloc) <= (rec->numfree - rec->prevfree)) )
    {
      rec->prevalloc = rec->numalloc;
      rec->prevfree = rec->numfree;
      rec->prevsize = rec->totalsize;
      return;
    }
  /* The path to file should not be printed */
  if (NULL != (localname = strrchr (rec->fname, '/')))
    localname++;
  else if (NULL != (localname = strrchr (rec->fname, '\\')))
    localname++;
  else localname = rec->fname;
  /* Printing aligned filename and line number */
  if (rec->linenum == -1)
    snprintf(buf, sizeof (buf), "%s (mark)", localname);
  else
    snprintf(buf, sizeof (buf), "%s (%4d)", localname, rec->linenum);
  buflen = strlen(buf);
  if (buflen < 20)
    {
      memset (buf+buflen, ' ', 20-buflen);
      buf[20] = '\0';
    }
  /* Printing statistics */
  fprintf ((FILE *) arg,
    "%s%7ld leaks =%7ld -%7ld |%7ld +%7ld =%7ld b\n",
    buf,
    (rec->numalloc - rec->prevalloc) - (rec->numfree - rec->prevfree),
    (rec->numalloc - rec->prevalloc),
    (rec->numfree - rec->prevfree),
    (long)(rec->prevsize),
    (long)(rec->totalsize - rec->prevsize),
    (long)(rec->totalsize) );
  rec->prevalloc = rec->numalloc;
  rec->prevfree = rec->numfree;
  rec->prevsize = rec->totalsize;
}


static u_int
mal_hashfun (htrecord_t record)
{
  malrec_t *rec = (malrec_t *) record;
  char *cp;
  u_int h;

  for (h = 0, cp = rec->fname; *cp; cp++)
    {
      h *= 3;
      h += *cp;
    }
  h ^= rec->linenum;
  h ^= ((rec->linenum) << 16);

  return h;
}


static int
mal_comparefun (htrecord_t recA, htrecord_t recB)
{
  malrec_t *r1 = (malrec_t *) recA;
  malrec_t *r2 = (malrec_t *) recB;
  int i;

  i = r1->linenum - r2->linenum;
  if (i)
    return i;
  return strcmp (r2->fname, r1->fname);
}


static malrec_t *
mal_register (const char *name, u_int line)
{
  malrec_t xrec, *r;

  strncpy (xrec.fname, name, MALREC_FNAME_BUFLEN);
  xrec.fname[MALREC_FNAME_BUFLEN-1] = '\0';
  xrec.linenum = line;

  r = (malrec_t *) dtab_find_record (_dbgtab, 1, (htrecord_t) &xrec);

  if (r == NULL)
    {
      dtab_create_record (_dbgtab, (htrecord_t *) &r);
      strcpy (r->fname, xrec.fname);
      r->linenum = line;
      r->numalloc = r->prevalloc = 0;
      r->numfree = r->prevfree = 0;
      r->totalsize = r->prevsize = 0;
#ifdef MALLOC_DEBUG_2
      memset (r->aaal_malhdrs, 0, sizeof (r->aaal_malhdrs));
      r->aaal_count = 0;
#endif
      dtab_add_record ((htrecord_t) r);
    }

  return r;
}

size_t
dbg_mal_count (const char *name, u_int line)
{
  malrec_t xrec, *r;

  strncpy (xrec.fname, name, MALREC_FNAME_BUFLEN);
  xrec.fname[MALREC_FNAME_BUFLEN-1] = '\0';
  xrec.linenum = line;

  r = (malrec_t *) dtab_find_record (_dbgtab, 1, (htrecord_t) &xrec);
  return r ? r->numalloc - r->numfree : 0;
}

#ifdef DBGMAL_SIGNAL
static RETSIGTYPE
mal_sighandler (int sig)
{
  signal (DBGMAL_SIGNAL, mal_sighandler);
  dbg_malstats (stderr, DBG_MALSTATS_ALL);
}
#endif


static void
mal_init (void)
{
  _dbgmal_mtx = mutex_allocate ();	/* Note - calls dbg_malloc!! */

  dtab_create_table (
	&_dbgtab,
	sizeof(malrec_t),	/* record size */
	1021,			/* init record count */
	1021,			/* record incr */
	NULL, 0,
	NULL);

  dtab_define_key (_dbgtab, mal_hashfun, 1021, mal_comparefun, 1);

#ifdef DBGMAL_SIGNAL
  signal (DBGMAL_SIGNAL, mal_sighandler);
#endif
}


void dbg_malloc_enable(void)
{
  if (_dbgmal_enabled)
    return;
  mal_init();
  _dbgmal_enabled = 1;
}

#ifdef MALLOC_STRESS
#define DBG_MALLOC_HARD_LIMIT_CHECK \
  if (_totalmem > dbg_malloc_hard_memlimit) \
    { \
      fprintf (stderr, "WARNING: running out of memory limit (%ld) on allocation of %ld at %s (%u)\n", \
	  (long) dbg_malloc_hard_memlimit, (long) size, file, line); \
      _totalmem -= size; \
      goto err; \
    }
#define DBG_MALLOC_HIT_LIMIT_CHECK \
  if ((rec == hit_rec) && ((rec->totalsize + size) > hit_totalsize)) \
    { \
      fprintf (stderr, "WARNING: running out of local memory limit (%ld) on allocation of %ld at %s (%u)\n", \
	  (long) (rec->totalsize), (long) size, file, line); \
      _totalmem -= size; \
      goto err; \
    }
#else
#define DBG_MALLOC_HARD_LIMIT_CHECK do { ; } while (0)
#define DBG_MALLOC_HIT_LIMIT_CHECK do { ; } while (0)
#endif

#ifdef MALLOC_DEBUG_2
#define DBG_MALLOC_ADD_TO_CHAIN(rec,data) do { \
    if (SLOW_MALLOC_DEBUG) \
      { \
        void **last_ptr = rec->aaal_malhdrs + ((ptrlong)(data) % AAAL_BUCKETS_COUNT); \
        data->next_malhdr = last_ptr[0]; last_ptr[0] = data; \
        rec->aaal_count++; \
        slow_malloc_debug_uses++; \
      } \
  } while (0)

/* In the absence of slow_malloc_debug */
#define DBG_MALLOC_REMOVE_FROM_CHAIN(rec,data) do { \
    if (SLOW_MALLOC_DEBUG) \
      { \
        void **ptr_to_update = rec->aaal_malhdrs + ((ptrlong)(data) % AAAL_BUCKETS_COUNT); \
        malhdr_t *iter = ptr_to_update[0]; \
        for (;;) \
          { \
            if (NULL == iter) \
              { \
                if (SLOW_MALLOC_DEBUG_PERMANENT) \
                  fprintf (stderr, "\nWARNING: corrupted MALLOC_DEBUG_2 data for allocations at line %u of %s\n", \
                    rec->linenum, rec->fname ); \
                break; \
              } \
            if (iter == data) \
              { \
                ptr_to_update[0] = data->next_malhdr; \
                rec->aaal_count--; \
                break; \
              } \
            ptr_to_update = &(iter->next_malhdr); \
            iter = iter->next_malhdr; \
          } \
      } \
  } while (0)

#else
#define DBG_MALLOC_ADD_TO_CHAIN(rec,data) do { ; } while (0)
#define DBG_MALLOC_REMOVE_FROM_CHAIN(rec,data) do { ; } while (0)
#endif

#define DBG_MALLOC_IMPL(RAW_MALLOC,MAGIC,POOL,MEMSET)  \
do { \
  malhdr_t *data; \
  malrec_t *rec; \
  void *user; \
  if (!_dbgmal_enabled) \
    return RAW_MALLOC; \
  mutex_enter (_dbgmal_mtx); \
  if (size == 0) \
    { \
      fprintf (stderr, "WARNING: allocating 0 bytes in %s (%u)\n", \
	  file, line); \
    } \
  _totalmem += size; \
  DBG_MALLOC_HARD_LIMIT_CHECK; \
  rec = mal_register (file, line); \
  DBG_MALLOC_HIT_LIMIT_CHECK; \
  if ((data = (malhdr_t *) malloc (sizeof (malhdr_t) + size + 4)) == NULL) \
    { \
      fprintf (stderr, "WARNING: malloc(%ld) returned NULL for %s (%u)\n", \
	  (long) size, file, line); \
      goto err; \
    } \
  data->magic = MAGIC; \
  data->origin = rec; \
  data->size = size; \
  data->pool = POOL; \
  data->origin->totalsize += size; \
  data->origin->numalloc++; \
  DBG_MALLOC_ADD_TO_CHAIN(rec,data) ; \
  mutex_leave (_dbgmal_mtx); \
  user = (u_char *) data + sizeof (malhdr_t); \
  MEMSET; \
  ((unsigned char *) user)[size + 0] = 0xDE; \
  ((unsigned char *) user)[size + 1] = 0xAD; \
  ((unsigned char *) user)[size + 2] = 0xC0; \
  ((unsigned char *) user)[size + 3] = 0xDE; \
  return user; \
err: \
  mutex_leave (_dbgmal_mtx); \
  return NULL; \
  } while (0);

#ifndef USE_TLSF
void *
dbg_malloc (const char *file, u_int line, size_t size)
{
  DBG_MALLOC_IMPL (malloc(size), MALMAGIC_OK, NULL, 0)
}
#endif

void *
dbg_realloc (const char *file, u_int line, void *old, size_t size)
{
  void *res;
  if (0 == size)
    {
      if (NULL != old)
        dbg_free (file, line, old);
      return NULL;
    }
  res = dbg_malloc (file, line, size);
  if (NULL != old)
    {
      malhdr_t *mhdr = (malhdr_t *) ((u_char *) old - sizeof (malhdr_t));
      size_t oldsize;
      if (mhdr->magic != MALMAGIC_OK)
        {
          const char *msg = dbg_find_allocation_error (old, NULL);
          fprintf (stderr, "WARNING: free of invalid pointer in %s (%u): %s\n",
	    file, line,
	    msg ? msg : "");
	  _free_invalid++;
          memdbg_abort ();
          return NULL;
        }
      oldsize = mhdr->size;
      memcpy (res, old, ((oldsize > size) ? size : oldsize));
      dbg_free (file, line, old);
    }
  return res;
}

void *
dbg_mallocp (const char *file, u_int line, size_t size, void *pool)
{
  DBG_MALLOC_IMPL (malloc(size), MALPMAGIC_OK, pool, 0)
}


void *
dbg_calloc (const char *file, u_int line, size_t num, size_t size)
{
  size *= num;
  DBG_MALLOC_IMPL (calloc (1, size), MALMAGIC_OK, NULL, memset (user, '\0', size))
}


void *
dbg_callocp (const char *file, u_int line, size_t num, size_t size, void *pool)
{
  size *= num;
  DBG_MALLOC_IMPL (calloc (1, size), MALPMAGIC_OK, pool, memset (user, '\0', size))
}


char *
dbg_strdup (const char *file, u_int line, const char *str)
{
  size_t length = strlen (str) + 1;
  char *tmp = (char *) dbg_malloc (file, line, length);
  memcpy (tmp, str, length);
  return tmp;
}


void
memdbg_abort (void)
{
  if (1 || getenv ("MEMDBG_ABORT"))
#if 0
    abort ();
#else
  *(long*)-1 = -1;
#endif
}

#if 0
#define ERROR_FOUND(err) return NULL;
#else
#define ERROR_FOUND(err) { sprintf err; return buf; }
#endif

const char *dbg_find_allocation_error (void *data, void *expected_pool)
{
  static char buf[0x100];
  malhdr_t *mhdr;
  u_char *cp;
  if (data == NULL)
    ERROR_FOUND((buf, "NULL pointer"));
  if (!_dbgmal_enabled)
    {
      return NULL;
    }
  mhdr = (malhdr_t *) ((u_char *) data - sizeof (malhdr_t));
  if (NULL != expected_pool)
    {
      if (mhdr->magic != MALPMAGIC_OK)
	{
	  if (mhdr->magic == MALMAGIC_OK)
	    return NULL; /*"Pointer to allocated non-pooled buffer, pooled expected";*/
	  if (mhdr->magic == MALMAGIC_FREED)
	    ERROR_FOUND((buf, "Pointer to freed non-pooled buffer"))
	  if (mhdr->magic == MALMAGIC_FREED)
	    ERROR_FOUND((buf, "Pointer to freed pooled buffer"))
	  ERROR_FOUND((buf, "Invalid pointer, magic number not found"))
	}
      if (mhdr->pool != expected_pool)
	ERROR_FOUND((buf, "Pointer to buffer wrom wrong pool"));
    }
  else
    {
      if (mhdr->magic != MALMAGIC_OK)
	{
	  if (mhdr->magic == MALMAGIC_FREED)
	    ERROR_FOUND((buf, "Pointer to freed buffer"))
	  if (mhdr->magic == MALPMAGIC_OK)
	    ERROR_FOUND((buf, "Pointer to pooled buffer"))
	  if (mhdr->magic == MALPMAGIC_FREED)
	    ERROR_FOUND((buf, "Pointer to freed pooled buffer"))
	  ERROR_FOUND((buf, "Invalid pointer, magic number not found"))
	}
    }
  cp = (unsigned char *) data + mhdr->size;
  if (cp[0] != 0xDE || cp[1] != 0xAD || cp[2] != 0xC0 || cp[3] != 0xDE)
    ERROR_FOUND((buf, "Area thrash detected past the end of buffer"))
  return NULL;
}

int dbg_allows_free_nulls = 0;

#ifndef USE_TLSF
void
dbg_free (const char *file, u_int line, void *data)
{
  malhdr_t *mhdr;
  malrec_t *r;
  unsigned char *cp;

  if (data == NULL)
    {
      fprintf (stderr, "WARNING: free of NULL pointer in %s (%u)\n",
	  file, line);
      _free_nulls++;
      if (0 >= dbg_allows_free_nulls)
        memdbg_abort ();
      return;
    }
  if (!_dbgmal_enabled)
    {
      FREE_WITH_DELAY (data);
      return;
    }
  mutex_enter (_dbgmal_mtx);
  mhdr = (malhdr_t *) ((u_char *) data - sizeof (malhdr_t));
  if (mhdr->magic != MALMAGIC_OK)
    {
      const char *msg = dbg_find_allocation_error (data, NULL);
      fprintf (stderr, "WARNING: free of invalid pointer in %s (%u): %s\n",
	file, line,
	 msg ? msg : "");
      _free_invalid++;
      memdbg_abort ();
      mutex_leave (_dbgmal_mtx);
      return;
    }
  mhdr->magic = MALMAGIC_FREED;
  cp = (u_char *) data + mhdr->size;
  if (cp[0] != 0xDE || cp[1] != 0xAD || cp[2] != 0xC0 || cp[3] != 0xDE)
    {
      fprintf (stderr, "WARNING: area thrash detected in %s (%u)\n",
	  file, line);
      memdbg_abort ();
      mutex_leave (_dbgmal_mtx);
      return;
    }
  _totalmem -= mhdr->size;
  r = mhdr->origin;
  DBG_MALLOC_REMOVE_FROM_CHAIN(r,mhdr);
  r->totalsize -= mhdr->size;
  r->numfree++;

/*  if (r->numfree == r->numalloc)
    dtab_delete_record ((htrecord_t *) &r); */

  memset (mhdr + 1, 0xDD, mhdr->size); /* The header remains 'as is' to found the place where the block was allocated */
  FREE_WITH_DELAY (mhdr);
  mutex_leave (_dbgmal_mtx);
}
#endif

void
dbg_free_sized (const char *file, u_int line, void *data, size_t sz)
{
  malhdr_t *mhdr;
  malrec_t *r;
  u_char *cp;

  if (data == NULL)
    {
      fprintf (stderr, "WARNING: free of NULL pointer in %s (%u)\n",
	  file, line);
      _free_nulls++;
      memdbg_abort ();
      return;
    }
  if (!_dbgmal_enabled)
    {
      FREE_WITH_DELAY (data);
      return;
    }
  mutex_enter (_dbgmal_mtx);
  mhdr = (malhdr_t *) ((u_char *) data - sizeof (malhdr_t));
  if (mhdr->magic != MALMAGIC_OK)
    {
      const char *msg = dbg_find_allocation_error (data, NULL);
      fprintf (stderr, "WARNING: free of invalid pointer in %s (%u): %s\n",
	file, line,
	 msg ? msg : "");
      _free_invalid++;
      memdbg_abort ();
      mutex_leave (_dbgmal_mtx);
      return;
    }
  mhdr->magic = MALMAGIC_FREED;
  cp = (unsigned char *) data + mhdr->size;
  if (cp[0] != 0xDE || cp[1] != 0xAD || cp[2] != 0xC0 || cp[3] != 0xDE)
    {
      fprintf (stderr, "WARNING: area thrash detected in %s (%u)\n",
	  file, line);
      memdbg_abort ();
      mutex_leave (_dbgmal_mtx);
      return;
    }
  if ((sz != ((size_t)-1)) && sz != 0x1000000 && ((size_t)(mhdr->size) != sz))
    {
      fprintf (stderr, "WARNING: free of area of actual size %ld with declared size %ld in %s (%u)\n",
        (long)(mhdr->size), (long)sz,
	file, line );
      _free_invalid++;
      memdbg_abort ();
      mutex_leave (_dbgmal_mtx);
      return;
    }
  _totalmem -= mhdr->size;
  r = mhdr->origin;
  DBG_MALLOC_REMOVE_FROM_CHAIN(r,mhdr);
  r->totalsize -= mhdr->size;
  r->numfree++;

/*  if (r->numfree == r->numalloc)
    dtab_delete_record ((htrecord_t *) &r); */

  memset (mhdr + 1, 0xDD, mhdr->size); /* The header remains 'as is' to found the place where the block was allocated */
  FREE_WITH_DELAY (mhdr);
  mutex_leave (_dbgmal_mtx);
}


void
dbg_freep (const char *file, u_int line, void *data, void *pool)
{
  malhdr_t *mhdr;
  malrec_t *r;
  unsigned char *cp;

  if (data == NULL)
    {
      fprintf (stderr, "WARNING: free of NULL pointer in %s (%u)\n",
	  file, line);
      _free_nulls++;
      memdbg_abort ();
      return;
    }
  if (!_dbgmal_enabled)
    {
      FREE_WITH_DELAY (data);
      return;
    }
  mutex_enter (_dbgmal_mtx);
  mhdr = (malhdr_t *) ((u_char *) data - sizeof (malhdr_t));
  if (mhdr->magic != MALPMAGIC_OK)
    {
      const char *err = dbg_find_allocation_error (data, pool);
      if ((NULL == err) && (mhdr->magic == MALMAGIC_OK))
	err = "Pointer to valid non-pool buffer";
      if (!err)
	err = "";
      fprintf (stderr, "WARNING: free of invalid pointer in %s (%u): %s\n",
	file, line, err );
      _free_invalid++;
      memdbg_abort ();
      FREE_WITH_DELAY (data);
      mutex_leave (_dbgmal_mtx);
      return;
    }
  mhdr->magic = MALPMAGIC_FREED;
  cp = (unsigned char *) data + mhdr->size;
  if (cp[0] != 0xDE || cp[1] != 0xAD || cp[2] != 0xC0 || cp[3] != 0xDE)
    {
      fprintf (stderr, "WARNING: area thrash detected in %s (%u)\n",
	  file, line);
      memdbg_abort ();
      mutex_leave (_dbgmal_mtx);
      return;
    }
  _totalmem -= mhdr->size;
  r = mhdr->origin;
  DBG_MALLOC_REMOVE_FROM_CHAIN(r,mhdr);
  r->totalsize -= mhdr->size;
  r->numfree++;

/*    if (r->numfree == r->numalloc)
    dtab_delete_record ((htrecord_t *) &r); */

  memset (mhdr + 1, 0xDD, mhdr->size); /* The header remains 'as is' to found the place where the block was allocated */
  FREE_WITH_DELAY (mhdr);
  mutex_leave (_dbgmal_mtx);
}


void
dbg_malstats (FILE *fd, int mode)
{
  fprintf (fd, "##########################################\n");
  fprintf (fd, "# TOTAL MEMORY IN USE      : %lu\n", (long) _totalmem);
  fprintf (fd, "# Frees of NULL pointer    : %lu\n", (long) _free_nulls);
  fprintf (fd, "# Frees of invalid pointer : %lu\n", (long) _free_invalid);
  fprintf (fd, "##########################################\n");
  switch (mode)
    {
    case DBG_MALSTATS_ALL:
      dtab_foreach (_dbgtab, 0, mal_printall, fd);
      break;
    case DBG_MALSTATS_NEW:
      dtab_foreach (_dbgtab, 0, mal_printnew, fd);
      break;
    case DBG_MALSTATS_LEAKS:
      dtab_foreach (_dbgtab, 0, mal_printoneleak, fd);
      break;
    }
  fprintf (fd, "\n\n");
}


int
dbg_mark (char *name)
{
  malrec_t xrec, *r;

  strncpy (xrec.fname, name, MALREC_FNAME_BUFLEN);
  xrec.fname[MALREC_FNAME_BUFLEN-1] = 0;
  xrec.linenum = -1;

  r = (malrec_t *) dtab_find_record (_dbgtab, 1, (htrecord_t) &xrec);

  if (r == NULL)
    {
      dtab_create_record (_dbgtab, (htrecord_t *) &r);
      strcpy (r->fname, xrec.fname);
      r->linenum = -1;
      r->numalloc = r->numfree = 0;
      r->totalsize = 0;
      dtab_add_record ((htrecord_t) r);
    }

  return ++r->numalloc;
}


int
dbg_unmark (char *name)
{
  malrec_t xrec, *r;

  strncpy (xrec.fname, name, MALREC_FNAME_BUFLEN);
  xrec.fname[MALREC_FNAME_BUFLEN-1] = 0;
  xrec.linenum = -1;

  r = (malrec_t *) dtab_find_record (_dbgtab, 1, (htrecord_t) &xrec);

  if (r != NULL)
    {
      r->numfree++;
      if (r->numfree == r->numalloc)
	{
	  dtab_delete_record ((htrecord_t *) &r);
	  return 1;
	}
      return 0;
    }

  return -1;
}

void dbg_add_dumpentry (htrecord_t rec_, void* file_)
{
  malrec_t* rec = (malrec_t*)rec_;
  if (0 != rec->totalsize)
    fprintf ((FILE*)file_, "file: %s line: %u sz: %ld\n",
	   rec->fname, rec->linenum, (long)(rec->totalsize));
}

void dbg_dump_mem()
{
  FILE* file = fopen ("xmemdump.txt","w+");
  if (file)
    {
      fprintf (file, "Starting memory dumping....\n");
      dtab_foreach (_dbgtab,0,dbg_add_dumpentry, (void*)file);
    };
  fprintf (file, "End of memory dump.\n");
  fclose (file);
}

#ifdef MALLOC_DEBUG_2

typedef struct aaal_saved_item_s {
    malhdr_t *malhdr;
#if 0
    unsigned char data_begin[16];
    size_t size;
    void *pool;
#endif
  } aaal_saved_item_t;


typedef struct aaal_res_s {
  malrec_t *rec;
  int alloc_count;
  int free_count;
  size_t total_size;
  struct aaal_res_s *prev_res;
  long aaal_count;
  aaal_saved_item_t *saved;
  long saved_part_sums[AAAL_BUCKETS_COUNT];
} aaal_res_t;

aaal_res_t *aaal_res_stack = NULL;
int aaal_res_count = 0;

aaal_res_t *
make_aaal_res (malrec_t *r)
{
  int b_ctr;
  long saved_ctr;
  aaal_res_t *res;
  res = dbg_malloc (__FILE__, __LINE__, sizeof (aaal_res_t));
  res->rec = r;
  res->alloc_count = r->numalloc;
  res->free_count =  r->numfree;
  res->total_size = r->totalsize;
  res->aaal_count = r->aaal_count;
  res->saved = dbg_calloc (__FILE__, __LINE__, 1, sizeof (aaal_saved_item_t) * r->aaal_count);
  saved_ctr = 0;
  for (b_ctr = 0; b_ctr < AAAL_BUCKETS_COUNT; b_ctr++)
    {
      malhdr_t *iter;
      for (iter = r->aaal_malhdrs[b_ctr]; NULL != iter; iter = iter->next_malhdr)
        {
          aaal_saved_item_t *sav = res->saved + saved_ctr++;
          sav->malhdr = iter;
#if 0
          memcpy (sav->data_begin, iter + sizeof (malhdr_t), sizeof (sav->data_begin));
          sav->size = iter->size;
          sav->pool = iter->pool;
#endif
        }
      res->saved_part_sums[b_ctr] = saved_ctr;
    }
  if (saved_ctr != r->aaal_count)
    GPF_T1 ("corrupted aaal_malhdrs");
  return res;
}

void
dbg_print_block (malhdr_t *hdr)
{
  int l, ctr;
  printf ("Block at %p length %d memory pool %p |", hdr+1, (int)(hdr->size), hdr->pool);
  l = 16;
  if (l < hdr->size)
    l = hdr->size;
  for (ctr = 0; ctr < l; ctr++)
    printf (" %02x", (int)(((unsigned char *)hdr)[sizeof (malhdr_t) + ctr]));
  printf ((l < hdr->size) ? " ...\n" : "\n");
}

int
all_allocs_at_line (const char *file, int line)
{
  int b_ctr, sample_ctr = 0, sample_count = 10;
  aaal_res_t *res;
  malrec_t xrec, *r;
  strncpy (xrec.fname, file, MALREC_FNAME_BUFLEN);
  xrec.fname[MALREC_FNAME_BUFLEN-1] = 0;
  xrec.linenum = line;
  r = (malrec_t *) dtab_find_record (_dbgtab, 1, (htrecord_t) &xrec);
  if (0 == slow_malloc_debug_uses)
    {
      printf ("The system parameter slow_malloc_debug was never set to 1 so there are no recorded memory allocations\n");
      return 0;
    }
  if (NULL == r)
    {
      printf ("There are no known memory allocations at line %d of file %s\n", line, file);
      return 0;
    }
  printf ("%ld bytes in %ld blocks are allocated at line %d of File %s\n", (long)(r->totalsize), (long)(r->numalloc - r->numfree), line, file);
  if (r->numalloc == r->numfree)
    return 0;
  printf ("There were %ld alloc-s and only %ld free-s,%s %ld recorded allocated blocks are\n",
    (long)(r->numalloc), (long)(r->numfree), ((r->numalloc - r->numfree > r->aaal_count) ? " not all were recorded," : ""), (long)(r->aaal_count) );
  for (b_ctr = 0; (b_ctr < AAAL_BUCKETS_COUNT) && (sample_ctr < sample_count); b_ctr++)
    {
      if (NULL != r->aaal_malhdrs[b_ctr])
        {
          dbg_print_block (r->aaal_malhdrs[b_ctr]);
          sample_ctr++;
        }
    }
  for (b_ctr = 0; (b_ctr < AAAL_BUCKETS_COUNT) && (sample_ctr < sample_count); b_ctr++)
    {
      malhdr_t *iter = r->aaal_malhdrs[b_ctr];
      if (NULL != iter)
        {
          while ((NULL != (iter = iter->next_malhdr)) && (sample_ctr < sample_count))
            {
              dbg_print_block (iter);
              sample_ctr++;
            }
        }
    }
  res = make_aaal_res (r);
  res->prev_res = aaal_res_stack;
  aaal_res_stack = res;
  aaal_res_count++;
  printf ("The full set of allocated blocks is saved as #%d\n", aaal_res_count);
  return aaal_res_count;
}

aaal_res_t *
aaal_get_saved (int res_no)
{
  aaal_res_t *res;
  int ctr;
  if (0 == slow_malloc_debug_uses)
    {
      printf ("The system parameter slow_malloc_debug was never set to 1 so there are no recorded memory allocations\n");
      return NULL;
    }
  if (res_no > aaal_res_count)
    {
      printf ("The are only %d saved sets of allocated blocks, there is no set #%d\n", aaal_res_count, res_no);
      return NULL;
    }
  if (res_no < 0)
    {
      printf ("There is no set #%d, valid numbers are 1 to %d\n", res_no, aaal_res_count);
      return NULL;
    }
  res = aaal_res_stack;
  ctr = aaal_res_count;
  while (ctr > res_no) {ctr--; res = res->prev_res; }
  return res;
}

int
new_allocs_after (int res_no)
{
  int b_ctr;
  int sample_ctr = 0, sample_count = 10;
  aaal_res_t *old_res, *new_res;
  old_res = aaal_get_saved (res_no);
  if (NULL == old_res)
    return 0;
  new_res = make_aaal_res (old_res->rec);
  printf ("%ld bytes in %ld blocks were allocated before at line %d of File %s\n",
    (long)(old_res->total_size), (long)(old_res->alloc_count - old_res->free_count), old_res->rec->linenum, old_res->rec->fname );
  printf ("Now it is %ld bytes in %ld blocks\n",
    (long)(new_res->total_size), (long)(new_res->alloc_count - new_res->free_count) );
  for (b_ctr = 0; (b_ctr < AAAL_BUCKETS_COUNT) && (sample_ctr < sample_count); b_ctr++)
    {
      int old_scan_begin = ((b_ctr > 0) ? old_res->saved_part_sums[b_ctr-1] : 0);
      int new_scan_begin = ((b_ctr > 0) ? new_res->saved_part_sums[b_ctr-1] : 0);
      int old_scan_end = old_res->saved_part_sums[b_ctr];
      int new_scan_end = new_res->saved_part_sums[b_ctr];
      int ctr_in_old, ctr_in_new;
      for (ctr_in_new = new_scan_begin; ctr_in_new < new_scan_end; ctr_in_new++)
        {
          aaal_saved_item_t *new_saved = new_res->saved + ctr_in_new;
          for (ctr_in_old = old_scan_begin; ctr_in_old < old_scan_end; ctr_in_old++)
            {
              aaal_saved_item_t *old_saved = old_res->saved + ctr_in_old;
              if (new_saved->malhdr == old_saved->malhdr)
                goto found_on_old; /* see below */
            }
          dbg_print_block (new_saved->malhdr);
          sample_ctr++;
          if (sample_ctr >= sample_count)
            goto enough; /* see below */
found_on_old: ;
        }
    }
enough: ;
  new_res->prev_res = aaal_res_stack;
  aaal_res_stack = new_res;
  aaal_res_count++;
  printf ("The full set of allocated blocks is saved as #%d\n", aaal_res_count);
  return aaal_res_count;
}

#endif
