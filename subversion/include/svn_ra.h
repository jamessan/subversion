/*
 * svn_ra.h :  structures related to repository access
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#ifndef SVN_RA_H
#define SVN_RA_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_dso.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_wc.h"


/* A vtable structure that encapsulates all the functionality of a
   particular repository-access implementation.

   Note: libsvn_client will keep an array of these objects,
   representing all RA libraries that it has simultaneously loaded
   into memory.  Depending on the situation, the client can look
   through this array and find the appropriate implementation it
   needs. */

typedef struct svn_ra_plugin_t
{
  const char *name;         /* The name of the ra library,
                                 e.g. "ra_dav" or "ra_local" */

  const char *description;  /* Short documentation string */

  /* The vtable hooks */
  
  /* Open a "session" with a repository at URL.  *SESSION_BATON is
     returned and then used (opaquely) for all further interactions
     with the repository. */
  svn_error_t *(*svn_ra_open) (void **session_baton,
                               svn_string_t *repository_URL,
                               apr_pool_t *pool);


  /* Close a repository session. */
  svn_error_t *(*svn_ra_close) (void *session_baton);


  /* Return an *EDITOR and *EDIT_BATON capable of transmitting a
     commit to the repository.  Also, ra's editor must guarantee that
     if close_edit() returns successfully, that *NEW_REVISION will be
     set to the repository's new revision number resulting from the
     commit. */
  svn_error_t *(*svn_ra_get_commit_editor) (void *session_baton,
                                            const svn_delta_edit_fns_t **editor,
                                            void **edit_baton,
                                            svn_revnum_t *new_revision);


  /* Ask the network layer to check out a copy of ROOT_PATH from a
     repository's filesystem, using EDITOR and EDIT_BATON to create a
     working copy. */
  svn_error_t *(*svn_ra_do_checkout) (void *session_baton,
                                      const svn_delta_edit_fns_t *editor,
                                      void *edit_baton,
                                      svn_string_t *root_path);


  /* Ask the network layer to update a working copy from URL.

     The network layer returns a COMMIT_EDITOR and COMMIT_BATON to the
     client; the client then uses it to transmit an empty tree-delta
     to the repository which describes all revision numbers in the
     working copy.

     There is one special property of the COMMIT_EDITOR: its
     close_edit() function.  When the client calls close_edit(), the
     network layer then talks the repository and proceeds to use
     UPDATE_EDITOR and UPDATE_BATON to patch the working copy!  

     When the update_editor->close_edit() returns, then
     commit_editor->close_edit() returns too.  */
  svn_error_t *(*svn_ra_do_update) (void *session_baton,
                                    const svn_delta_edit_fns_t **commit_editor,
                                    void **commit_baton,
                                    const svn_delta_edit_fns_t *update_editor,
                                    void *update_baton);

} svn_ra_plugin_t;




/* The client will keep a private hash that maps
   names->svn_ra_library_t objects. */
typedef struct svn_ra_library_t
{
  const svn_ra_plugin_t *plugin;  /* the vtable to use */
  apr_dso_handle_t *dso;          /* handle on the actual library loaded */

} svn_ra_library_t;


/* libsvn_client will be reponsible for loading each RA DSO it needs.
   However, all "ra_FOO" implmentations *must* export a function named
   `svn_ra_FOO_init()':

      svn_error_t *svn_ra_FOO_init (int abi_version,
                                    svn_ra_plugin_t **plugin);

   When called by libsvn_client, this routine simply returns an
   internal, static plugin structure.  (The client then adds it to its
   ra_library hash.)

*/


#endif  /* SVN_RA_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
