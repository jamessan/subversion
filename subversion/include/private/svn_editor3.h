/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file svn_editor3.h
 * @brief Tree editing
 *
 * @since New in 1.10.
 */

#ifndef SVN_EDITOR3_H
#define SVN_EDITOR3_H

#include "svn_editor.h"

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"    /* for svn_stream_t  */
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*
 * ### Under construction. Currently, two kinds of editor interface are
 *     declared within the same "svn_editor3_t" framework. This is for
 *     experimentation, and not intended to stay that way.
 */

/*
 * ===================================================================
 * Possible contexts (uses) for an editor
 * ===================================================================
 *
 * (1) Commit
 *
 *   - From single-rev or mixed-rev;
 *       need to tell the receiver the "from" revision(s)
 *   - To single-rev (implied new head revision)
 *   - Diff: with simple context (for simple merge with recent commits)
 *   - Copies: can send O(1) "copy"
 *       with O(E) edits inside; E ~ size of edits
 *   - Copies: can copy from within the new rev (?)
 *
 * Commit is logically the same whether from a WC or "direct". In either
 * case the client has to have an idea of what it is basing its changes
 * on, and tell the server so that the server can perform its Out-Of-Date
 * checks. This base could potentially be mixed-revision. A non-WC commit
 * is typically unlikely to work from a mixed-rev base, but logically it
 * is possible. An O(1) copy is more obviously needed for a non-WC commit
 * such as creating a branch directly in the repository. One could argue
 * that committing a copy from a WC already involves O(N) space and time
 * for the copy within the WC, and so requiring an O(1) commit is not
 * necessarily justifiable; but as commit may be vastly more expensive
 * than local operations, making it important even in this case. There is
 * also the WC-to-repo copy operation which involves elements of committing
 * from a WC and "directly".
 *
 * (2) Update/Switch
 *
 *   - One change per *WC* path rather than per *repo* path
 *   - From mixed-rev to single-rev
 *   - Rx initially has a complete copy of the "from" state
 *   - Diff: with context (for merging)
 *   - Copies: can expand "copy" (non-recursive)
 *
 * (3) Diff (wc-base/repo:repo) (for merging/patching/displaying)
 *
 *   - From mixed-rev (for wc-base) to single-rev
 *       (enhancement: mixed-rev "to" state?)
 *   - Rx needs to be told the "from" revisions
 *   - Diff: with context (for merging)
 *   - Diff: can be reversible
 *   - Copies: can send O(1) "copy" (recursive + edits)
 *   - Copies: can expand "copy" (non-recursive)
 *
 * ===================================================================
 * Two different styles of "editing"
 * ===================================================================
 *
 * (1) Ordered, cumulative changes to a txn
 *
 * (2) Transmission of a set of independent changes
 *
 * These can be mixed: e.g. one interface declared here uses style (1)
 * for tree changes with style (2) for content changes.
 *
 * ===================================================================
 * Two different ways of "addressing" a node
 * ===================================================================
 *
 * (1) path [@ old-rev]
 *
 * (2) node-id
 *
 * Either way, the intent is the same: to be able to specify "where" a
 * modification or a new node should go in the tree. The difference
 * between path-based and id-based addressing is not *what* the address
 * means (they would have to mean the same thing, ultimately, at the
 * point of use) but *how* and how easily they achieve that meaning.
 *
 * Either way, two variations need to be handled:
 *   * Addressing a node that already existed in the sender's base state
 *   * Addressing a node that the sender has created
 *
 * Addressing by Path
 * ------------------
 *
 * A node-branch that exists at the start of the edit can be addressed
 * by giving a location (peg-path @ peg-rev) where it was known to exist.
 *
 * The receiver can trace (peg-path @ peg-rev) forward to the txn, and
 * find the path at which that node-branch is currently located in the
 * txn (or find that it is not present), as well as discovering whether
 * there was any change to it (including deletion) between peg-rev and
 * the txn-base, or after txn-base up to the current state of the txn.
 *
 * A node-branch created within the txn can be addressed by path only if
 * the sender knows that path. In order to create the node the sender
 * would have specified a parent node-branch and a new name. The node can
 * now be addressed as
 *
 *   (parent-peg-path @ peg-rev) / new-name
 *
 * which translates in the txn to
 *
 *   parent-path-in-txn / new-name
 *
 * When the sender creates another node as a child of this one, this second
 * new node can be addressed as either
 *
 *   (parent-peg-path @ peg-rev) / new-name / new-name-2
 *
 * or, if the sender knows the path-in-txn that resulted from the first one
 *
 *   parent-path-in-txn / new-name / new-name-2
 *
 * The difficulty is that, in a commit, the txn is based on a repository
 * state that the sender does not know. The paths may be different in that
 * state, due to recently committed moves, if the Out-Of-Date logic permits
 * that. The "parent-path-in-txn" is not, in general, known to the sender.
 *
 * Therefore the sender needs to address nested additions as
 *
 *   (peg-path @ peg-rev) / (path-created-in-txn)
 *
 * Why can't we use the old Ev1 form (path-in-txn, wc-base-rev)?
 *
 *     Basically because, in general (if other commits on the server
 *     are allowed to move the nodes that this commit is editing),
 *     then (path-in-txn, wc-base-rev) does not unambiguously identify
 *     a node-revision or a specific path in revision wc-base-rev. The
 *     sender cannot know what path in the txn corresponds to a given path
 *     in wc-base-rev.
 *
 * Why not restrict OOD checking to never merge with out-of-date moves?
 *
 *     It would seem unnecessarily restrictive to expect that we would
 *     never want the OOD check to allow merging with a repository-side
 *     move of a parent of the node we are editing. That would not be in
 *     the spirit of move tracking, nor would it be symmetrical with the
 *     client-side expected behaviour of silently merging child edits
 *     with a parent move.
 *
 * A possible alternative design direction:
 *
 *   * Provide a way for the client to learn the path-in-txn resulting
 *     from each edit, to be used in further edits referring to the same
 *     node-branch.
 *
 * Addressing by Node-Id
 * ---------------------
 *
 * For the purposes of addressing nodes within an edit, node-ids need not
 * be repository-wide unique ids, they only need to be known within the
 * editor. However, if the sender is to use ids that are not already known
 * to the receiver, then it must provide a mapping from ids to nodes.
 *
 * The sender assigns an id to each node including new nodes. (It is not
 * appropriate for the editor or its receiver to assign an id to an added
 * node, because the sender needs to be able to refer to that node as a
 * parent node for other nodes without creating any ordering dependency.)
 *
 * If the sender does not know the repository-wide id for a node, which is
 * especially likely for a new node, it must assign a temporary id for use
 * just within the edit. In that case, each new node or new node-branch is
 * necessarily independent. On the other hand, if the sender is able to
 * use repository-wide ids, then the possibility arises of the sender
 * asking to create a new node or a new node-branch that has the same id
 * as an existing one. The receiver would consider that to be a conflict.
 *
 *
 * ===================================================================
 * WC update/switch
 * ===================================================================
 *
 * How Subversion does an update (or switch), roughly:
 *
 *   - Client sends a "report" of WC base node locations to server.
 *   - Server calculates a diff from reported mixed-rev WC-base to
 *     requested single-rev target.
 *   - Server maps repo paths to WC paths (using the report) before
 *     transmitting edits.
 *
 * ===================================================================
 * Commit from WC
 * ===================================================================
 * 
 * How Subversion does a commit, roughly:
 *
 *   - Server starts a txn based on current head rev
 *
 *                   r1 2 3 4 5 6 7 8 head  txn
 *     WC-base  @4 -> A . . M . . . . .     |...
 *      |_B     @3 -> A . M . . . . . .  == |...D
 *      |_C     @3 -> A . M . . . . . .     |...
 *        |_foo @6 -> . A . . . M . D .     |...
 *       \_____________________________________/
 *            del /B r3
 *
 *   - Client sends changes based on its WC-base rev for each node,
 *     sending "this is the base rev I'm using" for each node.
 *
 *   - Server "merges" the client's changes into the txn on the fly,
 *     rejecting as "out of date" any change that requires a non-trivial
 *     merge.
 *
 *                   r1 2 3 4 5 6 7 8 head
 *     WC-base  @4 -> A . . M . . . . .
 *      |_B     @3 -> A . M . . . . . .    txn
 *      |_C     @3 -> A . M . . . . . . \  |...
 *        |_foo @6 -> . A . . . M . D .  \ |...x
 *       \                                 |...
 *        \                                |...OOD! (deleted since r6)
 *         \___________________________________/
 *            edit /C/foo r6
 *
 *   - Server "merges" the txn in the same way with any further commits,
 *     until there are no further commits, and then commits the txn.
 *
 * The old design assumes that the client can refer to a node by its path.
 * Either this path in the txn refers to the same node as in the WC base,
 * or the WC base node has since been deleted and perhaps replaced. This is
 * detected by the OOD check. The node's path-in-txn can never be different
 * from its path-in-WC-base.
 *
 * When we introduce moves, it is possible that nodes referenced by the WC
 * will have been moved in the repository after the WC-base and before the
 * txn-base. Unless the client queries for such moves, it will not know
 * what path-in-txn corresponds to each path-in-WC-base.
 * 
 * It seems wrong to design an editor interface that requires there have
 * been no moves in the repository between the WC base and the txn-base
 * affecting the paths being referenced in the commit. Not totally
 * unreasonable for the typical work flows of today, but unreasonably
 * restricting the work flows that should be possible in the future with
 * move tracking in place.
 */

/**
 * @defgroup svn_editor The editor interface
 * @{
 */

/** Tree Editor
 */
typedef struct svn_editor3_t svn_editor3_t;

/** A location in the current transaction (when @a rev == -1) or in
 * a revision (when @a rev != -1). */
typedef struct svn_editor3_peg_path_t
{
  svn_revnum_t rev;
  const char *relpath;
} svn_editor3_peg_path_t;

/** A reference to a node in a txn. If it refers to a node created in
 * the txn, @a relpath specifies the one or more components that are
 * newly created; otherwise @a relpath should be empty. */
typedef struct svn_editor3_txn_path_t
{
  svn_editor3_peg_path_t peg;
  const char *relpath;
} svn_editor3_txn_path_t;

/** Node-Branch Identifier -- functionally similar to the FSFS
 * <node-id>.<copy-id>, but the ids used within an editor drive may be
 * scoped locally to that editor drive rather than in-repository ids.
 * (Presently a null-terminated C string.) */
typedef char *svn_editor3_nbid_t;

/** Versioned content of a node, excluding tree structure information.
 *
 * This specifies the content (properties, text of a file, symbolic link
 * target) directly, or by reference to an existing committed node, or
 * by a delta against such a reference content.
 *
 * ### An idea: If the sender and receiver agree, the content for a node
 *     may be specified as "null" to designate that the content is not
 *     available. For example, when a client performing a WC update has
 *     no read authorization for a given path, the server may send null
 *     content and the client may record an 'absent' WC node. (This
 *     would not make sense in a commit.)
 */
typedef struct svn_editor3_node_content_t svn_editor3_node_content_t;

/** The kind of the checksum to be used throughout the #svn_editor3_t APIs.
 */
#define SVN_EDITOR3_CHECKSUM_KIND svn_checksum_sha1


/** These functions are called by the tree delta driver to edit the target.
 *
 * @see #svn_editor3_t
 *
 * @defgroup svn_editor3_drive Driving the editor
 * @{
 */

/*
 * ===================================================================
 * Editor for Commit (incremental tree changes; path-based addressing)
 * ===================================================================
 *
 * Versioning model assumed:
 *
 *   - per-node, copying-is-branching
 *   - copying is independent per node: a copy-child is not detectably
 *     "the same copy" as its parent, it's just copied at the same time
 *       => (cp ^/a@5 b; del b/c; cp ^/a/c@5 b/c) == (cp ^/a@5 b)
 *   - a node-rev's versioned state consists of:
 *        its tree linkage (parent node-branch identity, name)
 *        its content (props, text, link-target)
 *   - resurrection is supported
 *
 * Edit Operations:
 *
 *   - mk  kind               {dir-path | ^/dir-path@rev}[1] new-path[2]
 *   - cp  ^/from-path@rev[3] {dir-path | ^/dir-path@rev}[1] new-path[2]
 *   - cp  from-path[4]       {dir-path | ^/dir-path@rev}[1] new-path[2]
 *   - mv  ^/from-path@rev[4] {dir-path | ^/dir-path@rev}[1] new-path[2]
 *   - res ^/from-path@rev[3] {dir-path | ^/dir-path@rev}[1] new-path[2]
 *   - rm                     {path | ^/path@rev}[5]
 *   - put new-content        {path | ^/path@rev}[5]
 *
 * Preconditions:
 *
 *   [1] target parent dir must exist in txn
 *   [2] target name (in parent dir) must not exist in txn
 *   [3] source must exist in committed revision
 *   [4] source must exist in txn
 *   [5] target must exist in txn
 *
 * Characteristics of this editor:
 *
 *   - tree changes form an ordered list
 *   - content changes are unordered and independent
 *   - all tree changes MAY be sent before all content changes
 *
 *   ### In order to expand the scope of this editor to situations like
 *       update/switch, where the receiver doesn't have the repository
 *       to refer to, Can we add a full-traversal kind of copy?
 *       Is that merely a matter of driving the same API in a different
 *       way ("let the copy operation mean non-recursive copy")? Or is
 *       it totally out of scope? (To support WC update we need other
 *       changes too, not just this.)
 *
 * Description of operations:
 *
 *   - "cp", "mv" and "rm" are recursive; "mk" and "put" are non-recursive.
 *
 *   - "mk": Create a single new node, not related to any other existing
 *     node. The default content is empty, and MAY be altered by "put".
 *
 *   - "cp": Create a copy of the subtree found at the specified "from"
 *     location in a committed revision or [if supported] in the current
 *     txn. Each node in the target subtree is marked as "copied from" the
 *     node with the corresponding path in the source subtree.
 *
 *   - "mv": Move a subtree to a new parent node-branch and/or a new name.
 *     The source must be present in the txn but is specified by reference
 *     to a location in a committed revision.
 *
 *   - "res": Resurrect a previously deleted node-branch. The specified
 *     source is any location at which this node-branch existed, not
 *     necessarily at its youngest revision nor even within its most
 *     recent period of existence. The default content is that of the
 *     source location, and MAY be altered by "put".
 *
 *     The source node-branch MUST NOT exist in the txn. If the source
 *     node-branch exists in the txn-base, resurrection would be
 *     equivalent to reverting a local delete in the txn; the sender
 *     SHOULD NOT do this. [### Why not? Just because it seems like
 *     unnecessary flexibility.]
 *
 *     ### Can we have a recursive resurrect operation? What should it do
 *         if a child node is still alive (moved or already resurrected)?
 *
 *   - "rm": Remove the specified node and, recursively, all nodes that
 *     are currently its children in the txn. It does not delete nodes
 *     that used to be its children that have since been moved away.
 *     "rm" SHOULD NOT be used on a node-branch created by "mk" nor on the
 *     root node-branch created by "cp", but MAY be used on a child of a
 *     copy.
 *
 *   - "put": Set the content of a node to the specified value. (The new
 *     content may be described in terms of a delta against another node's
 *     content.)
 *
 *     "put" MAY be sent for any node that exists in the final state, and
 *     SHOULD NOT be sent for a node that will no longer exist in the final
 *     state. "put" SHOULD NOT be sent more than once for any node-branch.
 *     "put" MUST provide the right kind of content to match the node kind;
 *     it cannot change the kind of a node nor convert the content to match
 *     the node kind.
 *
 * Commit Rebase and OOD Checks:
 *
 *   - If the base of a change to a given node is out of date, a merge of
 *     this node would be required. The merge cannot be done on the server
 *     as then the committed version may differ from the version sent by
 *     the client, and there is no mechanism to inform the client of this.
 *     The granularity with which we can inform the client of a change
 *     is per node: either the node is updated to a new revision, meaning
 *     all its attributes reflect the committed changes, or not.
 *     Therefore the commit must be rejected and the merge done on the
 *     client side via an "update".
 *
 *     (As a possible special case, if each side of the merge has identical
 *     changes, this may be considered a null merge when a "permissive"
 *     strictness policy is in effect.)
 *
 *   - The editor is designed such that the commit rebase MAY allow moves
 *     in intervening commits that overlap path-wise with the edits we
 *     are making, and vice-versa. The out-of-date checks MAY work in the
 *     following way.
 *
 *     ### Are these the least restrictive OOD supported by the editor?
 *
 *       Operations on incoming commit vs. requirements on recent commits
 *       -----------------------------------------------------------------
 *       op    source node             target parent node
 *       ---   ---------------------   -----------------------------------
 *
 *       mk                            parent exists in txn
 *                                     parents may be moved/altered/new
 *
 *       cp   (no restriction)         --- // ---
 *
 *       res  ???                      --- // ---
 *
 *       mv   unchanged name&parent    --- // ---
 *           *unchanged own-content
 *            not created
 *            not deleted
 *            (parents may be moved/altered/created/deleted-non-recursively)
 *            (children may be moved/altered/created/deleted)
 *
 *       rm   unchanged name&parent
 *            unchanged own-content
 *            not created
 *            not deleted
 *            (for recursive delete, the conditions apply recursively)
 *            (we need not explicitly check for new children on the
 *            recent-commits side, as they would end up as orphans)
 *
 *       put *unchanged name&parent
 *            unchanged own-content
 *            not created
 *            not deleted
 *
 *     The conditions marked '*' could be relaxed.
 *
 * Notes on Paths:
 *
 *   - A bare "path" refers to a "path-in-txn", that is a path in the
 *     current state of the transaction. ^/path@rev refers to a path in a
 *     committed revision which is to be traced to the current transaction.
 *     A path-in-txn can refer to a node that was created with "mk" or
 *     "cp" (including children) and MAY [### or SHOULD NOT?] refer to a
 *     node-branch that already existed before the edit began.
 *
 *   - Ev1 declares, by nesting, exactly what parent dir each operation
 *     refers to: a pre-existing one (in which case it checks it's still
 *     the same one) or one it has just created in the txn. We make this
 *     distinction with {path-in-txn | ^/path-in-rev@rev} instead.
 *
 *   - When an existing target path is specified as
 *     (^/peg-path@rev, created-relpath), the path in the txn is:
 *
 *         (^/peg-path@rev traced forward to the txn)/(relpath)
 *
 *     When a target path to be created is specified as
 *     (^/peg-path@rev, created-relpath, new-name), the path-in-txn is:
 *
 *         (^/peg-path@rev traced forward to the txn)/(relpath)/(new-name)
 *
 *     The "peg-path @ rev" part acts like an unambiguous identifier for
 *     each pre-existing node. The remaining part extends the identifier
 *     for nodes created in the txn. (That part would be ambiguous if we
 *     allowed created nodes to be moved, replaced, etc.; we don't allow
 *     that.)
 *
 * Notes on Copying:
 *
 *   - Copy from path-in-txn is required iff we want to support copying
 *     from "this revision". If we don't then the source is necessarily
 *     a pre-existing node and so can be referenced by ^/path@rev.
 *
 *   - There is no provision for making a non-tracked copy of a subtree,
 *     nor a copy in which some nodes are tracked and others untracked,
 *     in a single operation.
 *
 * Notes on Moving:
 *
 *   - There is no operation to move a subtree whose root node was created
 *     in this txn, merely because it is not necessary. (A node created by
 *     "mk" can always be created in the required location. A subtree of a
 *     copy can be moved by deleting it and making a new copy from the
 *     corresponding subtree of the original copy root, as there is no
 *     distinction between the first copy and the second copy.)
 *
 */

/** Make a single new node ("versioned object") with empty content.
 * 
 * Set the node kind to @a new_kind. Create the node in the parent
 * directory node-branch specified by @a parent_loc. Set the new node's
 * name to @a new_name.
 *
 * The new node is not related by node identity to any other existing node
 * nor to any other node created by another "mk" operation.
 *
 * @node "put" is optional for a node made by "mk".
 * ### For use as an 'update' editor, maybe 'mk' without 'put' should
 *     make an 'absent' node.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_mk(svn_editor3_t *editor,
               svn_node_kind_t new_kind,
               svn_editor3_txn_path_t parent_loc,
               const char *new_name);

/** Create a copy of a subtree.
 *
 * The source subtree is found at @a from_loc. If @a from_loc is a
 * location in a committed revision, make a copy from (and referring to)
 * that location. [If supported] If @a from_loc is a location in the
 * current txn, make a copy from the current txn, which when committed
 * will refer to the committed revision.
 *
 * Create the root node of the new subtree in the parent directory
 * node-branch specified by @a parent_loc with the name @a new_name.
 *
 * Each node in the target subtree has a "copied from" relationship with
 * the node with the corresponding path in the source subtree.
 *
 * The content of a node copied from an existing revision is, by default,
 * the content of the source node. The content of a node copied from this
 * revision is, by default, the FINAL content of the source node as
 * committed, even if the source node is changed after the copy operation.
 * In either case, the default content MAY be changed by a "put".
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_cp(svn_editor3_t *editor,
               svn_editor3_peg_path_t from_loc,
               svn_editor3_txn_path_t parent_loc,
               const char *new_name);

/** Move a subtree to a new parent directory and/or a new name.
 *
 * The root node of the source subtree in the current txn is the node-branch
 * specified by @a from_loc. @a from_loc must refer to a committed revision.
 *
 * Create the root node of the new subtree in the parent directory
 * node-branch specified by @a new_parent_loc with the name @a new_name.
 *
 * Each node in the target subtree remains the same node-branch as
 * the node with the corresponding path in the source subtree.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_mv(svn_editor3_t *editor,
               svn_editor3_peg_path_t from_loc,
               svn_editor3_txn_path_t new_parent_loc,
               const char *new_name);

/** Resurrect a node.
 *
 * Resurrect the node-branch that previously existed at @a from_loc,
 * a location in a committed revision. Put the resurrected node at
 * @a parent_loc, @a new_name.
 *
 * Set the content to @a new_content.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_res(svn_editor3_t *editor,
                svn_editor3_peg_path_t from_loc,
                svn_editor3_txn_path_t parent_loc,
                const char *new_name);

/** Remove the existing node-branch identified by @a loc and, recursively,
 * all nodes that are currently its children in the txn.
 *
 * @note This does not delete nodes that used to be children of the specified
 * node-branch that have since been moved away.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_rm(svn_editor3_t *editor,
               svn_editor3_peg_path_t loc);

/** Set the content of the node-branch identified by @a loc.
 *
 * Set the content to @a new_content.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_put(svn_editor3_t *editor,
                svn_editor3_txn_path_t loc,
                const svn_editor3_node_content_t *new_content);


/*
 * ========================================================================
 * Editor for Commit (independent per-node changes; node-id addressing)
 * ========================================================================
 *
 * Versioning model assumed:
 *
 *   - per-node, copying-is-branching
 *   - copying is independent per node: a copy-child is not detectably
 *     "the same copy" as its parent, it's just copied at the same time
 *       => (cp ^/a@5 b; del b/c; cp ^/a/c@5 b/c) == (cp ^/a@5 b)
 *   - a node-rev's versioned state consists of:
 *        its tree linkage (parent node-branch identity, name)
 *        its content (props, text, link-target)
 *   - resurrection is supported
 *
 * Edit Operations:
 *
 *   - add       kind      new-parent-nb[2] new-name new-content  ->  new-nb
 *   - copy-one  nb@rev[3] new-parent-nb[2] new-name new-content  ->  new-nb
 *   - copy-tree nb@rev[3] new-parent-nb[2] new-name new-content  ->  new-nb
 *   - delete    nb[1]   since-rev
 *   - alter     nb[1,2] since-rev new-parent-nb[2] new-name new-content
 *
 * Preconditions:
 *
 *   [1] node-branch must exist in initial state
 *   [2] node-branch must exist in final state
 *   [3] source must exist in committed revision or txn final state
 *
 * Characteristics of this editor:
 *
 *   - Tree structure is partitioned among the nodes, in such a way that
 *     each of the most important concepts such as "move", "copy",
 *     "create" and "delete" is modeled as a single change to a single
 *     node. The name and the identitiy of its parent directory node are
 *     considered to be attributes of that node, alongside its content.
 *
 *   - Changes are independent and unordered. The change to one node is
 *     independent of the change to any other node, except for the
 *     requirement that the final state forms a valid (path-wise) tree
 *     hierarchy. A valid tree hierarchy is NOT required in any
 *     intermediate state after each change or after a subset of changes.
 *
 *   - Copies can be made in two ways: a copy of a single node can have
 *     its content changed and its children may be arbitrarily arranged,
 *     or a "cheap" O(1) copy of a subtree which cannot be edited.
 *
 *
 * Notes on Copying:
 *
 *   - copy_one and copy_tree are separate. In this model it doesn't
 *     make sense to describe a copy-and-modify by means of generating
 *     a full copy (with ids, at least implicitly, for each node) and
 *     then potentially "deleting" some of the generated child nodes.
 *     Instead, each node has to be specified in its final state or not
 *     at all. Tree-copy therefore generates an immutable copy, while
 *     single-node copy supports arbitrary copy-and-modify operations,
 *     and tree-copy can be used for any unmodified subtrees therein.
 *     There is no need to reference the root node of a tree-copy again
 *     within the same edit, and so no id is provided.
 */

/** Create a new versioned object of kind @a new_kind.
 * 
 * Assign the new node a locally unique node-branch-id, @a local_nbid,
 * with which it can be referenced within this edit.
 *
 * Set the node's parent and name to @a new_parent_nbid and @a new_name.
 *
 * Set the content to @a new_content.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_add(svn_editor3_t *editor,
                svn_editor3_nbid_t local_nbid,
                svn_node_kind_t new_kind,
                svn_editor3_nbid_t new_parent_nbid,
                const char *new_name,
                const svn_editor3_node_content_t *new_content);

/** Create a copy of an existing or new node, and optionally change its
 * content.
 *
 * Assign the target node a locally unique node-branch-id, @a local_nbid,
 * with which it can be referenced within this edit.
 *
 * Copy from the source node at @a src_revision, @a src_nbid. If
 * @a src_revision is #SVN_INVALID_REVNUM, it means copy from within
 * the new revision being described.
 *   ### See note on copy_tree().
 *
 * Set the target node's parent and name to @a new_parent_nbid and
 * @a new_name. Set the target node's content to @a new_content.
 *
 * @note This copy is not recursive. Children may be copied separately if
 * required.
 *
 * @see svn_editor3_copy_tree(), #svn_editor3_t
 */
svn_error_t *
svn_editor3_copy_one(svn_editor3_t *editor,
                     svn_editor3_nbid_t local_nbid,
                     svn_revnum_t src_revision,
                     svn_editor3_nbid_t src_nbid,
                     svn_editor3_nbid_t new_parent_nbid,
                     const char *new_name,
                     const svn_editor3_node_content_t *new_content);

/** Create a copy of an existing or new subtree. Each node in the source
 * subtree will be copied (branched) to the same relative path within the
 * target subtree. The nodes created by this copy cannot be modified or
 * addressed within this edit.
 *
 * Set the target root node's parent and name to @a new_parent_nbid and
 * @a new_name.
 *
 * Copy from the source node at @a src_revision, @a src_nbid. If
 * @a src_revision is #SVN_INVALID_REVNUM, it means copy from within
 * the new revision being described. In this case the subtree copied is
 * the FINAL subtree as committed, regardless of the order in which the
 * edit operations are described.
 *   ### Is it necessarily the case that the state at the end of the edit
 *       is the state to be committed (subject to rebasing), or is it
 *       possible that a later edit might be performed on the txn?
 *       And how might we apply this principle to a non-commit editor
 *       such as a WC update?
 *
 * The content of each node copied from an existing revision is the content
 * of the source node. The content of each node copied from this revision
 * is the FINAL content of the source node as committed.
 *
 * @see svn_editor3_copy_one(), #svn_editor3_t
 */
svn_error_t *
svn_editor3_copy_tree(svn_editor3_t *editor,
                      svn_revnum_t src_revision,
                      svn_editor3_nbid_t src_nbid,
                      svn_editor3_nbid_t new_parent_nbid,
                      const char *new_name);

/** Delete the existing node-branch identified by @a nbid.
 *
 * @a since_rev specifies the base revision on which this deletion was
 * performed: the server can consider the change "out of date" if a commit
 * since then has changed or deleted this node-branch.
 *
 * ###  @note The delete is not recursive. Child nodes must be explicitly
 *      deleted or moved away.
 *   OR @note The delete is implicitly recursive: each child node that
 *      is not otherwise moved to a new parent will be deleted as well.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_delete(svn_editor3_t *editor,
                   svn_revnum_t since_rev,
                   svn_editor3_nbid_t nbid);

/** Alter the tree position and/or contents of the node-branch identified
 * by @a nbid, or resurrect it if it previously existed.
 *
 * @a since_rev specifies the base revision on which this edit was
 * performed: the server can consider the change "out of date" if a commit
 * since then has changed or deleted this node-branch.
 *
 * Set the node's parent and name to @a new_parent_nbid and @a new_name.
 *
 * Set the content to @a new_content.
 *
 * A no-op change MUST be accepted but, in the interest of efficiency,
 * SHOULD NOT be sent.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_alter(svn_editor3_t *editor,
                  svn_revnum_t since_rev,
                  svn_editor3_nbid_t nbid,
                  svn_editor3_nbid_t new_parent_nbid,
                  const char *new_name,
                  const svn_editor3_node_content_t *new_content);

/** Drive @a editor's #svn_editor3_cb_complete_t callback.
 *
 * Send word that the edit has been completed successfully.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_complete(svn_editor3_t *editor);

/** Drive @a editor's #svn_editor3_cb_abort_t callback.
 *
 * Notify that the edit transmission was not successful.
 * ### TODO @todo Shouldn't we add a reason-for-aborting argument?
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_abort(svn_editor3_t *editor);

/** @} */


/** These function types define the callback functions a tree delta consumer
 * implements.
 *
 * Each of these "receiving" function types matches a "driving" function,
 * which has the same arguments with these differences:
 *
 * - These "receiving" functions have a @a baton argument, which is the
 *   @a editor_baton originally passed to svn_editor3_create(), as well as
 *   a @a scratch_pool argument.
 *
 * - The "driving" functions have an #svn_editor3_t* argument, in order to
 *   call the implementations of the function types defined here that are
 *   registered with the given #svn_editor3_t instance.
 *
 * Note that any remaining arguments for these function types are explained
 * in the comment for the "driving" functions. Each function type links to
 * its corresponding "driver".
 *
 * @see #svn_editor3_cb_funcs_t, #svn_editor3_t
 *
 * @defgroup svn_editor_callbacks Editor callback definitions
 * @{
 */

/** @see svn_editor3_mk(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_mk_t)(
  void *baton,
  svn_node_kind_t new_kind,
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_cp(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_cp_t)(
  void *baton,
  svn_editor3_peg_path_t from_loc,
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_mv(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_mv_t)(
  void *baton,
  svn_editor3_peg_path_t from_loc,
  svn_editor3_txn_path_t new_parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_res(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_res_t)(
  void *baton,
  svn_editor3_peg_path_t from_loc,
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_rm(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_rm_t)(
  void *baton,
  svn_editor3_peg_path_t loc,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_put(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_put_t)(
  void *baton,
  svn_editor3_txn_path_t loc,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_add(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_add_t)(
  void *baton,
  svn_editor3_nbid_t local_nbid,
  svn_node_kind_t new_kind,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_copy_one(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_copy_one_t)(
  void *baton,
  svn_editor3_nbid_t local_nbid,
  svn_revnum_t src_revision,
  svn_editor3_nbid_t src_nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_copy_tree(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_copy_tree_t)(
  void *baton,
  svn_revnum_t src_revision,
  svn_editor3_nbid_t src_nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_delete(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_delete_t)(
  void *baton,
  svn_revnum_t since_rev,
  svn_editor3_nbid_t nbid,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_alter(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_alter_t)(
  void *baton,
  svn_revnum_t since_rev,
  svn_editor3_nbid_t nbid,
  svn_editor3_nbid_t new_parent_nbid,
  const char *new_name,
  const svn_editor3_node_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_complete(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_complete_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @see svn_editor3_abort(), #svn_editor3_t
 */
typedef svn_error_t *(*svn_editor3_cb_abort_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @} */


/** These functions create an editor instance so that it can be driven.
 *
 * @defgroup svn_editor3_create Editor creation
 * @{
 */

/** A set of editor callback functions.
 *
 * If a function pointer is NULL, it will not be called.
 *
 * @see svn_editor3_create(), #svn_editor3_t
 */
typedef struct svn_editor3_cb_funcs_t
{
  svn_editor3_cb_mk_t cb_mk;
  svn_editor3_cb_cp_t cb_cp;
  svn_editor3_cb_mv_t cb_mv;
  svn_editor3_cb_res_t cb_res;
  svn_editor3_cb_rm_t cb_rm;
  svn_editor3_cb_put_t cb_put;

  svn_editor3_cb_add_t cb_add;
  svn_editor3_cb_copy_one_t cb_copy_one;
  svn_editor3_cb_copy_tree_t cb_copy_tree;
  svn_editor3_cb_delete_t cb_delete;
  svn_editor3_cb_alter_t cb_alter;

  svn_editor3_cb_complete_t cb_complete;
  svn_editor3_cb_abort_t cb_abort;

} svn_editor3_cb_funcs_t;

/** Allocate an #svn_editor3_t instance from @a result_pool, store
 * @a *editor_funcs, @a editor_baton, @a cancel_func and @a cancel_baton
 * in the new instance and return it in @a *editor.
 *
 * @a scratch_pool is used for temporary allocations (if any). Note that
 * this is NOT the same @a scratch_pool that is passed to callback functions.
 *
 * @see #svn_editor3_t
 */
svn_error_t *
svn_editor3_create(svn_editor3_t **editor,
                   const svn_editor3_cb_funcs_t *editor_funcs,
                   void *editor_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);

/** Return an editor's private baton.
 *
 * In some cases, the baton is required outside of the callbacks. This
 * function returns the private baton for use.
 *
 * @see svn_editor3_create(), #svn_editor3_t
 */
void *
svn_editor3_get_baton(const svn_editor3_t *editor);

/** @} */


/*
 * ========================================================================
 * Node Content Interface
 * ========================================================================
 *
 * @defgroup svn_editor3_node_content Node content interface
 * @{
 */

/** Versioned content of a node, excluding tree structure information.
 *
 * The @a kind field specifies the kind of content described. It must
 * match the kind of node it is being put into, as a node's kind cannot
 * be changed.
 *
 * The @a ref field specifies a reference content: the content of an
 * existing committed node, or empty. The other fields are optional
 * overrides for parts of the content.
 *
 * ### Specify content as deltas against the (optional) reference instead
 *     of as overrides?
 */
struct svn_editor3_node_content_t
{
  /* The node kind: dir, file, symlink, or unknown.
   * 
   * MUST NOT be 'unknown' if the content is of a known kind, including
   * if a kind-specific field (checksum, stream or target) is non-null.
   * MAY be 'unknown' when only copying content from a reference node
   * and/or only changing properties. */
  svn_node_kind_t kind;

  /* Reference the content in an existing, committed node-rev.
   *
   * If this is (SVN_INVALID_REVNUM, NULL) then the reference content
   * is empty.
   *
   * ### Reference a whole node-rev instead? (Don't need to reference a
   *     specific rev.)
   */
  svn_editor3_peg_path_t ref;

  /* Properties (for all node kinds).
   * Maps (const char *) name -> (svn_string_t) value. */
  apr_hash_t *props;

  /* Text checksum (only for a file; otherwise SHOULD be NULL). */
  const svn_checksum_t *checksum;

  /* Text stream, readable (only for a file; otherwise SHOULD be NULL).
   * ### May be null if we expect the receiver to retrieve the text by its
   *     checksum? */
  svn_stream_t *stream;

  /* Symlink target (only for a symlink; otherwise SHOULD be NULL). */
  const char *target;

};

/* Duplicate a node-content into result_pool.
 * ### What about the stream though? Maybe we shouldn't have a _dup.
 */
/* svn_editor3_node_content_t *
svn_editor3_node_content_dup(const svn_editor3_node_content_t *old,
                             apr_pool_t *result_pool); */

/* Create a new node-content object by reference to an existing node.
 *
 * Allocate it in @a result_pool. */
svn_editor3_node_content_t *
svn_editor3_node_content_create_ref(svn_editor3_peg_path_t ref,
                                    apr_pool_t *result_pool);

/* Create a new node-content object for a directory node.
 *
 * Allocate it in @a result_pool. */
svn_editor3_node_content_t *
svn_editor3_node_content_create_dir(svn_editor3_peg_path_t ref,
                                    apr_hash_t *props,
                                    apr_pool_t *result_pool);

/* Create a new node-content object for a file node.
 *
 * Allocate it in @a result_pool. */
svn_editor3_node_content_t *
svn_editor3_node_content_create_file(svn_editor3_peg_path_t ref,
                                     apr_hash_t *props,
                                     const svn_checksum_t *checksum,
                                     svn_stream_t *stream,
                                     apr_pool_t *result_pool);

/* Create a new node-content object for a symlink node.
 *
 * Allocate it in @a result_pool. */
svn_editor3_node_content_t *
svn_editor3_node_content_create_symlink(svn_editor3_peg_path_t ref,
                                        apr_hash_t *props,
                                        const char *target,
                                        apr_pool_t *result_pool);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR3_H */