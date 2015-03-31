
/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include "splatt_cmds.h"
#include "../io.h"
#include "../sptensor.h"
#include "../ftensor.h"
#include "../tile.h"
#include "../stats.h"
#include "../cpd.h"
#include "../splatt_mpi.h"


/******************************************************************************
 * PRIVATE FUNCTIONS
 *****************************************************************************/


/**
* @brief Copy global information into local tt, print statistics, and
*        restore local information.
*
* @param tt The tensor to hold global information.
* @param rinfo Global tensor information.
*/
static void __mpi_global_stats(
  sptensor_t * const tt,
  rank_info * const rinfo,
  cpd_opts const * const args)
{
  idx_t * tmpdims = tt->dims;
  idx_t tmpnnz = tt->nnz;
  tt->dims = rinfo->global_dims;
  tt->nnz = rinfo->global_nnz;

  /* print stats */
  stats_tt(tt, args->ifname, STATS_BASIC, 0, NULL);

  /* restore local stats */
  tt->dims = tmpdims;
  tt->nnz = tmpnnz;
}


/******************************************************************************
 * SPLATT CPD
 *****************************************************************************/
static char cpd_args_doc[] = "TENSOR";
static char cpd_doc[] =
  "splatt-cpd -- compute the CPD of a sparse tensor\n\n";

#define TT_TILE 255
static struct argp_option cpd_options[] = {
  {"distribute", 'd', "DIM", 0, "MPI: dimension of data distribution "
                                 "(default: 3)"},
  {"iters", 'i', "NITERS", 0, "number of iterations to use (default: 5)"},
  {"rank", 'r', "RANK", 0, "rank of decomposition to find (default: 10)"},
  {"threads", 't', "NTHREADS", 0, "number of threads to use (default: 1)"},
  {"tile", TT_TILE, 0, 0, "use tiling during SPLATT"},
  { 0 }
};


static error_t parse_cpd_opt(
  int key,
  char * arg,
  struct argp_state * state)
{
  cpd_opts *args = state->input;
  switch(key) {
  case 'd':
    args->distribution = atoi(arg);
    break;
  case 'i':
    args->niters = atoi(arg);
    break;
  case 'r':
    args->rank = atoi(arg);
    break;
  case 't':
    args->nthreads = atoi(arg);
    break;
  case TT_TILE:
    args->tile = 1;
    break;

  case ARGP_KEY_ARG:
    if(args->ifname != NULL) {
      argp_usage(state);
      break;
    }
    args->ifname = arg;
    break;
  case ARGP_KEY_END:
    if(args->ifname == NULL) {
      argp_usage(state);
      break;
    }
  }
  return 0;
}

static struct argp cpd_argp =
  {cpd_options, parse_cpd_opt, cpd_args_doc, cpd_doc};

void splatt_cpd(
  int argc,
  char ** argv)
{
  cpd_opts args;
  args.ifname = NULL;
  args.niters = 5;
  args.rank = 10;
  args.nthreads = 1;
  args.tile = 0;
  args.distribution = 3;

  argp_parse(&cpd_argp, argc, argv, ARGP_IN_ORDER, 0, &args);

  rank_info rinfo;
  rinfo.rank = 0;

  sptensor_t * tt = NULL;
  ftensor_t * ft[MAX_NMODES];

#ifdef SPLATT_USE_MPI
  mpi_setup_comms(&rinfo, args.distribution);
  if(rinfo.npes == 1) {
    fprintf(stderr, "SPLATT: I was configured with MPI support. Please re-run\n"
                    "        with > 1 ranks or recompile without MPI.\n");
    abort();
  }
  tt = mpi_tt_read(args.ifname, &rinfo);
  /* print stats */
  if(rinfo.rank == 0) {
    print_header();
    __mpi_global_stats(tt, &rinfo, &args);
  }

  /* determine matrix distribution - this also calls tt_remove_empty() */
  permutation_t * perm = mpi_distribute_mats(&rinfo, tt, args.distribution);

  printf("distributed\n");

  /* 1D and 2D distributions require filtering because tt has nonzeros that
   * don't belong in each ftensor */
  if(args.distribution == 1) {
    sptensor_t * tt_filtered = tt_alloc(tt->nnz, tt->nmodes);
    for(idx_t m=0; m < tt->nmodes; ++m) {
      /* tt has more nonzeros than any of the modes actually need, so we need
       * to filter them first. */
      mpi_filter_tt_1d(m, tt, tt_filtered, rinfo.layer_starts[m],
          rinfo.layer_ends[m]);

      /* compress tensor to own local coordinate system */
      tt_remove_empty(tt_filtered);

      mpi_write_part(tt_filtered, perm, &rinfo);
      return;

      /* index into local tensor to grab owned rows */
      for(idx_t m=0; m < tt->nmodes; ++m) {
        rinfo.ownstart[m] = 0;
        rinfo.ownend[m] = tt_filtered->dims[m];
        rinfo.nowned[m] = tt_filtered->dims[m];

        /* sanity check to ensure owned rows are contiguous */
        idx_t const * const indmap = tt_filtered->indmap[m];
        if(indmap != NULL) {
          idx_t const start = rinfo.mat_start[m];
          idx_t const end   = rinfo.mat_end[m];
          for(idx_t i=0; i < tt_filtered->dims[m]; ++i) {
            assert(indmap[i] >= start && indmap[i] < end);
            //assert(indmap[i] == indmap[i-1]+1);
            if(indmap[i] != indmap[i-1]+1) {
              printf("%lu != %lu\n", indmap[i], indmap[i-1]+1);
            }
          }
        }
      }
    } /* foreach mode */

    tt_free(tt_filtered);

  /* 3D distribution is simpler */
  } else {
    /* compress tensor to own local coordinate system */
    tt_remove_empty(tt);

    /* index into local tensor to grab owned rows */
    mpi_find_owned(tt, &rinfo);

    /* determine isend and ineed lists */
    mpi_compute_ineed(&rinfo, tt, args.rank);
  }

  return;

#else
  tt = tt_read(args.ifname);
  print_header();
  stats_tt(tt, args.ifname, STATS_BASIC, 0, NULL);

  tt_remove_empty(tt);
#endif

  /* fill each ftensor */
  for(idx_t m=0; m < tt->nmodes; ++m) {
    ft[m] = ften_alloc(tt, m, args.tile);
  }
  tt_free(tt);

  /* allocate / initialize matrices */
  idx_t max_dim = 0;
  /* M, the result matrix is stored at mats[MAX_NMODES] */
  matrix_t * mats[MAX_NMODES+1];
  matrix_t * globmats[MAX_NMODES];
  for(idx_t m=0; m < ft[0]->nmodes; ++m) {
    mats[m] = mat_rand(ft[0]->dims[m], args.rank);
    if(ft[0]->dims[m] > max_dim) {
      max_dim = ft[0]->dims[m];
    }
#ifdef SPLATT_USE_MPI
    /* for actual factor matrix */
    globmats[m] = mat_rand(rinfo.mat_end[m] - rinfo.mat_start[m], args.rank);
#else
    globmats[m] = mats[m];
#endif
  }
  mats[MAX_NMODES] = mat_alloc(max_dim, args.rank);

  val_t * lambda = (val_t *) malloc(args.rank * sizeof(val_t));

  if(rinfo.rank == 0) {
    printf("Factoring "
           "------------------------------------------------------\n");
    printf("NFACTORS=%"SS_IDX" MAXITS=%"SS_IDX" ", args.rank, args.niters);
#ifdef SPLATT_USE_MPI
    printf("RANKS=%d ", rinfo.npes);
#endif
    printf("THREADS=%"SS_IDX" ", args.nthreads);
    if(args.tile == 1) {
      printf("TILE=%"SS_IDX"x%"SS_IDX"x%"SS_IDX" ",
        TILE_SIZES[0], TILE_SIZES[1], TILE_SIZES[2]);
    } else {
      printf("TILE=NO ");
    }
    printf("\n");
  }

  /* do the factorization! */
  cpd(ft, mats, globmats, lambda, &rinfo, &args);

  idx_t const nmodes = ft[0]->nmodes;
  for(idx_t m=0;m < nmodes; ++m) {
    ften_free(ft[m]);
    mat_free(mats[m]);
#ifdef SPLATT_USE_MPI
    mat_free(globmats[m]);
#endif
  }
  mat_free(mats[MAX_NMODES]);
  free(lambda);

#ifdef SPLATT_USE_MPI
  /* write output */
  //mpi_write_mats(globmats, perm, &rinfo, "test", nmodes);
  perm_free(perm);
  rank_free(rinfo, nmodes);
#endif
}

