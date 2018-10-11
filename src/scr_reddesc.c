/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-411039.
 * All rights reserved.
 * This file is part of The Scalable Checkpoint / Restart (SCR) library.
 * For details, see https://sourceforge.net/projects/scalablecr/
 * Please also read this file: LICENSE.TXT.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mpi.h"

#include "kvtree.h"
#include "kvtree_util.h"
#include "spath.h"
#include "er.h"

#include "scr_globals.h"

/*
=========================================
Redundancy descriptor functions
=========================================
*/

/* initialize the specified redundancy descriptor */
int scr_reddesc_init(scr_reddesc* d)
{
  /* check that we got a valid redundancy descriptor */
  if (d == NULL) {
    scr_err("No redundancy descriptor to fill from hash @ %s:%d",
      __FILE__, __LINE__
    );
    return SCR_FAILURE;
  }

  /* initialize the descriptor */
  d->enabled        =  0;
  d->index          = -1;
  d->interval       = -1;
  d->output         = -1;
  d->store_index    = -1;
  d->group_index    = -1;
  d->base           = NULL;
  d->directory      = NULL;
  d->copy_type      = SCR_COPY_NULL;
  d->er_scheme      = -1;

  return SCR_SUCCESS;
}

/* free any memory associated with the specified redundancy
 * descriptor */
int scr_reddesc_free(scr_reddesc* d)
{
  /* free the strings we strdup'd */
  scr_free(&d->base);
  scr_free(&d->directory);

  /* free off ER scheme resources */
  if (d->er_scheme != -1) {
    ER_Free_Scheme(d->er_scheme);
  }

  return SCR_SUCCESS;
}

/* given a checkpoint id and a list of redundancy descriptors,
 * select and return a pointer to a descriptor for the specified id */
scr_reddesc* scr_reddesc_for_checkpoint(
  int id,
  int ndescs,
  scr_reddesc* descs)
{
  scr_reddesc* d = NULL;

  /* pick the redundancy descriptor that is:
   *   1) enabled
   *   2) has the highest interval that evenly divides id */
  int i;
  int interval = 0;
  for (i=0; i < ndescs; i++) {
    if (descs[i].enabled &&
        interval < descs[i].interval &&
        id % descs[i].interval == 0)
    {
      d = &descs[i];
      interval = descs[i].interval;
    }
  }

  return d;
}

/* convert the specified redundancy descritpor into a corresponding
 * hash */
int scr_reddesc_store_to_hash(const scr_reddesc* d, kvtree* hash)
{
  /* check that we got a valid pointer to a redundancy descriptor and
   * a hash */
  if (d == NULL || hash == NULL) {
    return SCR_FAILURE;
  }

  /* clear the hash */
  kvtree_unset_all(hash);

  /* set the ENABLED key */
  kvtree_set_kv_int(hash, SCR_CONFIG_KEY_ENABLED, d->enabled);

  /* we don't set the INDEX because this is dependent on runtime
   * environment */

  /* set the INTERVAL key */
  kvtree_set_kv_int(hash, SCR_CONFIG_KEY_INTERVAL, d->interval);

  /* set the OUTPUT key */
  kvtree_set_kv_int(hash, SCR_CONFIG_KEY_OUTPUT, d->output);

  /* we don't set STORE_INDEX because this is dependent on runtime
   * environment */

  /* we don't set GROUP_INDEX because this is dependent on runtime
   * environment */

  /* set the STORE key */
  if (d->base != NULL) {
    kvtree_set_kv(hash, SCR_CONFIG_KEY_STORE, d->base);
  }

  /* set the DIRECTORY key */
  if (d->directory != NULL) {
    kvtree_set_kv(hash, SCR_CONFIG_KEY_DIRECTORY, d->directory);
  }

  /* set the TYPE key */
  switch (d->copy_type) {
  case SCR_COPY_SINGLE:
    kvtree_set_kv(hash, SCR_CONFIG_KEY_TYPE, "SINGLE");
    break;
  case SCR_COPY_PARTNER:
    kvtree_set_kv(hash, SCR_CONFIG_KEY_TYPE, "PARTNER");
    break;
  case SCR_COPY_XOR:
    kvtree_set_kv(hash, SCR_CONFIG_KEY_TYPE, "XOR");
    break;
  }

  return SCR_SUCCESS;
}

/* convert copy type string to integer value */
static int scr_reddesc_type_int_from_str(const char* value, int* type)
{
  int rc = SCR_SUCCESS;

  int copy_type;
  if (strcasecmp(value, "SINGLE") == 0) {
    copy_type = SCR_COPY_SINGLE;
  } else if (strcasecmp(value, "PARTNER") == 0) {
    copy_type = SCR_COPY_PARTNER;
  } else if (strcasecmp(value, "XOR") == 0) {
    copy_type = SCR_COPY_XOR;
  } else {
    if (scr_my_rank_world == 0) {
      scr_warn("Unknown copy type %s @ %s:%d",
        value, __FILE__, __LINE__
      );
    }
    rc = SCR_FAILURE;
  }

  *type = copy_type;
  return rc;
}

/* build a redundancy descriptor corresponding to the specified hash,
 * this function is collective */
int scr_reddesc_create_from_hash(
  scr_reddesc* d,
  int index,
  const kvtree* hash)
{
  int rc = SCR_SUCCESS;

  /* check that we got a valid redundancy descriptor */
  if (d == NULL) {
    scr_err("No redundancy descriptor to fill from hash @ %s:%d",
      __FILE__, __LINE__
    );
    rc = SCR_FAILURE;
  }

  /* check that we got a valid pointer to a hash */
  if (hash == NULL) {
    scr_err("No hash specified to build redundancy descriptor from @ %s:%d",
      __FILE__, __LINE__
    );
    rc = SCR_FAILURE;
  }

  /* check that everyone made it this far */
  if (! scr_alltrue(rc == SCR_SUCCESS, scr_comm_world)) {
    if (d != NULL) {
      d->enabled = 0;
    }
    return SCR_FAILURE;
  }

  /* initialize the descriptor */
  scr_reddesc_init(d);

  /* enable / disable the descriptor */
  d->enabled = 1;
  kvtree_util_get_int(hash, SCR_CONFIG_KEY_ENABLED, &(d->enabled));

  /* index of the descriptor */
  d->index = index;

  /* set the interval, default to 1 unless specified otherwise */
  d->interval = 1;
  kvtree_util_get_int(hash, SCR_CONFIG_KEY_INTERVAL, &(d->interval));

  /* set output flag, assume this can't be used for output */
  d->output = 0;
  kvtree_util_get_int(hash, SCR_CONFIG_KEY_OUTPUT, &(d->output));

  /* get the store name */
  char* base = scr_cache_base;
  kvtree_util_get_str(hash, SCR_CONFIG_KEY_STORE, &base);
  if (base != NULL) {
    /* strdup base after reducing it */
    d->base = spath_strdup_reduce_str(base);

    /* set the index to the store descriptor for this base directory */
    int store_index = scr_storedescs_index_from_name(d->base);
    if (store_index >= 0) {
      d->store_index = store_index;
    } else {
      /* couldn't find requested store, disable this descriptor and
       * warn user */
      d->enabled = 0;
      scr_warn("Failed to find store descriptor named %s @ %s:%d",
        d->base, __FILE__, __LINE__
      );
    }
  } else {
    /* couldn't find requested store, disable this descriptor and
     * warn user */
    d->enabled = 0;
    scr_warn("Failed to find store parameter for redundancy descriptor @ %s:%d",
      __FILE__, __LINE__
    );
  }

  /* build the directory name */
  spath* dir = spath_from_str(d->base);
  spath_append_str(dir, scr_username);
  spath_append_strf(dir, "scr.%s", scr_jobid);
//  spath_append_strf(dir, "index.%d", d->index);
  spath_reduce(dir);
  d->directory = spath_strdup(dir);
  spath_delete(&dir);
    
  /* set the xor set size */
  int set_size = scr_set_size;
  kvtree_util_get_int(hash, SCR_CONFIG_KEY_SET_SIZE, &set_size);

  /* read the redundancy scheme type from the hash */
  char* type;
  d->copy_type = scr_copy_type;
  if (kvtree_util_get_str(hash, SCR_CONFIG_KEY_TYPE, &type) == KVTREE_SUCCESS)
  {
    if (scr_reddesc_type_int_from_str(type, &d->copy_type) != SCR_SUCCESS) {
      /* don't recognize copy type, disable this descriptor */
      d->enabled = 0;
      if (scr_my_rank_world == 0) {
        scr_warn("Unknown copy type %s in redundancy descriptor %d, disabling @ %s:%d",
          type, d->index, __FILE__, __LINE__
        );
      }
    }
  }

  /* CONVENIENCE: if all ranks are on the same node, change
   * type to SINGLE, we do this so single-node jobs can run without
   * requiring the user to change the copy type */
  const scr_groupdesc* groupdesc = scr_groupdescs_from_name(SCR_GROUP_NODE);
  if (groupdesc != NULL && groupdesc->ranks == scr_ranks_world) {
    if (scr_my_rank_world == 0) {
      if (d->copy_type != SCR_COPY_SINGLE) {
        /* print a warning if we changed things on the user */
        scr_warn("Forcing copy type to SINGLE in redundancy descriptor %d @ %s:%d",
          d->index, __FILE__, __LINE__
        );
      }
    }
    d->copy_type = SCR_COPY_SINGLE;
  }

  /* read the group name */
  char* groupname = scr_group;
  kvtree_util_get_str(hash, SCR_CONFIG_KEY_GROUP, &groupname);

  /* get group descriptor */
  groupdesc = scr_groupdescs_from_name(groupname);

  /* define a string for our failure group, use global rank
   * for leader of group communicator */
  char* failure_domain = NULL;
  if (groupdesc->rank == 0) {
    char rankstr[128];
    snprintf(rankstr, sizeof(rankstr), "%d", scr_my_rank_world);
    failure_domain = strdup(rankstr);
  }
  scr_str_bcast(&failure_domain, 0, groupdesc->comm);

  /* build the communicator based on the copy type
   * and other parameters */
  d->er_scheme = -1;
  switch (d->copy_type) {
  case SCR_COPY_SINGLE:
    d->er_scheme = ER_Create_Scheme(scr_comm_world, failure_domain, scr_ranks_world, 0);
    break;
  case SCR_COPY_PARTNER:
    d->er_scheme = ER_Create_Scheme(scr_comm_world, failure_domain, scr_ranks_world, scr_ranks_world);
    break;
  case SCR_COPY_XOR:
    d->er_scheme = ER_Create_Scheme(scr_comm_world, failure_domain, scr_ranks_world, 1);
    break;
  }

  /* free failure domain string */
  scr_free(&failure_domain);

  /* disable descriptor if we failed to build a scheme */
  if (d->er_scheme == -1) {
    d->enabled = 0;
  }

  /* if anyone has disabled this, everyone needs to */
  if (! scr_alltrue(d->enabled, scr_comm_world)) {
    d->enabled = 0;
  }

  return SCR_SUCCESS;
}

/* return pointer to store descriptor associated with redundancy
 * descriptor, returns NULL if reddesc or storedesc is not enabled */
scr_storedesc* scr_reddesc_get_store(const scr_reddesc* desc)
{
  /* verify that our redundancy descriptor is valid */
  if (desc == NULL) {
    return NULL;
  }

  /* check that redudancy descriptor is enabled */
  if (! desc->enabled) {
    return NULL;
  }

  /* check that its store index is within range */
  int index = desc->store_index;
  if (index < 0 || index >= scr_nstoredescs) {
    return NULL;
  }

  /* check that the store descriptor is enabled */
  scr_storedesc* store = &scr_storedescs[index];
  if (! store->enabled) {
    return NULL;
  }

  /* finally, all is good, return the address of the store descriptor */
  return store;
}

/* define prefix to ER files given the hidden dataset directory */
static char* scr_reddesc_prefix(const char* dir)
{
  spath* path = spath_from_str(dir);
  spath_append_str(path, "reddesc");
  char* prefix = spath_strdup(path);
  spath_delete(&path);
  return prefix;
}

/* apply redundancy scheme to file and return number of bytes copied
 * in bytes parameter */
int scr_reddesc_apply(
  scr_filemap* map,
  const scr_reddesc* desc,
  int id,
  double* bytes)
{
  /* initialize to 0 */
  *bytes = 0.0;

  /* get store descriptor for this redudancy scheme */
  scr_storedesc* store = scr_reddesc_get_store(desc);

  /* define path for hidden directory */
  const char* dir_hidden = scr_cache_dir_hidden_get(desc, id);

  /* define path to er files */
  char* reddesc_dir = scr_reddesc_prefix(dir_hidden);

  /* create ER set */
  int set_id = ER_Create(scr_comm_world, store->comm, reddesc_dir, ER_DIRECTION_ENCODE, desc->er_scheme);
  if (set_id < 0) {
    scr_err("Failed to create ER set @ %s:%d",
            __FILE__, __LINE__
    );
  }

  /* free directory path strings */
  scr_free(&reddesc_dir);
  scr_free(&dir_hidden);
 
  /* step through each of my files for the specified dataset
   * to scan for any incomplete files */
  int valid = 1;
  double my_bytes = 0.0;
  kvtree_elem* file_elem;
  for (file_elem = scr_filemap_first_file(map);
       file_elem != NULL;
       file_elem = kvtree_elem_next(file_elem))
  {
    /* get the filename */
    char* file = kvtree_elem_key(file_elem);

    /* check the file */
    if (! scr_bool_have_file(map, file)) {
      scr_dbg(2, "File determined to be invalid: %s", file);
      valid = 0;
    }

    /* add file to the set */
    if (ER_Add(set_id, file) != ER_SUCCESS) {
      scr_err("Failed to add file to ER set: %s @ %s:%d", file, __FILE__, __LINE__);
      valid = 0;
    }

    /* add up the number of bytes on our way through */
    my_bytes += (double) scr_file_size(file);

    /* if crc_on_copy is set, compute crc and update meta file
     * (PARTNER does this during the copy) */
    if (scr_crc_on_copy && desc->copy_type != SCR_COPY_PARTNER) {
      scr_compute_crc(map, file);
    }
  }

  /* include filemap as protected file */
  spath* mapfile_path = scr_cache_get_map_path(scr_cindex, id);
  const char* mapfile_str = spath_strdup(mapfile_path);
  if (ER_Add(set_id, mapfile_str) != ER_SUCCESS) {
    scr_err("Failed to add map file to ER set: %s @ %s:%d", mapfile_str, __FILE__, __LINE__);
    valid = 0;
  }
  scr_free(&mapfile_str);
  spath_delete(&mapfile_path);

  /* determine whether everyone's files are good */
  int all_valid = scr_alltrue(valid, scr_comm_world);
  if (! all_valid) {
    if (scr_my_rank_world == 0) {
      scr_dbg(1, "Exiting copy since one or more checkpoint files is invalid");
    }
    ER_Free(set_id);
    return SCR_FAILURE;
  }

  /* start timer */
  time_t timestamp_start;
  double time_start;
  if (scr_my_rank_world == 0) {
    timestamp_start = scr_log_seconds();
    time_start = MPI_Wtime();
  }

  /* apply the redundancy scheme */
  int rc = SCR_SUCCESS;
  if (ER_Dispatch(set_id) != ER_SUCCESS) {
    scr_err("ER_Dispatch failed @ %s:%d", __FILE__, __LINE__);
    rc = SCR_FAILURE;
  }
  if (ER_Wait(set_id) != ER_SUCCESS) {
    scr_err("ER_Wait failed @ %s:%d", __FILE__, __LINE__);
    rc = SCR_FAILURE;
  }
  if (ER_Free(set_id) != ER_SUCCESS) {
    scr_err("ER_Free failed @ %s:%d", __FILE__, __LINE__);
    rc = SCR_FAILURE;
  }

  /* determine whether everyone succeeded in their copy */
  int valid_copy = (rc == SCR_SUCCESS);
  if (! valid_copy) {
    scr_err("scr_copy_files failed with return code %d @ %s:%d",
            rc, __FILE__, __LINE__
    );
  }
  int all_valid_copy = scr_alltrue(valid_copy, scr_comm_world);
  rc = all_valid_copy ? SCR_SUCCESS : SCR_FAILURE;

  /* add up total number of bytes */
  MPI_Allreduce(&my_bytes, bytes, 1, MPI_DOUBLE, MPI_SUM, scr_comm_world);

  /* stop timer and report performance info */
  if (scr_my_rank_world == 0) {
    double time_end = MPI_Wtime();
    double time_diff = time_end - time_start;
    double bw = *bytes / (1024.0 * 1024.0 * time_diff);
    scr_dbg(1, "scr_reddesc_apply: %f secs, %e bytes, %f MB/s, %f MB/s per proc",
            time_diff, *bytes, bw, bw/scr_ranks_world
    );

    /* log data on the copy in the database */
    if (scr_log_enable) {
      char* dir = scr_cache_dir_get(desc, id);
      scr_log_transfer("COPY", desc->base, dir, &id, &timestamp_start, &time_diff, bytes);
      scr_free(&dir);
    }
  }

  return rc;
}

/* rebuilds files for specified dataset id using specified redundancy descriptor,
 * adds them to filemap, and returns SCR_SUCCESS if all processes succeeded */
int scr_reddesc_recover(scr_cache_index* cindex, int id, const char* dir)
{
  int rc = SCR_SUCCESS;

  /* get store descriptor for this redudancy scheme */
  int store_index = scr_storedescs_index_from_child_path(dir);

  /* TODO: verify that everyone found a matching store descriptor */
  scr_storedesc* store = &scr_storedescs[store_index];

  /* build prefix for reddesc files */
  char* reddesc_dir = scr_reddesc_prefix(dir);

  /* create ER set */
  int set_id = ER_Create(scr_comm_world, store->comm, reddesc_dir, ER_DIRECTION_REBUILD, 0);

  scr_free(&reddesc_dir);

  if (ER_Dispatch(set_id) != ER_SUCCESS) {
    rc = SCR_FAILURE;
  }

  if (ER_Wait(set_id) != ER_SUCCESS) {
    rc = SCR_FAILURE;
  }

  ER_Free(set_id);

  return rc;
}

/* remove redundancy files added during scr_reddesc_apply */
int scr_reddesc_unapply(const scr_cache_index* cindex, int id, const char* dir)
{
  int rc = SCR_SUCCESS;

  /* get store descriptor for this redudancy scheme */
  int store_index = scr_storedescs_index_from_child_path(dir);

  /* TODO: verify that everyone found a matching store descriptor */
  scr_storedesc* store = &scr_storedescs[store_index];

  /* build prefix for reddesc files */
  char* reddesc_dir = scr_reddesc_prefix(dir);

  /* create ER set */
  int set_id = ER_Create(scr_comm_world, store->comm, reddesc_dir, ER_DIRECTION_REMOVE, 0);
  if (set_id < 0) {
    rc = SCR_FAILURE;
  }

  scr_free(&reddesc_dir);

  if (ER_Dispatch(set_id) != ER_SUCCESS) {
    rc = SCR_FAILURE;
  }

  if (ER_Wait(set_id) != ER_SUCCESS) {
    rc = SCR_FAILURE;
  }

  if (ER_Free(set_id) != ER_SUCCESS) {
    rc = SCR_FAILURE;
  }

  return rc;
}

/*
=========================================
Routines that operate on scr_reddescs array
=========================================
*/

/* create scr_reddescs array from scr_reddescs_hash */
int scr_reddescs_create()
{
  /* set the number of redundancy descriptors */
  scr_nreddescs = 0;
  kvtree* descs = kvtree_get(scr_reddesc_hash, SCR_CONFIG_KEY_CKPTDESC);
  if (descs != NULL) {
    scr_nreddescs = kvtree_size(descs);
  }

  /* allocate our redundancy descriptors */
  scr_reddescs = (scr_reddesc*) SCR_MALLOC(scr_nreddescs * sizeof(scr_reddesc));

  /* flag to indicate whether we successfully build all redundancy
   * descriptors */
  int all_valid = 1;

  /* sort the hash to ensure we step through all elements in the same
   * order on all procs */
  kvtree_sort(descs, KVTREE_SORT_ASCENDING);

  /* iterate over each of our hash entries filling in each
   * corresponding descriptor, have rank 0 determine the
   * order in which we'll create the descriptors */
  int index = 0;
  kvtree_elem* elem;
  for (elem = kvtree_elem_first(descs);
       elem != NULL;
       elem = kvtree_elem_next(elem))
  {
    /* select redundancy descriptor name on rank 0 */
    char* name = kvtree_elem_key(elem);

    /* get the info hash for this descriptor */
    kvtree* hash = kvtree_get(descs, name);

    /* create descriptor */
    if (scr_reddesc_create_from_hash(&scr_reddescs[index], index, hash)
        != SCR_SUCCESS)
    {
      if (scr_my_rank_world == 0) {
        scr_err("Failed to set up %s=%s @ %s:%f",
          SCR_CONFIG_KEY_CKPTDESC, name, __FILE__, __LINE__
        );
      }
      all_valid = 0;
    }

    /* advance to our next descriptor */
    index++;
  }

  /* determine whether everyone found a valid redundancy descriptor */
  if (! all_valid) {
    return SCR_FAILURE;
  }
  return SCR_SUCCESS;
}

/* free scr_reddescs array */
int scr_reddescs_free()
{
  /* iterate over and free each of our redundancy descriptors */
  if (scr_nreddescs > 0 && scr_reddescs != NULL) {
    int i;
    for (i=0; i < scr_nreddescs; i++) {
      scr_reddesc_free(&scr_reddescs[i]);
    }
  }

  /* set the count back to zero */
  scr_nreddescs = 0;

  /* and free off the memory allocated */
  scr_free(&scr_reddescs);

  return SCR_SUCCESS;
}
