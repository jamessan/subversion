/*
 * adm_ops.c: routines for affecting working copy administrative
 *            information.  NOTE: this code doesn't know where the adm
 *            info is actually stored.  Instead, generic handles to
 *            adm data are requested via a reference to some PATH
 *            (PATH being a regular, non-administrative directory or
 *            file in the working copy).
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"



/*** adm area guarantees ***/

/* Make sure that PATH (a directory) contains a complete adm area,
 * based at REPOSITORY.
 *
 * Creates the adm area if none, in which case PATH starts out at
 * revision 0.
 *
 * Note: The adm area's lock-state is not changed by this function,
 * and if the adm area is created, it is left in an unlocked state.
 */
svn_error_t *
svn_wc__ensure_wc (svn_string_t *path,
                   svn_string_t *repository,
                   svn_string_t *ancestor_path,
                   svn_revnum_t ancestor_revision,
                   apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc__ensure_adm (path,
                            repository,
                            ancestor_path,
                            ancestor_revision,
                            pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*** Closing commits. ***/

svn_error_t *
svn_wc_close_commit (svn_string_t *path,
                     svn_revnum_t new_revision,
                     apr_hash_t *targets,
                     apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc__log_commit (path, targets, new_revision, pool);
  if (err)
    return err;

  err = svn_wc__cleanup (path, targets, 0, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}




/* kff todo: not all of these really belong in wc_adm.  Some may get
   broken out into other files later.  They're just here to satisfy
   the public header file that they exist. */

svn_error_t *
svn_wc_rename (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_copy (svn_string_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  /* kff todo */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete_file (svn_string_t *file, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;
  svn_error_t *err;

  svn_path_split (file, &dir, &basename, svn_path_local_style, pool);

  err = svn_wc__entry_merge_sync (dir,
                                  basename,
                                  SVN_INVALID_REVNUM,
                                  svn_node_file,
                                  SVN_WC_ENTRY_DELETE,
                                  0,
                                  0,
                                  pool,
                                  NULL,
                                  NULL);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_file (svn_string_t *file, apr_pool_t *pool)
{
  svn_string_t *dir, *basename;
  svn_error_t *err;

  svn_path_split (file, &dir, &basename, svn_path_local_style, pool);

  err = svn_wc__entry_merge_sync (dir,
                                  basename,
                                  0,
                                  svn_node_file,
                                  SVN_WC_ENTRY_ADD,
                                  0,
                                  0,
                                  pool,
                                  NULL,
                                  NULL);
  if (err)
    return err;

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
