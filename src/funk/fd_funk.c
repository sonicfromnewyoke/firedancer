#include "fd_funk.h"
#include <stdio.h>

ulong
fd_funk_align( void ) {
  return alignof(fd_funk_t);
}

ulong
fd_funk_footprint( void ) {
  return sizeof(fd_funk_t);
}

/* TODO: Consider letter user just passing a join of alloc to use,
   inferring the backing wksp and cgroup_hint from that and then
   allocating exclusively from that? */

void *
fd_funk_new( void * shmem,
             ulong  wksp_tag,
             ulong  seed,
             ulong  txn_max,
             ulong  rec_max ) {
  fd_funk_t * funk = (fd_funk_t *)shmem;

  if( FD_UNLIKELY( !funk ) ) {
    FD_LOG_WARNING(( "NULL funk" ));
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)funk, fd_funk_align() ) ) ) {
    FD_LOG_WARNING(( "misaligned funk" ));
    return NULL;
  }

  if( FD_UNLIKELY( !wksp_tag ) ) {
    FD_LOG_WARNING(( "bad wksp_tag" ));
    return NULL;
  }

  fd_wksp_t * wksp = fd_wksp_containing( funk );
  if( FD_UNLIKELY( !wksp ) ) {
    FD_LOG_WARNING(( "shmem must be part of a workspace" ));
    return NULL;
  }

  if( txn_max>FD_FUNK_TXN_IDX_NULL ) { /* See note in fd_funk.h about this limit */
    FD_LOG_WARNING(( "txn_max too large for index compression" ));
    return NULL;
  }

  void * txn_shmem = fd_wksp_alloc_laddr( wksp, fd_funk_txn_map_align(), fd_funk_txn_map_footprint( txn_max ), wksp_tag );
  if( FD_UNLIKELY( !txn_shmem ) ) {
    FD_LOG_WARNING(( "txn_max too large for workspace" ));
    return NULL;
  }

  void * txn_shmap = fd_funk_txn_map_new( txn_shmem, txn_max, seed );
  if( FD_UNLIKELY( !txn_shmap ) ) {
    FD_LOG_WARNING(( "fd_funk_txn_map_new failed" ));
    fd_wksp_free_laddr( txn_shmem );
    return NULL;
  }

  fd_funk_txn_t * txn_map = fd_funk_txn_map_join( txn_shmap );
  if( FD_UNLIKELY( !txn_map ) ) {
    FD_LOG_WARNING(( "fd_funk_txn_map_join failed" ));
    fd_wksp_free_laddr( fd_funk_txn_map_delete( txn_shmap ) );
    return NULL;
  }

  void * rec_shmem = fd_wksp_alloc_laddr( wksp, fd_funk_rec_map_align(), fd_funk_rec_map_footprint( rec_max ), wksp_tag );
  if( FD_UNLIKELY( !rec_shmem ) ) {
    FD_LOG_WARNING(( "rec_max too large for workspace" ));
    fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( txn_map ) ) );
    return NULL;
  }

  void * rec_shmap = fd_funk_rec_map_new( rec_shmem, rec_max, seed );
  if( FD_UNLIKELY( !rec_shmap ) ) {
    FD_LOG_WARNING(( "fd_funk_rec_map_new failed" ));
    fd_wksp_free_laddr( rec_shmem );
    fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( txn_map ) ) );
    return NULL;
  }

  fd_funk_rec_t * rec_map = fd_funk_rec_map_join( rec_shmap );
  if( FD_UNLIKELY( !rec_map ) ) {
    FD_LOG_WARNING(( "fd_funk_rec_map_join failed" ));
    fd_wksp_free_laddr( fd_funk_rec_map_delete( rec_shmap ) );
    fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( txn_map ) ) );
    return NULL;
  }

  void * alloc_shmem = fd_wksp_alloc_laddr( wksp, fd_alloc_align(), fd_alloc_footprint(), wksp_tag );
  if( FD_UNLIKELY( !alloc_shmem ) ) {
    FD_LOG_WARNING(( "fd_alloc too large for workspace" ));
    fd_wksp_free_laddr( fd_funk_rec_map_delete( fd_funk_rec_map_leave( rec_map ) ) );
    fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( txn_map ) ) );
    return NULL;
  }

  void * alloc_shalloc = fd_alloc_new( alloc_shmem, wksp_tag );
  if( FD_UNLIKELY( !alloc_shalloc ) ) {
    FD_LOG_WARNING(( "fd_alloc_new failed" ));
    fd_wksp_free_laddr( fd_funk_rec_map_delete( fd_funk_rec_map_leave( rec_map ) ) );
    fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( txn_map ) ) );
    return NULL;
  }

  fd_alloc_t * alloc = fd_alloc_join( alloc_shalloc, 0UL ); /* TODO: Consider letting user pass the cgroup hint? */
  if( FD_UNLIKELY( !alloc ) ) {
    FD_LOG_WARNING(( "fd_alloc_join failed" ));
    fd_wksp_free_laddr( fd_alloc_delete( alloc_shalloc ) );
    fd_wksp_free_laddr( fd_funk_rec_map_delete( fd_funk_rec_map_leave( rec_map ) ) );
    fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( txn_map ) ) );
    return NULL;
  }

  fd_memset( funk, 0, fd_funk_footprint() );

  funk->funk_gaddr = fd_wksp_gaddr_fast( wksp, funk );
  funk->wksp_tag   = wksp_tag;
  funk->seed       = seed;
  funk->cycle_tag  = 3UL; /* various verify functions use tags 0-2 */

  funk->txn_max         = txn_max;
  funk->txn_map_gaddr   = fd_wksp_gaddr_fast( wksp, txn_map ); /* Note that this persists the join until delete */
  funk->child_head_cidx = fd_funk_txn_cidx( FD_FUNK_TXN_IDX_NULL );
  funk->child_tail_cidx = fd_funk_txn_cidx( FD_FUNK_TXN_IDX_NULL );

  fd_funk_txn_xid_set_root( funk->root         );
  fd_funk_txn_xid_set_root( funk->last_publish );

  funk->rec_max       = rec_max;
  funk->rec_map_gaddr = fd_wksp_gaddr_fast( wksp, rec_map ); /* Note that this persists the join until delete */
  funk->rec_head_idx  = FD_FUNK_REC_IDX_NULL;
  funk->rec_tail_idx  = FD_FUNK_REC_IDX_NULL;

  funk->alloc_gaddr = fd_wksp_gaddr_fast( wksp, alloc ); /* Note that this persists the join until delete */

  ulong tmp_max;
  fd_funk_partvec_t * partvec = (fd_funk_partvec_t *)fd_alloc_malloc_at_least( alloc, fd_funk_partvec_align(), fd_funk_partvec_footprint(0U), &tmp_max );
  if( FD_UNLIKELY( !partvec ) ) {
    FD_LOG_WARNING(( "partvec alloc failed" ));
    fd_wksp_free_laddr( fd_alloc_delete( alloc_shalloc ) );
    fd_wksp_free_laddr( fd_funk_rec_map_delete( fd_funk_rec_map_leave( rec_map ) ) );
    fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( txn_map ) ) );
    return NULL;
  }
  partvec->num_part = 0U;
  funk->partvec_gaddr = fd_wksp_gaddr_fast( wksp, partvec );

  funk->write_lock = 0UL;

  FD_COMPILER_MFENCE();
  FD_VOLATILE( funk->magic ) = FD_FUNK_MAGIC;
  FD_COMPILER_MFENCE();

  return (void *)funk;
}

fd_funk_t *
fd_funk_join( void * shfunk ) {
  fd_funk_t * funk = (fd_funk_t *)shfunk;

  if( FD_UNLIKELY( !funk ) ) {
    FD_LOG_WARNING(( "NULL shfunk" ));
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)funk, fd_funk_align() ) ) ) {
    FD_LOG_WARNING(( "misaligned shfunk" ));
    return NULL;
  }

  fd_wksp_t * wksp = fd_wksp_containing( funk );
  if( FD_UNLIKELY( !wksp ) ) {
    FD_LOG_WARNING(( "shfunk must be part of a workspace" ));
    return NULL;
  }

  if( FD_UNLIKELY( funk->magic!=FD_FUNK_MAGIC ) ) {
    FD_LOG_WARNING(( "bad magic" ));
    return NULL;
  }

#ifdef FD_FUNK_WKSP_PROTECT
  fd_wksp_mprotect( wksp, 1 );
#endif

  return funk;
}

void *
fd_funk_leave( fd_funk_t * funk ) {

  if( FD_UNLIKELY( !funk ) ) {
    FD_LOG_WARNING(( "NULL funk" ));
    return NULL;
  }

  return (void *)funk;
}

void *
fd_funk_delete( void * shfunk ) {
  fd_funk_t * funk = (fd_funk_t *)shfunk;

  if( FD_UNLIKELY( !funk ) ) {
    FD_LOG_WARNING(( "NULL shfunk" ));
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)funk, fd_funk_align() ) ) ) {
    FD_LOG_WARNING(( "misaligned shfunk" ));
    return NULL;
  }

  fd_wksp_t * wksp = fd_wksp_containing( funk );
  if( FD_UNLIKELY( !wksp ) ) {
    FD_LOG_WARNING(( "shfunk must be part of a workspace" ));
    return NULL;
  }

  if( FD_UNLIKELY( funk->magic!=FD_FUNK_MAGIC ) ) {
    FD_LOG_WARNING(( "bad magic" ));
    return NULL;
  }

  /* Free all value resources here */
  fd_alloc_free( fd_funk_alloc( funk, wksp ), fd_funk_get_partvec( funk, wksp ) );

  fd_wksp_free_laddr( fd_alloc_delete       ( fd_alloc_leave       ( fd_funk_alloc  ( funk, wksp ) ) ) );
  fd_wksp_free_laddr( fd_funk_rec_map_delete( fd_funk_rec_map_leave( fd_funk_rec_map( funk, wksp ) ) ) );
  fd_wksp_free_laddr( fd_funk_txn_map_delete( fd_funk_txn_map_leave( fd_funk_txn_map( funk, wksp ) ) ) );

  FD_COMPILER_MFENCE();
  FD_VOLATILE( funk->magic ) = 0UL;
  FD_COMPILER_MFENCE();

  return funk;
}

int
fd_funk_verify( fd_funk_t * funk ) {

# define TEST(c) do {                                                                           \
    if( FD_UNLIKELY( !(c) ) ) { FD_LOG_WARNING(( "FAIL: %s", #c )); return FD_FUNK_ERR_INVAL; } \
  } while(0)

  TEST( funk );

  /* Test metadata */

  TEST( funk->magic==FD_FUNK_MAGIC );

  ulong funk_gaddr = funk->funk_gaddr;
  TEST( funk_gaddr );
  fd_wksp_t * wksp = fd_funk_wksp( funk );
  TEST( wksp );
  TEST( fd_wksp_laddr_fast( wksp, funk_gaddr )==(void *)funk );
  TEST( fd_wksp_gaddr_fast( wksp, funk       )==funk_gaddr   );

  ulong wksp_tag = fd_funk_wksp_tag( funk );
  TEST( !!wksp_tag );

  ulong seed = funk->seed; /* seed can be anything */

  TEST( funk->cycle_tag>2UL );

  /* Test transaction map */

  ulong txn_max = funk->txn_max;
  TEST( txn_max<=FD_FUNK_TXN_IDX_NULL );

  ulong txn_map_gaddr = funk->txn_map_gaddr;
  TEST( txn_map_gaddr );
  TEST( fd_wksp_tag( wksp, txn_map_gaddr-1UL )==wksp_tag ); /* When txn_max is 0, txn_map_gaddr can be first byte after alloc */
  fd_funk_txn_t * txn_map = fd_funk_txn_map( funk, wksp );
  TEST( txn_map );
  TEST( txn_max==fd_funk_txn_map_key_max( txn_map ) );
  TEST( seed   ==fd_funk_txn_map_seed   ( txn_map ) );

  ulong child_head_idx = fd_funk_txn_idx( funk->child_head_cidx );
  ulong child_tail_idx = fd_funk_txn_idx( funk->child_tail_cidx );

  int null_child_head = fd_funk_txn_idx_is_null( child_head_idx );
  int null_child_tail = fd_funk_txn_idx_is_null( child_tail_idx );

  if( !txn_max ) TEST( null_child_head & null_child_tail );
  else {
    if( null_child_head ) TEST( null_child_tail );
    else                  TEST( child_head_idx<txn_max );

    if( null_child_tail ) TEST( null_child_head );
    else                  TEST( child_tail_idx<txn_max );
  }

  if( !txn_max ) TEST( fd_funk_txn_idx_is_null( child_tail_idx ) );

  fd_funk_txn_xid_t const * root = fd_funk_root( funk );
  TEST( root ); /* Practically guaranteed */
  TEST( fd_funk_txn_xid_eq_root( root ) );

  fd_funk_txn_xid_t * last_publish = funk->last_publish;
  TEST( last_publish ); /* Practically guaranteed */
  /* (*last_publish) only be root at creation and anything but root post
     creation.  But we don't know which situation applies here so this
     could be anything. */

  TEST( !fd_funk_txn_verify( funk ) );

  /* Test record map */

  ulong rec_max = funk->rec_max;
  TEST( rec_max<=FD_FUNK_TXN_IDX_NULL );

  ulong rec_map_gaddr = funk->rec_map_gaddr;
  TEST( rec_map_gaddr );
  TEST( fd_wksp_tag( wksp, rec_map_gaddr-1UL )==wksp_tag ); /* When rec_max is zero, rec_map_gaddr can be first byte after alloc */
  fd_funk_rec_t * rec_map = fd_funk_rec_map( funk, wksp );
  TEST( rec_map );
  TEST( rec_max==fd_funk_rec_map_key_max( rec_map ) );
  TEST( seed   ==fd_funk_rec_map_seed   ( rec_map ) );

  ulong rec_head_idx = funk->rec_head_idx;
  ulong rec_tail_idx = funk->rec_tail_idx;

  int null_rec_head = fd_funk_rec_idx_is_null( rec_head_idx );
  int null_rec_tail = fd_funk_rec_idx_is_null( rec_tail_idx );

  if( !rec_max ) TEST( null_rec_head & null_rec_tail );
  else {
    if( null_rec_head ) TEST( null_rec_tail );
    else                TEST( rec_head_idx<rec_max );

    if( null_rec_tail ) TEST( null_rec_head );
    else                TEST( rec_tail_idx<rec_max );
  }

  if( !rec_max ) TEST( fd_funk_rec_idx_is_null( rec_tail_idx ) );

  TEST( !fd_funk_rec_verify( funk ) );
  TEST( !fd_funk_part_verify( funk ) );

  /* Test values */

  ulong alloc_gaddr = funk->alloc_gaddr;
  TEST( alloc_gaddr );
  TEST( fd_wksp_tag( wksp, alloc_gaddr )==wksp_tag );
  fd_alloc_t * alloc = fd_funk_alloc( funk, wksp );
  TEST( alloc );

  TEST( !fd_funk_val_verify( funk ) );

# undef TEST

  return FD_FUNK_SUCCESS;
}

static char *
fd_smart_size( ulong sz, char * tmp, size_t tmpsz ) {
  if( sz <= (1UL<<7) )
    snprintf( tmp, tmpsz, "%lu B", sz );
  else if( sz <= (1UL<<17) )
    snprintf( tmp, tmpsz, "%.3f KB", ((double)sz/((double)(1UL<<10))) );
  else if( sz <= (1UL<<27) )
    snprintf( tmp, tmpsz, "%.3f MB", ((double)sz/((double)(1UL<<20))) );
  else
    snprintf( tmp, tmpsz, "%.3f GB", ((double)sz/((double)(1UL<<30))) );
  return tmp;
}

void
fd_funk_log_mem_usage( fd_funk_t * funk ) {
  char tmp1[100];
  char tmp2[100];

  FD_LOG_NOTICE(( "funk base footprint: %s",
                  fd_smart_size( fd_funk_footprint(), tmp1, sizeof(tmp1) ) ));
  fd_wksp_t * wksp = fd_funk_wksp( funk );
  fd_funk_txn_t const * txn_map = fd_funk_txn_map( funk, wksp );
  FD_LOG_NOTICE(( "txn table footprint: %s (%lu entries used out of %lu, %lu%%)",
                  fd_smart_size( fd_funk_txn_map_footprint( funk->txn_max ), tmp1, sizeof(tmp1) ),
                  fd_funk_txn_map_key_cnt( txn_map ),
                  fd_funk_txn_map_key_max( txn_map ),
                  (100U*fd_funk_txn_map_key_cnt( txn_map )) / fd_funk_txn_map_key_max( txn_map ) ));
  fd_funk_rec_t * rec_map = fd_funk_rec_map( funk, wksp );
  FD_LOG_NOTICE(( "rec table footprint: %s (%lu entries used out of %lu, %lu%%)",
                  fd_smart_size( fd_funk_rec_map_footprint( funk->rec_max ), tmp1, sizeof(tmp1) ),
                  fd_funk_rec_map_key_cnt( rec_map ),
                  fd_funk_rec_map_key_max( rec_map ),
                  (100U*fd_funk_rec_map_key_cnt( rec_map )) / fd_funk_rec_map_key_max( rec_map ) ));
  ulong val_cnt   = 0;
  ulong val_min   = ULONG_MAX;
  ulong val_max   = 0;
  ulong val_used  = 0;
  ulong val_alloc = 0;
  for( fd_funk_rec_map_iter_t iter = fd_funk_rec_map_iter_init( rec_map );
       !fd_funk_rec_map_iter_done( rec_map, iter );
       iter = fd_funk_rec_map_iter_next( rec_map, iter ) ) {
    fd_funk_rec_t * rec = fd_funk_rec_map_iter_ele( rec_map, iter );
    val_cnt ++;
    val_min = fd_ulong_min( val_min, rec->val_sz );
    val_max = fd_ulong_max( val_max, rec->val_sz );
    val_used += rec->val_sz;
    val_alloc += rec->val_max;
  }
  ulong avg_size = val_cnt ? (val_used / val_cnt) : 0;
  FD_LOG_NOTICE(( "  rec count: %lu, min size: %lu, avg_size: %lu, max_size: %lu, total_size: %s, total_allocated: %s",
                  val_cnt, val_min, avg_size, val_max,
                  fd_smart_size( val_used, tmp1, sizeof(tmp1) ),
                  fd_smart_size( val_alloc, tmp2, sizeof(tmp2) ) ));
  FD_LOG_NOTICE(( "part vec footprint: %s",
                  fd_smart_size( fd_funk_partvec_footprint(0U), tmp1, sizeof(tmp1) ) ));
}

#include "../flamenco/fd_rwlock.h"
static fd_rwlock_t lock[ 1 ] = {0};

void
fd_funk_start_write( fd_funk_t * funk ) {
  fd_rwlock_write( lock );
#ifdef FD_FUNK_WKSP_PROTECT
  fd_wksp_mprotect( fd_funk_wksp( funk ), 0 );
#endif
# if FD_HAS_THREADS
  register ulong oldval;
  for(;;) {
    oldval = funk->write_lock;
    if( FD_LIKELY( FD_ATOMIC_CAS( &funk->write_lock, oldval, oldval+1U) == oldval ) ) break;
    FD_SPIN_PAUSE();
  }
  if( FD_UNLIKELY(oldval&1UL) ) {
     FD_LOG_CRIT(( "attempt to lock funky when it is already locked" ));
  }
  FD_COMPILER_MFENCE();
# else
  (void)funk;
# endif
}

void
fd_funk_end_write( fd_funk_t * funk ) {
# if FD_HAS_THREADS
  FD_COMPILER_MFENCE();
  register ulong oldval;
  for(;;) {
    oldval = funk->write_lock;
    if( FD_LIKELY( FD_ATOMIC_CAS( &funk->write_lock, oldval, oldval+1U) == oldval ) ) break;
    FD_SPIN_PAUSE();
  }
  if( FD_UNLIKELY(!(oldval&1UL)) ) {
    FD_LOG_CRIT(( "attempt to unlock funky when it is already unlocked" ));
  }
# else
  (void)funk;
# endif
#ifdef FD_FUNK_WKSP_PROTECT
  fd_wksp_mprotect( fd_funk_wksp( funk ), 1 );
#endif
  fd_rwlock_unwrite( lock );
}

void
fd_funk_check_write( fd_funk_t * funk ) {
  ulong val = funk->write_lock;
  if( FD_UNLIKELY(!(val&1UL)) ) FD_LOG_CRIT(( "missing call to fd_funk_start_write" ));
}
