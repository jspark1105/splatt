#ifndef SPLATT_CSF_H
#define SPLATT_CSF_H

#include "base.h"

/******************************************************************************
 * STRUCTURES
 *****************************************************************************/

#if 0
typedef struct
{
  idx_t nfibs[MAX_NMODES];
  idx_t * fptr[MAX_NMODES];
  idx_t * fids[MAX_NMODES];
  val_t * vals;
} csf_sparsity_t;


typedef struct
{
  idx_t nnz;
  idx_t nmodes;
  idx_t dims[MAX_NMODES];
  idx_t dim_perm[MAX_NMODES];

  splatt_tile_t which_tile;
  idx_t ntiles;
  idx_t tile_dims[MAX_NMODES];

  csf_sparsity_t * pt; /** sparsity structure -- one for each tile */
} splatt_csf;

#endif

/* The types of mode ordering available. */
typedef enum
{
  CSF_SORTED_SMALLFIRST, /** sort the modes in non-decreasing order */
  CSF_SORTED_BIGFIRST,   /** sort the modes in non-increasing order */
  CSF_SORTED_MINUSONE    /** one mode is placed first, rest sorted  */
} csf_mode_type;


/**
* @brief Only tile modes at least this depth in the tree.
*        NOTE: 0-indexed! So, depth=1 will tile all but the top level modes.
*/
static idx_t const MIN_TILE_DEPTH = 1;


/******************************************************************************
 * INCLUDES
 *****************************************************************************/

#include "sptensor.h"


/******************************************************************************
 * PUBLIC FUNCTIONS
 *****************************************************************************/
#define make_csf splatt_make_csf
void csf_make(
  splatt_csf * const ct,
  sptensor_t * const tt,
  double const * const opts);

#define csf_alloc splatt_csf_alloc
splatt_csf * splatt_csf_alloc(
  sptensor_t * const tt,
  double const * const opts);

#define csf_free splatt_csf_free
void csf_free(
  splatt_csf * const ct,
  double const * const opts);

#define csf_storage splatt_csf_storage
idx_t csf_storage(
  splatt_csf const * const ct);

#define csf_find_mode_order splatt_csf_find_mode_order
void csf_find_mode_order(
  idx_t const * const dims,
  idx_t const nmodes,
  csf_mode_type which,
  idx_t const mode,
  idx_t * const perm_dims);


#define csf_frobsq splatt_csf_frobsq
val_t csf_frobsq(
  splatt_csf const * const tensor);


/**
* @brief Map a mode (in the input system) to the tree level that it is found.
*        This is equivalent to a linear-time lookup in the inverse dim_perm.
*
* @param mode The mode (relative to the input) to lookup.
* @param perm The dimenison permutation.
* @param nmodes The number of modes.
*
* @return The level of the tree that mode is mapped to.
*/
static inline idx_t csf_mode_depth(
  idx_t const mode,
  idx_t const * const perm,
  idx_t const nmodes)
{
  for(idx_t m=0; m < nmodes; ++m) {
    if(perm[m] == mode) {
      return m;
    }
  }

  /* XXX: ERROR */
  return MAX_NMODES;
}

#endif
