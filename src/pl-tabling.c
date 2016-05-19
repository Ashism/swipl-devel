/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (c)  2016, VU University Amsterdam
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in
       the documentation and/or other materials provided with the
       distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include "pl-incl.h"
#include "pl-tabling.h"

static void	free_worklist(worklist *wl);


static worklist_set *
thread_worklist(worklist_set **wlp)
{ if ( *wlp == NULL )
  { worklist_set *wl = PL_malloc(sizeof(*wl));
    initBuffer(&wl->members);
    *wlp = wl;
  }

  return *wlp;
}

static worklist_set *
global_worklist(void)
{ GET_LD

  return thread_worklist(&LD->tabling.worklist);
}

static worklist_set *
newly_created_worklist(void)
{ GET_LD

  return thread_worklist(&LD->tabling.created_worklists);
}

		 /*******************************
		 *     THE GLOBAL WORKLIST	*
		 *******************************/

static void
add_global_worklist(worklist *wl)
{ worklist_set *wls = global_worklist();

  addBuffer(&wls->members, wl, worklist*);
  wl->in_global_wl = TRUE;
}


static worklist *
pop_worklist(void)
{ worklist_set *wls = global_worklist();

  if ( !isEmptyBuffer(&wls->members) )
  { worklist *wl = popBuffer(&wls->members, worklist*);
    wl->in_global_wl = FALSE;

    return wl;
  }

  return NULL;
}


static void
reset_global_worklist(void)
{ worklist_set *wls = global_worklist();
  worklist **wlp = (worklist**)baseBuffer(&wls->members, worklist*);
  size_t i, nwpl = entriesBuffer(&wls->members, worklist*);

  for(i=0; i<nwpl; i++)
  { worklist *wl = wlp[i];

    free_worklist(wl);
  }

  discardBuffer(&wls->members);
  initBuffer(&wls->members);
}


static void
add_newly_created_worklist(worklist *wl)
{ worklist_set *wls = newly_created_worklist();

  addBuffer(&wls->members, wl, worklist*);
}

static void
reset_newly_created_worklists(void)
{ worklist_set *wls = newly_created_worklist();

  discardBuffer(&wls->members);
  initBuffer(&wls->members);
}

static size_t
newly_created_worklists(worklist ***wlp)
{ worklist_set *wls = newly_created_worklist();

  *wlp = (worklist**)baseBuffer(&wls->members, worklist*);
  return entriesBuffer(&wls->members, worklist*);
}



		 /*******************************
		 *     THREAD VARIANT TABLE	*
		 *******************************/

static trie *
thread_variant_table(ARG1_LD)
{ if ( !LD->tabling.variant_table )
  { LD->tabling.variant_table = trie_create();
    trie_symbol(LD->tabling.variant_table);
  }

  return LD->tabling.variant_table;
}


static void
clear_variant_table(ARG1_LD)
{ if ( LD->tabling.variant_table )
  { trie_destroy(LD->tabling.variant_table);
    LD->tabling.variant_table = NULL;
  }
}


static trie *
get_variant_table(term_t t, int create ARG_LD)
{ trie *variants = thread_variant_table(PASS_LD1);
  trie_node *node;
  int rc;
  Word v = valTermRef(t);

  if ( (rc=trie_lookup(variants, &node, v, create PASS_LD)) == TRUE )
  { if ( node->value )
    { return symbol_trie(node->value);
    } else if ( create )
    { trie *vt = trie_create();
      node->value = trie_symbol(vt);
      return vt;
    } else
      return NULL;
  }

  trie_error(rc, t);
  return NULL;
}


		 /*******************************
		 *  ANSWER/SUSPENSION CLUSTERS	*
		 *******************************/

static cluster *
new_answer_cluster(trie_node *first)
{ cluster *c;

  c = PL_malloc(sizeof(*c));
  c->type = CLUSTER_ANSWERS;
  initBuffer(&c->members);
  addBuffer(&c->members, first, trie_node*);

  return c;
}

static void
free_answer_cluster(cluster *c)
{ discardBuffer(&c->members);
  PL_free(c);
}

static void
add_to_answer_cluster(cluster *c, trie_node *answer)
{ addBuffer(&c->members, answer, trie_node*);
}

static trie_node*
get_answer_from_cluster(cluster *c, size_t index)
{ if ( index < entriesBuffer(&c->members, trie_node*) )
    return fetchBuffer(&c->members, index, trie_node*);
  return NULL;
}

static int
inc_acp_index(cluster *c, size_t *index)
{ assert(c->type == CLUSTER_ANSWERS);
  if ( *index + 1 < entriesBuffer(&c->members, trie_node*) )
  { (*index)++;
    return TRUE;
  }
  return FALSE;
}

static cluster *
new_suspension_cluster(term_t first)
{ cluster *c;

  c = PL_malloc(sizeof(*c));
  c->type = CLUSTER_SUSPENSIONS;
  initBuffer(&c->members);
  addBuffer(&c->members, PL_record(first), record_t);

  return c;
}

static void
free_suspension_cluster(cluster *c)
{ record_t *base = baseBuffer(&c->members, record_t);
  size_t entries = entriesBuffer(&c->members, record_t);
  size_t i;

  for(i=0; i<entries; i++)
    PL_erase(base[i]);

  discardBuffer(&c->members);
  PL_free(c);
}

static void
add_to_suspension_cluster(cluster *c, term_t suspension)
{ addBuffer(&c->members, PL_record(suspension), record_t);
}

static record_t
get_suspension_from_cluster(cluster *c, size_t index)
{ if ( index < entriesBuffer(&c->members, record_t) )
    return fetchBuffer(&c->members, index, record_t);
  return 0;
}

static int
inc_scp_index(cluster *c, size_t *index)
{ if ( *index + 1 < entriesBuffer(&c->members, record_t) )
  { (*index)++;
    return TRUE;
  }
  return FALSE;
}

static void
free_cluster(cluster *c)
{ if ( c->type == CLUSTER_ANSWERS )
    free_answer_cluster(c);
  else
    free_suspension_cluster(c);
}

#ifdef O_DEBUG
static int
acp_size(cluster *c)
{ return entriesBuffer(&c->members, trie_node*);
}

static int
scp_size(cluster *c)
{ return entriesBuffer(&c->members, record_t);
}
#endif

		 /*******************************
		 *	   TABLE WORKLIST	*
		 *******************************/

static worklist *
new_worklist(trie *trie)
{ worklist *wl;

  wl = PL_malloc(sizeof(*wl));
  memset(wl, 0, sizeof(*wl));
  wl->magic = WORKLIST_MAGIC;
  wl->table = trie;
  trie->data.worklist = wl;

  return wl;
}


static void
free_worklist(worklist *wl)
{ cluster *c, *next;

  for(c=wl->head; c; c = next)
  { next = c->next;

    free_cluster(c);
  }

  PL_free(wl);
}



/* The work is done if there is no answer cluster or there is
   no suspension right of the answer cluster
*/

static int
worklist_work_done(worklist *wl)
{ return !wl->riac || !wl->riac->next;
}


static void
wkl_append_left(worklist *wl, cluster *c)
{ if ( wl->head )
  { c->prev = NULL;
    c->next = wl->head;
    wl->head->prev = c;
    wl->head = c;
  } else
  { c->next = c->prev = NULL;
    wl->head = wl->tail = c;
  }
}


static void
wkl_append_right(worklist *wl, cluster *c)
{ if ( wl->tail )
  { c->next = NULL;
    c->prev = wl->tail;
    wl->tail->next = c;
    wl->tail = c;
  } else
  { c->next = c->prev = NULL;
    wl->head = wl->tail = c;
  }
}


static void
wkl_swap_clusters(cluster *acp, cluster *scp)
{ cluster *a = acp->prev;		/* before the couple */
  cluster *z = scp->next;		/* after the couple */

  assert(acp->next == scp);

  if ( a ) a->next = scp;
  if ( z ) z->prev = acp;
  scp->prev = a;
  acp->next = z;
  scp->next = acp;
  acp->prev = scp;
}


static void
potentially_add_to_global_worklist(worklist *wl)
{ if ( !wl->in_global_wl && !wl->executing )
    add_global_worklist(wl);
}


static int
wkl_add_answer(worklist *wl, trie_node *node)
{ potentially_add_to_global_worklist(wl);
  if ( wl->head && wl->head->type == CLUSTER_ANSWERS )
  { add_to_answer_cluster(wl->head, node);
  } else
  { cluster *c = new_answer_cluster(node);
    wkl_append_left(wl, c);
    if ( !wl->riac )
      wl->riac = c;
  }

  return TRUE;
}


static int
wkl_add_suspension(worklist *wl, term_t suspension)
{ potentially_add_to_global_worklist(wl);
  if ( wl->tail && wl->tail->type == CLUSTER_SUSPENSIONS )
  { add_to_suspension_cluster(wl->tail, suspension);
  } else
  { cluster *c = new_suspension_cluster(suspension);
    wkl_append_right(wl, c);
    if ( c->prev && c->prev->type == CLUSTER_ANSWERS )
      wl->riac = c->prev;
  }

  return TRUE;
}


		 /*******************************
		 *	PROLOG CONNECTION	*
		 *******************************/

#define WL_IS_SPECIAL(wl)  (((intptr_t)(wl)) & 0x1)
#define WL_IS_WORKLIST(wl) ((wl) && !WL_IS_SPECIAL(wl))

#define WL_COMPLETE ((worklist *)0x11)

static int
unify_table_status(term_t t, trie *trie ARG_LD)
{ worklist *wl = trie->data.worklist;

  if ( WL_IS_WORKLIST(wl) )
    return PL_unify_pointer(t, wl);
  if ( !wl )
    return PL_unify_atom(t, ATOM_fresh);
  if ( wl == WL_COMPLETE )
    return PL_unify_atom(t, ATOM_complete);

  assert(0);
  return FALSE;
}


static int
get_worklist(term_t t, worklist **wlp)
{ GET_LD
  void *ptr;

  if ( PL_get_pointer(t, &ptr) )
  { worklist *wl = ptr;
    assert(wl->magic == WORKLIST_MAGIC);
    *wlp = wl;
    return TRUE;
  }

  return PL_type_error("worklist", t);
}

/*
static int
get_trie_node(term_t t, trie_node **np)
{ GET_LD
  void *ptr;

  if ( PL_get_pointer(t, &ptr) )
  { trie_node *n = ptr;
    *np = n;
    return TRUE;
  }

  return PL_type_error("trie_node", t);
}
*/

/** '$tbl_new_worklist'(-Worklist, +Trie) is det.
 *
 * Create a new worklist for Trie add add it it the global worklist
 * set.
 */

static
PRED_IMPL("$tbl_new_worklist", 2, tbl_new_worklist, 0)
{ PRED_LD
  trie *trie;

  if ( get_trie(A2, &trie) )
  { worklist *wl = new_worklist(trie);

    add_global_worklist(wl);
    add_newly_created_worklist(wl);
    return PL_unify_pointer(A1, wl);
  }

  return FALSE;
}


/** '$tbl_pop_worklist'(-Worklist) is semidet.
 *
 * Pop next worklist from the global worklist.
 */

static
PRED_IMPL("$tbl_pop_worklist", 1, tbl_pop_worklist, 0)
{ PRED_LD
  worklist *wl;

  if ( (wl=pop_worklist()) )
    return PL_unify_pointer(A1, wl);

  return FALSE;
}

/** '$tbl_wkl_add_answer'(+Worklist, +Term) is semidet.
 *
 * Add an answer to the worklist's trie  and the worklist answer cluster
 * using trie_insert_new/3. Fails if a  variant   of  Term is already in
 * Worklist.
 */

static
PRED_IMPL("$tbl_wkl_add_answer", 2, tbl_wkl_add_answer, 0)
{ PRED_LD
  worklist *wl;

  if ( get_worklist(A1, &wl) )
  { Word kp;
    trie_node *node;
    int rc;

    kp = valTermRef(A2);

    if ( (rc=trie_lookup(wl->table, &node, kp, TRUE PASS_LD)) == TRUE )
    { if ( node->value )
      { if ( node->value == ATOM_nil )
	  return FALSE;				/* already in trie */
	return PL_permission_error("modify", "trie_key", A2);
      }
      node->value = ATOM_nil;

      return wkl_add_answer(wl, node);
    }

    return trie_error(rc, A2);
  }

  return FALSE;
}

/** '$tbl_wkl_add_suspension'(+Worklist, +Suspension) is det.
 *
 * Add a suspension to the worklist.
 */

static
PRED_IMPL("$tbl_wkl_add_suspension", 2, tbl_wkl_add_suspension, 0)
{ worklist *wl;

  if ( get_worklist(A1, &wl) )
  { wkl_add_suspension(wl, A2);
    return TRUE;
  }

  return FALSE;
}

/** '$tbl_wkl_done'(+Worklist) is semidet.
 *
 * True if the worklist is complete
 */

static
PRED_IMPL("$tbl_wkl_done", 1, tbl_wkl_done, 0)
{ worklist *wl;

  return get_worklist(A1, &wl) && worklist_work_done(wl);
}


/** '$tbl_wkl_work'(+Worklist, -Answer, -Suspension) is nondet.
 *
 * True when Answer must be tried on Suspension.  Backtracking
 * basically does
 *
 *   ==
 *   member(Answer, RIAC),
 *   member(Suspension, LastSuspensionCluster)
 *   ==
 *
 * If the carthesian product is exhausted it tries to re-start using the
 * possible new RIAC and SCP.  During its execution, worklist->executing
 * is TRUE to avoid the worklist to   become part of the global worklist
 * again.
 *
 * This replaces table_get_work/3 from the pure Prolog implementation.
 */

typedef struct
{ worklist *list;
  cluster *acp;
  cluster *scp;
  size_t acp_index;
  size_t scp_index;
  int iteration;
  int next_step;
} wkl_step_state;

static
PRED_IMPL("$tbl_wkl_work", 3, tbl_wkl_work, PL_FA_NONDETERMINISTIC)
{ PRED_LD
  trie_node *an;
  record_t sr;
  wkl_step_state *state;

  switch( CTX_CNTRL )
  { case FRG_FIRST_CALL:
    { worklist *wl;

      if ( get_worklist(A1, &wl) )
      { cluster *acp, *scp;

	if ( (acp=wl->riac) && (scp=acp->next) )
	{ wkl_swap_clusters(acp, scp);

	  state = allocForeignState(sizeof(*state));
	  memset(state, 0, sizeof(*state));
	  state->list	   = wl;
	  state->acp	   = acp;
	  state->scp	   = scp;
	  wl->executing    = TRUE;
	  DEBUG(MSG_TABLING_WORK,
		Sdprintf("Processing workset (#ACP=%d, #SCP=%d)\n",
			 acp_size(acp), scp_size(scp)));

	  break;
	}
      }

      return FALSE;
    }
    case FRG_REDO:
      state = CTX_PTR;
      break;
    case FRG_CUTTED:
      state = CTX_PTR;
      state->list->executing = FALSE;
      freeForeignState(state, sizeof(*state));
      return TRUE;
    default:
      assert(0);
      return FALSE;
  }

  if ( state->next_step )
  { cluster *acp, *scp;

    if ( (acp=state->list->riac) && (scp=acp->next) )
    { state->acp       = acp;
      state->scp       = scp;
      state->acp_index = 0;
      state->scp_index = 0;
      state->next_step = FALSE;
      DEBUG(MSG_TABLING_WORK,
	    Sdprintf("Re-processing workset [%d] (#ACP=%d, #SCP=%d)\n",
		     ++state->iteration, acp_size(acp), scp_size(scp)));
      if ( state->iteration > 10 )
      { Sdprintf("> 10 iterations; aborted\n");
	return FALSE;
      }
    }
  }

  if ( state->next_step == FALSE &&
       (an=get_answer_from_cluster(state->acp, state->acp_index)) )
  { if ( (sr=get_suspension_from_cluster(state->scp, state->scp_index)) )
    { term_t answer     = PL_new_term_ref();
      term_t suspension = PL_new_term_ref();

      if ( !( put_trie_term(an, answer PASS_LD) &&
	      PL_recorded(sr, suspension) &&
	      PL_unify(A2, answer) &&
	      PL_unify(A3, suspension) ) )
      { freeForeignState(state, sizeof(*state));
	return FALSE;			/* resource error */
      }

      DEBUG(MSG_TABLING_WORK,
	    { Sdprintf("Work: %d %d\n\t",
		       (int)state->acp_index+1, (int)state->scp_index+1);
	      PL_write_term(Serror, answer, 1200, PL_WRT_NEWLINE);
	      Sdprintf("\t");
	      PL_write_term(Serror, suspension, 1200, PL_WRT_NEWLINE);
	    });

      if ( !inc_scp_index(state->scp, &state->scp_index) )
      { state->scp_index = 0;
	if ( !inc_acp_index(state->acp, &state->acp_index) )
	  state->next_step = TRUE;
      }

      ForeignRedoPtr(state);
    }
  }

  state->list->executing = FALSE;
  freeForeignState(state, sizeof(*state));
  return FALSE;
}


/** '$tbl_variant_table'(+Variant, -Trie, -Status) is det.
 *
 * Retrieve the table for Variant. Status is one of
 *
 *   - `fresh` if the table is new
 *   - `complete` if the table is completed
 *   - A worklist pointer
 */

static
PRED_IMPL("$tbl_variant_table", 3, tbl_variant_table, 0)
{ PRED_LD
  trie *trie;

  if ( (trie=get_variant_table(A1, TRUE PASS_LD)) )
  { return ( _PL_unify_atomic(A2, trie->symbol) &&
	     unify_table_status(A3, trie PASS_LD) );
  }

  return FALSE;
}


static
PRED_IMPL("$tbl_variant_table", 1, tbl_variant_table, 0)
{ PRED_LD
  trie *trie = LD->tabling.variant_table;

  if ( trie )
    return _PL_unify_atomic(A1, trie->symbol);

  return FALSE;
}


/** '$tbl_table_status'(+Trie, -Status)
 *
 * Set the status of Trie. Old is unified to one of `fresh`, `active`, a
 * worklist  or  `complete`.  New  is  one    of  `fresh`,  `active`  or
 * `complete`. In all cases the worklist is removed.
 */

static
PRED_IMPL("$tbl_table_status", 2, tbl_table_status, 0)
{ PRED_LD
  trie *trie;

  return ( get_trie(A1, &trie) &&
	   unify_table_status(A2, trie PASS_LD) );
}

/** '$tbl_table_complete_all'
 *
 * Complete and reset all newly created tables.
 */

static
PRED_IMPL("$tbl_table_complete_all", 0, tbl_table_complete_all, 0)
{ size_t i, ntables;
  worklist **wls;

  ntables = newly_created_worklists(&wls);
  for(i=0; i<ntables; i++)
  { worklist *wl = wls[i];
    trie *trie = wl->table;

    trie->data.worklist = WL_COMPLETE;
    free_worklist(wl);
  }
  reset_newly_created_worklists();

  return TRUE;
}


static
PRED_IMPL("$tbl_scheduling_component", 2, tbl_scheduling_component, 0)
{ PRED_LD

  return ( PL_unify_bool(A1, LD->tabling.has_scheduling_component) &&
	   PL_get_bool_ex(A2, &LD->tabling.has_scheduling_component) );
}


/** '$tbl_abolish_all_tables' is det.
 *
 * Clear the thread table data.
 */

static
PRED_IMPL("$tbl_abolish_all_tables", 0, tbl_abolish_all_tables, 0)
{ PRED_LD

  reset_global_worklist();
  reset_newly_created_worklists();
  clear_variant_table(PASS_LD1);

  return TRUE;
}


		 /*******************************
		 *      PUBLISH PREDICATES	*
		 *******************************/

BeginPredDefs(tabling)
  PRED_DEF("$tbl_new_worklist",		2, tbl_new_worklist,	     0)
  PRED_DEF("$tbl_pop_worklist",		1, tbl_pop_worklist,	     0)
  PRED_DEF("$tbl_wkl_add_answer",	2, tbl_wkl_add_answer,	     0)
  PRED_DEF("$tbl_wkl_add_suspension",	2, tbl_wkl_add_suspension,   0)
  PRED_DEF("$tbl_wkl_done",		1, tbl_wkl_done,	     0)
  PRED_DEF("$tbl_wkl_work",		3, tbl_wkl_work, PL_FA_NONDETERMINISTIC)
  PRED_DEF("$tbl_variant_table",	3, tbl_variant_table,	     0)
  PRED_DEF("$tbl_variant_table",        1, tbl_variant_table,        0)
  PRED_DEF("$tbl_table_status",		2, tbl_table_status,	     0)
  PRED_DEF("$tbl_table_complete_all",	0, tbl_table_complete_all,   0)
  PRED_DEF("$tbl_scheduling_component",	2, tbl_scheduling_component, 0)
  PRED_DEF("$tbl_abolish_all_tables",   0, tbl_abolish_all_tables,   0)
EndPredDefs
