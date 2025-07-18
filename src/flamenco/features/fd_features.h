#ifndef HEADER_fd_src_flamenco_features_fd_features_h
#define HEADER_fd_src_flamenco_features_fd_features_h

#include "../fd_flamenco_base.h"
#include "fd_features_generated.h"
#include "../types/fd_types.h"
/* Macro FEATURE_ID_CNT expands to the number of features in
   fd_features_t. */

//#define FD_FEATURE_ID_CNT (... see generated.h ...)

/* FD_FEATURE_DISABLED is the sentinel value of the feature activation
   slot when the feature has not yet been activated. */

#define FD_FEATURE_DISABLED (ULONG_MAX)

/* Convenience macros for checking features */

#define FD_FEATURE_ACTIVE_(_slot, _features, _feature_name)               ( _slot >= (_features)-> _feature_name )
#define FD_FEATURE_JUST_ACTIVATED_(_slot, _features, _feature_name)       ( _slot == (_features). _feature_name )
#define FD_FEATURE_ACTIVE_OFFSET_(_slot, _features, _offset)              ( _slot >= (_features).f[_offset>>3] )
#define FD_FEATURE_JUST_ACTIVATED_OFFSET_(_slot, _features, _offset)      ( _slot == (_features).f[_offset>>3] )

#define FD_FEATURE_SET_ACTIVE(_features, _feature_name, _slot)            ( (_features)-> _feature_name = _slot )
#define FD_FEATURE_JUST_ACTIVATED(_slot_ctx, _feature_name)               FD_FEATURE_JUST_ACTIVATED_( fd_bank_slot_get( _slot_ctx->bank ), fd_bank_features_get( _slot_ctx->bank ), _feature_name )
#define FD_FEATURE_ACTIVE_OFFSET(_slot, _features, _offset)               FD_FEATURE_ACTIVE_OFFSET_( _slot, _features, _offset )
#define FD_FEATURE_JUST_ACTIVATED_OFFSET(_slot_ctx, _offset)              FD_FEATURE_JUST_ACTIVATED_OFFSET_( fd_bank_slot_get( _slot_ctx->bank ), fd_bank_features_get( _slot_ctx->bank ), _offset )
#define FD_FEATURE_ACTIVE(_slot, _features, _feature_name)                FD_FEATURE_ACTIVE_( _slot, _features, _feature_name )
#define FD_FEATURE_ACTIVE_BANK(_bank, _feature_name)                      FD_FEATURE_ACTIVE_( fd_bank_slot_get( _bank ), fd_bank_features_query( _bank ), _feature_name )


/* fd_features_t is the current set of enabled feature flags.

   Each feature has a corresponding account in the account database,
   which are used to control activation.  This structure contains an
   ulong of the activation slots of each feature for convenience (or
   FD_FEATURE_DISABLED if not yet activated).  The feature params
   contained in this structure change over time, as activated features
   become default, and as new pending feature activations get added.

   Usage:

     fd_features_t * features;

     // Direct API
     ulong activation_slot = features->FEATURE_NAME;

     // Indirect API
     fd_feature_id_t const * id;
     ulong activation_slot = fd_features_get( id );
     ... id->index safe in [0,FD_FEATURE_CNT) ... */

typedef union fd_features fd_features_t;

/* fd_feature_id_t maps a feature ID (account address) to the byte
   offset in fd_features_t. */

struct fd_feature_id {
  ulong        index;                     /* index of feature in fd_features_t */
  fd_pubkey_t  id;                        /* pubkey of feature */
  char const * name;                      /* feature name cstr */
  uint         cleaned_up[3];             /* cleaned_up cluster version for feature */
  uchar        reverted;                  /* if the feature was reverted */
  uchar        activated_on_all_clusters; /* if the feature was activated on all clusters (currently only used for fuzzing) */
};
typedef struct fd_feature_id fd_feature_id_t;

FD_PROTOTYPES_BEGIN

/* fd_feature_ids is the list of known feature IDs.
   The last element has offset==ULONG_MAX. */
extern fd_feature_id_t const ids[];

/* fd_features_disable_all disables all features (cleaned_up or not). */

void
fd_features_disable_all( fd_features_t * f );

/* fd_features_enable_all enables all features (supported or not). */

void
fd_features_enable_all( fd_features_t * );

/* fd_features_enable_cleaned_up enables all features marked as "hard
   coded".  These are features that are baked into the current version
   of the Firedancer software and can't be disabled. */

void
fd_features_enable_cleaned_up( fd_features_t *, fd_cluster_version_t const * );

/* fd_features_enable_one_offs enables all manually passed in features. */

void
fd_features_enable_one_offs( fd_features_t * features,
                             char const * *  one_offs,
                             uint            one_offs_cnt,
                             ulong           slot );

/* fd_feature_iter_{...} is an iterator-style API over all supported
   features in this version of Firedancer.  Usage:

     for( fd_feature_id_t const * id = fd_feature_iter_init();
                                      !fd_feature_iter_done( id );
                                  id = fd_feature_iter_next( id ) ) {
       ...
     } */

static inline fd_feature_id_t const *
fd_feature_iter_init( void ) {
  return ids;
}

static inline int
fd_feature_iter_done( fd_feature_id_t const * id ) {
  return id->index == ULONG_MAX;
}

static inline fd_feature_id_t const *
fd_feature_iter_next( fd_feature_id_t const * id ) {
  return id+1;
}

/* fd_features_set sets the activation slot of the given feature ID. */

static inline void
fd_features_set( fd_features_t *         features,
                 fd_feature_id_t const * id,
                 ulong                   slot ) {
  features->f[ id->index ] = slot;
}

/* fd_features_get returns the activation slot of the given feature ID.
   Returns ULONG_MAX if the feature is not scheduled for activation. */

static inline ulong
fd_features_get( fd_features_t const *   features,
                 fd_feature_id_t const * id ) {
  return features->f[ id->index ];
}

/* fd_feature_id_query queries a feature ID given the first 8 bytes of
   the feature address (little-endian order).  Returns pointer to ID in
   `ids` array on success, or NULL on failure. */

FD_FN_CONST fd_feature_id_t const *
fd_feature_id_query( ulong prefix );

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_flamenco_features_fd_features_h */
