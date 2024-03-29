/*    sv.c
 *
 *    Copyright (C) 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
 *    2000, 2001, 2002, 2003, by Larry Wall and others
 *
 *    You may distribute under the terms of either the GNU General Public
 *    License or the Artistic License, as specified in the README file.
 *
 * "I wonder what the Entish is for 'yes' and 'no'," he thought.
 *
 *
 * This file contains the code that creates, manipulates and destroys
 * scalar values (SVs). The other types (AV, HV, GV, etc.) reuse the
 * structure of an SV, so their creation and destruction is handled
 * here; higher-level functions are in av.c, hv.c, and so on. Opcode
 * level functions (eg. substr, split, join) for each of the types are
 * in the pp*.c files.
 */

#include "EXTERN.h"
#define PERL_IN_SV_C
#include "perl.h"
#include "regcomp.h"

#define FCALL *f

#ifdef PERL_UTF8_CACHE_ASSERT
/* The cache element 0 is the Unicode offset;
 * the cache element 1 is the byte offset of the element 0;
 * the cache element 2 is the Unicode length of the substring;
 * the cache element 3 is the byte length of the substring;
 * The checking of the substring side would be good
 * but substr() has enough code paths to make my head spin;
 * if adding more checks watch out for the following tests:
 *   t/op/index.t t/op/length.t t/op/pat.t t/op/substr.t
 *   lib/utf8.t lib/Unicode/Collate/t/index.t
 * --jhi
 */
#define ASSERT_UTF8_CACHE(cache) \
	STMT_START { if (cache) { assert((cache)[0] <= (cache)[1]); } } STMT_END
#else
#define ASSERT_UTF8_CACHE(cache) NOOP
#endif

/* ============================================================================

=head1 Allocation and deallocation of SVs.

An SV (or AV, HV, etc.) is allocated in two parts: the head (struct sv,
av, hv...) contains type and reference count information, as well as a
pointer to the body (struct xrv, xpv, xpviv...), which contains fields
specific to each type.

Normally, this allocation is done using arenas, which are approximately
1K chunks of memory parcelled up into N heads or bodies. The first slot
in each arena is reserved, and is used to hold a link to the next arena.
In the case of heads, the unused first slot also contains some flags and
a note of the number of slots.  Snaked through each arena chain is a
linked list of free items; when this becomes empty, an extra arena is
allocated and divided up into N items which are threaded into the free
list.

The following global variables are associated with arenas:

    PL_sv_arenaroot	pointer to list of SV arenas
    PL_sv_root		pointer to list of free SV structures

    PL_foo_arenaroot	pointer to list of foo arenas,
    PL_foo_root		pointer to list of free foo bodies
			    ... for foo in xiv, xnv, xrv, xpv etc.

Note that some of the larger and more rarely used body types (eg xpvio)
are not allocated using arenas, but are instead just malloc()/free()ed as
required. Also, if PURIFY is defined, arenas are abandoned altogether,
with all items individually malloc()ed. In addition, a few SV heads are
not allocated from an arena, but are instead directly created as static
or auto variables, eg PL_sv_undef.

The SV arena serves the secondary purpose of allowing still-live SVs
to be located and destroyed during final cleanup.

At the lowest level, the macros new_SV() and del_SV() grab and free
an SV head.  (If debugging with -DD, del_SV() calls the function S_del_sv()
to return the SV to the free list with error checking.) new_SV() calls
more_sv() / sv_add_arena() to add an extra arena if the free list is empty.
SVs in the free list have their SvTYPE field set to all ones.

Similarly, there are macros new_XIV()/del_XIV(), new_XNV()/del_XNV() etc
that allocate and return individual body types. Normally these are mapped
to the arena-manipulating functions new_xiv()/del_xiv() etc, but may be
instead mapped directly to malloc()/free() if PURIFY is defined. The
new/del functions remove from, or add to, the appropriate PL_foo_root
list, and call more_xiv() etc to add a new arena if the list is empty.

At the time of very final cleanup, sv_free_arenas() is called from
perl_destruct() to physically free all the arenas allocated since the
start of the interpreter.  Note that this also clears PL_he_arenaroot,
which is otherwise dealt with in hv.c.

Manipulation of any of the PL_*root pointers is protected by enclosing
LOCK_SV_MUTEX; ... UNLOCK_SV_MUTEX calls which should Do the Right Thing
if threads are enabled.

The function visit() scans the SV arenas list, and calls a specified
function for each SV it finds which is still live - ie which has an SvTYPE
other than all 1's, and a non-zero SvREFCNT. visit() is used by the
following functions (specified as [function that calls visit()] / [function
called by visit() for each SV]):

    sv_report_used() / do_report_used()
    			dump all remaining SVs (debugging aid)

    sv_clean_objs() / do_clean_objs(),do_clean_named_objs()
			Attempt to free all objects pointed to by RVs,
			and, unless DISABLE_DESTRUCTOR_KLUDGE is defined,
			try to do the same for all objects indirectly
			referenced by typeglobs too.  Called once from
			perl_destruct(), prior to calling sv_clean_all()
			below.

    sv_clean_all() / do_clean_all()
			SvREFCNT_dec(sv) each remaining SV, possibly
			triggering an sv_free(). It also sets the
			SVf_BREAK flag on the SV to indicate that the
			refcnt has been artificially lowered, and thus
			stopping sv_free() from giving spurious warnings
			about SVs which unexpectedly have a refcnt
			of zero.  called repeatedly from perl_destruct()
			until there are no SVs left.

=head2 Summary

Private API to rest of sv.c

    new_SV(),  del_SV(),

    new_XIV(), del_XIV(),
    new_XNV(), del_XNV(),
    etc

Public API:

    sv_report_used(), sv_clean_objs(), sv_clean_all(), sv_free_arenas()


=cut

============================================================================ */



/*
 * "A time to plant, and a time to uproot what was planted..."
 */

#define plant_SV(p) \
    STMT_START {					\
	SvANY(p) = (void *)PL_sv_root;			\
	SvFLAGS(p) = SVTYPEMASK;			\
	PL_sv_root = (p);				\
	--PL_sv_count;					\
    } STMT_END

/* sv_mutex must be held while calling uproot_SV() */
#define uproot_SV(p) \
    STMT_START {					\
	(p) = PL_sv_root;				\
	PL_sv_root = (SV*)SvANY(p);			\
	++PL_sv_count;					\
    } STMT_END


/* new_SV(): return a new, empty SV head */

#ifdef DEBUG_LEAKING_SCALARS
/* provide a real function for a debugger to play with */
STATIC SV*
S_new_SV(pTHX)
{
    SV* sv;

    LOCK_SV_MUTEX;
    if (PL_sv_root)
	uproot_SV(sv);
    else
	sv = more_sv();
    UNLOCK_SV_MUTEX;
    SvANY(sv) = 0;
    SvREFCNT(sv) = 1;
    SvFLAGS(sv) = 0;
    return sv;
}
#  define new_SV(p) (p)=S_new_SV(aTHX)

#else
#  define new_SV(p) \
    STMT_START {					\
	LOCK_SV_MUTEX;					\
	if (PL_sv_root)					\
	    uproot_SV(p);				\
	else						\
	    (p) = more_sv();				\
	UNLOCK_SV_MUTEX;				\
	SvANY(p) = 0;					\
	SvREFCNT(p) = 1;				\
	SvFLAGS(p) = 0;					\
    } STMT_END
#endif


/* del_SV(): return an empty SV head to the free list */

#ifdef DEBUGGING

#define del_SV(p) \
    STMT_START {					\
	LOCK_SV_MUTEX;					\
	if (DEBUG_D_TEST)				\
	    del_sv(p);					\
	else						\
	    plant_SV(p);				\
	UNLOCK_SV_MUTEX;				\
    } STMT_END

STATIC void
S_del_sv(pTHX_ SV *p)
{
    if (DEBUG_D_TEST) {
	SV* sva;
	SV* sv;
	SV* svend;
	int ok = 0;
	for (sva = PL_sv_arenaroot; sva; sva = (SV *) SvANY(sva)) {
	    sv = sva + 1;
	    svend = &sva[SvREFCNT(sva)];
	    if (p >= sv && p < svend)
		ok = 1;
	}
	if (!ok) {
	    if (ckWARN_d(WARN_INTERNAL))	
	        Perl_warner(aTHX_ packWARN(WARN_INTERNAL),
			    "Attempt to free non-arena SV: 0x%"UVxf,
			    PTR2UV(p));
	    return;
	}
    }
    plant_SV(p);
}

#else /* ! DEBUGGING */

#define del_SV(p)   plant_SV(p)

#endif /* DEBUGGING */


/*
=head1 SV Manipulation Functions

=for apidoc sv_add_arena

Given a chunk of memory, link it to the head of the list of arenas,
and split it into a list of free SVs.

=cut
*/

void
Perl_sv_add_arena(pTHX_ char *ptr, U32 size, U32 flags)
{
    SV* sva = (SV*)ptr;
    register SV* sv;
    register SV* svend;
    Zero(ptr, size, char);

    /* The first SV in an arena isn't an SV. */
    SvANY(sva) = (void *) PL_sv_arenaroot;		/* ptr to next arena */
    SvREFCNT(sva) = size / sizeof(SV);		/* number of SV slots */
    SvFLAGS(sva) = flags;			/* FAKE if not to be freed */

    PL_sv_arenaroot = sva;
    PL_sv_root = sva + 1;

    svend = &sva[SvREFCNT(sva) - 1];
    sv = sva + 1;
    while (sv < svend) {
	SvANY(sv) = (void *)(SV*)(sv + 1);
	SvFLAGS(sv) = SVTYPEMASK;
	sv++;
    }
    SvANY(sv) = 0;
    SvFLAGS(sv) = SVTYPEMASK;
}

/* make some more SVs by adding another arena */

/* sv_mutex must be held while calling more_sv() */
STATIC SV*
S_more_sv(pTHX)
{
    register SV* sv;

    if (PL_nice_chunk) {
	sv_add_arena(PL_nice_chunk, PL_nice_chunk_size, 0);
	PL_nice_chunk = Nullch;
        PL_nice_chunk_size = 0;
    }
    else {
	char *chunk;                /* must use New here to match call to */
	New(704,chunk,1008,char);   /* Safefree() in sv_free_arenas()     */
	sv_add_arena(chunk, 1008, 0);
    }
    uproot_SV(sv);
    return sv;
}

/* visit(): call the named function for each non-free SV in the arenas. */

STATIC I32
S_visit(pTHX_ SVFUNC_t f)
{
    SV* sva;
    SV* sv;
    register SV* svend;
    I32 visited = 0;

    for (sva = PL_sv_arenaroot; sva; sva = (SV*)SvANY(sva)) {
	svend = &sva[SvREFCNT(sva)];
	for (sv = sva + 1; sv < svend; ++sv) {
	    if (SvTYPE(sv) != SVTYPEMASK && SvREFCNT(sv)) {
		(FCALL)(aTHX_ sv);
		++visited;
	    }
	}
    }
    return visited;
}

#ifdef DEBUGGING

/* called by sv_report_used() for each live SV */

static void
do_report_used(pTHX_ SV *sv)
{
    if (SvTYPE(sv) != SVTYPEMASK) {
	PerlIO_printf(Perl_debug_log, "****\n");
	sv_dump(sv);
    }
}
#endif

/*
=for apidoc sv_report_used

Dump the contents of all SVs not yet freed. (Debugging aid).

=cut
*/

void
Perl_sv_report_used(pTHX)
{
#ifdef DEBUGGING
    visit(do_report_used);
#endif
}

/* called by sv_clean_objs() for each live SV */

static void
do_clean_objs(pTHX_ SV *sv)
{
    SV* rv;

    if (SvROK(sv) && SvOBJECT(rv = SvRV(sv))) {
	DEBUG_D((PerlIO_printf(Perl_debug_log, "Cleaning object ref:\n "), sv_dump(sv)));
	if (SvWEAKREF(sv)) {
	    sv_del_backref(sv);
	    SvWEAKREF_off(sv);
	    SvRV(sv) = 0;
	} else {
	    SvROK_off(sv);
	    SvRV(sv) = 0;
	    SvREFCNT_dec(rv);
	}
    }

    /* XXX Might want to check arrays, etc. */
}

/* called by sv_clean_objs() for each live SV */

#ifndef DISABLE_DESTRUCTOR_KLUDGE
static void
do_clean_named_objs(pTHX_ SV *sv)
{
    if (SvTYPE(sv) == SVt_PVGV && GvGP(sv)) {
	if ( SvOBJECT(GvSV(sv)) ||
	     (GvAV(sv) && SvOBJECT(GvAV(sv))) ||
	     (GvHV(sv) && SvOBJECT(GvHV(sv))) ||
	     (GvIO(sv) && SvOBJECT(GvIO(sv))) ||
	     (GvCV(sv) && SvOBJECT(GvCV(sv))) )
	{
	    DEBUG_D((PerlIO_printf(Perl_debug_log, "Cleaning named glob object:\n "), sv_dump(sv)));
	    SvREFCNT_dec(sv);
	}
    }
}
#endif

/*
=for apidoc sv_clean_objs

Attempt to destroy all objects not yet freed

=cut
*/

void
Perl_sv_clean_objs(pTHX)
{
    PL_in_clean_objs = TRUE;
    visit(do_clean_objs);
#ifndef DISABLE_DESTRUCTOR_KLUDGE
    /* some barnacles may yet remain, clinging to typeglobs */
    visit(do_clean_named_objs);
#endif
    PL_in_clean_objs = FALSE;
}

/* called by sv_clean_all() for each live SV */

static void
do_clean_all(pTHX_ SV *sv)
{
    DEBUG_D((PerlIO_printf(Perl_debug_log, "Cleaning loops: SV at 0x%"UVxf"\n", PTR2UV(sv)) ));
    SvFLAGS(sv) |= SVf_BREAK;
    SvREFCNT_dec(sv);
}

/*
=for apidoc sv_clean_all

Decrement the refcnt of each remaining SV, possibly triggering a
cleanup. This function may have to be called multiple times to free
SVs which are in complex self-referential hierarchies.

=cut
*/

I32
Perl_sv_clean_all(pTHX)
{
    I32 cleaned;
    PL_in_clean_all = TRUE;
    cleaned = visit(do_clean_all);
    PL_in_clean_all = FALSE;
    return cleaned;
}

/*
=for apidoc sv_free_arenas

Deallocate the memory used by all arenas. Note that all the individual SV
heads and bodies within the arenas must already have been freed.

=cut
*/

void
Perl_sv_free_arenas(pTHX)
{
    SV* sva;
    SV* svanext;
    XPV *arena, *arenanext;

    /* Free arenas here, but be careful about fake ones.  (We assume
       contiguity of the fake ones with the corresponding real ones.) */

    for (sva = PL_sv_arenaroot; sva; sva = svanext) {
	svanext = (SV*) SvANY(sva);
	while (svanext && SvFAKE(svanext))
	    svanext = (SV*) SvANY(svanext);

	if (!SvFAKE(sva))
	    Safefree((void *)sva);
    }

    for (arena = PL_xiv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xiv_arenaroot = 0;

    for (arena = PL_xnv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xnv_arenaroot = 0;

    for (arena = PL_xrv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xrv_arenaroot = 0;

    for (arena = PL_xpv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpv_arenaroot = 0;

    for (arena = (XPV*)PL_xpviv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpviv_arenaroot = 0;

    for (arena = (XPV*)PL_xpvnv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpvnv_arenaroot = 0;

    for (arena = (XPV*)PL_xpvcv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpvcv_arenaroot = 0;

    for (arena = (XPV*)PL_xpvav_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpvav_arenaroot = 0;

    for (arena = (XPV*)PL_xpvhv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpvhv_arenaroot = 0;

    for (arena = (XPV*)PL_xpvmg_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpvmg_arenaroot = 0;

    for (arena = (XPV*)PL_xpvlv_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpvlv_arenaroot = 0;

    for (arena = (XPV*)PL_xpvbm_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_xpvbm_arenaroot = 0;

    for (arena = (XPV*)PL_he_arenaroot; arena; arena = arenanext) {
	arenanext = (XPV*)arena->xpv_pv;
	Safefree(arena);
    }
    PL_he_arenaroot = 0;

    if (PL_nice_chunk)
	Safefree(PL_nice_chunk);
    PL_nice_chunk = Nullch;
    PL_nice_chunk_size = 0;
    PL_sv_arenaroot = 0;
    PL_sv_root = 0;
}

/*
=for apidoc report_uninit

Print appropriate "Use of uninitialized variable" warning

=cut
*/

void
Perl_report_uninit(pTHX)
{
    if (PL_op)
	Perl_warner(aTHX_ packWARN(WARN_UNINITIALIZED), PL_warn_uninit,
		    " in ", OP_DESC(PL_op));
    else
	Perl_warner(aTHX_ packWARN(WARN_UNINITIALIZED), PL_warn_uninit, "", "");
}

/* grab a new IV body from the free list, allocating more if necessary */

STATIC XPVIV*
S_new_xiv(pTHX)
{
    IV* xiv;
    LOCK_SV_MUTEX;
    if (!PL_xiv_root)
	more_xiv();
    xiv = PL_xiv_root;
    /*
     * See comment in more_xiv() -- RAM.
     */
    PL_xiv_root = *(IV**)xiv;
    UNLOCK_SV_MUTEX;
    return (XPVIV*)((char*)xiv - STRUCT_OFFSET(XPVIV, xiv_iv));
}

/* return an IV body to the free list */

STATIC void
S_del_xiv(pTHX_ XPVIV *p)
{
    IV* xiv = (IV*)((char*)(p) + STRUCT_OFFSET(XPVIV, xiv_iv));
    LOCK_SV_MUTEX;
    *(IV**)xiv = PL_xiv_root;
    PL_xiv_root = xiv;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of IV bodies */

STATIC void
S_more_xiv(pTHX)
{
    register IV* xiv;
    register IV* xivend;
    XPV* ptr;
    New(705, ptr, 1008/sizeof(XPV), XPV);
    ptr->xpv_pv = (char*)PL_xiv_arenaroot;	/* linked list of xiv arenas */
    PL_xiv_arenaroot = ptr;			/* to keep Purify happy */

    xiv = (IV*) ptr;
    xivend = &xiv[1008 / sizeof(IV) - 1];
    xiv += (sizeof(XPV) - 1) / sizeof(IV) + 1;	/* fudge by size of XPV */
    PL_xiv_root = xiv;
    while (xiv < xivend) {
	*(IV**)xiv = (IV *)(xiv + 1);
	xiv++;
    }
    *(IV**)xiv = 0;
}

/* grab a new NV body from the free list, allocating more if necessary */

STATIC XPVNV*
S_new_xnv(pTHX)
{
    NV* xnv;
    LOCK_SV_MUTEX;
    if (!PL_xnv_root)
	more_xnv();
    xnv = PL_xnv_root;
    PL_xnv_root = *(NV**)xnv;
    UNLOCK_SV_MUTEX;
    return (XPVNV*)((char*)xnv - STRUCT_OFFSET(XPVNV, xnv_nv));
}

/* return an NV body to the free list */

STATIC void
S_del_xnv(pTHX_ XPVNV *p)
{
    NV* xnv = (NV*)((char*)(p) + STRUCT_OFFSET(XPVNV, xnv_nv));
    LOCK_SV_MUTEX;
    *(NV**)xnv = PL_xnv_root;
    PL_xnv_root = xnv;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of NV bodies */

STATIC void
S_more_xnv(pTHX)
{
    register NV* xnv;
    register NV* xnvend;
    XPV *ptr;
    New(711, ptr, 1008/sizeof(XPV), XPV);
    ptr->xpv_pv = (char*)PL_xnv_arenaroot;
    PL_xnv_arenaroot = ptr;

    xnv = (NV*) ptr;
    xnvend = &xnv[1008 / sizeof(NV) - 1];
    xnv += (sizeof(XPVIV) - 1) / sizeof(NV) + 1; /* fudge by sizeof XPVIV */
    PL_xnv_root = xnv;
    while (xnv < xnvend) {
	*(NV**)xnv = (NV*)(xnv + 1);
	xnv++;
    }
    *(NV**)xnv = 0;
}

/* grab a new struct xrv from the free list, allocating more if necessary */

STATIC XRV*
S_new_xrv(pTHX)
{
    XRV* xrv;
    LOCK_SV_MUTEX;
    if (!PL_xrv_root)
	more_xrv();
    xrv = PL_xrv_root;
    PL_xrv_root = (XRV*)xrv->xrv_rv;
    UNLOCK_SV_MUTEX;
    return xrv;
}

/* return a struct xrv to the free list */

STATIC void
S_del_xrv(pTHX_ XRV *p)
{
    LOCK_SV_MUTEX;
    p->xrv_rv = (SV*)PL_xrv_root;
    PL_xrv_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xrv */

STATIC void
S_more_xrv(pTHX)
{
    register XRV* xrv;
    register XRV* xrvend;
    XPV *ptr;
    New(712, ptr, 1008/sizeof(XPV), XPV);
    ptr->xpv_pv = (char*)PL_xrv_arenaroot;
    PL_xrv_arenaroot = ptr;

    xrv = (XRV*) ptr;
    xrvend = &xrv[1008 / sizeof(XRV) - 1];
    xrv += (sizeof(XPV) - 1) / sizeof(XRV) + 1;
    PL_xrv_root = xrv;
    while (xrv < xrvend) {
	xrv->xrv_rv = (SV*)(xrv + 1);
	xrv++;
    }
    xrv->xrv_rv = 0;
}

/* grab a new struct xpv from the free list, allocating more if necessary */

STATIC XPV*
S_new_xpv(pTHX)
{
    XPV* xpv;
    LOCK_SV_MUTEX;
    if (!PL_xpv_root)
	more_xpv();
    xpv = PL_xpv_root;
    PL_xpv_root = (XPV*)xpv->xpv_pv;
    UNLOCK_SV_MUTEX;
    return xpv;
}

/* return a struct xpv to the free list */

STATIC void
S_del_xpv(pTHX_ XPV *p)
{
    LOCK_SV_MUTEX;
    p->xpv_pv = (char*)PL_xpv_root;
    PL_xpv_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpv */

STATIC void
S_more_xpv(pTHX)
{
    register XPV* xpv;
    register XPV* xpvend;
    New(713, xpv, 1008/sizeof(XPV), XPV);
    xpv->xpv_pv = (char*)PL_xpv_arenaroot;
    PL_xpv_arenaroot = xpv;

    xpvend = &xpv[1008 / sizeof(XPV) - 1];
    PL_xpv_root = ++xpv;
    while (xpv < xpvend) {
	xpv->xpv_pv = (char*)(xpv + 1);
	xpv++;
    }
    xpv->xpv_pv = 0;
}

/* grab a new struct xpviv from the free list, allocating more if necessary */

STATIC XPVIV*
S_new_xpviv(pTHX)
{
    XPVIV* xpviv;
    LOCK_SV_MUTEX;
    if (!PL_xpviv_root)
	more_xpviv();
    xpviv = PL_xpviv_root;
    PL_xpviv_root = (XPVIV*)xpviv->xpv_pv;
    UNLOCK_SV_MUTEX;
    return xpviv;
}

/* return a struct xpviv to the free list */

STATIC void
S_del_xpviv(pTHX_ XPVIV *p)
{
    LOCK_SV_MUTEX;
    p->xpv_pv = (char*)PL_xpviv_root;
    PL_xpviv_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpviv */

STATIC void
S_more_xpviv(pTHX)
{
    register XPVIV* xpviv;
    register XPVIV* xpvivend;
    New(714, xpviv, 1008/sizeof(XPVIV), XPVIV);
    xpviv->xpv_pv = (char*)PL_xpviv_arenaroot;
    PL_xpviv_arenaroot = xpviv;

    xpvivend = &xpviv[1008 / sizeof(XPVIV) - 1];
    PL_xpviv_root = ++xpviv;
    while (xpviv < xpvivend) {
	xpviv->xpv_pv = (char*)(xpviv + 1);
	xpviv++;
    }
    xpviv->xpv_pv = 0;
}

/* grab a new struct xpvnv from the free list, allocating more if necessary */

STATIC XPVNV*
S_new_xpvnv(pTHX)
{
    XPVNV* xpvnv;
    LOCK_SV_MUTEX;
    if (!PL_xpvnv_root)
	more_xpvnv();
    xpvnv = PL_xpvnv_root;
    PL_xpvnv_root = (XPVNV*)xpvnv->xpv_pv;
    UNLOCK_SV_MUTEX;
    return xpvnv;
}

/* return a struct xpvnv to the free list */

STATIC void
S_del_xpvnv(pTHX_ XPVNV *p)
{
    LOCK_SV_MUTEX;
    p->xpv_pv = (char*)PL_xpvnv_root;
    PL_xpvnv_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpvnv */

STATIC void
S_more_xpvnv(pTHX)
{
    register XPVNV* xpvnv;
    register XPVNV* xpvnvend;
    New(715, xpvnv, 1008/sizeof(XPVNV), XPVNV);
    xpvnv->xpv_pv = (char*)PL_xpvnv_arenaroot;
    PL_xpvnv_arenaroot = xpvnv;

    xpvnvend = &xpvnv[1008 / sizeof(XPVNV) - 1];
    PL_xpvnv_root = ++xpvnv;
    while (xpvnv < xpvnvend) {
	xpvnv->xpv_pv = (char*)(xpvnv + 1);
	xpvnv++;
    }
    xpvnv->xpv_pv = 0;
}

/* grab a new struct xpvcv from the free list, allocating more if necessary */

STATIC XPVCV*
S_new_xpvcv(pTHX)
{
    XPVCV* xpvcv;
    LOCK_SV_MUTEX;
    if (!PL_xpvcv_root)
	more_xpvcv();
    xpvcv = PL_xpvcv_root;
    PL_xpvcv_root = (XPVCV*)xpvcv->xpv_pv;
    UNLOCK_SV_MUTEX;
    return xpvcv;
}

/* return a struct xpvcv to the free list */

STATIC void
S_del_xpvcv(pTHX_ XPVCV *p)
{
    LOCK_SV_MUTEX;
    p->xpv_pv = (char*)PL_xpvcv_root;
    PL_xpvcv_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpvcv */

STATIC void
S_more_xpvcv(pTHX)
{
    register XPVCV* xpvcv;
    register XPVCV* xpvcvend;
    New(716, xpvcv, 1008/sizeof(XPVCV), XPVCV);
    xpvcv->xpv_pv = (char*)PL_xpvcv_arenaroot;
    PL_xpvcv_arenaroot = xpvcv;

    xpvcvend = &xpvcv[1008 / sizeof(XPVCV) - 1];
    PL_xpvcv_root = ++xpvcv;
    while (xpvcv < xpvcvend) {
	xpvcv->xpv_pv = (char*)(xpvcv + 1);
	xpvcv++;
    }
    xpvcv->xpv_pv = 0;
}

/* grab a new struct xpvav from the free list, allocating more if necessary */

STATIC XPVAV*
S_new_xpvav(pTHX)
{
    XPVAV* xpvav;
    LOCK_SV_MUTEX;
    if (!PL_xpvav_root)
	more_xpvav();
    xpvav = PL_xpvav_root;
    PL_xpvav_root = (XPVAV*)xpvav->xav_array;
    UNLOCK_SV_MUTEX;
    return xpvav;
}

/* return a struct xpvav to the free list */

STATIC void
S_del_xpvav(pTHX_ XPVAV *p)
{
    LOCK_SV_MUTEX;
    p->xav_array = (char*)PL_xpvav_root;
    PL_xpvav_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpvav */

STATIC void
S_more_xpvav(pTHX)
{
    register XPVAV* xpvav;
    register XPVAV* xpvavend;
    New(717, xpvav, 1008/sizeof(XPVAV), XPVAV);
    xpvav->xav_array = (char*)PL_xpvav_arenaroot;
    PL_xpvav_arenaroot = xpvav;

    xpvavend = &xpvav[1008 / sizeof(XPVAV) - 1];
    PL_xpvav_root = ++xpvav;
    while (xpvav < xpvavend) {
	xpvav->xav_array = (char*)(xpvav + 1);
	xpvav++;
    }
    xpvav->xav_array = 0;
}

/* grab a new struct xpvhv from the free list, allocating more if necessary */

STATIC XPVHV*
S_new_xpvhv(pTHX)
{
    XPVHV* xpvhv;
    LOCK_SV_MUTEX;
    if (!PL_xpvhv_root)
	more_xpvhv();
    xpvhv = PL_xpvhv_root;
    PL_xpvhv_root = (XPVHV*)xpvhv->xhv_array;
    UNLOCK_SV_MUTEX;
    return xpvhv;
}

/* return a struct xpvhv to the free list */

STATIC void
S_del_xpvhv(pTHX_ XPVHV *p)
{
    LOCK_SV_MUTEX;
    p->xhv_array = (char*)PL_xpvhv_root;
    PL_xpvhv_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpvhv */

STATIC void
S_more_xpvhv(pTHX)
{
    register XPVHV* xpvhv;
    register XPVHV* xpvhvend;
    New(718, xpvhv, 1008/sizeof(XPVHV), XPVHV);
    xpvhv->xhv_array = (char*)PL_xpvhv_arenaroot;
    PL_xpvhv_arenaroot = xpvhv;

    xpvhvend = &xpvhv[1008 / sizeof(XPVHV) - 1];
    PL_xpvhv_root = ++xpvhv;
    while (xpvhv < xpvhvend) {
	xpvhv->xhv_array = (char*)(xpvhv + 1);
	xpvhv++;
    }
    xpvhv->xhv_array = 0;
}

/* grab a new struct xpvmg from the free list, allocating more if necessary */

STATIC XPVMG*
S_new_xpvmg(pTHX)
{
    XPVMG* xpvmg;
    LOCK_SV_MUTEX;
    if (!PL_xpvmg_root)
	more_xpvmg();
    xpvmg = PL_xpvmg_root;
    PL_xpvmg_root = (XPVMG*)xpvmg->xpv_pv;
    UNLOCK_SV_MUTEX;
    return xpvmg;
}

/* return a struct xpvmg to the free list */

STATIC void
S_del_xpvmg(pTHX_ XPVMG *p)
{
    LOCK_SV_MUTEX;
    p->xpv_pv = (char*)PL_xpvmg_root;
    PL_xpvmg_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpvmg */

STATIC void
S_more_xpvmg(pTHX)
{
    register XPVMG* xpvmg;
    register XPVMG* xpvmgend;
    New(719, xpvmg, 1008/sizeof(XPVMG), XPVMG);
    xpvmg->xpv_pv = (char*)PL_xpvmg_arenaroot;
    PL_xpvmg_arenaroot = xpvmg;

    xpvmgend = &xpvmg[1008 / sizeof(XPVMG) - 1];
    PL_xpvmg_root = ++xpvmg;
    while (xpvmg < xpvmgend) {
	xpvmg->xpv_pv = (char*)(xpvmg + 1);
	xpvmg++;
    }
    xpvmg->xpv_pv = 0;
}

/* grab a new struct xpvlv from the free list, allocating more if necessary */

STATIC XPVLV*
S_new_xpvlv(pTHX)
{
    XPVLV* xpvlv;
    LOCK_SV_MUTEX;
    if (!PL_xpvlv_root)
	more_xpvlv();
    xpvlv = PL_xpvlv_root;
    PL_xpvlv_root = (XPVLV*)xpvlv->xpv_pv;
    UNLOCK_SV_MUTEX;
    return xpvlv;
}

/* return a struct xpvlv to the free list */

STATIC void
S_del_xpvlv(pTHX_ XPVLV *p)
{
    LOCK_SV_MUTEX;
    p->xpv_pv = (char*)PL_xpvlv_root;
    PL_xpvlv_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpvlv */

STATIC void
S_more_xpvlv(pTHX)
{
    register XPVLV* xpvlv;
    register XPVLV* xpvlvend;
    New(720, xpvlv, 1008/sizeof(XPVLV), XPVLV);
    xpvlv->xpv_pv = (char*)PL_xpvlv_arenaroot;
    PL_xpvlv_arenaroot = xpvlv;

    xpvlvend = &xpvlv[1008 / sizeof(XPVLV) - 1];
    PL_xpvlv_root = ++xpvlv;
    while (xpvlv < xpvlvend) {
	xpvlv->xpv_pv = (char*)(xpvlv + 1);
	xpvlv++;
    }
    xpvlv->xpv_pv = 0;
}

/* grab a new struct xpvbm from the free list, allocating more if necessary */

STATIC XPVBM*
S_new_xpvbm(pTHX)
{
    XPVBM* xpvbm;
    LOCK_SV_MUTEX;
    if (!PL_xpvbm_root)
	more_xpvbm();
    xpvbm = PL_xpvbm_root;
    PL_xpvbm_root = (XPVBM*)xpvbm->xpv_pv;
    UNLOCK_SV_MUTEX;
    return xpvbm;
}

/* return a struct xpvbm to the free list */

STATIC void
S_del_xpvbm(pTHX_ XPVBM *p)
{
    LOCK_SV_MUTEX;
    p->xpv_pv = (char*)PL_xpvbm_root;
    PL_xpvbm_root = p;
    UNLOCK_SV_MUTEX;
}

/* allocate another arena's worth of struct xpvbm */

STATIC void
S_more_xpvbm(pTHX)
{
    register XPVBM* xpvbm;
    register XPVBM* xpvbmend;
    New(721, xpvbm, 1008/sizeof(XPVBM), XPVBM);
    xpvbm->xpv_pv = (char*)PL_xpvbm_arenaroot;
    PL_xpvbm_arenaroot = xpvbm;

    xpvbmend = &xpvbm[1008 / sizeof(XPVBM) - 1];
    PL_xpvbm_root = ++xpvbm;
    while (xpvbm < xpvbmend) {
	xpvbm->xpv_pv = (char*)(xpvbm + 1);
	xpvbm++;
    }
    xpvbm->xpv_pv = 0;
}

#define my_safemalloc(s)	(void*)safemalloc(s)
#define my_safefree(p)	safefree((char*)p)

#ifdef PURIFY

#define new_XIV()	my_safemalloc(sizeof(XPVIV))
#define del_XIV(p)	my_safefree(p)

#define new_XNV()	my_safemalloc(sizeof(XPVNV))
#define del_XNV(p)	my_safefree(p)

#define new_XRV()	my_safemalloc(sizeof(XRV))
#define del_XRV(p)	my_safefree(p)

#define new_XPV()	my_safemalloc(sizeof(XPV))
#define del_XPV(p)	my_safefree(p)

#define new_XPVIV()	my_safemalloc(sizeof(XPVIV))
#define del_XPVIV(p)	my_safefree(p)

#define new_XPVNV()	my_safemalloc(sizeof(XPVNV))
#define del_XPVNV(p)	my_safefree(p)

#define new_XPVCV()	my_safemalloc(sizeof(XPVCV))
#define del_XPVCV(p)	my_safefree(p)

#define new_XPVAV()	my_safemalloc(sizeof(XPVAV))
#define del_XPVAV(p)	my_safefree(p)

#define new_XPVHV()	my_safemalloc(sizeof(XPVHV))
#define del_XPVHV(p)	my_safefree(p)

#define new_XPVMG()	my_safemalloc(sizeof(XPVMG))
#define del_XPVMG(p)	my_safefree(p)

#define new_XPVLV()	my_safemalloc(sizeof(XPVLV))
#define del_XPVLV(p)	my_safefree(p)

#define new_XPVBM()	my_safemalloc(sizeof(XPVBM))
#define del_XPVBM(p)	my_safefree(p)

#else /* !PURIFY */

#define new_XIV()	(void*)new_xiv()
#define del_XIV(p)	del_xiv((XPVIV*) p)

#define new_XNV()	(void*)new_xnv()
#define del_XNV(p)	del_xnv((XPVNV*) p)

#define new_XRV()	(void*)new_xrv()
#define del_XRV(p)	del_xrv((XRV*) p)

#define new_XPV()	(void*)new_xpv()
#define del_XPV(p)	del_xpv((XPV *)p)

#define new_XPVIV()	(void*)new_xpviv()
#define del_XPVIV(p)	del_xpviv((XPVIV *)p)

#define new_XPVNV()	(void*)new_xpvnv()
#define del_XPVNV(p)	del_xpvnv((XPVNV *)p)

#define new_XPVCV()	(void*)new_xpvcv()
#define del_XPVCV(p)	del_xpvcv((XPVCV *)p)

#define new_XPVAV()	(void*)new_xpvav()
#define del_XPVAV(p)	del_xpvav((XPVAV *)p)

#define new_XPVHV()	(void*)new_xpvhv()
#define del_XPVHV(p)	del_xpvhv((XPVHV *)p)

#define new_XPVMG()	(void*)new_xpvmg()
#define del_XPVMG(p)	del_xpvmg((XPVMG *)p)

#define new_XPVLV()	(void*)new_xpvlv()
#define del_XPVLV(p)	del_xpvlv((XPVLV *)p)

#define new_XPVBM()	(void*)new_xpvbm()
#define del_XPVBM(p)	del_xpvbm((XPVBM *)p)

#endif /* PURIFY */

#define new_XPVGV()	my_safemalloc(sizeof(XPVGV))
#define del_XPVGV(p)	my_safefree(p)

#define new_XPVFM()	my_safemalloc(sizeof(XPVFM))
#define del_XPVFM(p)	my_safefree(p)

#define new_XPVIO()	my_safemalloc(sizeof(XPVIO))
#define del_XPVIO(p)	my_safefree(p)

/*
=for apidoc sv_upgrade

Upgrade an SV to a more complex form.  Generally adds a new body type to the
SV, then copies across as much information as possible from the old body.
You generally want to use the C<SvUPGRADE> macro wrapper. See also C<svtype>.

=cut
*/

bool
Perl_sv_upgrade(pTHX_ register SV *sv, U32 mt)
{
    char*	pv = NULL;
    U32		cur = 0;
    U32		len = 0;
    IV		iv = 0;
    NV		nv = 0.0;
    MAGIC*	magic = NULL;
    HV*		stash = Nullhv;

    if (mt != SVt_PV && SvREADONLY(sv) && SvFAKE(sv)) {
	sv_force_normal(sv);
    }

    if (SvTYPE(sv) == mt)
	return TRUE;

    if (mt < SVt_PVIV)
	(void)SvOOK_off(sv);

    switch (SvTYPE(sv)) {
    case SVt_NULL:
	pv	= 0;
	cur	= 0;
	len	= 0;
	iv	= 0;
	nv	= 0.0;
	magic	= 0;
	stash	= 0;
	break;
    case SVt_IV:
	pv	= 0;
	cur	= 0;
	len	= 0;
	iv	= SvIVX(sv);
	nv	= (NV)SvIVX(sv);
	del_XIV(SvANY(sv));
	magic	= 0;
	stash	= 0;
	if (mt == SVt_NV)
	    mt = SVt_PVNV;
	else if (mt < SVt_PVIV)
	    mt = SVt_PVIV;
	break;
    case SVt_NV:
	pv	= 0;
	cur	= 0;
	len	= 0;
	nv	= SvNVX(sv);
	iv	= I_V(nv);
	magic	= 0;
	stash	= 0;
	del_XNV(SvANY(sv));
	SvANY(sv) = 0;
	if (mt < SVt_PVNV)
	    mt = SVt_PVNV;
	break;
    case SVt_RV:
	pv	= (char*)SvRV(sv);
	cur	= 0;
	len	= 0;
	iv	= PTR2IV(pv);
	nv	= PTR2NV(pv);
	del_XRV(SvANY(sv));
	magic	= 0;
	stash	= 0;
	break;
    case SVt_PV:
	pv	= SvPVX(sv);
	cur	= SvCUR(sv);
	len	= SvLEN(sv);
	iv	= 0;
	nv	= 0.0;
	magic	= 0;
	stash	= 0;
	del_XPV(SvANY(sv));
	if (mt <= SVt_IV)
	    mt = SVt_PVIV;
	else if (mt == SVt_NV)
	    mt = SVt_PVNV;
	break;
    case SVt_PVIV:
	pv	= SvPVX(sv);
	cur	= SvCUR(sv);
	len	= SvLEN(sv);
	iv	= SvIVX(sv);
	nv	= 0.0;
	magic	= 0;
	stash	= 0;
	del_XPVIV(SvANY(sv));
	break;
    case SVt_PVNV:
	pv	= SvPVX(sv);
	cur	= SvCUR(sv);
	len	= SvLEN(sv);
	iv	= SvIVX(sv);
	nv	= SvNVX(sv);
	magic	= 0;
	stash	= 0;
	del_XPVNV(SvANY(sv));
	break;
    case SVt_PVMG:
	pv	= SvPVX(sv);
	cur	= SvCUR(sv);
	len	= SvLEN(sv);
	iv	= SvIVX(sv);
	nv	= SvNVX(sv);
	magic	= SvMAGIC(sv);
	stash	= SvSTASH(sv);
	del_XPVMG(SvANY(sv));
	break;
    default:
	Perl_croak(aTHX_ "Can't upgrade that kind of scalar");
    }

    switch (mt) {
    case SVt_NULL:
	Perl_croak(aTHX_ "Can't upgrade to undef");
    case SVt_IV:
	SvANY(sv) = new_XIV();
	SvIVX(sv)	= iv;
	break;
    case SVt_NV:
	SvANY(sv) = new_XNV();
	SvNVX(sv)	= nv;
	break;
    case SVt_RV:
	SvANY(sv) = new_XRV();
	SvRV(sv) = (SV*)pv;
	break;
    case SVt_PV:
	SvANY(sv) = new_XPV();
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	break;
    case SVt_PVIV:
	SvANY(sv) = new_XPVIV();
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	if (SvNIOK(sv))
	    (void)SvIOK_on(sv);
	SvNOK_off(sv);
	break;
    case SVt_PVNV:
	SvANY(sv) = new_XPVNV();
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	break;
    case SVt_PVMG:
	SvANY(sv) = new_XPVMG();
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	break;
    case SVt_PVLV:
	SvANY(sv) = new_XPVLV();
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	LvTARGOFF(sv)	= 0;
	LvTARGLEN(sv)	= 0;
	LvTARG(sv)	= 0;
	LvTYPE(sv)	= 0;
	break;
    case SVt_PVAV:
	SvANY(sv) = new_XPVAV();
	if (pv)
	    Safefree(pv);
	SvPVX(sv)	= 0;
	AvMAX(sv)	= -1;
	AvFILLp(sv)	= -1;
	SvIVX(sv)	= 0;
	SvNVX(sv)	= 0.0;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	AvALLOC(sv)	= 0;
	AvARYLEN(sv)	= 0;
	AvFLAGS(sv)	= 0;
	break;
    case SVt_PVHV:
	SvANY(sv) = new_XPVHV();
	if (pv)
	    Safefree(pv);
	SvPVX(sv)	= 0;
	HvFILL(sv)	= 0;
	HvMAX(sv)	= 0;
	HvTOTALKEYS(sv)	= 0;
	HvPLACEHOLDERS(sv) = 0;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	HvRITER(sv)	= 0;
	HvEITER(sv)	= 0;
	HvPMROOT(sv)	= 0;
	HvNAME(sv)	= 0;
	break;
    case SVt_PVCV:
	SvANY(sv) = new_XPVCV();
	Zero(SvANY(sv), 1, XPVCV);
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	break;
    case SVt_PVGV:
	SvANY(sv) = new_XPVGV();
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	GvGP(sv)	= 0;
	GvNAME(sv)	= 0;
	GvNAMELEN(sv)	= 0;
	GvSTASH(sv)	= 0;
	GvFLAGS(sv)	= 0;
	break;
    case SVt_PVBM:
	SvANY(sv) = new_XPVBM();
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	BmRARE(sv)	= 0;
	BmUSEFUL(sv)	= 0;
	BmPREVIOUS(sv)	= 0;
	break;
    case SVt_PVFM:
	SvANY(sv) = new_XPVFM();
	Zero(SvANY(sv), 1, XPVFM);
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	break;
    case SVt_PVIO:
	SvANY(sv) = new_XPVIO();
	Zero(SvANY(sv), 1, XPVIO);
	SvPVX(sv)	= pv;
	SvCUR(sv)	= cur;
	SvLEN(sv)	= len;
	SvIVX(sv)	= iv;
	SvNVX(sv)	= nv;
	SvMAGIC(sv)	= magic;
	SvSTASH(sv)	= stash;
	IoPAGE_LEN(sv)	= 60;
	break;
    }
    SvFLAGS(sv) &= ~SVTYPEMASK;
    SvFLAGS(sv) |= mt;
    return TRUE;
}

/*
=for apidoc sv_backoff

Remove any string offset. You should normally use the C<SvOOK_off> macro
wrapper instead.

=cut
*/

int
Perl_sv_backoff(pTHX_ register SV *sv)
{
    assert(SvOOK(sv));
    if (SvIVX(sv)) {
	char *s = SvPVX(sv);
	SvLEN(sv) += SvIVX(sv);
	SvPVX(sv) -= SvIVX(sv);
	SvIV_set(sv, 0);
	Move(s, SvPVX(sv), SvCUR(sv)+1, char);
    }
    SvFLAGS(sv) &= ~SVf_OOK;
    return 0;
}

/*
=for apidoc sv_grow

Expands the character buffer in the SV.  If necessary, uses C<sv_unref> and
upgrades the SV to C<SVt_PV>.  Returns a pointer to the character buffer.
Use the C<SvGROW> wrapper instead.

=cut
*/

char *
Perl_sv_grow(pTHX_ register SV *sv, register STRLEN newlen)
{
    register char *s;



#ifdef HAS_64K_LIMIT
    if (newlen >= 0x10000) {
	PerlIO_printf(Perl_debug_log,
		      "Allocation too large: %"UVxf"\n", (UV)newlen);
	my_exit(1);
    }
#endif /* HAS_64K_LIMIT */
    if (SvROK(sv))
	sv_unref(sv);
    if (SvTYPE(sv) < SVt_PV) {
	sv_upgrade(sv, SVt_PV);
	s = SvPVX(sv);
    }
    else if (SvOOK(sv)) {	/* pv is offset? */
	sv_backoff(sv);
	s = SvPVX(sv);
	if (newlen > SvLEN(sv))
	    newlen += 10 * (newlen - SvCUR(sv)); /* avoid copy each time */
#ifdef HAS_64K_LIMIT
	if (newlen >= 0x10000)
	    newlen = 0xFFFF;
#endif
    }
    else
	s = SvPVX(sv);

    if (newlen > SvLEN(sv)) {		/* need more room? */
	if (SvLEN(sv) && s) {
#ifdef MYMALLOC
	    STRLEN l = malloced_size((void*)SvPVX(sv));
	    if (newlen <= l) {
		SvLEN_set(sv, l);
		return s;
	    } else
#endif
	    Renew(s,newlen,char);
	}
        else {
	    /* sv_force_normal_flags() must not try to unshare the new
	       PVX we allocate below. AMS 20010713 */
	    if (SvREADONLY(sv) && SvFAKE(sv)) {
		SvFAKE_off(sv);
		SvREADONLY_off(sv);
	    }
	    New(703, s, newlen, char);
	    if (SvPVX(sv) && SvCUR(sv)) {
	        Move(SvPVX(sv), s, (newlen < SvCUR(sv)) ? newlen : SvCUR(sv), char);
	    }
	}
	SvPV_set(sv, s);
        SvLEN_set(sv, newlen);
    }
    return s;
}

/*
=for apidoc sv_setiv

Copies an integer into the given SV, upgrading first if necessary.
Does not handle 'set' magic.  See also C<sv_setiv_mg>.

=cut
*/

void
Perl_sv_setiv(pTHX_ register SV *sv, IV i)
{
    SV_CHECK_THINKFIRST(sv);
    switch (SvTYPE(sv)) {
    case SVt_NULL:
	sv_upgrade(sv, SVt_IV);
	break;
    case SVt_NV:
	sv_upgrade(sv, SVt_PVNV);
	break;
    case SVt_RV:
    case SVt_PV:
	sv_upgrade(sv, SVt_PVIV);
	break;

    case SVt_PVGV:
    case SVt_PVAV:
    case SVt_PVHV:
    case SVt_PVCV:
    case SVt_PVFM:
    case SVt_PVIO:
	Perl_croak(aTHX_ "Can't coerce %s to integer in %s", sv_reftype(sv,0),
		   OP_DESC(PL_op));
    }
    (void)SvIOK_only(sv);			/* validate number */
    SvIVX(sv) = i;
    SvTAINT(sv);
}

/*
=for apidoc sv_setiv_mg

Like C<sv_setiv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setiv_mg(pTHX_ register SV *sv, IV i)
{
    sv_setiv(sv,i);
    SvSETMAGIC(sv);
}

/*
=for apidoc sv_setuv

Copies an unsigned integer into the given SV, upgrading first if necessary.
Does not handle 'set' magic.  See also C<sv_setuv_mg>.

=cut
*/

void
Perl_sv_setuv(pTHX_ register SV *sv, UV u)
{
    /* With these two if statements:
       u=1.49  s=0.52  cu=72.49  cs=10.64  scripts=270  tests=20865

       without
       u=1.35  s=0.47  cu=73.45  cs=11.43  scripts=270  tests=20865

       If you wish to remove them, please benchmark to see what the effect is
    */
    if (u <= (UV)IV_MAX) {
       sv_setiv(sv, (IV)u);
       return;
    }
    sv_setiv(sv, 0);
    SvIsUV_on(sv);
    SvUVX(sv) = u;
}

/*
=for apidoc sv_setuv_mg

Like C<sv_setuv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setuv_mg(pTHX_ register SV *sv, UV u)
{
    /* With these two if statements:
       u=1.49  s=0.52  cu=72.49  cs=10.64  scripts=270  tests=20865

       without
       u=1.35  s=0.47  cu=73.45  cs=11.43  scripts=270  tests=20865

       If you wish to remove them, please benchmark to see what the effect is
    */
    if (u <= (UV)IV_MAX) {
       sv_setiv(sv, (IV)u);
    } else {
       sv_setiv(sv, 0);
       SvIsUV_on(sv);
       sv_setuv(sv,u);
    }
    SvSETMAGIC(sv);
}

/*
=for apidoc sv_setnv

Copies a double into the given SV, upgrading first if necessary.
Does not handle 'set' magic.  See also C<sv_setnv_mg>.

=cut
*/

void
Perl_sv_setnv(pTHX_ register SV *sv, NV num)
{
    SV_CHECK_THINKFIRST(sv);
    switch (SvTYPE(sv)) {
    case SVt_NULL:
    case SVt_IV:
	sv_upgrade(sv, SVt_NV);
	break;
    case SVt_RV:
    case SVt_PV:
    case SVt_PVIV:
	sv_upgrade(sv, SVt_PVNV);
	break;

    case SVt_PVGV:
    case SVt_PVAV:
    case SVt_PVHV:
    case SVt_PVCV:
    case SVt_PVFM:
    case SVt_PVIO:
	Perl_croak(aTHX_ "Can't coerce %s to number in %s", sv_reftype(sv,0),
		   OP_NAME(PL_op));
    }
    SvNVX(sv) = num;
    (void)SvNOK_only(sv);			/* validate number */
    SvTAINT(sv);
}

/*
=for apidoc sv_setnv_mg

Like C<sv_setnv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setnv_mg(pTHX_ register SV *sv, NV num)
{
    sv_setnv(sv,num);
    SvSETMAGIC(sv);
}

/* Print an "isn't numeric" warning, using a cleaned-up,
 * printable version of the offending string
 */

STATIC void
S_not_a_number(pTHX_ SV *sv)
{
     SV *dsv;
     char tmpbuf[64];
     char *pv;

     if (DO_UTF8(sv)) {
          dsv = sv_2mortal(newSVpv("", 0));
          pv = sv_uni_display(dsv, sv, 10, 0);
     } else {
	  char *d = tmpbuf;
	  char *limit = tmpbuf + sizeof(tmpbuf) - 8;
	  /* each *s can expand to 4 chars + "...\0",
	     i.e. need room for 8 chars */
	
	  char *s, *end;
	  for (s = SvPVX(sv), end = s + SvCUR(sv); s < end && d < limit; s++) {
	       int ch = *s & 0xFF;
	       if (ch & 128 && !isPRINT_LC(ch)) {
		    *d++ = 'M';
		    *d++ = '-';
		    ch &= 127;
	       }
	       if (ch == '\n') {
		    *d++ = '\\';
		    *d++ = 'n';
	       }
	       else if (ch == '\r') {
		    *d++ = '\\';
		    *d++ = 'r';
	       }
	       else if (ch == '\f') {
		    *d++ = '\\';
		    *d++ = 'f';
	       }
	       else if (ch == '\\') {
		    *d++ = '\\';
		    *d++ = '\\';
	       }
	       else if (ch == '\0') {
		    *d++ = '\\';
		    *d++ = '0';
	       }
	       else if (isPRINT_LC(ch))
		    *d++ = ch;
	       else {
		    *d++ = '^';
		    *d++ = toCTRL(ch);
	       }
	  }
	  if (s < end) {
	       *d++ = '.';
	       *d++ = '.';
	       *d++ = '.';
	  }
	  *d = '\0';
	  pv = tmpbuf;
    }

    if (PL_op)
	Perl_warner(aTHX_ packWARN(WARN_NUMERIC),
		    "Argument \"%s\" isn't numeric in %s", pv,
		    OP_DESC(PL_op));
    else
	Perl_warner(aTHX_ packWARN(WARN_NUMERIC),
		    "Argument \"%s\" isn't numeric", pv);
}

/*
=for apidoc looks_like_number

Test if the content of an SV looks like a number (or is a number).
C<Inf> and C<Infinity> are treated as numbers (so will not issue a
non-numeric warning), even if your atof() doesn't grok them.

=cut
*/

I32
Perl_looks_like_number(pTHX_ SV *sv)
{
    register char *sbegin;
    STRLEN len;

    if (SvPOK(sv)) {
	sbegin = SvPVX(sv);
	len = SvCUR(sv);
    }
    else if (SvPOKp(sv))
	sbegin = SvPV(sv, len);
    else
	return 1; /* Historic.  Wrong?  */
    return grok_number(sbegin, len, NULL);
}

/* Actually, ISO C leaves conversion of UV to IV undefined, but
   until proven guilty, assume that things are not that bad... */

/*
   NV_PRESERVES_UV:

   As 64 bit platforms often have an NV that doesn't preserve all bits of
   an IV (an assumption perl has been based on to date) it becomes necessary
   to remove the assumption that the NV always carries enough precision to
   recreate the IV whenever needed, and that the NV is the canonical form.
   Instead, IV/UV and NV need to be given equal rights. So as to not lose
   precision as a side effect of conversion (which would lead to insanity
   and the dragon(s) in t/op/numconvert.t getting very angry) the intent is
   1) to distinguish between IV/UV/NV slots that have cached a valid
      conversion where precision was lost and IV/UV/NV slots that have a
      valid conversion which has lost no precision
   2) to ensure that if a numeric conversion to one form is requested that
      would lose precision, the precise conversion (or differently
      imprecise conversion) is also performed and cached, to prevent
      requests for different numeric formats on the same SV causing
      lossy conversion chains. (lossless conversion chains are perfectly
      acceptable (still))


   flags are used:
   SvIOKp is true if the IV slot contains a valid value
   SvIOK  is true only if the IV value is accurate (UV if SvIOK_UV true)
   SvNOKp is true if the NV slot contains a valid value
   SvNOK  is true only if the NV value is accurate

   so
   while converting from PV to NV, check to see if converting that NV to an
   IV(or UV) would lose accuracy over a direct conversion from PV to
   IV(or UV). If it would, cache both conversions, return NV, but mark
   SV as IOK NOKp (ie not NOK).

   While converting from PV to IV, check to see if converting that IV to an
   NV would lose accuracy over a direct conversion from PV to NV. If it
   would, cache both conversions, flag similarly.

   Before, the SV value "3.2" could become NV=3.2 IV=3 NOK, IOK quite
   correctly because if IV & NV were set NV *always* overruled.
   Now, "3.2" will become NV=3.2 IV=3 NOK, IOKp, because the flag's meaning
   changes - now IV and NV together means that the two are interchangeable:
   SvIVX == (IV) SvNVX && SvNVX == (NV) SvIVX;

   The benefit of this is that operations such as pp_add know that if
   SvIOK is true for both left and right operands, then integer addition
   can be used instead of floating point (for cases where the result won't
   overflow). Before, floating point was always used, which could lead to
   loss of precision compared with integer addition.

   * making IV and NV equal status should make maths accurate on 64 bit
     platforms
   * may speed up maths somewhat if pp_add and friends start to use
     integers when possible instead of fp. (Hopefully the overhead in
     looking for SvIOK and checking for overflow will not outweigh the
     fp to integer speedup)
   * will slow down integer operations (callers of SvIV) on "inaccurate"
     values, as the change from SvIOK to SvIOKp will cause a call into
     sv_2iv each time rather than a macro access direct to the IV slot
   * should speed up number->string conversion on integers as IV is
     favoured when IV and NV are equally accurate

   ####################################################################
   You had better be using SvIOK_notUV if you want an IV for arithmetic:
   SvIOK is true if (IV or UV), so you might be getting (IV)SvUV.
   On the other hand, SvUOK is true iff UV.
   ####################################################################

   Your mileage will vary depending your CPU's relative fp to integer
   performance ratio.
*/

#ifndef NV_PRESERVES_UV
#  define IS_NUMBER_UNDERFLOW_IV 1
#  define IS_NUMBER_UNDERFLOW_UV 2
#  define IS_NUMBER_IV_AND_UV    2
#  define IS_NUMBER_OVERFLOW_IV  4
#  define IS_NUMBER_OVERFLOW_UV  5

/* sv_2iuv_non_preserve(): private routine for use by sv_2iv() and sv_2uv() */

/* For sv_2nv these three cases are "SvNOK and don't bother casting"  */
STATIC int
S_sv_2iuv_non_preserve(pTHX_ register SV *sv, I32 numtype)
{
    DEBUG_c(PerlIO_printf(Perl_debug_log,"sv_2iuv_non '%s', IV=0x%"UVxf" NV=%"NVgf" inttype=%"UVXf"\n", SvPVX(sv), SvIVX(sv), SvNVX(sv), (UV)numtype));
    if (SvNVX(sv) < (NV)IV_MIN) {
	(void)SvIOKp_on(sv);
	(void)SvNOK_on(sv);
	SvIVX(sv) = IV_MIN;
	return IS_NUMBER_UNDERFLOW_IV;
    }
    if (SvNVX(sv) > (NV)UV_MAX) {
	(void)SvIOKp_on(sv);
	(void)SvNOK_on(sv);
	SvIsUV_on(sv);
	SvUVX(sv) = UV_MAX;
	return IS_NUMBER_OVERFLOW_UV;
    }
    (void)SvIOKp_on(sv);
    (void)SvNOK_on(sv);
    /* Can't use strtol etc to convert this string.  (See truth table in
       sv_2iv  */
    if (SvNVX(sv) <= (UV)IV_MAX) {
        SvIVX(sv) = I_V(SvNVX(sv));
        if ((NV)(SvIVX(sv)) == SvNVX(sv)) {
            SvIOK_on(sv); /* Integer is precise. NOK, IOK */
        } else {
            /* Integer is imprecise. NOK, IOKp */
        }
        return SvNVX(sv) < 0 ? IS_NUMBER_UNDERFLOW_UV : IS_NUMBER_IV_AND_UV;
    }
    SvIsUV_on(sv);
    SvUVX(sv) = U_V(SvNVX(sv));
    if ((NV)(SvUVX(sv)) == SvNVX(sv)) {
        if (SvUVX(sv) == UV_MAX) {
            /* As we know that NVs don't preserve UVs, UV_MAX cannot
               possibly be preserved by NV. Hence, it must be overflow.
               NOK, IOKp */
            return IS_NUMBER_OVERFLOW_UV;
        }
        SvIOK_on(sv); /* Integer is precise. NOK, UOK */
    } else {
        /* Integer is imprecise. NOK, IOKp */
    }
    return IS_NUMBER_OVERFLOW_IV;
}
#endif /* !NV_PRESERVES_UV*/

/*
=for apidoc sv_2iv

Return the integer value of an SV, doing any necessary string conversion,
magic etc. Normally used via the C<SvIV(sv)> and C<SvIVx(sv)> macros.

=cut
*/

IV
Perl_sv_2iv(pTHX_ register SV *sv)
{
    if (!sv)
	return 0;
    if (SvGMAGICAL(sv)) {
	mg_get(sv);
	if (SvIOKp(sv))
	    return SvIVX(sv);
	if (SvNOKp(sv)) {
	    return I_V(SvNVX(sv));
	}
	if (SvPOKp(sv) && SvLEN(sv))
	    return asIV(sv);
	if (!SvROK(sv)) {
	    if (!(SvFLAGS(sv) & SVs_PADTMP)) {
		if (ckWARN(WARN_UNINITIALIZED) && !PL_localizing)
		    report_uninit();
	    }
	    return 0;
	}
    }
    if (SvTHINKFIRST(sv)) {
	if (SvROK(sv)) {
	  SV* tmpstr;
          if (SvAMAGIC(sv) && (tmpstr=AMG_CALLun(sv,numer)) &&
                (!SvROK(tmpstr) || (SvRV(tmpstr) != SvRV(sv))))
	      return SvIV(tmpstr);
	  return PTR2IV(SvRV(sv));
	}
	if (SvREADONLY(sv) && SvFAKE(sv)) {
	    sv_force_normal(sv);
	}
	if (SvREADONLY(sv) && !SvOK(sv)) {
	    if (ckWARN(WARN_UNINITIALIZED))
		report_uninit();
	    return 0;
	}
    }
    if (SvIOKp(sv)) {
	if (SvIsUV(sv)) {
	    return (IV)(SvUVX(sv));
	}
	else {
	    return SvIVX(sv);
	}
    }
    if (SvNOKp(sv)) {
	/* erm. not sure. *should* never get NOKp (without NOK) from sv_2nv
	 * without also getting a cached IV/UV from it at the same time
	 * (ie PV->NV conversion should detect loss of accuracy and cache
	 * IV or UV at same time to avoid this.  NWC */

	if (SvTYPE(sv) == SVt_NV)
	    sv_upgrade(sv, SVt_PVNV);

	(void)SvIOKp_on(sv);	/* Must do this first, to clear any SvOOK */
	/* < not <= as for NV doesn't preserve UV, ((NV)IV_MAX+1) will almost
	   certainly cast into the IV range at IV_MAX, whereas the correct
	   answer is the UV IV_MAX +1. Hence < ensures that dodgy boundary
	   cases go to UV */
	if (SvNVX(sv) < (NV)IV_MAX + 0.5) {
	    SvIVX(sv) = I_V(SvNVX(sv));
	    if (SvNVX(sv) == (NV) SvIVX(sv)
#ifndef NV_PRESERVES_UV
		&& (((UV)1 << NV_PRESERVES_UV_BITS) >
		    (UV)(SvIVX(sv) > 0 ? SvIVX(sv) : -SvIVX(sv)))
		/* Don't flag it as "accurately an integer" if the number
		   came from a (by definition imprecise) NV operation, and
		   we're outside the range of NV integer precision */
#endif
		) {
		SvIOK_on(sv);  /* Can this go wrong with rounding? NWC */
		DEBUG_c(PerlIO_printf(Perl_debug_log,
				      "0x%"UVxf" iv(%"NVgf" => %"IVdf") (precise)\n",
				      PTR2UV(sv),
				      SvNVX(sv),
				      SvIVX(sv)));

	    } else {
		/* IV not precise.  No need to convert from PV, as NV
		   conversion would already have cached IV if it detected
		   that PV->IV would be better than PV->NV->IV
		   flags already correct - don't set public IOK.  */
		DEBUG_c(PerlIO_printf(Perl_debug_log,
				      "0x%"UVxf" iv(%"NVgf" => %"IVdf") (imprecise)\n",
				      PTR2UV(sv),
				      SvNVX(sv),
				      SvIVX(sv)));
	    }
	    /* Can the above go wrong if SvIVX == IV_MIN and SvNVX < IV_MIN,
	       but the cast (NV)IV_MIN rounds to a the value less (more
	       negative) than IV_MIN which happens to be equal to SvNVX ??
	       Analogous to 0xFFFFFFFFFFFFFFFF rounding up to NV (2**64) and
	       NV rounding back to 0xFFFFFFFFFFFFFFFF, so UVX == UV(NVX) and
	       (NV)UVX == NVX are both true, but the values differ. :-(
	       Hopefully for 2s complement IV_MIN is something like
	       0x8000000000000000 which will be exact. NWC */
	}
	else {
	    SvUVX(sv) = U_V(SvNVX(sv));
	    if (
		(SvNVX(sv) == (NV) SvUVX(sv))
#ifndef  NV_PRESERVES_UV
		/* Make sure it's not 0xFFFFFFFFFFFFFFFF */
		/*&& (SvUVX(sv) != UV_MAX) irrelevant with code below */
		&& (((UV)1 << NV_PRESERVES_UV_BITS) > SvUVX(sv))
		/* Don't flag it as "accurately an integer" if the number
		   came from a (by definition imprecise) NV operation, and
		   we're outside the range of NV integer precision */
#endif
		)
		SvIOK_on(sv);
	    SvIsUV_on(sv);
	  ret_iv_max:
	    DEBUG_c(PerlIO_printf(Perl_debug_log,
				  "0x%"UVxf" 2iv(%"UVuf" => %"IVdf") (as unsigned)\n",
				  PTR2UV(sv),
				  SvUVX(sv),
				  SvUVX(sv)));
	    return (IV)SvUVX(sv);
	}
    }
    else if (SvPOKp(sv) && SvLEN(sv)) {
	UV value;
	int numtype = grok_number(SvPVX(sv), SvCUR(sv), &value);
	/* We want to avoid a possible problem when we cache an IV which
	   may be later translated to an NV, and the resulting NV is not
	   the same as the direct translation of the initial string
	   (eg 123.456 can shortcut to the IV 123 with atol(), but we must
	   be careful to ensure that the value with the .456 is around if the
	   NV value is requested in the future).
	
	   This means that if we cache such an IV, we need to cache the
	   NV as well.  Moreover, we trade speed for space, and do not
	   cache the NV if we are sure it's not needed.
	 */

	/* SVt_PVNV is one higher than SVt_PVIV, hence this order  */
	if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
	     == IS_NUMBER_IN_UV) {
	    /* It's definitely an integer, only upgrade to PVIV */
	    if (SvTYPE(sv) < SVt_PVIV)
		sv_upgrade(sv, SVt_PVIV);
	    (void)SvIOK_on(sv);
	} else if (SvTYPE(sv) < SVt_PVNV)
	    sv_upgrade(sv, SVt_PVNV);

	/* If NV preserves UV then we only use the UV value if we know that
	   we aren't going to call atof() below. If NVs don't preserve UVs
	   then the value returned may have more precision than atof() will
	   return, even though value isn't perfectly accurate.  */
	if ((numtype & (IS_NUMBER_IN_UV
#ifdef NV_PRESERVES_UV
			| IS_NUMBER_NOT_INT
#endif
	    )) == IS_NUMBER_IN_UV) {
	    /* This won't turn off the public IOK flag if it was set above  */
	    (void)SvIOKp_on(sv);

	    if (!(numtype & IS_NUMBER_NEG)) {
		/* positive */;
		if (value <= (UV)IV_MAX) {
		    SvIVX(sv) = (IV)value;
		} else {
		    SvUVX(sv) = value;
		    SvIsUV_on(sv);
		}
	    } else {
		/* 2s complement assumption  */
		if (value <= (UV)IV_MIN) {
		    SvIVX(sv) = -(IV)value;
		} else {
		    /* Too negative for an IV.  This is a double upgrade, but
		       I'm assuming it will be rare.  */
		    if (SvTYPE(sv) < SVt_PVNV)
			sv_upgrade(sv, SVt_PVNV);
		    SvNOK_on(sv);
		    SvIOK_off(sv);
		    SvIOKp_on(sv);
		    SvNVX(sv) = -(NV)value;
		    SvIVX(sv) = IV_MIN;
		}
	    }
	}
	/* For !NV_PRESERVES_UV and IS_NUMBER_IN_UV and IS_NUMBER_NOT_INT we
           will be in the previous block to set the IV slot, and the next
           block to set the NV slot.  So no else here.  */
	
	if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
	    != IS_NUMBER_IN_UV) {
	    /* It wasn't an (integer that doesn't overflow the UV). */
	    SvNVX(sv) = Atof(SvPVX(sv));

	    if (! numtype && ckWARN(WARN_NUMERIC))
		not_a_number(sv);

#if defined(USE_LONG_DOUBLE)
	    DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2iv(%" PERL_PRIgldbl ")\n",
				  PTR2UV(sv), SvNVX(sv)));
#else
	    DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2iv(%"NVgf")\n",
				  PTR2UV(sv), SvNVX(sv)));
#endif


#ifdef NV_PRESERVES_UV
	    (void)SvIOKp_on(sv);
	    (void)SvNOK_on(sv);
	    if (SvNVX(sv) < (NV)IV_MAX + 0.5) {
		SvIVX(sv) = I_V(SvNVX(sv));
		if ((NV)(SvIVX(sv)) == SvNVX(sv)) {
		    SvIOK_on(sv);
		} else {
		    /* Integer is imprecise. NOK, IOKp */
		}
		/* UV will not work better than IV */
	    } else {
		if (SvNVX(sv) > (NV)UV_MAX) {
		    SvIsUV_on(sv);
		    /* Integer is inaccurate. NOK, IOKp, is UV */
		    SvUVX(sv) = UV_MAX;
		    SvIsUV_on(sv);
		} else {
		    SvUVX(sv) = U_V(SvNVX(sv));
		    /* 0xFFFFFFFFFFFFFFFF not an issue in here */
		    if ((NV)(SvUVX(sv)) == SvNVX(sv)) {
			SvIOK_on(sv);
			SvIsUV_on(sv);
		    } else {
			/* Integer is imprecise. NOK, IOKp, is UV */
			SvIsUV_on(sv);
		    }
		}
		goto ret_iv_max;
	    }
#else /* NV_PRESERVES_UV */
            if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
                == (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT)) {
                /* The IV slot will have been set from value returned by
                   grok_number above.  The NV slot has just been set using
                   Atof.  */
	        SvNOK_on(sv);
                assert (SvIOKp(sv));
            } else {
                if (((UV)1 << NV_PRESERVES_UV_BITS) >
                    U_V(SvNVX(sv) > 0 ? SvNVX(sv) : -SvNVX(sv))) {
                    /* Small enough to preserve all bits. */
                    (void)SvIOKp_on(sv);
                    SvNOK_on(sv);
                    SvIVX(sv) = I_V(SvNVX(sv));
                    if ((NV)(SvIVX(sv)) == SvNVX(sv))
                        SvIOK_on(sv);
                    /* Assumption: first non-preserved integer is < IV_MAX,
                       this NV is in the preserved range, therefore: */
                    if (!(U_V(SvNVX(sv) > 0 ? SvNVX(sv) : -SvNVX(sv))
                          < (UV)IV_MAX)) {
                        Perl_croak(aTHX_ "sv_2iv assumed (U_V(fabs((double)SvNVX(sv))) < (UV)IV_MAX) but SvNVX(sv)=%"NVgf" U_V is 0x%"UVxf", IV_MAX is 0x%"UVxf"\n", SvNVX(sv), U_V(SvNVX(sv)), (UV)IV_MAX);
                    }
                } else {
                    /* IN_UV NOT_INT
                         0      0	already failed to read UV.
                         0      1       already failed to read UV.
                         1      0       you won't get here in this case. IV/UV
                         	        slot set, public IOK, Atof() unneeded.
                         1      1       already read UV.
                       so there's no point in sv_2iuv_non_preserve() attempting
                       to use atol, strtol, strtoul etc.  */
                    if (sv_2iuv_non_preserve (sv, numtype)
                        >= IS_NUMBER_OVERFLOW_IV)
                    goto ret_iv_max;
                }
            }
#endif /* NV_PRESERVES_UV */
	}
    } else  {
	if (ckWARN(WARN_UNINITIALIZED) && !PL_localizing && !(SvFLAGS(sv) & SVs_PADTMP))
	    report_uninit();
	if (SvTYPE(sv) < SVt_IV)
	    /* Typically the caller expects that sv_any is not NULL now.  */
	    sv_upgrade(sv, SVt_IV);
	return 0;
    }
    DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2iv(%"IVdf")\n",
	PTR2UV(sv),SvIVX(sv)));
    return SvIsUV(sv) ? (IV)SvUVX(sv) : SvIVX(sv);
}

/*
=for apidoc sv_2uv

Return the unsigned integer value of an SV, doing any necessary string
conversion, magic etc. Normally used via the C<SvUV(sv)> and C<SvUVx(sv)>
macros.

=cut
*/

UV
Perl_sv_2uv(pTHX_ register SV *sv)
{
    if (!sv)
	return 0;
    if (SvGMAGICAL(sv)) {
	mg_get(sv);
	if (SvIOKp(sv))
	    return SvUVX(sv);
	if (SvNOKp(sv))
	    return U_V(SvNVX(sv));
	if (SvPOKp(sv) && SvLEN(sv))
	    return asUV(sv);
	if (!SvROK(sv)) {
	    if (!(SvFLAGS(sv) & SVs_PADTMP)) {
		if (ckWARN(WARN_UNINITIALIZED) && !PL_localizing)
		    report_uninit();
	    }
	    return 0;
	}
    }
    if (SvTHINKFIRST(sv)) {
	if (SvROK(sv)) {
	  SV* tmpstr;
          if (SvAMAGIC(sv) && (tmpstr=AMG_CALLun(sv,numer)) &&
                (!SvROK(tmpstr) || (SvRV(tmpstr) != SvRV(sv))))
	      return SvUV(tmpstr);
	  return PTR2UV(SvRV(sv));
	}
	if (SvREADONLY(sv) && SvFAKE(sv)) {
	    sv_force_normal(sv);
	}
	if (SvREADONLY(sv) && !SvOK(sv)) {
	    if (ckWARN(WARN_UNINITIALIZED))
		report_uninit();
	    return 0;
	}
    }
    if (SvIOKp(sv)) {
	if (SvIsUV(sv)) {
	    return SvUVX(sv);
	}
	else {
	    return (UV)SvIVX(sv);
	}
    }
    if (SvNOKp(sv)) {
	/* erm. not sure. *should* never get NOKp (without NOK) from sv_2nv
	 * without also getting a cached IV/UV from it at the same time
	 * (ie PV->NV conversion should detect loss of accuracy and cache
	 * IV or UV at same time to avoid this. */
	/* IV-over-UV optimisation - choose to cache IV if possible */

	if (SvTYPE(sv) == SVt_NV)
	    sv_upgrade(sv, SVt_PVNV);

	(void)SvIOKp_on(sv);	/* Must do this first, to clear any SvOOK */
	if (SvNVX(sv) < (NV)IV_MAX + 0.5) {
	    SvIVX(sv) = I_V(SvNVX(sv));
	    if (SvNVX(sv) == (NV) SvIVX(sv)
#ifndef NV_PRESERVES_UV
		&& (((UV)1 << NV_PRESERVES_UV_BITS) >
		    (UV)(SvIVX(sv) > 0 ? SvIVX(sv) : -SvIVX(sv)))
		/* Don't flag it as "accurately an integer" if the number
		   came from a (by definition imprecise) NV operation, and
		   we're outside the range of NV integer precision */
#endif
		) {
		SvIOK_on(sv);  /* Can this go wrong with rounding? NWC */
		DEBUG_c(PerlIO_printf(Perl_debug_log,
				      "0x%"UVxf" uv(%"NVgf" => %"IVdf") (precise)\n",
				      PTR2UV(sv),
				      SvNVX(sv),
				      SvIVX(sv)));

	    } else {
		/* IV not precise.  No need to convert from PV, as NV
		   conversion would already have cached IV if it detected
		   that PV->IV would be better than PV->NV->IV
		   flags already correct - don't set public IOK.  */
		DEBUG_c(PerlIO_printf(Perl_debug_log,
				      "0x%"UVxf" uv(%"NVgf" => %"IVdf") (imprecise)\n",
				      PTR2UV(sv),
				      SvNVX(sv),
				      SvIVX(sv)));
	    }
	    /* Can the above go wrong if SvIVX == IV_MIN and SvNVX < IV_MIN,
	       but the cast (NV)IV_MIN rounds to a the value less (more
	       negative) than IV_MIN which happens to be equal to SvNVX ??
	       Analogous to 0xFFFFFFFFFFFFFFFF rounding up to NV (2**64) and
	       NV rounding back to 0xFFFFFFFFFFFFFFFF, so UVX == UV(NVX) and
	       (NV)UVX == NVX are both true, but the values differ. :-(
	       Hopefully for 2s complement IV_MIN is something like
	       0x8000000000000000 which will be exact. NWC */
	}
	else {
	    SvUVX(sv) = U_V(SvNVX(sv));
	    if (
		(SvNVX(sv) == (NV) SvUVX(sv))
#ifndef  NV_PRESERVES_UV
		/* Make sure it's not 0xFFFFFFFFFFFFFFFF */
		/*&& (SvUVX(sv) != UV_MAX) irrelevant with code below */
		&& (((UV)1 << NV_PRESERVES_UV_BITS) > SvUVX(sv))
		/* Don't flag it as "accurately an integer" if the number
		   came from a (by definition imprecise) NV operation, and
		   we're outside the range of NV integer precision */
#endif
		)
		SvIOK_on(sv);
	    SvIsUV_on(sv);
	    DEBUG_c(PerlIO_printf(Perl_debug_log,
				  "0x%"UVxf" 2uv(%"UVuf" => %"IVdf") (as unsigned)\n",
				  PTR2UV(sv),
				  SvUVX(sv),
				  SvUVX(sv)));
	}
    }
    else if (SvPOKp(sv) && SvLEN(sv)) {
	UV value;
	int numtype = grok_number(SvPVX(sv), SvCUR(sv), &value);

	/* We want to avoid a possible problem when we cache a UV which
	   may be later translated to an NV, and the resulting NV is not
	   the translation of the initial data.
	
	   This means that if we cache such a UV, we need to cache the
	   NV as well.  Moreover, we trade speed for space, and do not
	   cache the NV if not needed.
	 */

	/* SVt_PVNV is one higher than SVt_PVIV, hence this order  */
	if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
	     == IS_NUMBER_IN_UV) {
	    /* It's definitely an integer, only upgrade to PVIV */
	    if (SvTYPE(sv) < SVt_PVIV)
		sv_upgrade(sv, SVt_PVIV);
	    (void)SvIOK_on(sv);
	} else if (SvTYPE(sv) < SVt_PVNV)
	    sv_upgrade(sv, SVt_PVNV);

	/* If NV preserves UV then we only use the UV value if we know that
	   we aren't going to call atof() below. If NVs don't preserve UVs
	   then the value returned may have more precision than atof() will
	   return, even though it isn't accurate.  */
	if ((numtype & (IS_NUMBER_IN_UV
#ifdef NV_PRESERVES_UV
			| IS_NUMBER_NOT_INT
#endif
	    )) == IS_NUMBER_IN_UV) {
	    /* This won't turn off the public IOK flag if it was set above  */
	    (void)SvIOKp_on(sv);

	    if (!(numtype & IS_NUMBER_NEG)) {
		/* positive */;
		if (value <= (UV)IV_MAX) {
		    SvIVX(sv) = (IV)value;
		} else {
		    /* it didn't overflow, and it was positive. */
		    SvUVX(sv) = value;
		    SvIsUV_on(sv);
		}
	    } else {
		/* 2s complement assumption  */
		if (value <= (UV)IV_MIN) {
		    SvIVX(sv) = -(IV)value;
		} else {
		    /* Too negative for an IV.  This is a double upgrade, but
		       I'm assuming it will be rare.  */
		    if (SvTYPE(sv) < SVt_PVNV)
			sv_upgrade(sv, SVt_PVNV);
		    SvNOK_on(sv);
		    SvIOK_off(sv);
		    SvIOKp_on(sv);
		    SvNVX(sv) = -(NV)value;
		    SvIVX(sv) = IV_MIN;
		}
	    }
	}
	
	if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
	    != IS_NUMBER_IN_UV) {
	    /* It wasn't an integer, or it overflowed the UV. */
	    SvNVX(sv) = Atof(SvPVX(sv));

            if (! numtype && ckWARN(WARN_NUMERIC))
		    not_a_number(sv);

#if defined(USE_LONG_DOUBLE)
            DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2uv(%" PERL_PRIgldbl ")\n",
                                  PTR2UV(sv), SvNVX(sv)));
#else
            DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2uv(%"NVgf")\n",
                                  PTR2UV(sv), SvNVX(sv)));
#endif

#ifdef NV_PRESERVES_UV
            (void)SvIOKp_on(sv);
            (void)SvNOK_on(sv);
            if (SvNVX(sv) < (NV)IV_MAX + 0.5) {
                SvIVX(sv) = I_V(SvNVX(sv));
                if ((NV)(SvIVX(sv)) == SvNVX(sv)) {
                    SvIOK_on(sv);
                } else {
                    /* Integer is imprecise. NOK, IOKp */
                }
                /* UV will not work better than IV */
            } else {
                if (SvNVX(sv) > (NV)UV_MAX) {
                    SvIsUV_on(sv);
                    /* Integer is inaccurate. NOK, IOKp, is UV */
                    SvUVX(sv) = UV_MAX;
                    SvIsUV_on(sv);
                } else {
                    SvUVX(sv) = U_V(SvNVX(sv));
                    /* 0xFFFFFFFFFFFFFFFF not an issue in here, NVs
                       NV preservse UV so can do correct comparison.  */
                    if ((NV)(SvUVX(sv)) == SvNVX(sv)) {
                        SvIOK_on(sv);
                        SvIsUV_on(sv);
                    } else {
                        /* Integer is imprecise. NOK, IOKp, is UV */
                        SvIsUV_on(sv);
                    }
                }
            }
#else /* NV_PRESERVES_UV */
            if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
                == (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT)) {
                /* The UV slot will have been set from value returned by
                   grok_number above.  The NV slot has just been set using
                   Atof.  */
	        SvNOK_on(sv);
                assert (SvIOKp(sv));
            } else {
                if (((UV)1 << NV_PRESERVES_UV_BITS) >
                    U_V(SvNVX(sv) > 0 ? SvNVX(sv) : -SvNVX(sv))) {
                    /* Small enough to preserve all bits. */
                    (void)SvIOKp_on(sv);
                    SvNOK_on(sv);
                    SvIVX(sv) = I_V(SvNVX(sv));
                    if ((NV)(SvIVX(sv)) == SvNVX(sv))
                        SvIOK_on(sv);
                    /* Assumption: first non-preserved integer is < IV_MAX,
                       this NV is in the preserved range, therefore: */
                    if (!(U_V(SvNVX(sv) > 0 ? SvNVX(sv) : -SvNVX(sv))
                          < (UV)IV_MAX)) {
                        Perl_croak(aTHX_ "sv_2uv assumed (U_V(fabs((double)SvNVX(sv))) < (UV)IV_MAX) but SvNVX(sv)=%"NVgf" U_V is 0x%"UVxf", IV_MAX is 0x%"UVxf"\n", SvNVX(sv), U_V(SvNVX(sv)), (UV)IV_MAX);
                    }
                } else
                    sv_2iuv_non_preserve (sv, numtype);
            }
#endif /* NV_PRESERVES_UV */
	}
    }
    else  {
	if (!(SvFLAGS(sv) & SVs_PADTMP)) {
	    if (ckWARN(WARN_UNINITIALIZED) && !PL_localizing)
		report_uninit();
	}
	if (SvTYPE(sv) < SVt_IV)
	    /* Typically the caller expects that sv_any is not NULL now.  */
	    sv_upgrade(sv, SVt_IV);
	return 0;
    }

    DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2uv(%"UVuf")\n",
			  PTR2UV(sv),SvUVX(sv)));
    return SvIsUV(sv) ? SvUVX(sv) : (UV)SvIVX(sv);
}

/*
=for apidoc sv_2nv

Return the num value of an SV, doing any necessary string or integer
conversion, magic etc. Normally used via the C<SvNV(sv)> and C<SvNVx(sv)>
macros.

=cut
*/

NV
Perl_sv_2nv(pTHX_ register SV *sv)
{
    if (!sv)
	return 0.0;
    if (SvGMAGICAL(sv)) {
	mg_get(sv);
	if (SvNOKp(sv))
	    return SvNVX(sv);
	if (SvPOKp(sv) && SvLEN(sv)) {
	    if (ckWARN(WARN_NUMERIC) && !SvIOKp(sv) &&
		!grok_number(SvPVX(sv), SvCUR(sv), NULL))
		not_a_number(sv);
	    return Atof(SvPVX(sv));
	}
	if (SvIOKp(sv)) {
	    if (SvIsUV(sv))
		return (NV)SvUVX(sv);
	    else
		return (NV)SvIVX(sv);
	}	
        if (!SvROK(sv)) {
	    if (!(SvFLAGS(sv) & SVs_PADTMP)) {
		if (ckWARN(WARN_UNINITIALIZED) && !PL_localizing)
		    report_uninit();
	    }
            return 0;
        }
    }
    if (SvTHINKFIRST(sv)) {
	if (SvROK(sv)) {
	  SV* tmpstr;
          if (SvAMAGIC(sv) && (tmpstr=AMG_CALLun(sv,numer)) &&
                (!SvROK(tmpstr) || (SvRV(tmpstr) != SvRV(sv))))
	      return SvNV(tmpstr);
	  return PTR2NV(SvRV(sv));
	}
	if (SvREADONLY(sv) && SvFAKE(sv)) {
	    sv_force_normal(sv);
	}
	if (SvREADONLY(sv) && !SvOK(sv)) {
	    if (ckWARN(WARN_UNINITIALIZED))
		report_uninit();
	    return 0.0;
	}
    }
    if (SvTYPE(sv) < SVt_NV) {
	if (SvTYPE(sv) == SVt_IV)
	    sv_upgrade(sv, SVt_PVNV);
	else
	    sv_upgrade(sv, SVt_NV);
#ifdef USE_LONG_DOUBLE
	DEBUG_c({
	    STORE_NUMERIC_LOCAL_SET_STANDARD();
	    PerlIO_printf(Perl_debug_log,
			  "0x%"UVxf" num(%" PERL_PRIgldbl ")\n",
			  PTR2UV(sv), SvNVX(sv));
	    RESTORE_NUMERIC_LOCAL();
	});
#else
	DEBUG_c({
	    STORE_NUMERIC_LOCAL_SET_STANDARD();
	    PerlIO_printf(Perl_debug_log, "0x%"UVxf" num(%"NVgf")\n",
			  PTR2UV(sv), SvNVX(sv));
	    RESTORE_NUMERIC_LOCAL();
	});
#endif
    }
    else if (SvTYPE(sv) < SVt_PVNV)
	sv_upgrade(sv, SVt_PVNV);
    if (SvNOKp(sv)) {
        return SvNVX(sv);
    }
    if (SvIOKp(sv)) {
	SvNVX(sv) = SvIsUV(sv) ? (NV)SvUVX(sv) : (NV)SvIVX(sv);
#ifdef NV_PRESERVES_UV
	SvNOK_on(sv);
#else
	/* Only set the public NV OK flag if this NV preserves the IV  */
	/* Check it's not 0xFFFFFFFFFFFFFFFF */
	if (SvIsUV(sv) ? ((SvUVX(sv) != UV_MAX)&&(SvUVX(sv) == U_V(SvNVX(sv))))
		       : (SvIVX(sv) == I_V(SvNVX(sv))))
	    SvNOK_on(sv);
	else
	    SvNOKp_on(sv);
#endif
    }
    else if (SvPOKp(sv) && SvLEN(sv)) {
	UV value;
	int numtype = grok_number(SvPVX(sv), SvCUR(sv), &value);
	if (ckWARN(WARN_NUMERIC) && !SvIOKp(sv) && !numtype)
	    not_a_number(sv);
#ifdef NV_PRESERVES_UV
	if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
	    == IS_NUMBER_IN_UV) {
	    /* It's definitely an integer */
	    SvNVX(sv) = (numtype & IS_NUMBER_NEG) ? -(NV)value : (NV)value;
	} else
	    SvNVX(sv) = Atof(SvPVX(sv));
	SvNOK_on(sv);
#else
	SvNVX(sv) = Atof(SvPVX(sv));
	/* Only set the public NV OK flag if this NV preserves the value in
	   the PV at least as well as an IV/UV would.
	   Not sure how to do this 100% reliably. */
	/* if that shift count is out of range then Configure's test is
	   wonky. We shouldn't be in here with NV_PRESERVES_UV_BITS ==
	   UV_BITS */
	if (((UV)1 << NV_PRESERVES_UV_BITS) >
	    U_V(SvNVX(sv) > 0 ? SvNVX(sv) : -SvNVX(sv))) {
	    SvNOK_on(sv); /* Definitely small enough to preserve all bits */
	} else if (!(numtype & IS_NUMBER_IN_UV)) {
            /* Can't use strtol etc to convert this string, so don't try.
               sv_2iv and sv_2uv will use the NV to convert, not the PV.  */
            SvNOK_on(sv);
        } else {
            /* value has been set.  It may not be precise.  */
	    if ((numtype & IS_NUMBER_NEG) && (value > (UV)IV_MIN)) {
		/* 2s complement assumption for (UV)IV_MIN  */
                SvNOK_on(sv); /* Integer is too negative.  */
            } else {
                SvNOKp_on(sv);
                SvIOKp_on(sv);

                if (numtype & IS_NUMBER_NEG) {
                    SvIVX(sv) = -(IV)value;
                } else if (value <= (UV)IV_MAX) {
		    SvIVX(sv) = (IV)value;
		} else {
		    SvUVX(sv) = value;
		    SvIsUV_on(sv);
		}

                if (numtype & IS_NUMBER_NOT_INT) {
                    /* I believe that even if the original PV had decimals,
                       they are lost beyond the limit of the FP precision.
                       However, neither is canonical, so both only get p
                       flags.  NWC, 2000/11/25 */
                    /* Both already have p flags, so do nothing */
                } else {
                    NV nv = SvNVX(sv);
                    if (SvNVX(sv) < (NV)IV_MAX + 0.5) {
                        if (SvIVX(sv) == I_V(nv)) {
                            SvNOK_on(sv);
                            SvIOK_on(sv);
                        } else {
                            SvIOK_on(sv);
                            /* It had no "." so it must be integer.  */
                        }
                    } else {
                        /* between IV_MAX and NV(UV_MAX).
                           Could be slightly > UV_MAX */

                        if (numtype & IS_NUMBER_NOT_INT) {
                            /* UV and NV both imprecise.  */
                        } else {
                            UV nv_as_uv = U_V(nv);

                            if (value == nv_as_uv && SvUVX(sv) != UV_MAX) {
                                SvNOK_on(sv);
                                SvIOK_on(sv);
                            } else {
                                SvIOK_on(sv);
                            }
                        }
                    }
                }
            }
        }
#endif /* NV_PRESERVES_UV */
    }
    else  {
	if (ckWARN(WARN_UNINITIALIZED) && !PL_localizing && !(SvFLAGS(sv) & SVs_PADTMP))
	    report_uninit();
	if (SvTYPE(sv) < SVt_NV)
	    /* Typically the caller expects that sv_any is not NULL now.  */
	    /* XXX Ilya implies that this is a bug in callers that assume this
	       and ideally should be fixed.  */
	    sv_upgrade(sv, SVt_NV);
	return 0.0;
    }
#if defined(USE_LONG_DOUBLE)
    DEBUG_c({
	STORE_NUMERIC_LOCAL_SET_STANDARD();
	PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2nv(%" PERL_PRIgldbl ")\n",
		      PTR2UV(sv), SvNVX(sv));
	RESTORE_NUMERIC_LOCAL();
    });
#else
    DEBUG_c({
	STORE_NUMERIC_LOCAL_SET_STANDARD();
	PerlIO_printf(Perl_debug_log, "0x%"UVxf" 1nv(%"NVgf")\n",
		      PTR2UV(sv), SvNVX(sv));
	RESTORE_NUMERIC_LOCAL();
    });
#endif
    return SvNVX(sv);
}

/* asIV(): extract an integer from the string value of an SV.
 * Caller must validate PVX  */

STATIC IV
S_asIV(pTHX_ SV *sv)
{
    UV value;
    int numtype = grok_number(SvPVX(sv), SvCUR(sv), &value);

    if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
	== IS_NUMBER_IN_UV) {
	/* It's definitely an integer */
	if (numtype & IS_NUMBER_NEG) {
	    if (value < (UV)IV_MIN)
		return -(IV)value;
	} else {
	    if (value < (UV)IV_MAX)
		return (IV)value;
	}
    }
    if (!numtype) {
	if (ckWARN(WARN_NUMERIC))
	    not_a_number(sv);
    }
    return I_V(Atof(SvPVX(sv)));
}

/* asUV(): extract an unsigned integer from the string value of an SV
 * Caller must validate PVX  */

STATIC UV
S_asUV(pTHX_ SV *sv)
{
    UV value;
    int numtype = grok_number(SvPVX(sv), SvCUR(sv), &value);

    if ((numtype & (IS_NUMBER_IN_UV | IS_NUMBER_NOT_INT))
	== IS_NUMBER_IN_UV) {
	/* It's definitely an integer */
	if (!(numtype & IS_NUMBER_NEG))
	    return value;
    }
    if (!numtype) {
	if (ckWARN(WARN_NUMERIC))
	    not_a_number(sv);
    }
    return U_V(Atof(SvPVX(sv)));
}

/*
=for apidoc sv_2pv_nolen

Like C<sv_2pv()>, but doesn't return the length too. You should usually
use the macro wrapper C<SvPV_nolen(sv)> instead.
=cut
*/

char *
Perl_sv_2pv_nolen(pTHX_ register SV *sv)
{
    STRLEN n_a;
    return sv_2pv(sv, &n_a);
}

/* uiv_2buf(): private routine for use by sv_2pv_flags(): print an IV or
 * UV as a string towards the end of buf, and return pointers to start and
 * end of it.
 *
 * We assume that buf is at least TYPE_CHARS(UV) long.
 */

static char *
uiv_2buf(char *buf, IV iv, UV uv, int is_uv, char **peob)
{
    char *ptr = buf + TYPE_CHARS(UV);
    char *ebuf = ptr;
    int sign;

    if (is_uv)
	sign = 0;
    else if (iv >= 0) {
	uv = iv;
	sign = 0;
    } else {
	uv = -iv;
	sign = 1;
    }
    do {
	*--ptr = '0' + (char)(uv % 10);
    } while (uv /= 10);
    if (sign)
	*--ptr = '-';
    *peob = ebuf;
    return ptr;
}

/* sv_2pv() is now a macro using Perl_sv_2pv_flags();
 * this function provided for binary compatibility only
 */

char *
Perl_sv_2pv(pTHX_ register SV *sv, STRLEN *lp)
{
    return sv_2pv_flags(sv, lp, SV_GMAGIC);
}

/*
=for apidoc sv_2pv_flags

Returns a pointer to the string value of an SV, and sets *lp to its length.
If flags includes SV_GMAGIC, does an mg_get() first. Coerces sv to a string
if necessary.
Normally invoked via the C<SvPV_flags> macro. C<sv_2pv()> and C<sv_2pv_nomg>
usually end up here too.

=cut
*/

char *
Perl_sv_2pv_flags(pTHX_ register SV *sv, STRLEN *lp, I32 flags)
{
    register char *s;
    int olderrno;
    SV *tsv, *origsv;
    char tbuf[64];	/* Must fit sprintf/Gconvert of longest IV/NV */
    char *tmpbuf = tbuf;

    if (!sv) {
	*lp = 0;
	return "";
    }
    if (SvGMAGICAL(sv)) {
	if (flags & SV_GMAGIC)
	    mg_get(sv);
	if (SvPOKp(sv)) {
	    *lp = SvCUR(sv);
	    return SvPVX(sv);
	}
	if (SvIOKp(sv)) {
	    if (SvIsUV(sv))
		(void)sprintf(tmpbuf,"%"UVuf, (UV)SvUVX(sv));
	    else
		(void)sprintf(tmpbuf,"%"IVdf, (IV)SvIVX(sv));
	    tsv = Nullsv;
	    goto tokensave;
	}
	if (SvNOKp(sv)) {
	    Gconvert(SvNVX(sv), NV_DIG, 0, tmpbuf);
	    tsv = Nullsv;
	    goto tokensave;
	}
        if (!SvROK(sv)) {
	    if (!(SvFLAGS(sv) & SVs_PADTMP)) {
		if (ckWARN(WARN_UNINITIALIZED) && !PL_localizing)
		    report_uninit();
	    }
            *lp = 0;
            return "";
        }
    }
    if (SvTHINKFIRST(sv)) {
	if (SvROK(sv)) {
	    SV* tmpstr;
            if (SvAMAGIC(sv) && (tmpstr=AMG_CALLun(sv,string)) &&
                (!SvROK(tmpstr) || (SvRV(tmpstr) != SvRV(sv)))) {
                char *pv = SvPV(tmpstr, *lp);
                if (SvUTF8(tmpstr))
                    SvUTF8_on(sv);
                else
                    SvUTF8_off(sv);
                return pv;
            }
	    origsv = sv;
	    sv = (SV*)SvRV(sv);
	    if (!sv)
		s = "NULLREF";
	    else {
		MAGIC *mg;
		
		switch (SvTYPE(sv)) {
		case SVt_PVMG:
		    if ( ((SvFLAGS(sv) &
			   (SVs_OBJECT|SVf_OK|SVs_GMG|SVs_SMG|SVs_RMG))
			  == (SVs_OBJECT|SVs_SMG))
			 && (mg = mg_find(sv, PERL_MAGIC_qr))) {
			regexp *re = (regexp *)mg->mg_obj;

			if (!mg->mg_ptr) {
			    char *fptr = "msix";
			    char reflags[6];
			    char ch;
			    int left = 0;
			    int right = 4;
                            char need_newline = 0;
 			    U16 reganch = (U16)((re->reganch & PMf_COMPILETIME) >> 12);

 			    while((ch = *fptr++)) {
 				if(reganch & 1) {
 				    reflags[left++] = ch;
 				}
 				else {
 				    reflags[right--] = ch;
 				}
 				reganch >>= 1;
 			    }
 			    if(left != 4) {
 				reflags[left] = '-';
 				left = 5;
 			    }

			    mg->mg_len = re->prelen + 4 + left;
                            /*
                             * If /x was used, we have to worry about a regex
                             * ending with a comment later being embedded
                             * within another regex. If so, we don't want this
                             * regex's "commentization" to leak out to the
                             * right part of the enclosing regex, we must cap
                             * it with a newline.
                             *
                             * So, if /x was used, we scan backwards from the
                             * end of the regex. If we find a '#' before we
                             * find a newline, we need to add a newline
                             * ourself. If we find a '\n' first (or if we
                             * don't find '#' or '\n'), we don't need to add
                             * anything.  -jfriedl
                             */
                            if (PMf_EXTENDED & re->reganch)
                            {
                                char *endptr = re->precomp + re->prelen;
                                while (endptr >= re->precomp)
                                {
                                    char c = *(endptr--);
                                    if (c == '\n')
                                        break; /* don't need another */
                                    if (c == '#') {
                                        /* we end while in a comment, so we
                                           need a newline */
                                        mg->mg_len++; /* save space for it */
                                        need_newline = 1; /* note to add it */
					break;
                                    }
                                }
                            }

			    New(616, mg->mg_ptr, mg->mg_len + 1 + left, char);
			    Copy("(?", mg->mg_ptr, 2, char);
			    Copy(reflags, mg->mg_ptr+2, left, char);
			    Copy(":", mg->mg_ptr+left+2, 1, char);
			    Copy(re->precomp, mg->mg_ptr+3+left, re->prelen, char);
                            if (need_newline)
                                mg->mg_ptr[mg->mg_len - 2] = '\n';
			    mg->mg_ptr[mg->mg_len - 1] = ')';
			    mg->mg_ptr[mg->mg_len] = 0;
			}
			PL_reginterp_cnt += re->program[0].next_off;

			if (re->reganch & ROPT_UTF8)
			    SvUTF8_on(origsv);
			else
			    SvUTF8_off(origsv);
			*lp = mg->mg_len;
			return mg->mg_ptr;
		    }
					/* Fall through */
		case SVt_NULL:
		case SVt_IV:
		case SVt_NV:
		case SVt_RV:
		case SVt_PV:
		case SVt_PVIV:
		case SVt_PVNV:
		case SVt_PVBM:	if (SvROK(sv))
				    s = "REF";
				else
				    s = "SCALAR";		break;
		case SVt_PVLV:	s = SvROK(sv) ? "REF"
				/* tied lvalues should appear to be
				 * scalars for backwards compatitbility */
				: (LvTYPE(sv) == 't' || LvTYPE(sv) == 'T')
				    ? "SCALAR" : "LVALUE";	break;
		case SVt_PVAV:	s = "ARRAY";			break;
		case SVt_PVHV:	s = "HASH";			break;
		case SVt_PVCV:	s = "CODE";			break;
		case SVt_PVGV:	s = "GLOB";			break;
		case SVt_PVFM:	s = "FORMAT";			break;
		case SVt_PVIO:	s = "IO";			break;
		default:	s = "UNKNOWN";			break;
		}
		tsv = NEWSV(0,0);
		if (SvOBJECT(sv)) {
                    HV *svs = SvSTASH(sv);
		    Perl_sv_setpvf(
                        aTHX_ tsv, "%s=%s",
                        /* [20011101.072] This bandaid for C<package;>
                           should eventually be removed. AMS 20011103 */
                        (svs ? HvNAME(svs) : "<none>"), s
                    );
                }
		else
		    sv_setpv(tsv, s);
		Perl_sv_catpvf(aTHX_ tsv, "(0x%"UVxf")", PTR2UV(sv));
		goto tokensaveref;
	    }
	    *lp = strlen(s);
	    return s;
	}
	if (SvREADONLY(sv) && !SvOK(sv)) {
	    if (ckWARN(WARN_UNINITIALIZED))
		report_uninit();
	    *lp = 0;
	    return "";
	}
    }
    if (SvIOK(sv) || ((SvIOKp(sv) && !SvNOKp(sv)))) {
	/* I'm assuming that if both IV and NV are equally valid then
	   converting the IV is going to be more efficient */
	U32 isIOK = SvIOK(sv);
	U32 isUIOK = SvIsUV(sv);
	char buf[TYPE_CHARS(UV)];
	char *ebuf, *ptr;

	if (SvTYPE(sv) < SVt_PVIV)
	    sv_upgrade(sv, SVt_PVIV);
	if (isUIOK)
	    ptr = uiv_2buf(buf, 0, SvUVX(sv), 1, &ebuf);
	else
	    ptr = uiv_2buf(buf, SvIVX(sv), 0, 0, &ebuf);
	SvGROW(sv, (STRLEN)(ebuf - ptr + 1));	/* inlined from sv_setpvn */
	Move(ptr,SvPVX(sv),ebuf - ptr,char);
	SvCUR_set(sv, ebuf - ptr);
	s = SvEND(sv);
	*s = '\0';
	if (isIOK)
	    SvIOK_on(sv);
	else
	    SvIOKp_on(sv);
	if (isUIOK)
	    SvIsUV_on(sv);
    }
    else if (SvNOKp(sv)) {
	if (SvTYPE(sv) < SVt_PVNV)
	    sv_upgrade(sv, SVt_PVNV);
	/* The +20 is pure guesswork.  Configure test needed. --jhi */
	SvGROW(sv, NV_DIG + 20);
	s = SvPVX(sv);
	olderrno = errno;	/* some Xenix systems wipe out errno here */
#ifdef apollo
	if (SvNVX(sv) == 0.0)
	    (void)strcpy(s,"0");
	else
#endif /*apollo*/
	{
	    Gconvert(SvNVX(sv), NV_DIG, 0, s);
	}
	errno = olderrno;
#ifdef FIXNEGATIVEZERO
        if (*s == '-' && s[1] == '0' && !s[2])
	    strcpy(s,"0");
#endif
	while (*s) s++;
#ifdef hcx
	if (s[-1] == '.')
	    *--s = '\0';
#endif
    }
    else {
	if (ckWARN(WARN_UNINITIALIZED)
	    && !PL_localizing && !(SvFLAGS(sv) & SVs_PADTMP))
	    report_uninit();
	*lp = 0;
	if (SvTYPE(sv) < SVt_PV)
	    /* Typically the caller expects that sv_any is not NULL now.  */
	    sv_upgrade(sv, SVt_PV);
	return "";
    }
    *lp = s - SvPVX(sv);
    SvCUR_set(sv, *lp);
    SvPOK_on(sv);
    DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2pv(%s)\n",
			  PTR2UV(sv),SvPVX(sv)));
    return SvPVX(sv);

  tokensave:
    if (SvROK(sv)) {	/* XXX Skip this when sv_pvn_force calls */
	/* Sneaky stuff here */

      tokensaveref:
	if (!tsv)
	    tsv = newSVpv(tmpbuf, 0);
	sv_2mortal(tsv);
	*lp = SvCUR(tsv);
	return SvPVX(tsv);
    }
    else {
	STRLEN len;
	char *t;

	if (tsv) {
	    sv_2mortal(tsv);
	    t = SvPVX(tsv);
	    len = SvCUR(tsv);
	}
	else {
	    t = tmpbuf;
	    len = strlen(tmpbuf);
	}
#ifdef FIXNEGATIVEZERO
	if (len == 2 && t[0] == '-' && t[1] == '0') {
	    t = "0";
	    len = 1;
	}
#endif
	(void)SvUPGRADE(sv, SVt_PV);
	*lp = len;
	s = SvGROW(sv, len + 1);
	SvCUR_set(sv, len);
	(void)strcpy(s, t);
	SvPOKp_on(sv);
	return s;
    }
}

/*
=for apidoc sv_copypv

Copies a stringified representation of the source SV into the
destination SV.  Automatically performs any necessary mg_get and
coercion of numeric values into strings.  Guaranteed to preserve
UTF-8 flag even from overloaded objects.  Similar in nature to
sv_2pv[_flags] but operates directly on an SV instead of just the
string.  Mostly uses sv_2pv_flags to do its work, except when that
would lose the UTF-8'ness of the PV.

=cut
*/

void
Perl_sv_copypv(pTHX_ SV *dsv, register SV *ssv)
{
    STRLEN len;
    char *s;
    s = SvPV(ssv,len);
    sv_setpvn(dsv,s,len);
    if (SvUTF8(ssv))
	SvUTF8_on(dsv);
    else
	SvUTF8_off(dsv);
}

/*
=for apidoc sv_2pvbyte_nolen

Return a pointer to the byte-encoded representation of the SV.
May cause the SV to be downgraded from UTF-8 as a side-effect.

Usually accessed via the C<SvPVbyte_nolen> macro.

=cut
*/

char *
Perl_sv_2pvbyte_nolen(pTHX_ register SV *sv)
{
    STRLEN n_a;
    return sv_2pvbyte(sv, &n_a);
}

/*
=for apidoc sv_2pvbyte

Return a pointer to the byte-encoded representation of the SV, and set *lp
to its length.  May cause the SV to be downgraded from UTF-8 as a
side-effect.

Usually accessed via the C<SvPVbyte> macro.

=cut
*/

char *
Perl_sv_2pvbyte(pTHX_ register SV *sv, STRLEN *lp)
{
    sv_utf8_downgrade(sv,0);
    return SvPV(sv,*lp);
}

/*
=for apidoc sv_2pvutf8_nolen

Return a pointer to the UTF-8-encoded representation of the SV.
May cause the SV to be upgraded to UTF-8 as a side-effect.

Usually accessed via the C<SvPVutf8_nolen> macro.

=cut
*/

char *
Perl_sv_2pvutf8_nolen(pTHX_ register SV *sv)
{
    STRLEN n_a;
    return sv_2pvutf8(sv, &n_a);
}

/*
=for apidoc sv_2pvutf8

Return a pointer to the UTF-8-encoded representation of the SV, and set *lp
to its length.  May cause the SV to be upgraded to UTF-8 as a side-effect.

Usually accessed via the C<SvPVutf8> macro.

=cut
*/

char *
Perl_sv_2pvutf8(pTHX_ register SV *sv, STRLEN *lp)
{
    sv_utf8_upgrade(sv);
    return SvPV(sv,*lp);
}

/*
=for apidoc sv_2bool

This function is only called on magical items, and is only used by
sv_true() or its macro equivalent.

=cut
*/

bool
Perl_sv_2bool(pTHX_ register SV *sv)
{
    if (SvGMAGICAL(sv))
	mg_get(sv);

    if (!SvOK(sv))
	return 0;
    if (SvROK(sv)) {
	SV* tmpsv;
        if (SvAMAGIC(sv) && (tmpsv=AMG_CALLun(sv,bool_)) &&
                (!SvROK(tmpsv) || (SvRV(tmpsv) != SvRV(sv))))
	    return (bool)SvTRUE(tmpsv);
      return SvRV(sv) != 0;
    }
    if (SvPOKp(sv)) {
	register XPV* Xpvtmp;
	if ((Xpvtmp = (XPV*)SvANY(sv)) &&
		(*Xpvtmp->xpv_pv > '0' ||
		Xpvtmp->xpv_cur > 1 ||
		(Xpvtmp->xpv_cur && *Xpvtmp->xpv_pv != '0')))
	    return 1;
	else
	    return 0;
    }
    else {
	if (SvIOKp(sv))
	    return SvIVX(sv) != 0;
	else {
	    if (SvNOKp(sv))
		return SvNVX(sv) != 0.0;
	    else
		return FALSE;
	}
    }
}

/* sv_utf8_upgrade() is now a macro using sv_utf8_upgrade_flags();
 * this function provided for binary compatibility only
 */


STRLEN
Perl_sv_utf8_upgrade(pTHX_ register SV *sv)
{
    return sv_utf8_upgrade_flags(sv, SV_GMAGIC);
}

/*
=for apidoc sv_utf8_upgrade

Convert the PV of an SV to its UTF-8-encoded form.
Forces the SV to string form if it is not already.
Always sets the SvUTF8 flag to avoid future validity checks even
if all the bytes have hibit clear.

This is not as a general purpose byte encoding to Unicode interface:
use the Encode extension for that.

=for apidoc sv_utf8_upgrade_flags

Convert the PV of an SV to its UTF-8-encoded form.
Forces the SV to string form if it is not already.
Always sets the SvUTF8 flag to avoid future validity checks even
if all the bytes have hibit clear. If C<flags> has C<SV_GMAGIC> bit set,
will C<mg_get> on C<sv> if appropriate, else not. C<sv_utf8_upgrade> and
C<sv_utf8_upgrade_nomg> are implemented in terms of this function.

This is not as a general purpose byte encoding to Unicode interface:
use the Encode extension for that.

=cut
*/

STRLEN
Perl_sv_utf8_upgrade_flags(pTHX_ register SV *sv, I32 flags)
{
    U8 *s, *t, *e;
    int  hibit = 0;

    if (!sv)
	return 0;

    if (!SvPOK(sv)) {
	STRLEN len = 0;
	(void) sv_2pv_flags(sv,&len, flags);
	if (!SvPOK(sv))
	     return len;
    }

    if (SvUTF8(sv))
	return SvCUR(sv);

    if (SvREADONLY(sv) && SvFAKE(sv)) {
	sv_force_normal(sv);
    }

    if (PL_encoding && !(flags & SV_UTF8_NO_ENCODING))
        sv_recode_to_utf8(sv, PL_encoding);
    else { /* Assume Latin-1/EBCDIC */
	 /* This function could be much more efficient if we
	  * had a FLAG in SVs to signal if there are any hibit
	  * chars in the PV.  Given that there isn't such a flag
	  * make the loop as fast as possible. */
	 s = (U8 *) SvPVX(sv);
	 e = (U8 *) SvEND(sv);
	 t = s;
	 while (t < e) {
	      U8 ch = *t++;
	      if ((hibit = !NATIVE_IS_INVARIANT(ch)))
		   break;
	 }
	 if (hibit) {
	      STRLEN len;
	
	      len = SvCUR(sv) + 1; /* Plus the \0 */
	      SvPVX(sv) = (char*)bytes_to_utf8((U8*)s, &len);
	      SvCUR(sv) = len - 1;
	      if (SvLEN(sv) != 0)
		   Safefree(s); /* No longer using what was there before. */
	      SvLEN(sv) = len; /* No longer know the real size. */
	 }
	 /* Mark as UTF-8 even if no hibit - saves scanning loop */
	 SvUTF8_on(sv);
    }
    return SvCUR(sv);
}

/*
=for apidoc sv_utf8_downgrade

Attempt to convert the PV of an SV from UTF-8-encoded to byte encoding.
This may not be possible if the PV contains non-byte encoding characters;
if this is the case, either returns false or, if C<fail_ok> is not
true, croaks.

This is not as a general purpose Unicode to byte encoding interface:
use the Encode extension for that.

=cut
*/

bool
Perl_sv_utf8_downgrade(pTHX_ register SV* sv, bool fail_ok)
{
    if (SvPOK(sv) && SvUTF8(sv)) {
        if (SvCUR(sv)) {
	    U8 *s;
	    STRLEN len;

	    if (SvREADONLY(sv) && SvFAKE(sv))
		sv_force_normal(sv);
	    s = (U8 *) SvPV(sv, len);
	    if (!utf8_to_bytes(s, &len)) {
	        if (fail_ok)
		    return FALSE;
		else {
		    if (PL_op)
		        Perl_croak(aTHX_ "Wide character in %s",
				   OP_DESC(PL_op));
		    else
		        Perl_croak(aTHX_ "Wide character");
		}
	    }
	    SvCUR(sv) = len;
	}
    }
    SvUTF8_off(sv);
    return TRUE;
}

/*
=for apidoc sv_utf8_encode

Convert the PV of an SV to UTF-8-encoded, but then turn off the C<SvUTF8>
flag so that it looks like octets again. Used as a building block
for encode_utf8 in Encode.xs

=cut
*/

void
Perl_sv_utf8_encode(pTHX_ register SV *sv)
{
    (void) sv_utf8_upgrade(sv);
    SvUTF8_off(sv);
}

/*
=for apidoc sv_utf8_decode

Convert the octets in the PV from UTF-8 to chars. Scan for validity and then
turn off SvUTF8 if needed so that we see characters. Used as a building block
for decode_utf8 in Encode.xs

=cut
*/

bool
Perl_sv_utf8_decode(pTHX_ register SV *sv)
{
    if (SvPOK(sv)) {
        U8 *c;
        U8 *e;

	/* The octets may have got themselves encoded - get them back as
	 * bytes
	 */
	if (!sv_utf8_downgrade(sv, TRUE))
	    return FALSE;

        /* it is actually just a matter of turning the utf8 flag on, but
         * we want to make sure everything inside is valid utf8 first.
         */
        c = (U8 *) SvPVX(sv);
	if (!is_utf8_string(c, SvCUR(sv)+1))
	    return FALSE;
        e = (U8 *) SvEND(sv);
        while (c < e) {
	    U8 ch = *c++;
            if (!UTF8_IS_INVARIANT(ch)) {
		SvUTF8_on(sv);
		break;
	    }
        }
    }
    return TRUE;
}

/* sv_setsv() is now a macro using Perl_sv_setsv_flags();
 * this function provided for binary compatibility only
 */

void
Perl_sv_setsv(pTHX_ SV *dstr, register SV *sstr)
{
    sv_setsv_flags(dstr, sstr, SV_GMAGIC);
}

/*
=for apidoc sv_setsv

Copies the contents of the source SV C<ssv> into the destination SV
C<dsv>.  The source SV may be destroyed if it is mortal, so don't use this
function if the source SV needs to be reused. Does not handle 'set' magic.
Loosely speaking, it performs a copy-by-value, obliterating any previous
content of the destination.

You probably want to use one of the assortment of wrappers, such as
C<SvSetSV>, C<SvSetSV_nosteal>, C<SvSetMagicSV> and
C<SvSetMagicSV_nosteal>.

=for apidoc sv_setsv_flags

Copies the contents of the source SV C<ssv> into the destination SV
C<dsv>.  The source SV may be destroyed if it is mortal, so don't use this
function if the source SV needs to be reused. Does not handle 'set' magic.
Loosely speaking, it performs a copy-by-value, obliterating any previous
content of the destination.
If the C<flags> parameter has the C<SV_GMAGIC> bit set, will C<mg_get> on
C<ssv> if appropriate, else not. C<sv_setsv> and C<sv_setsv_nomg> are
implemented in terms of this function.

You probably want to use one of the assortment of wrappers, such as
C<SvSetSV>, C<SvSetSV_nosteal>, C<SvSetMagicSV> and
C<SvSetMagicSV_nosteal>.

This is the primary function for copying scalars, and most other
copy-ish functions and macros use this underneath.

=cut
*/

void
Perl_sv_setsv_flags(pTHX_ SV *dstr, register SV *sstr, I32 flags)
{
    register U32 sflags;
    register int dtype;
    register int stype;

    if (sstr == dstr)
	return;
    SV_CHECK_THINKFIRST(dstr);
    if (!sstr)
	sstr = &PL_sv_undef;
    stype = SvTYPE(sstr);
    dtype = SvTYPE(dstr);

    SvAMAGIC_off(dstr);
    if ( SvVOK(dstr) ) 
    {
	/* need to nuke the magic */
	mg_free(dstr);
	SvRMAGICAL_off(dstr);
    }

    /* There's a lot of redundancy below but we're going for speed here */

    switch (stype) {
    case SVt_NULL:
      undef_sstr:
	if (dtype != SVt_PVGV) {
	    (void)SvOK_off(dstr);
	    return;
	}
	break;
    case SVt_IV:
	if (SvIOK(sstr)) {
	    switch (dtype) {
	    case SVt_NULL:
		sv_upgrade(dstr, SVt_IV);
		break;
	    case SVt_NV:
		sv_upgrade(dstr, SVt_PVNV);
		break;
	    case SVt_RV:
	    case SVt_PV:
		sv_upgrade(dstr, SVt_PVIV);
		break;
	    }
	    (void)SvIOK_only(dstr);
	    SvIVX(dstr) = SvIVX(sstr);
	    if (SvIsUV(sstr))
		SvIsUV_on(dstr);
	    if (SvTAINTED(sstr))
		SvTAINT(dstr);
	    return;
	}
	goto undef_sstr;

    case SVt_NV:
	if (SvNOK(sstr)) {
	    switch (dtype) {
	    case SVt_NULL:
	    case SVt_IV:
		sv_upgrade(dstr, SVt_NV);
		break;
	    case SVt_RV:
	    case SVt_PV:
	    case SVt_PVIV:
		sv_upgrade(dstr, SVt_PVNV);
		break;
	    }
	    SvNVX(dstr) = SvNVX(sstr);
	    (void)SvNOK_only(dstr);
	    if (SvTAINTED(sstr))
		SvTAINT(dstr);
	    return;
	}
	goto undef_sstr;

    case SVt_RV:
	if (dtype < SVt_RV)
	    sv_upgrade(dstr, SVt_RV);
	else if (dtype == SVt_PVGV &&
		 SvROK(sstr) && SvTYPE(SvRV(sstr)) == SVt_PVGV) {
	    sstr = SvRV(sstr);
	    if (sstr == dstr) {
		if (GvIMPORTED(dstr) != GVf_IMPORTED
		    && CopSTASH_ne(PL_curcop, GvSTASH(dstr)))
		{
		    GvIMPORTED_on(dstr);
		}
		GvMULTI_on(dstr);
		return;
	    }
	    goto glob_assign;
	}
	break;
    case SVt_PV:
    case SVt_PVFM:
	if (dtype < SVt_PV)
	    sv_upgrade(dstr, SVt_PV);
	break;
    case SVt_PVIV:
	if (dtype < SVt_PVIV)
	    sv_upgrade(dstr, SVt_PVIV);
	break;
    case SVt_PVNV:
	if (dtype < SVt_PVNV)
	    sv_upgrade(dstr, SVt_PVNV);
	break;
    case SVt_PVAV:
    case SVt_PVHV:
    case SVt_PVCV:
    case SVt_PVIO:
	if (PL_op)
	    Perl_croak(aTHX_ "Bizarre copy of %s in %s", sv_reftype(sstr, 0),
		OP_NAME(PL_op));
	else
	    Perl_croak(aTHX_ "Bizarre copy of %s", sv_reftype(sstr, 0));
	break;

    case SVt_PVGV:
	if (dtype <= SVt_PVGV) {
  glob_assign:
	    if (dtype != SVt_PVGV) {
		char *name = GvNAME(sstr);
		STRLEN len = GvNAMELEN(sstr);
		sv_upgrade(dstr, SVt_PVGV);
		sv_magic(dstr, dstr, PERL_MAGIC_glob, Nullch, 0);
		GvSTASH(dstr) = (HV*)SvREFCNT_inc(GvSTASH(sstr));
		GvNAME(dstr) = savepvn(name, len);
		GvNAMELEN(dstr) = len;
		SvFAKE_on(dstr);	/* can coerce to non-glob */
	    }
	    /* ahem, death to those who redefine active sort subs */
	    else if (PL_curstackinfo->si_type == PERLSI_SORT
		     && GvCV(dstr) && PL_sortcop == CvSTART(GvCV(dstr)))
		Perl_croak(aTHX_ "Can't redefine active sort subroutine %s",
		      GvNAME(dstr));

#ifdef GV_UNIQUE_CHECK
                if (GvUNIQUE((GV*)dstr)) {
                    Perl_croak(aTHX_ PL_no_modify);
                }
#endif

	    (void)SvOK_off(dstr);
	    GvINTRO_off(dstr);		/* one-shot flag */
	    gp_free((GV*)dstr);
	    GvGP(dstr) = gp_ref(GvGP(sstr));
	    if (SvTAINTED(sstr))
		SvTAINT(dstr);
	    if (GvIMPORTED(dstr) != GVf_IMPORTED
		&& CopSTASH_ne(PL_curcop, GvSTASH(dstr)))
	    {
		GvIMPORTED_on(dstr);
	    }
	    GvMULTI_on(dstr);
	    return;
	}
	/* FALL THROUGH */

    default:
	if (SvGMAGICAL(sstr) && (flags & SV_GMAGIC)) {
	    mg_get(sstr);
	    if ((int)SvTYPE(sstr) != stype) {
		stype = SvTYPE(sstr);
		if (stype == SVt_PVGV && dtype <= SVt_PVGV)
		    goto glob_assign;
	    }
	}
	if (stype == SVt_PVLV)
	    (void)SvUPGRADE(dstr, SVt_PVNV);
	else
	    (void)SvUPGRADE(dstr, (U32)stype);
    }

    sflags = SvFLAGS(sstr);

    if (sflags & SVf_ROK) {
	if (dtype >= SVt_PV) {
	    if (dtype == SVt_PVGV) {
		SV *sref = SvREFCNT_inc(SvRV(sstr));
		SV *dref = 0;
		int intro = GvINTRO(dstr);

#ifdef GV_UNIQUE_CHECK
                if (GvUNIQUE((GV*)dstr)) {
                    Perl_croak(aTHX_ PL_no_modify);
                }
#endif

		if (intro) {
		    GvINTRO_off(dstr);	/* one-shot flag */
		    GvLINE(dstr) = CopLINE(PL_curcop);
		    GvEGV(dstr) = (GV*)dstr;
		}
		GvMULTI_on(dstr);
		switch (SvTYPE(sref)) {
		case SVt_PVAV:
		    if (intro)
			SAVEGENERICSV(GvAV(dstr));
		    else
			dref = (SV*)GvAV(dstr);
		    GvAV(dstr) = (AV*)sref;
		    if (!GvIMPORTED_AV(dstr)
			&& CopSTASH_ne(PL_curcop, GvSTASH(dstr)))
		    {
			GvIMPORTED_AV_on(dstr);
		    }
		    break;
		case SVt_PVHV:
		    if (intro)
			SAVEGENERICSV(GvHV(dstr));
		    else
			dref = (SV*)GvHV(dstr);
		    GvHV(dstr) = (HV*)sref;
		    if (!GvIMPORTED_HV(dstr)
			&& CopSTASH_ne(PL_curcop, GvSTASH(dstr)))
		    {
			GvIMPORTED_HV_on(dstr);
		    }
		    break;
		case SVt_PVCV:
		    if (intro) {
			if (GvCVGEN(dstr) && GvCV(dstr) != (CV*)sref) {
			    SvREFCNT_dec(GvCV(dstr));
			    GvCV(dstr) = Nullcv;
			    GvCVGEN(dstr) = 0; /* Switch off cacheness. */
			    PL_sub_generation++;
			}
			SAVEGENERICSV(GvCV(dstr));
		    }
		    else
			dref = (SV*)GvCV(dstr);
		    if (GvCV(dstr) != (CV*)sref) {
			CV* cv = GvCV(dstr);
			if (cv) {
			    if (!GvCVGEN((GV*)dstr) &&
				(CvROOT(cv) || CvXSUB(cv)))
			    {
				/* ahem, death to those who redefine
				 * active sort subs */
				if (PL_curstackinfo->si_type == PERLSI_SORT &&
				      PL_sortcop == CvSTART(cv))
				    Perl_croak(aTHX_
				    "Can't redefine active sort subroutine %s",
					  GvENAME((GV*)dstr));
 				/* Redefining a sub - warning is mandatory if
 				   it was a const and its value changed. */
 				if (ckWARN(WARN_REDEFINE)
 				    || (CvCONST(cv)
 					&& (!CvCONST((CV*)sref)
 					    || sv_cmp(cv_const_sv(cv),
 						      cv_const_sv((CV*)sref)))))
 				{
 				    Perl_warner(aTHX_ packWARN(WARN_REDEFINE),
 					CvCONST(cv)
 					? "Constant subroutine %s::%s redefined"
 					: "Subroutine %s::%s redefined",
					HvNAME(GvSTASH((GV*)dstr)),
 					GvENAME((GV*)dstr));
 				}
			    }
			    if (!intro)
				cv_ckproto(cv, (GV*)dstr,
					SvPOK(sref) ? SvPVX(sref) : Nullch);
			}
			GvCV(dstr) = (CV*)sref;
			GvCVGEN(dstr) = 0; /* Switch off cacheness. */
			GvASSUMECV_on(dstr);
			PL_sub_generation++;
		    }
		    if (!GvIMPORTED_CV(dstr)
			&& CopSTASH_ne(PL_curcop, GvSTASH(dstr)))
		    {
			GvIMPORTED_CV_on(dstr);
		    }
		    break;
		case SVt_PVIO:
		    if (intro)
			SAVEGENERICSV(GvIOp(dstr));
		    else
			dref = (SV*)GvIOp(dstr);
		    GvIOp(dstr) = (IO*)sref;
		    break;
		case SVt_PVFM:
		    if (intro)
			SAVEGENERICSV(GvFORM(dstr));
		    else
			dref = (SV*)GvFORM(dstr);
		    GvFORM(dstr) = (CV*)sref;
		    break;
		default:
		    if (intro)
			SAVEGENERICSV(GvSV(dstr));
		    else
			dref = (SV*)GvSV(dstr);
		    GvSV(dstr) = sref;
		    if (!GvIMPORTED_SV(dstr)
			&& CopSTASH_ne(PL_curcop, GvSTASH(dstr)))
		    {
			GvIMPORTED_SV_on(dstr);
		    }
		    break;
		}
		if (dref)
		    SvREFCNT_dec(dref);
		if (SvTAINTED(sstr))
		    SvTAINT(dstr);
		return;
	    }
	    if (SvPVX(dstr)) {
		(void)SvOOK_off(dstr);		/* backoff */
		if (SvLEN(dstr))
		    Safefree(SvPVX(dstr));
		SvLEN(dstr)=SvCUR(dstr)=0;
	    }
	}
	(void)SvOK_off(dstr);
	SvRV(dstr) = SvREFCNT_inc(SvRV(sstr));
	SvROK_on(dstr);
	if (sflags & SVp_NOK) {
	    SvNOKp_on(dstr);
	    /* Only set the public OK flag if the source has public OK.  */
	    if (sflags & SVf_NOK)
		SvFLAGS(dstr) |= SVf_NOK;
	    SvNVX(dstr) = SvNVX(sstr);
	}
	if (sflags & SVp_IOK) {
	    (void)SvIOKp_on(dstr);
	    if (sflags & SVf_IOK)
		SvFLAGS(dstr) |= SVf_IOK;
	    if (sflags & SVf_IVisUV)
		SvIsUV_on(dstr);
	    SvIVX(dstr) = SvIVX(sstr);
	}
	if (SvAMAGIC(sstr)) {
	    SvAMAGIC_on(dstr);
	}
    }
    else if (sflags & SVp_POK) {

	/*
	 * Check to see if we can just swipe the string.  If so, it's a
	 * possible small lose on short strings, but a big win on long ones.
	 * It might even be a win on short strings if SvPVX(dstr)
	 * has to be allocated and SvPVX(sstr) has to be freed.
	 */

	if (SvTEMP(sstr) &&		/* slated for free anyway? */
	    SvREFCNT(sstr) == 1 && 	/* and no other references to it? */
	    !(sflags & SVf_OOK) && 	/* and not involved in OOK hack? */
	    SvLEN(sstr) 	&&	/* and really is a string */
	    			/* and won't be needed again, potentially */
	    !(PL_op && PL_op->op_type == OP_AASSIGN))
	{
	    if (SvPVX(dstr)) {		/* we know that dtype >= SVt_PV */
		if (SvOOK(dstr)) {
		    SvFLAGS(dstr) &= ~SVf_OOK;
		    Safefree(SvPVX(dstr) - SvIVX(dstr));
		}
		else if (SvLEN(dstr))
		    Safefree(SvPVX(dstr));
	    }
	    (void)SvPOK_only(dstr);
	    SvPV_set(dstr, SvPVX(sstr));
	    SvLEN_set(dstr, SvLEN(sstr));
	    SvCUR_set(dstr, SvCUR(sstr));

	    SvTEMP_off(dstr);
	    (void)SvOK_off(sstr);	/* NOTE: nukes most SvFLAGS on sstr */
	    SvPV_set(sstr, Nullch);
	    SvLEN_set(sstr, 0);
	    SvCUR_set(sstr, 0);
	    SvTEMP_off(sstr);
	}
	else {				/* have to copy actual string */
	    STRLEN len = SvCUR(sstr);
	    SvGROW(dstr, len + 1);	/* inlined from sv_setpvn */
	    Move(SvPVX(sstr),SvPVX(dstr),len,char);
	    SvCUR_set(dstr, len);
	    *SvEND(dstr) = '\0';
	    (void)SvPOK_only(dstr);
	}
	if (sflags & SVf_UTF8)
	    SvUTF8_on(dstr);
	/*SUPPRESS 560*/
	if (sflags & SVp_NOK) {
	    SvNOKp_on(dstr);
	    if (sflags & SVf_NOK)
		SvFLAGS(dstr) |= SVf_NOK;
	    SvNVX(dstr) = SvNVX(sstr);
	}
	if (sflags & SVp_IOK) {
	    (void)SvIOKp_on(dstr);
	    if (sflags & SVf_IOK)
		SvFLAGS(dstr) |= SVf_IOK;
	    if (sflags & SVf_IVisUV)
		SvIsUV_on(dstr);
	    SvIVX(dstr) = SvIVX(sstr);
	}
	if ( SvVOK(sstr) ) {
	    MAGIC *smg = mg_find(sstr,PERL_MAGIC_vstring);
	    sv_magic(dstr, NULL, PERL_MAGIC_vstring,
		     smg->mg_ptr, smg->mg_len);
	    SvRMAGICAL_on(dstr);
	} 
    }
    else if (sflags & SVp_IOK) {
	if (sflags & SVf_IOK)
	    (void)SvIOK_only(dstr);
	else {
	    (void)SvOK_off(dstr);
	    (void)SvIOKp_on(dstr);
	}
	/* XXXX Do we want to set IsUV for IV(ROK)?  Be extra safe... */
	if (sflags & SVf_IVisUV)
	    SvIsUV_on(dstr);
	SvIVX(dstr) = SvIVX(sstr);
	if (sflags & SVp_NOK) {
	    if (sflags & SVf_NOK)
		(void)SvNOK_on(dstr);
	    else
		(void)SvNOKp_on(dstr);
	    SvNVX(dstr) = SvNVX(sstr);
	}
    }
    else if (sflags & SVp_NOK) {
	if (sflags & SVf_NOK)
	    (void)SvNOK_only(dstr);
	else {
	    (void)SvOK_off(dstr);
	    SvNOKp_on(dstr);
	}
	SvNVX(dstr) = SvNVX(sstr);
    }
    else {
	if (dtype == SVt_PVGV) {
	    if (ckWARN(WARN_MISC))
		Perl_warner(aTHX_ packWARN(WARN_MISC), "Undefined value assigned to typeglob");
	}
	else
	    (void)SvOK_off(dstr);
    }
    if (SvTAINTED(sstr))
	SvTAINT(dstr);
}

/*
=for apidoc sv_setsv_mg

Like C<sv_setsv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setsv_mg(pTHX_ SV *dstr, register SV *sstr)
{
    sv_setsv(dstr,sstr);
    SvSETMAGIC(dstr);
}

/*
=for apidoc sv_setpvn

Copies a string into an SV.  The C<len> parameter indicates the number of
bytes to be copied.  Does not handle 'set' magic.  See C<sv_setpvn_mg>.

=cut
*/

void
Perl_sv_setpvn(pTHX_ register SV *sv, register const char *ptr, register STRLEN len)
{
    register char *dptr;

    SV_CHECK_THINKFIRST(sv);
    if (!ptr) {
	(void)SvOK_off(sv);
	return;
    }
    else {
        /* len is STRLEN which is unsigned, need to copy to signed */
	IV iv = len;
	if (iv < 0)
	    Perl_croak(aTHX_ "panic: sv_setpvn called with negative strlen");
    }
    (void)SvUPGRADE(sv, SVt_PV);

    SvGROW(sv, len + 1);
    dptr = SvPVX(sv);
    Move(ptr,dptr,len,char);
    dptr[len] = '\0';
    SvCUR_set(sv, len);
    (void)SvPOK_only_UTF8(sv);		/* validate pointer */
    SvTAINT(sv);
}

/*
=for apidoc sv_setpvn_mg

Like C<sv_setpvn>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setpvn_mg(pTHX_ register SV *sv, register const char *ptr, register STRLEN len)
{
    sv_setpvn(sv,ptr,len);
    SvSETMAGIC(sv);
}

/*
=for apidoc sv_setpv

Copies a string into an SV.  The string must be null-terminated.  Does not
handle 'set' magic.  See C<sv_setpv_mg>.

=cut
*/

void
Perl_sv_setpv(pTHX_ register SV *sv, register const char *ptr)
{
    register STRLEN len;

    SV_CHECK_THINKFIRST(sv);
    if (!ptr) {
	(void)SvOK_off(sv);
	return;
    }
    len = strlen(ptr);
    (void)SvUPGRADE(sv, SVt_PV);

    SvGROW(sv, len + 1);
    Move(ptr,SvPVX(sv),len+1,char);
    SvCUR_set(sv, len);
    (void)SvPOK_only_UTF8(sv);		/* validate pointer */
    SvTAINT(sv);
}

/*
=for apidoc sv_setpv_mg

Like C<sv_setpv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setpv_mg(pTHX_ register SV *sv, register const char *ptr)
{
    sv_setpv(sv,ptr);
    SvSETMAGIC(sv);
}

/*
=for apidoc sv_usepvn

Tells an SV to use C<ptr> to find its string value.  Normally the string is
stored inside the SV but sv_usepvn allows the SV to use an outside string.
The C<ptr> should point to memory that was allocated by C<malloc>.  The
string length, C<len>, must be supplied.  This function will realloc the
memory pointed to by C<ptr>, so that pointer should not be freed or used by
the programmer after giving it to sv_usepvn.  Does not handle 'set' magic.
See C<sv_usepvn_mg>.

=cut
*/

void
Perl_sv_usepvn(pTHX_ register SV *sv, register char *ptr, register STRLEN len)
{
    SV_CHECK_THINKFIRST(sv);
    (void)SvUPGRADE(sv, SVt_PV);
    if (!ptr) {
	(void)SvOK_off(sv);
	return;
    }
    (void)SvOOK_off(sv);
    if (SvPVX(sv) && SvLEN(sv))
	Safefree(SvPVX(sv));
    Renew(ptr, len+1, char);
    SvPVX(sv) = ptr;
    SvCUR_set(sv, len);
    SvLEN_set(sv, len+1);
    *SvEND(sv) = '\0';
    (void)SvPOK_only_UTF8(sv);		/* validate pointer */
    SvTAINT(sv);
}

/*
=for apidoc sv_usepvn_mg

Like C<sv_usepvn>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_usepvn_mg(pTHX_ register SV *sv, register char *ptr, register STRLEN len)
{
    sv_usepvn(sv,ptr,len);
    SvSETMAGIC(sv);
}

/*
=for apidoc sv_force_normal_flags

Undo various types of fakery on an SV: if the PV is a shared string, make
a private copy; if we're a ref, stop refing; if we're a glob, downgrade to
an xpvmg. The C<flags> parameter gets passed to  C<sv_unref_flags()>
when unrefing. C<sv_force_normal> calls this function with flags set to 0.

=cut
*/

void
Perl_sv_force_normal_flags(pTHX_ register SV *sv, U32 flags)
{
    if (SvREADONLY(sv)) {
	if (SvFAKE(sv)) {
	    char *pvx = SvPVX(sv);
	    STRLEN len = SvCUR(sv);
            U32 hash   = SvUVX(sv);
	    SvGROW(sv, len + 1);
	    Move(pvx,SvPVX(sv),len,char);
	    *SvEND(sv) = '\0';
	    SvFAKE_off(sv);
	    SvREADONLY_off(sv);
	    unsharepvn(pvx, SvUTF8(sv) ? -(I32)len : len, hash);
	}
	else if (IN_PERL_RUNTIME)
	    Perl_croak(aTHX_ PL_no_modify);
    }
    if (SvROK(sv))
	sv_unref_flags(sv, flags);
    else if (SvFAKE(sv) && SvTYPE(sv) == SVt_PVGV)
	sv_unglob(sv);
}

/*
=for apidoc sv_force_normal

Undo various types of fakery on an SV: if the PV is a shared string, make
a private copy; if we're a ref, stop refing; if we're a glob, downgrade to
an xpvmg. See also C<sv_force_normal_flags>.

=cut
*/

void
Perl_sv_force_normal(pTHX_ register SV *sv)
{
    sv_force_normal_flags(sv, 0);
}

/*
=for apidoc sv_chop

Efficient removal of characters from the beginning of the string buffer.
SvPOK(sv) must be true and the C<ptr> must be a pointer to somewhere inside
the string buffer.  The C<ptr> becomes the first character of the adjusted
string. Uses the "OOK hack".
Beware: after this function returns, C<ptr> and SvPVX(sv) may no longer
refer to the same chunk of data.

=cut
*/

void
Perl_sv_chop(pTHX_ register SV *sv, register char *ptr)
{
    register STRLEN delta;
    if (!ptr || !SvPOKp(sv))
	return;
    delta = ptr - SvPVX(sv);
    SV_CHECK_THINKFIRST(sv);
    if (SvTYPE(sv) < SVt_PVIV)
	sv_upgrade(sv,SVt_PVIV);

    if (!SvOOK(sv)) {
	if (!SvLEN(sv)) { /* make copy of shared string */
	    char *pvx = SvPVX(sv);
	    STRLEN len = SvCUR(sv);
	    SvGROW(sv, len + 1);
	    Move(pvx,SvPVX(sv),len,char);
	    *SvEND(sv) = '\0';
	}
	SvIVX(sv) = 0;
	/* Same SvOOK_on but SvOOK_on does a SvIOK_off
	   and we do that anyway inside the SvNIOK_off
	*/
	SvFLAGS(sv) |= SVf_OOK; 
    }
    SvNIOK_off(sv);
    SvLEN(sv) -= delta;
    SvCUR(sv) -= delta;
    SvPVX(sv) += delta;
    SvIVX(sv) += delta;
}

/* sv_catpvn() is now a macro using Perl_sv_catpvn_flags();
 * this function provided for binary compatibility only
 */

void
Perl_sv_catpvn(pTHX_ SV *dsv, const char* sstr, STRLEN slen)
{
    sv_catpvn_flags(dsv, sstr, slen, SV_GMAGIC);
}

/*
=for apidoc sv_catpvn

Concatenates the string onto the end of the string which is in the SV.  The
C<len> indicates number of bytes to copy.  If the SV has the UTF-8
status set, then the bytes appended should be valid UTF-8.
Handles 'get' magic, but not 'set' magic.  See C<sv_catpvn_mg>.

=for apidoc sv_catpvn_flags

Concatenates the string onto the end of the string which is in the SV.  The
C<len> indicates number of bytes to copy.  If the SV has the UTF-8
status set, then the bytes appended should be valid UTF-8.
If C<flags> has C<SV_GMAGIC> bit set, will C<mg_get> on C<dsv> if
appropriate, else not. C<sv_catpvn> and C<sv_catpvn_nomg> are implemented
in terms of this function.

=cut
*/

void
Perl_sv_catpvn_flags(pTHX_ register SV *dsv, register const char *sstr, register STRLEN slen, I32 flags)
{
    STRLEN dlen;
    char *dstr;

    dstr = SvPV_force_flags(dsv, dlen, flags);
    SvGROW(dsv, dlen + slen + 1);
    if (sstr == dstr)
	sstr = SvPVX(dsv);
    Move(sstr, SvPVX(dsv) + dlen, slen, char);
    SvCUR(dsv) += slen;
    *SvEND(dsv) = '\0';
    (void)SvPOK_only_UTF8(dsv);		/* validate pointer */
    SvTAINT(dsv);
}

/*
=for apidoc sv_catpvn_mg

Like C<sv_catpvn>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_catpvn_mg(pTHX_ register SV *sv, register const char *ptr, register STRLEN len)
{
    sv_catpvn(sv,ptr,len);
    SvSETMAGIC(sv);
}

/* sv_catsv() is now a macro using Perl_sv_catsv_flags();
 * this function provided for binary compatibility only
 */

void
Perl_sv_catsv(pTHX_ SV *dstr, register SV *sstr)
{
    sv_catsv_flags(dstr, sstr, SV_GMAGIC);
}

/*
=for apidoc sv_catsv

Concatenates the string from SV C<ssv> onto the end of the string in
SV C<dsv>.  Modifies C<dsv> but not C<ssv>.  Handles 'get' magic, but
not 'set' magic.  See C<sv_catsv_mg>.

=for apidoc sv_catsv_flags

Concatenates the string from SV C<ssv> onto the end of the string in
SV C<dsv>.  Modifies C<dsv> but not C<ssv>.  If C<flags> has C<SV_GMAGIC>
bit set, will C<mg_get> on the SVs if appropriate, else not. C<sv_catsv>
and C<sv_catsv_nomg> are implemented in terms of this function.

=cut */

void
Perl_sv_catsv_flags(pTHX_ SV *dsv, register SV *ssv, I32 flags)
{
    char *spv;
    STRLEN slen;
    if (!ssv)
	return;
    if ((spv = SvPV(ssv, slen))) {
	/*  sutf8 and dutf8 were type bool, but under USE_ITHREADS,
	    gcc version 2.95.2 20000220 (Debian GNU/Linux) for
	    Linux xxx 2.2.17 on sparc64 with gcc -O2, we erroneously
	    get dutf8 = 0x20000000, (i.e.  SVf_UTF8) even though
	    dsv->sv_flags doesn't have that bit set.
		Andy Dougherty  12 Oct 2001
	*/
	I32 sutf8 = DO_UTF8(ssv);
	I32 dutf8;

	if (SvGMAGICAL(dsv) && (flags & SV_GMAGIC))
	    mg_get(dsv);
	dutf8 = DO_UTF8(dsv);

	if (dutf8 != sutf8) {
	    if (dutf8) {
		/* Not modifying source SV, so taking a temporary copy. */
		SV* csv = sv_2mortal(newSVpvn(spv, slen));

		sv_utf8_upgrade(csv);
		spv = SvPV(csv, slen);
	    }
	    else
		sv_utf8_upgrade_nomg(dsv);
	}
	sv_catpvn_nomg(dsv, spv, slen);
    }
}

/*
=for apidoc sv_catsv_mg

Like C<sv_catsv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_catsv_mg(pTHX_ SV *dsv, register SV *ssv)
{
    sv_catsv(dsv,ssv);
    SvSETMAGIC(dsv);
}

/*
=for apidoc sv_catpv

Concatenates the string onto the end of the string which is in the SV.
If the SV has the UTF-8 status set, then the bytes appended should be
valid UTF-8.  Handles 'get' magic, but not 'set' magic.  See C<sv_catpv_mg>.

=cut */

void
Perl_sv_catpv(pTHX_ register SV *sv, register const char *ptr)
{
    register STRLEN len;
    STRLEN tlen;
    char *junk;

    if (!ptr)
	return;
    junk = SvPV_force(sv, tlen);
    len = strlen(ptr);
    SvGROW(sv, tlen + len + 1);
    if (ptr == junk)
	ptr = SvPVX(sv);
    Move(ptr,SvPVX(sv)+tlen,len+1,char);
    SvCUR(sv) += len;
    (void)SvPOK_only_UTF8(sv);		/* validate pointer */
    SvTAINT(sv);
}

/*
=for apidoc sv_catpv_mg

Like C<sv_catpv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_catpv_mg(pTHX_ register SV *sv, register const char *ptr)
{
    sv_catpv(sv,ptr);
    SvSETMAGIC(sv);
}

/*
=for apidoc newSV

Create a new null SV, or if len > 0, create a new empty SVt_PV type SV
with an initial PV allocation of len+1. Normally accessed via the C<NEWSV>
macro.

=cut
*/

SV *
Perl_newSV(pTHX_ STRLEN len)
{
    register SV *sv;

    new_SV(sv);
    if (len) {
	sv_upgrade(sv, SVt_PV);
	SvGROW(sv, len + 1);
    }
    return sv;
}
/*
=for apidoc sv_magicext

Adds magic to an SV, upgrading it if necessary. Applies the
supplied vtable and returns pointer to the magic added.

Note that sv_magicext will allow things that sv_magic will not.
In particular you can add magic to SvREADONLY SVs and and more than
one instance of the same 'how'

I C<namelen> is greater then zero then a savepvn() I<copy> of C<name> is stored,
if C<namelen> is zero then C<name> is stored as-is and - as another special
case - if C<(name && namelen == HEf_SVKEY)> then C<name> is assumed to contain
an C<SV*> and has its REFCNT incremented

(This is now used as a subroutine by sv_magic.)

=cut
*/
MAGIC *	
Perl_sv_magicext(pTHX_ SV* sv, SV* obj, int how, MGVTBL *vtable,
		 const char* name, I32 namlen)
{
    MAGIC* mg;

    if (SvTYPE(sv) < SVt_PVMG) {
	(void)SvUPGRADE(sv, SVt_PVMG);
    }
    Newz(702,mg, 1, MAGIC);
    mg->mg_moremagic = SvMAGIC(sv);
    SvMAGIC(sv) = mg;

    /* Some magic sontains a reference loop, where the sv and object refer to
       each other.  To prevent a reference loop that would prevent such
       objects being freed, we look for such loops and if we find one we
       avoid incrementing the object refcount.

       Note we cannot do this to avoid self-tie loops as intervening RV must
       have its REFCNT incremented to keep it in existence.

    */
    if (!obj || obj == sv ||
	how == PERL_MAGIC_arylen ||
	how == PERL_MAGIC_qr ||
	(SvTYPE(obj) == SVt_PVGV &&
	    (GvSV(obj) == sv || GvHV(obj) == (HV*)sv || GvAV(obj) == (AV*)sv ||
	    GvCV(obj) == (CV*)sv || GvIOp(obj) == (IO*)sv ||
	    GvFORM(obj) == (CV*)sv)))
    {
	mg->mg_obj = obj;
    }
    else {
	mg->mg_obj = SvREFCNT_inc(obj);
	mg->mg_flags |= MGf_REFCOUNTED;
    }

    /* Normal self-ties simply pass a null object, and instead of
       using mg_obj directly, use the SvTIED_obj macro to produce a
       new RV as needed.  For glob "self-ties", we are tieing the PVIO
       with an RV obj pointing to the glob containing the PVIO.  In
       this case, to avoid a reference loop, we need to weaken the
       reference.
    */

    if (how == PERL_MAGIC_tiedscalar && SvTYPE(sv) == SVt_PVIO &&
        obj && SvROK(obj) && GvIO(SvRV(obj)) == (IO*)sv)
    {
      sv_rvweaken(obj);
    }

    mg->mg_type = how;
    mg->mg_len = namlen;
    if (name) {
	if (namlen > 0)
	    mg->mg_ptr = savepvn(name, namlen);
	else if (namlen == HEf_SVKEY)
	    mg->mg_ptr = (char*)SvREFCNT_inc((SV*)name);
	else
	    mg->mg_ptr = (char *) name;
    }
    mg->mg_virtual = vtable;

    mg_magical(sv);
    if (SvGMAGICAL(sv))
	SvFLAGS(sv) &= ~(SVf_IOK|SVf_NOK|SVf_POK);
    return mg;
}

/*
=for apidoc sv_magic

Adds magic to an SV. First upgrades C<sv> to type C<SVt_PVMG> if necessary,
then adds a new magic item of type C<how> to the head of the magic list.

=cut
*/

void
Perl_sv_magic(pTHX_ register SV *sv, SV *obj, int how, const char *name, I32 namlen)
{
    MAGIC* mg;
    MGVTBL *vtable = 0;

    if (SvREADONLY(sv)) {
	if (IN_PERL_RUNTIME
	    && how != PERL_MAGIC_regex_global
	    && how != PERL_MAGIC_bm
	    && how != PERL_MAGIC_fm
	    && how != PERL_MAGIC_sv
	   )
	{
	    Perl_croak(aTHX_ PL_no_modify);
	}
    }
    if (SvMAGICAL(sv) || (how == PERL_MAGIC_taint && SvTYPE(sv) >= SVt_PVMG)) {
	if (SvMAGIC(sv) && (mg = mg_find(sv, how))) {
	    /* sv_magic() refuses to add a magic of the same 'how' as an
	       existing one
	     */
	    if (how == PERL_MAGIC_taint)
		mg->mg_len |= 1;
	    return;
	}
    }

    switch (how) {
    case PERL_MAGIC_sv:
	vtable = &PL_vtbl_sv;
	break;
    case PERL_MAGIC_overload:
        vtable = &PL_vtbl_amagic;
        break;
    case PERL_MAGIC_overload_elem:
        vtable = &PL_vtbl_amagicelem;
        break;
    case PERL_MAGIC_overload_table:
        vtable = &PL_vtbl_ovrld;
        break;
    case PERL_MAGIC_bm:
	vtable = &PL_vtbl_bm;
	break;
    case PERL_MAGIC_regdata:
	vtable = &PL_vtbl_regdata;
	break;
    case PERL_MAGIC_regdatum:
	vtable = &PL_vtbl_regdatum;
	break;
    case PERL_MAGIC_env:
	vtable = &PL_vtbl_env;
	break;
    case PERL_MAGIC_fm:
	vtable = &PL_vtbl_fm;
	break;
    case PERL_MAGIC_envelem:
	vtable = &PL_vtbl_envelem;
	break;
    case PERL_MAGIC_regex_global:
	vtable = &PL_vtbl_mglob;
	break;
    case PERL_MAGIC_isa:
	vtable = &PL_vtbl_isa;
	break;
    case PERL_MAGIC_isaelem:
	vtable = &PL_vtbl_isaelem;
	break;
    case PERL_MAGIC_nkeys:
	vtable = &PL_vtbl_nkeys;
	break;
    case PERL_MAGIC_dbfile:
	vtable = 0;
	break;
    case PERL_MAGIC_dbline:
	vtable = &PL_vtbl_dbline;
	break;
#ifdef USE_5005THREADS
    case PERL_MAGIC_mutex:
	vtable = &PL_vtbl_mutex;
	break;
#endif /* USE_5005THREADS */
#ifdef USE_LOCALE_COLLATE
    case PERL_MAGIC_collxfrm:
        vtable = &PL_vtbl_collxfrm;
        break;
#endif /* USE_LOCALE_COLLATE */
    case PERL_MAGIC_tied:
	vtable = &PL_vtbl_pack;
	break;
    case PERL_MAGIC_tiedelem:
    case PERL_MAGIC_tiedscalar:
	vtable = &PL_vtbl_packelem;
	break;
    case PERL_MAGIC_qr:
	vtable = &PL_vtbl_regexp;
	break;
    case PERL_MAGIC_sig:
	vtable = &PL_vtbl_sig;
	break;
    case PERL_MAGIC_sigelem:
	vtable = &PL_vtbl_sigelem;
	break;
    case PERL_MAGIC_taint:
	vtable = &PL_vtbl_taint;
	break;
    case PERL_MAGIC_uvar:
	vtable = &PL_vtbl_uvar;
	break;
    case PERL_MAGIC_vec:
	vtable = &PL_vtbl_vec;
	break;
    case PERL_MAGIC_vstring:
	vtable = 0;
	break;
    case PERL_MAGIC_utf8:
        vtable = &PL_vtbl_utf8;
        break;
    case PERL_MAGIC_substr:
	vtable = &PL_vtbl_substr;
	break;
    case PERL_MAGIC_defelem:
	vtable = &PL_vtbl_defelem;
	break;
    case PERL_MAGIC_glob:
	vtable = &PL_vtbl_glob;
	break;
    case PERL_MAGIC_arylen:
	vtable = &PL_vtbl_arylen;
	break;
    case PERL_MAGIC_pos:
	vtable = &PL_vtbl_pos;
	break;
    case PERL_MAGIC_backref:
	vtable = &PL_vtbl_backref;
	break;
    case PERL_MAGIC_ext:
	/* Reserved for use by extensions not perl internals.	        */
	/* Useful for attaching extension internal data to perl vars.	*/
	/* Note that multiple extensions may clash if magical scalars	*/
	/* etc holding private data from one are passed to another.	*/
	break;
    default:
	Perl_croak(aTHX_ "Don't know how to handle magic of type \\%o", how);
    }

    /* Rest of work is done else where */
    mg = sv_magicext(sv,obj,how,vtable,name,namlen);

    switch (how) {
    case PERL_MAGIC_taint:
	mg->mg_len = 1;
	break;
    case PERL_MAGIC_ext:
    case PERL_MAGIC_dbfile:
	SvRMAGICAL_on(sv);
	break;
    }
}

/*
=for apidoc sv_unmagic

Removes all magic of type C<type> from an SV.

=cut
*/

int
Perl_sv_unmagic(pTHX_ SV *sv, int type)
{
    MAGIC* mg;
    MAGIC** mgp;
    if (SvTYPE(sv) < SVt_PVMG || !SvMAGIC(sv))
	return 0;
    mgp = &SvMAGIC(sv);
    for (mg = *mgp; mg; mg = *mgp) {
	if (mg->mg_type == type) {
	    MGVTBL* vtbl = mg->mg_virtual;
	    *mgp = mg->mg_moremagic;
	    if (vtbl && vtbl->svt_free)
		CALL_FPTR(vtbl->svt_free)(aTHX_ sv, mg);
	    if (mg->mg_ptr && mg->mg_type != PERL_MAGIC_regex_global) {
		if (mg->mg_len > 0)
		    Safefree(mg->mg_ptr);
		else if (mg->mg_len == HEf_SVKEY)
		    SvREFCNT_dec((SV*)mg->mg_ptr);
		else if (mg->mg_type == PERL_MAGIC_utf8 && mg->mg_ptr)
		    Safefree(mg->mg_ptr);
            }
	    if (mg->mg_flags & MGf_REFCOUNTED)
		SvREFCNT_dec(mg->mg_obj);
	    Safefree(mg);
	}
	else
	    mgp = &mg->mg_moremagic;
    }
    if (!SvMAGIC(sv)) {
	SvMAGICAL_off(sv);
       SvFLAGS(sv) |= (SvFLAGS(sv) & (SVp_NOK|SVp_POK)) >> PRIVSHIFT;
    }

    return 0;
}

/*
=for apidoc sv_rvweaken

Weaken a reference: set the C<SvWEAKREF> flag on this RV; give the
referred-to SV C<PERL_MAGIC_backref> magic if it hasn't already; and
push a back-reference to this RV onto the array of backreferences
associated with that magic.

=cut
*/

SV *
Perl_sv_rvweaken(pTHX_ SV *sv)
{
    SV *tsv;
    if (!SvOK(sv))  /* let undefs pass */
	return sv;
    if (!SvROK(sv))
	Perl_croak(aTHX_ "Can't weaken a nonreference");
    else if (SvWEAKREF(sv)) {
	if (ckWARN(WARN_MISC))
	    Perl_warner(aTHX_ packWARN(WARN_MISC), "Reference is already weak");
	return sv;
    }
    tsv = SvRV(sv);
    sv_add_backref(tsv, sv);
    SvWEAKREF_on(sv);
    SvREFCNT_dec(tsv);
    return sv;
}

/* Give tsv backref magic if it hasn't already got it, then push a
 * back-reference to sv onto the array associated with the backref magic.
 */

STATIC void
S_sv_add_backref(pTHX_ SV *tsv, SV *sv)
{
    AV *av;
    MAGIC *mg;
    if (SvMAGICAL(tsv) && (mg = mg_find(tsv, PERL_MAGIC_backref)))
	av = (AV*)mg->mg_obj;
    else {
	av = newAV();
	sv_magic(tsv, (SV*)av, PERL_MAGIC_backref, NULL, 0);
	/* av now has a refcnt of 2, which avoids it getting freed
	 * before us during global cleanup. The extra ref is removed
	 * by magic_killbackrefs() when tsv is being freed */
    }
    if (AvFILLp(av) >= AvMAX(av)) {
        SV **svp = AvARRAY(av);
        I32 i = AvFILLp(av);
        while (i >= 0) {
            if (svp[i] == &PL_sv_undef) {
                svp[i] = sv;        /* reuse the slot */
                return;
            }
            i--;
        }
        av_extend(av, AvFILLp(av)+1);
    }
    AvARRAY(av)[++AvFILLp(av)] = sv; /* av_push() */
}

/* delete a back-reference to ourselves from the backref magic associated
 * with the SV we point to.
 */

STATIC void
S_sv_del_backref(pTHX_ SV *sv)
{
    AV *av;
    SV **svp;
    I32 i;
    SV *tsv = SvRV(sv);
    MAGIC *mg = NULL;
    if (!SvMAGICAL(tsv) || !(mg = mg_find(tsv, PERL_MAGIC_backref)))
	Perl_croak(aTHX_ "panic: del_backref");
    av = (AV *)mg->mg_obj;
    svp = AvARRAY(av);
    i = AvFILLp(av);
    while (i >= 0) {
	if (svp[i] == sv) {
	    svp[i] = &PL_sv_undef; /* XXX */
	}
	i--;
    }
}

/*
=for apidoc sv_insert

Inserts a string at the specified offset/length within the SV. Similar to
the Perl substr() function.

=cut
*/

void
Perl_sv_insert(pTHX_ SV *bigstr, STRLEN offset, STRLEN len, char *little, STRLEN littlelen)
{
    register char *big;
    register char *mid;
    register char *midend;
    register char *bigend;
    register I32 i;
    STRLEN curlen;


    if (!bigstr)
	Perl_croak(aTHX_ "Can't modify non-existent substring");
    SvPV_force(bigstr, curlen);
    (void)SvPOK_only_UTF8(bigstr);
    if (offset + len > curlen) {
	SvGROW(bigstr, offset+len+1);
	Zero(SvPVX(bigstr)+curlen, offset+len-curlen, char);
	SvCUR_set(bigstr, offset+len);
    }

    SvTAINT(bigstr);
    i = littlelen - len;
    if (i > 0) {			/* string might grow */
	big = SvGROW(bigstr, SvCUR(bigstr) + i + 1);
	mid = big + offset + len;
	midend = bigend = big + SvCUR(bigstr);
	bigend += i;
	*bigend = '\0';
	while (midend > mid)		/* shove everything down */
	    *--bigend = *--midend;
	Move(little,big+offset,littlelen,char);
	SvCUR(bigstr) += i;
	SvSETMAGIC(bigstr);
	return;
    }
    else if (i == 0) {
	Move(little,SvPVX(bigstr)+offset,len,char);
	SvSETMAGIC(bigstr);
	return;
    }

    big = SvPVX(bigstr);
    mid = big + offset;
    midend = mid + len;
    bigend = big + SvCUR(bigstr);

    if (midend > bigend)
	Perl_croak(aTHX_ "panic: sv_insert");

    if (mid - big > bigend - midend) {	/* faster to shorten from end */
	if (littlelen) {
	    Move(little, mid, littlelen,char);
	    mid += littlelen;
	}
	i = bigend - midend;
	if (i > 0) {
	    Move(midend, mid, i,char);
	    mid += i;
	}
	*mid = '\0';
	SvCUR_set(bigstr, mid - big);
    }
    /*SUPPRESS 560*/
    else if ((i = mid - big)) {	/* faster from front */
	midend -= littlelen;
	mid = midend;
	sv_chop(bigstr,midend-i);
	big += i;
	while (i--)
	    *--midend = *--big;
	if (littlelen)
	    Move(little, mid, littlelen,char);
    }
    else if (littlelen) {
	midend -= littlelen;
	sv_chop(bigstr,midend);
	Move(little,midend,littlelen,char);
    }
    else {
	sv_chop(bigstr,midend);
    }
    SvSETMAGIC(bigstr);
}

/*
=for apidoc sv_replace

Make the first argument a copy of the second, then delete the original.
The target SV physically takes over ownership of the body of the source SV
and inherits its flags; however, the target keeps any magic it owns,
and any magic in the source is discarded.
Note that this is a rather specialist SV copying operation; most of the
time you'll want to use C<sv_setsv> or one of its many macro front-ends.

=cut
*/

void
Perl_sv_replace(pTHX_ register SV *sv, register SV *nsv)
{
    U32 refcnt = SvREFCNT(sv);
    SV_CHECK_THINKFIRST(sv);
    if (SvREFCNT(nsv) != 1 && ckWARN_d(WARN_INTERNAL))
	Perl_warner(aTHX_ packWARN(WARN_INTERNAL), "Reference miscount in sv_replace()");
    if (SvMAGICAL(sv)) {
	if (SvMAGICAL(nsv))
	    mg_free(nsv);
	else
	    sv_upgrade(nsv, SVt_PVMG);
	SvMAGIC(nsv) = SvMAGIC(sv);
	SvFLAGS(nsv) |= SvMAGICAL(sv);
	SvMAGICAL_off(sv);
	SvMAGIC(sv) = 0;
    }
    SvREFCNT(sv) = 0;
    sv_clear(sv);
    assert(!SvREFCNT(sv));
    StructCopy(nsv,sv,SV);
    SvREFCNT(sv) = refcnt;
    SvFLAGS(nsv) |= SVTYPEMASK;		/* Mark as freed */
    SvREFCNT(nsv) = 0;
    del_SV(nsv);
}

/*
=for apidoc sv_clear

Clear an SV: call any destructors, free up any memory used by the body,
and free the body itself. The SV's head is I<not> freed, although
its type is set to all 1's so that it won't inadvertently be assumed
to be live during global destruction etc.
This function should only be called when REFCNT is zero. Most of the time
you'll want to call C<sv_free()> (or its macro wrapper C<SvREFCNT_dec>)
instead.

=cut
*/

void
Perl_sv_clear(pTHX_ register SV *sv)
{
    HV* stash;
    assert(sv);
    assert(SvREFCNT(sv) == 0);

    if (SvOBJECT(sv)) {
	if (PL_defstash) {		/* Still have a symbol table? */
	    dSP;
	    CV* destructor;



	    do {	
		stash = SvSTASH(sv);
		destructor = StashHANDLER(stash,DESTROY);
		if (destructor) {
		    SV* tmpref = newRV(sv);
	            SvREADONLY_on(tmpref);   /* DESTROY() could be naughty */
		    ENTER;
		    PUSHSTACKi(PERLSI_DESTROY);
		    EXTEND(SP, 2);
		    PUSHMARK(SP);
		    PUSHs(tmpref);
		    PUTBACK;
		    call_sv((SV*)destructor, G_DISCARD|G_EVAL|G_KEEPERR|G_VOID);
		   
		    
		    POPSTACK;
		    SPAGAIN;
		    LEAVE;
		    if(SvREFCNT(tmpref) < 2) {
		        /* tmpref is not kept alive! */
		        SvREFCNT(sv)--;
			SvRV(tmpref) = 0;
			SvROK_off(tmpref);
		    }
		    SvREFCNT_dec(tmpref);
		}
	    } while (SvOBJECT(sv) && SvSTASH(sv) != stash);


	    if (SvREFCNT(sv)) {
		if (PL_in_clean_objs)
		    Perl_croak(aTHX_ "DESTROY created new reference to dead object '%s'",
			  HvNAME(stash));
		/* DESTROY gave object new lease on life */
		return;
	    }
	}

	if (SvOBJECT(sv)) {
	    SvREFCNT_dec(SvSTASH(sv));	/* possibly of changed persuasion */
	    SvOBJECT_off(sv);	/* Curse the object. */
	    if (SvTYPE(sv) != SVt_PVIO)
		--PL_sv_objcount;	/* XXX Might want something more general */
	}
    }
    if (SvTYPE(sv) >= SVt_PVMG) {
    	if (SvMAGIC(sv))
	    mg_free(sv);
	if (SvFLAGS(sv) & SVpad_TYPED)
	    SvREFCNT_dec(SvSTASH(sv));
    }
    stash = NULL;
    switch (SvTYPE(sv)) {
    case SVt_PVIO:
	if (IoIFP(sv) &&
	    IoIFP(sv) != PerlIO_stdin() &&
	    IoIFP(sv) != PerlIO_stdout() &&
	    IoIFP(sv) != PerlIO_stderr())
	{
	    io_close((IO*)sv, FALSE);
	}
	if (IoDIRP(sv) && !(IoFLAGS(sv) & IOf_FAKE_DIRP))
	    PerlDir_close(IoDIRP(sv));
	IoDIRP(sv) = (DIR*)NULL;
	Safefree(IoTOP_NAME(sv));
	Safefree(IoFMT_NAME(sv));
	Safefree(IoBOTTOM_NAME(sv));
	/* FALL THROUGH */
    case SVt_PVBM:
	goto freescalar;
    case SVt_PVCV:
    case SVt_PVFM:
	cv_undef((CV*)sv);
	goto freescalar;
    case SVt_PVHV:
	hv_undef((HV*)sv);
	break;
    case SVt_PVAV:
	av_undef((AV*)sv);
	break;
    case SVt_PVLV:
	if (LvTYPE(sv) == 'T') { /* for tie: return HE to pool */
	    SvREFCNT_dec(HeKEY_sv((HE*)LvTARG(sv)));
	    HeNEXT((HE*)LvTARG(sv)) = PL_hv_fetch_ent_mh;
	    PL_hv_fetch_ent_mh = (HE*)LvTARG(sv);
	}
	else if (LvTYPE(sv) != 't') /* unless tie: unrefcnted fake SV**  */
	    SvREFCNT_dec(LvTARG(sv));
	goto freescalar;
    case SVt_PVGV:
	gp_free((GV*)sv);
	Safefree(GvNAME(sv));
	/* cannot decrease stash refcount yet, as we might recursively delete
	   ourselves when the refcnt drops to zero. Delay SvREFCNT_dec
	   of stash until current sv is completely gone.
	   -- JohnPC, 27 Mar 1998 */
	stash = GvSTASH(sv);
	/* FALL THROUGH */
    case SVt_PVMG:
    case SVt_PVNV:
    case SVt_PVIV:
      freescalar:
	(void)SvOOK_off(sv);
	/* FALL THROUGH */
    case SVt_PV:
    case SVt_RV:
	if (SvROK(sv)) {
	    if (SvWEAKREF(sv))
	        sv_del_backref(sv);
	    else
	        SvREFCNT_dec(SvRV(sv));
	}
	else if (SvPVX(sv) && SvLEN(sv))
	    Safefree(SvPVX(sv));
	else if (SvPVX(sv) && SvREADONLY(sv) && SvFAKE(sv)) {
	    unsharepvn(SvPVX(sv),
		       SvUTF8(sv) ? -(I32)SvCUR(sv) : SvCUR(sv),
		       SvUVX(sv));
	    SvFAKE_off(sv);
	}
	break;
/*
    case SVt_NV:
    case SVt_IV:
    case SVt_NULL:
	break;
*/
    }

    switch (SvTYPE(sv)) {
    case SVt_NULL:
	break;
    case SVt_IV:
	del_XIV(SvANY(sv));
	break;
    case SVt_NV:
	del_XNV(SvANY(sv));
	break;
    case SVt_RV:
	del_XRV(SvANY(sv));
	break;
    case SVt_PV:
	del_XPV(SvANY(sv));
	break;
    case SVt_PVIV:
	del_XPVIV(SvANY(sv));
	break;
    case SVt_PVNV:
	del_XPVNV(SvANY(sv));
	break;
    case SVt_PVMG:
	del_XPVMG(SvANY(sv));
	break;
    case SVt_PVLV:
	del_XPVLV(SvANY(sv));
	break;
    case SVt_PVAV:
	del_XPVAV(SvANY(sv));
	break;
    case SVt_PVHV:
	del_XPVHV(SvANY(sv));
	break;
    case SVt_PVCV:
	del_XPVCV(SvANY(sv));
	break;
    case SVt_PVGV:
	del_XPVGV(SvANY(sv));
	/* code duplication for increased performance. */
	SvFLAGS(sv) &= SVf_BREAK;
	SvFLAGS(sv) |= SVTYPEMASK;
	/* decrease refcount of the stash that owns this GV, if any */
	if (stash)
	    SvREFCNT_dec(stash);
	return; /* not break, SvFLAGS reset already happened */
    case SVt_PVBM:
	del_XPVBM(SvANY(sv));
	break;
    case SVt_PVFM:
	del_XPVFM(SvANY(sv));
	break;
    case SVt_PVIO:
	del_XPVIO(SvANY(sv));
	break;
    }
    SvFLAGS(sv) &= SVf_BREAK;
    SvFLAGS(sv) |= SVTYPEMASK;
}

/*
=for apidoc sv_newref

Increment an SV's reference count. Use the C<SvREFCNT_inc()> wrapper
instead.

=cut
*/

SV *
Perl_sv_newref(pTHX_ SV *sv)
{
    if (sv)
	ATOMIC_INC(SvREFCNT(sv));
    return sv;
}

/*
=for apidoc sv_free

Decrement an SV's reference count, and if it drops to zero, call
C<sv_clear> to invoke destructors and free up any memory used by
the body; finally, deallocate the SV's head itself.
Normally called via a wrapper macro C<SvREFCNT_dec>.

=cut
*/

void
Perl_sv_free(pTHX_ SV *sv)
{
    int refcount_is_zero;

    if (!sv)
	return;
    if (SvREFCNT(sv) == 0) {
	if (SvFLAGS(sv) & SVf_BREAK)
	    /* this SV's refcnt has been artificially decremented to
	     * trigger cleanup */
	    return;
	if (PL_in_clean_all) /* All is fair */
	    return;
	if (SvREADONLY(sv) && SvIMMORTAL(sv)) {
	    /* make sure SvREFCNT(sv)==0 happens very seldom */
	    SvREFCNT(sv) = (~(U32)0)/2;
	    return;
	}
	if (ckWARN_d(WARN_INTERNAL))
	    Perl_warner(aTHX_ packWARN(WARN_INTERNAL), "Attempt to free unreferenced scalar");
	return;
    }
    ATOMIC_DEC_AND_TEST(refcount_is_zero, SvREFCNT(sv));
    if (!refcount_is_zero)
	return;
#ifdef DEBUGGING
    if (SvTEMP(sv)) {
	if (ckWARN_d(WARN_DEBUGGING))
	    Perl_warner(aTHX_ packWARN(WARN_DEBUGGING),
			"Attempt to free temp prematurely: SV 0x%"UVxf,
			PTR2UV(sv));
	return;
    }
#endif
    if (SvREADONLY(sv) && SvIMMORTAL(sv)) {
	/* make sure SvREFCNT(sv)==0 happens very seldom */
	SvREFCNT(sv) = (~(U32)0)/2;
	return;
    }
    sv_clear(sv);
    if (! SvREFCNT(sv))
	del_SV(sv);
}

/*
=for apidoc sv_len

Returns the length of the string in the SV. Handles magic and type
coercion.  See also C<SvCUR>, which gives raw access to the xpv_cur slot.

=cut
*/

STRLEN
Perl_sv_len(pTHX_ register SV *sv)
{
    STRLEN len;

    if (!sv)
	return 0;

    if (SvGMAGICAL(sv))
	len = mg_length(sv);
    else
        (void)SvPV(sv, len);
    return len;
}

/*
=for apidoc sv_len_utf8

Returns the number of characters in the string in an SV, counting wide
UTF-8 bytes as a single character. Handles magic and type coercion.

=cut
*/

/*
 * The length is cached in PERL_UTF8_magic, in the mg_len field.  Also the
 * mg_ptr is used, by sv_pos_u2b(), see the comments of S_utf8_mg_pos_init().
 * (Note that the mg_len is not the length of the mg_ptr field.)
 *
 */

STRLEN
Perl_sv_len_utf8(pTHX_ register SV *sv)
{
    if (!sv)
	return 0;

    if (SvGMAGICAL(sv))
	return mg_length(sv);
    else
    {
	STRLEN len, ulen;
	U8 *s = (U8*)SvPV(sv, len);
	MAGIC *mg = SvMAGICAL(sv) ? mg_find(sv, PERL_MAGIC_utf8) : 0;

	if (mg && mg->mg_len != -1 && (mg->mg_len > 0 || len == 0)) {
	     ulen = mg->mg_len;
#ifdef PERL_UTF8_CACHE_ASSERT
	    assert(ulen == Perl_utf8_length(aTHX_ s, s + len));
#endif
        }
	else {
	     ulen = Perl_utf8_length(aTHX_ s, s + len);
	     if (!mg && !SvREADONLY(sv)) {
		  sv_magic(sv, 0, PERL_MAGIC_utf8, 0, 0);
		  mg = mg_find(sv, PERL_MAGIC_utf8);
		  assert(mg);
	     }
	     if (mg)
		  mg->mg_len = ulen;
	}
	return ulen;
    }
}

/* S_utf8_mg_pos_init() is used to initialize the mg_ptr field of
 * a PERL_UTF8_magic.  The mg_ptr is used to store the mapping
 * between UTF-8 and byte offsets.  There are two (substr offset and substr
 * length, the i offset, PERL_MAGIC_UTF8_CACHESIZE) times two (UTF-8 offset
 * and byte offset) cache positions.
 *
 * The mg_len field is used by sv_len_utf8(), see its comments.
 * Note that the mg_len is not the length of the mg_ptr field.
 *
 */
STATIC bool
S_utf8_mg_pos_init(pTHX_ SV *sv, MAGIC **mgp, STRLEN **cachep, I32 i, I32 *offsetp, U8 *s, U8 *start)
{
    bool found = FALSE; 

    if (SvMAGICAL(sv) && !SvREADONLY(sv)) {
        if (!*mgp) {
            sv_magic(sv, 0, PERL_MAGIC_utf8, 0, 0);
            *mgp = mg_find(sv, PERL_MAGIC_utf8);
        }
        assert(*mgp);

        if ((*mgp)->mg_ptr)
            *cachep = (STRLEN *) (*mgp)->mg_ptr;
        else {
            Newz(0, *cachep, PERL_MAGIC_UTF8_CACHESIZE * 2, STRLEN);
            (*mgp)->mg_ptr = (char *) *cachep;
        }
        assert(*cachep);

        (*cachep)[i]   = *offsetp;
        (*cachep)[i+1] = s - start;
        found = TRUE;
    }

    return found;
}

/*
 * S_utf8_mg_pos() is used to query and update mg_ptr field of
 * a PERL_UTF8_magic.  The mg_ptr is used to store the mapping
 * between UTF-8 and byte offsets.  See also the comments of
 * S_utf8_mg_pos_init().
 *
 */
STATIC bool
S_utf8_mg_pos(pTHX_ SV *sv, MAGIC **mgp, STRLEN **cachep, I32 i, I32 *offsetp, I32 uoff, U8 **sp, U8 *start, U8 *send)
{
    bool found = FALSE;

    if (SvMAGICAL(sv) && !SvREADONLY(sv)) {
        if (!*mgp)
            *mgp = mg_find(sv, PERL_MAGIC_utf8);
        if (*mgp && (*mgp)->mg_ptr) {
            *cachep = (STRLEN *) (*mgp)->mg_ptr;
	    ASSERT_UTF8_CACHE(*cachep);
            if ((*cachep)[i] == (STRLEN)uoff)	/* An exact match. */
		 found = TRUE;
	    else {			/* We will skip to the right spot. */
		 STRLEN forw  = 0;
		 STRLEN backw = 0;
		 U8* p = NULL;

		 /* The assumption is that going backward is half
		  * the speed of going forward (that's where the
		  * 2 * backw in the below comes from).  (The real
		  * figure of course depends on the UTF-8 data.) */

		 if ((*cachep)[i] > (STRLEN)uoff) {
		      forw  = uoff;
		      backw = (*cachep)[i] - (STRLEN)uoff;

		      if (forw < 2 * backw)
			   p = start;
		      else
			   p = start + (*cachep)[i+1];
		 }
		 /* Try this only for the substr offset (i == 0),
		  * not for the substr length (i == 2). */
		 else if (i == 0) { /* (*cachep)[i] < uoff */
		      STRLEN ulen = sv_len_utf8(sv);

		      if ((STRLEN)uoff < ulen) {
			   forw  = (STRLEN)uoff - (*cachep)[i];
			   backw = ulen - (STRLEN)uoff;

			   if (forw < 2 * backw)
				p = start + (*cachep)[i+1];
			   else
				p = send;
		      }

		      /* If the string is not long enough for uoff,
		       * we could extend it, but not at this low a level. */
		 }

		 if (p) {
		      if (forw < 2 * backw) {
			   while (forw--)
				p += UTF8SKIP(p);
		      }
		      else {
			   while (backw--) {
				p--;
				while (UTF8_IS_CONTINUATION(*p))
				     p--;
			   }
		      }

		      /* Update the cache. */
		      (*cachep)[i]   = (STRLEN)uoff;
		      (*cachep)[i+1] = p - start;

		      found = TRUE;
		 }
	    }
	    if (found) {	/* Setup the return values. */
		 *offsetp = (*cachep)[i+1];
		 *sp = start + *offsetp;
		 if (*sp >= send) {
		      *sp = send;
		      *offsetp = send - start;
		 }
		 else if (*sp < start) {
		      *sp = start;
		      *offsetp = 0;
		 }
	    }
	}
#ifdef PERL_UTF8_CACHE_ASSERT
	if (found) {
	     U8 *s = start;
	     I32 n = uoff;

	     while (n-- && s < send)
		  s += UTF8SKIP(s);

	     if (i == 0) {
		  assert(*offsetp == s - start);
		  assert((*cachep)[0] == (STRLEN)uoff);
		  assert((*cachep)[1] == *offsetp);
	     }
	     ASSERT_UTF8_CACHE(*cachep);
	}
#endif
    }

    return found;
}

/*
=for apidoc sv_pos_u2b

Converts the value pointed to by offsetp from a count of UTF-8 chars from
the start of the string, to a count of the equivalent number of bytes; if
lenp is non-zero, it does the same to lenp, but this time starting from
the offset, rather than from the start of the string. Handles magic and
type coercion.

=cut
*/

/*
 * sv_pos_u2b() uses, like sv_pos_b2u(), the mg_ptr of the potential
 * PERL_UTF8_magic of the sv to store the mapping between UTF-8 and
 * byte offsets.  See also the comments of S_utf8_mg_pos().
 *
 */

void
Perl_sv_pos_u2b(pTHX_ register SV *sv, I32* offsetp, I32* lenp)
{
    U8 *start;
    U8 *s;
    STRLEN len;
    STRLEN *cache = 0;
    STRLEN boffset = 0;

    if (!sv)
	return;

    start = s = (U8*)SvPV(sv, len);
    if (len) {
	 I32 uoffset = *offsetp;
	 U8 *send = s + len;
	 MAGIC *mg = 0;
	 bool found = FALSE;

         if (utf8_mg_pos(sv, &mg, &cache, 0, offsetp, *offsetp, &s, start, send))
             found = TRUE;
	 if (!found && uoffset > 0) {
	      while (s < send && uoffset--)
		   s += UTF8SKIP(s);
	      if (s >= send)
		   s = send;
              if (utf8_mg_pos_init(sv, &mg, &cache, 0, offsetp, s, start))
                  boffset = cache[1];
	      *offsetp = s - start;
	 }
	 if (lenp) {
	      found = FALSE;
	      start = s;
              if (utf8_mg_pos(sv, &mg, &cache, 2, lenp, *lenp + *offsetp, &s, start, send)) {
                  *lenp -= boffset;
                  found = TRUE;
              }
	      if (!found && *lenp > 0) {
		   I32 ulen = *lenp;
		   if (ulen > 0)
			while (s < send && ulen--)
			     s += UTF8SKIP(s);
		   if (s >= send)
			s = send;
                   if (utf8_mg_pos_init(sv, &mg, &cache, 2, lenp, s, start))
			cache[2] += *offsetp;
	      }
	      *lenp = s - start;
	 }
	 ASSERT_UTF8_CACHE(cache);
    }
    else {
	 *offsetp = 0;
	 if (lenp)
	      *lenp = 0;
    }

    return;
}

/*
=for apidoc sv_pos_b2u

Converts the value pointed to by offsetp from a count of bytes from the
start of the string, to a count of the equivalent number of UTF-8 chars.
Handles magic and type coercion.

=cut
*/

/*
 * sv_pos_b2u() uses, like sv_pos_u2b(), the mg_ptr of the potential
 * PERL_UTF8_magic of the sv to store the mapping between UTF-8 and
 * byte offsets.  See also the comments of S_utf8_mg_pos().
 *
 */

void
Perl_sv_pos_b2u(pTHX_ register SV* sv, I32* offsetp)
{
    U8* s;
    STRLEN len;

    if (!sv)
	return;

    s = (U8*)SvPV(sv, len);
    if ((I32)len < *offsetp)
	Perl_croak(aTHX_ "panic: sv_pos_b2u: bad byte offset");
    else {
	 U8* send = s + *offsetp;
	 MAGIC* mg = NULL;
	 STRLEN *cache = NULL;
      
	 len = 0;

	 if (SvMAGICAL(sv) && !SvREADONLY(sv)) {
	      mg = mg_find(sv, PERL_MAGIC_utf8);
	      if (mg && mg->mg_ptr) {
		   cache = (STRLEN *) mg->mg_ptr;
                   if (cache[1] == (STRLEN)*offsetp) {
                        /* An exact match. */
                        *offsetp = cache[0];

			return;
		   }
		   else if (cache[1] < (STRLEN)*offsetp) {
			/* We already know part of the way. */
			len = cache[0];
			s  += cache[1];
			/* Let the below loop do the rest. */ 
		   }
		   else { /* cache[1] > *offsetp */
			/* We already know all of the way, now we may
			 * be able to walk back.  The same assumption
			 * is made as in S_utf8_mg_pos(), namely that
			 * walking backward is twice slower than
			 * walking forward. */
			STRLEN forw  = *offsetp;
			STRLEN backw = cache[1] - *offsetp;

			if (!(forw < 2 * backw)) {
			     U8 *p = s + cache[1];
			     STRLEN ubackw = 0;
			     
			     cache[1] -= backw;

			     while (backw--) {
			    p--;
			    while (UTF8_IS_CONTINUATION(*p)) {
				p--;
				backw--;
			    }
			    ubackw++;
			}

			cache[0] -= ubackw;
			*offsetp = cache[0];
			return;
		    }
		}
	    }
	    ASSERT_UTF8_CACHE(cache);
	 }

	 while (s < send) {
	      STRLEN n = 1;

	      /* Call utf8n_to_uvchr() to validate the sequence
	       * (unless a simple non-UTF character) */
	      if (!UTF8_IS_INVARIANT(*s))
		   utf8n_to_uvchr(s, UTF8SKIP(s), &n, 0);
	      if (n > 0) {
		   s += n;
		   len++;
	      }
	      else
		   break;
	 }

	 if (!SvREADONLY(sv)) {
	      if (!mg) {
		   sv_magic(sv, 0, PERL_MAGIC_utf8, 0, 0);
		   mg = mg_find(sv, PERL_MAGIC_utf8);
	      }
	      assert(mg);

	      if (!mg->mg_ptr) {
		   Newz(0, cache, PERL_MAGIC_UTF8_CACHESIZE * 2, STRLEN);
		   mg->mg_ptr = (char *) cache;
	      }
	      assert(cache);

	      cache[0] = len;
	      cache[1] = *offsetp;
	 }

	 *offsetp = len;
    }

    return;
}

/*
=for apidoc sv_eq

Returns a boolean indicating whether the strings in the two SVs are
identical. Is UTF-8 and 'use bytes' aware, handles get magic, and will
coerce its args to strings if necessary.

=cut
*/

I32
Perl_sv_eq(pTHX_ register SV *sv1, register SV *sv2)
{
    char *pv1;
    STRLEN cur1;
    char *pv2;
    STRLEN cur2;
    I32  eq     = 0;
    char *tpv   = Nullch;
    SV* svrecode = Nullsv;

    if (!sv1) {
	pv1 = "";
	cur1 = 0;
    }
    else
	pv1 = SvPV(sv1, cur1);

    if (!sv2){
	pv2 = "";
	cur2 = 0;
    }
    else
	pv2 = SvPV(sv2, cur2);

    if (cur1 && cur2 && SvUTF8(sv1) != SvUTF8(sv2) && !IN_BYTES) {
        /* Differing utf8ness.
	 * Do not UTF8size the comparands as a side-effect. */
	 if (PL_encoding) {
	      if (SvUTF8(sv1)) {
		   svrecode = newSVpvn(pv2, cur2);
		   sv_recode_to_utf8(svrecode, PL_encoding);
		   pv2 = SvPV(svrecode, cur2);
	      }
	      else {
		   svrecode = newSVpvn(pv1, cur1);
		   sv_recode_to_utf8(svrecode, PL_encoding);
		   pv1 = SvPV(svrecode, cur1);
	      }
	      /* Now both are in UTF-8. */
	      if (cur1 != cur2)
		   return FALSE;
	 }
	 else {
	      bool is_utf8 = TRUE;

	      if (SvUTF8(sv1)) {
		   /* sv1 is the UTF-8 one,
		    * if is equal it must be downgrade-able */
		   char *pv = (char*)bytes_from_utf8((U8*)pv1,
						     &cur1, &is_utf8);
		   if (pv != pv1)
			pv1 = tpv = pv;
	      }
	      else {
		   /* sv2 is the UTF-8 one,
		    * if is equal it must be downgrade-able */
		   char *pv = (char *)bytes_from_utf8((U8*)pv2,
						      &cur2, &is_utf8);
		   if (pv != pv2)
			pv2 = tpv = pv;
	      }
	      if (is_utf8) {
		   /* Downgrade not possible - cannot be eq */
		   return FALSE;
	      }
	 }
    }

    if (cur1 == cur2)
	eq = memEQ(pv1, pv2, cur1);
	
    if (svrecode)
	 SvREFCNT_dec(svrecode);

    if (tpv)
	Safefree(tpv);

    return eq;
}

/*
=for apidoc sv_cmp

Compares the strings in two SVs.  Returns -1, 0, or 1 indicating whether the
string in C<sv1> is less than, equal to, or greater than the string in
C<sv2>. Is UTF-8 and 'use bytes' aware, handles get magic, and will
coerce its args to strings if necessary.  See also C<sv_cmp_locale>.

=cut
*/

I32
Perl_sv_cmp(pTHX_ register SV *sv1, register SV *sv2)
{
    STRLEN cur1, cur2;
    char *pv1, *pv2, *tpv = Nullch;
    I32  cmp;
    SV *svrecode = Nullsv;

    if (!sv1) {
	pv1 = "";
	cur1 = 0;
    }
    else
	pv1 = SvPV(sv1, cur1);

    if (!sv2) {
	pv2 = "";
	cur2 = 0;
    }
    else
	pv2 = SvPV(sv2, cur2);

    if (cur1 && cur2 && SvUTF8(sv1) != SvUTF8(sv2) && !IN_BYTES) {
        /* Differing utf8ness.
	 * Do not UTF8size the comparands as a side-effect. */
	if (SvUTF8(sv1)) {
	    if (PL_encoding) {
		 svrecode = newSVpvn(pv2, cur2);
		 sv_recode_to_utf8(svrecode, PL_encoding);
		 pv2 = SvPV(svrecode, cur2);
	    }
	    else {
		 pv2 = tpv = (char*)bytes_to_utf8((U8*)pv2, &cur2);
	    }
	}
	else {
	    if (PL_encoding) {
		 svrecode = newSVpvn(pv1, cur1);
		 sv_recode_to_utf8(svrecode, PL_encoding);
		 pv1 = SvPV(svrecode, cur1);
	    }
	    else {
		 pv1 = tpv = (char*)bytes_to_utf8((U8*)pv1, &cur1);
	    }
	}
    }

    if (!cur1) {
	cmp = cur2 ? -1 : 0;
    } else if (!cur2) {
	cmp = 1;
    } else {
	I32 retval = memcmp((void*)pv1, (void*)pv2, cur1 < cur2 ? cur1 : cur2);

	if (retval) {
	    cmp = retval < 0 ? -1 : 1;
	} else if (cur1 == cur2) {
	    cmp = 0;
        } else {
	    cmp = cur1 < cur2 ? -1 : 1;
	}
    }

    if (svrecode)
	 SvREFCNT_dec(svrecode);

    if (tpv)
	Safefree(tpv);

    return cmp;
}

/*
=for apidoc sv_cmp_locale

Compares the strings in two SVs in a locale-aware manner. Is UTF-8 and
'use bytes' aware, handles get magic, and will coerce its args to strings
if necessary.  See also C<sv_cmp_locale>.  See also C<sv_cmp>.

=cut
*/

I32
Perl_sv_cmp_locale(pTHX_ register SV *sv1, register SV *sv2)
{
#ifdef USE_LOCALE_COLLATE

    char *pv1, *pv2;
    STRLEN len1, len2;
    I32 retval;

    if (PL_collation_standard)
	goto raw_compare;

    len1 = 0;
    pv1 = sv1 ? sv_collxfrm(sv1, &len1) : (char *) NULL;
    len2 = 0;
    pv2 = sv2 ? sv_collxfrm(sv2, &len2) : (char *) NULL;

    if (!pv1 || !len1) {
	if (pv2 && len2)
	    return -1;
	else
	    goto raw_compare;
    }
    else {
	if (!pv2 || !len2)
	    return 1;
    }

    retval = memcmp((void*)pv1, (void*)pv2, len1 < len2 ? len1 : len2);

    if (retval)
	return retval < 0 ? -1 : 1;

    /*
     * When the result of collation is equality, that doesn't mean
     * that there are no differences -- some locales exclude some
     * characters from consideration.  So to avoid false equalities,
     * we use the raw string as a tiebreaker.
     */

  raw_compare:
    /* FALL THROUGH */

#endif /* USE_LOCALE_COLLATE */

    return sv_cmp(sv1, sv2);
}


#ifdef USE_LOCALE_COLLATE

/*
=for apidoc sv_collxfrm

Add Collate Transform magic to an SV if it doesn't already have it.

Any scalar variable may carry PERL_MAGIC_collxfrm magic that contains the
scalar data of the variable, but transformed to such a format that a normal
memory comparison can be used to compare the data according to the locale
settings.

=cut
*/

char *
Perl_sv_collxfrm(pTHX_ SV *sv, STRLEN *nxp)
{
    MAGIC *mg;

    mg = SvMAGICAL(sv) ? mg_find(sv, PERL_MAGIC_collxfrm) : (MAGIC *) NULL;
    if (!mg || !mg->mg_ptr || *(U32*)mg->mg_ptr != PL_collation_ix) {
	char *s, *xf;
	STRLEN len, xlen;

	if (mg)
	    Safefree(mg->mg_ptr);
	s = SvPV(sv, len);
	if ((xf = mem_collxfrm(s, len, &xlen))) {
	    if (SvREADONLY(sv)) {
		SAVEFREEPV(xf);
		*nxp = xlen;
		return xf + sizeof(PL_collation_ix);
	    }
	    if (! mg) {
		sv_magic(sv, 0, PERL_MAGIC_collxfrm, 0, 0);
		mg = mg_find(sv, PERL_MAGIC_collxfrm);
		assert(mg);
	    }
	    mg->mg_ptr = xf;
	    mg->mg_len = xlen;
	}
	else {
	    if (mg) {
		mg->mg_ptr = NULL;
		mg->mg_len = -1;
	    }
	}
    }
    if (mg && mg->mg_ptr) {
	*nxp = mg->mg_len;
	return mg->mg_ptr + sizeof(PL_collation_ix);
    }
    else {
	*nxp = 0;
	return NULL;
    }
}

#endif /* USE_LOCALE_COLLATE */

/*
=for apidoc sv_gets

Get a line from the filehandle and store it into the SV, optionally
appending to the currently-stored string.

=cut
*/

char *
Perl_sv_gets(pTHX_ register SV *sv, register PerlIO *fp, I32 append)
{
    char *rsptr;
    STRLEN rslen;
    register STDCHAR rslast;
    register STDCHAR *bp;
    register I32 cnt;
    I32 i = 0;
    I32 rspara = 0;
    I32 recsize;

    SV_CHECK_THINKFIRST(sv);
    (void)SvUPGRADE(sv, SVt_PV);

    SvSCREAM_off(sv);

    if (append) {
	if (PerlIO_isutf8(fp)) {
	    if (!SvUTF8(sv)) {
		sv_utf8_upgrade_nomg(sv);
		sv_pos_u2b(sv,&append,0);
	    }
	} else if (SvUTF8(sv)) {
	    SV *tsv = NEWSV(0,0);
	    sv_gets(tsv, fp, 0);
	    sv_utf8_upgrade_nomg(tsv);
	    SvCUR_set(sv,append);
	    sv_catsv(sv,tsv);
	    sv_free(tsv);
	    goto return_string_or_null;
	}
    }

    SvPOK_only(sv);
    if (PerlIO_isutf8(fp))
	SvUTF8_on(sv);

    if (IN_PERL_COMPILETIME) {
	/* we always read code in line mode */
	rsptr = "\n";
	rslen = 1;
    }
    else if (RsSNARF(PL_rs)) {
    	/* If it is a regular disk file use size from stat() as estimate 
	   of amount we are going to read - may result in malloc-ing 
	   more memory than we realy need if layers bellow reduce 
	   size we read (e.g. CRLF or a gzip layer)
	 */
	Stat_t st;
	if (!PerlLIO_fstat(PerlIO_fileno(fp), &st) && S_ISREG(st.st_mode))  {
	    Off_t offset = PerlIO_tell(fp);
	    if (offset != (Off_t) -1 && st.st_size + append > offset) {
	     	(void) SvGROW(sv, (STRLEN)((st.st_size - offset) + append + 1));
	    }
	}
	rsptr = NULL;
	rslen = 0;
    }
    else if (RsRECORD(PL_rs)) {
      I32 bytesread;
      char *buffer;

      /* Grab the size of the record we're getting */
      recsize = SvIV(SvRV(PL_rs));
      buffer = SvGROW(sv, (STRLEN)(recsize + append + 1)) + append;
      /* Go yank in */
#ifdef VMS
      /* VMS wants read instead of fread, because fread doesn't respect */
      /* RMS record boundaries. This is not necessarily a good thing to be */
      /* doing, but we've got no other real choice - except avoid stdio
         as implementation - perhaps write a :vms layer ?
       */
      bytesread = PerlLIO_read(PerlIO_fileno(fp), buffer, recsize);
#else
      bytesread = PerlIO_read(fp, buffer, recsize);
#endif
      if (bytesread < 0)
	  bytesread = 0;
      SvCUR_set(sv, bytesread += append);
      buffer[bytesread] = '\0';
      goto return_string_or_null;
    }
    else if (RsPARA(PL_rs)) {
	rsptr = "\n\n";
	rslen = 2;
	rspara = 1;
    }
    else {
	/* Get $/ i.e. PL_rs into same encoding as stream wants */
	if (PerlIO_isutf8(fp)) {
	    rsptr = SvPVutf8(PL_rs, rslen);
	}
	else {
	    if (SvUTF8(PL_rs)) {
		if (!sv_utf8_downgrade(PL_rs, TRUE)) {
		    Perl_croak(aTHX_ "Wide character in $/");
		}
	    }
	    rsptr = SvPV(PL_rs, rslen);
	}
    }

    rslast = rslen ? rsptr[rslen - 1] : '\0';

    if (rspara) {		/* have to do this both before and after */
	do {			/* to make sure file boundaries work right */
	    if (PerlIO_eof(fp))
		return 0;
	    i = PerlIO_getc(fp);
	    if (i != '\n') {
		if (i == -1)
		    return 0;
		PerlIO_ungetc(fp,i);
		break;
	    }
	} while (i != EOF);
    }

    /* See if we know enough about I/O mechanism to cheat it ! */

    /* This used to be #ifdef test - it is made run-time test for ease
       of abstracting out stdio interface. One call should be cheap
       enough here - and may even be a macro allowing compile
       time optimization.
     */

    if (PerlIO_fast_gets(fp)) {

    /*
     * We're going to steal some values from the stdio struct
     * and put EVERYTHING in the innermost loop into registers.
     */
    register STDCHAR *ptr;
    STRLEN bpx;
    I32 shortbuffered;

#if defined(VMS) && defined(PERLIO_IS_STDIO)
    /* An ungetc()d char is handled separately from the regular
     * buffer, so we getc() it back out and stuff it in the buffer.
     */
    i = PerlIO_getc(fp);
    if (i == EOF) return 0;
    *(--((*fp)->_ptr)) = (unsigned char) i;
    (*fp)->_cnt++;
#endif

    /* Here is some breathtakingly efficient cheating */

    cnt = PerlIO_get_cnt(fp);			/* get count into register */
    /* make sure we have the room */
    if ((I32)(SvLEN(sv) - append) <= cnt + 1) { 
    	/* Not room for all of it
	   if we are looking for a separator and room for some 
	 */
	if (rslen && cnt > 80 && (I32)SvLEN(sv) > append) {
	    /* just process what we have room for */ 
	    shortbuffered = cnt - SvLEN(sv) + append + 1;
	    cnt -= shortbuffered;
	}
	else {
	    shortbuffered = 0;
	    /* remember that cnt can be negative */
	    SvGROW(sv, (STRLEN)(append + (cnt <= 0 ? 2 : (cnt + 1))));
	}
    }
    else 
	shortbuffered = 0;
    bp = (STDCHAR*)SvPVX(sv) + append;  /* move these two too to registers */
    ptr = (STDCHAR*)PerlIO_get_ptr(fp);
    DEBUG_P(PerlIO_printf(Perl_debug_log,
	"Screamer: entering, ptr=%"UVuf", cnt=%ld\n",PTR2UV(ptr),(long)cnt));
    DEBUG_P(PerlIO_printf(Perl_debug_log,
	"Screamer: entering: PerlIO * thinks ptr=%"UVuf", cnt=%ld, base=%"UVuf"\n",
	       PTR2UV(PerlIO_get_ptr(fp)), (long)PerlIO_get_cnt(fp),
	       PTR2UV(PerlIO_has_base(fp) ? PerlIO_get_base(fp) : 0)));
    for (;;) {
      screamer:
	if (cnt > 0) {
	    if (rslen) {
		while (cnt > 0) {		     /* this     |  eat */
		    cnt--;
		    if ((*bp++ = *ptr++) == rslast)  /* really   |  dust */
			goto thats_all_folks;	     /* screams  |  sed :-) */
		}
	    }
	    else {
	        Copy(ptr, bp, cnt, char);	     /* this     |  eat */
		bp += cnt;			     /* screams  |  dust */
		ptr += cnt;			     /* louder   |  sed :-) */
		cnt = 0;
	    }
	}
	
	if (shortbuffered) {		/* oh well, must extend */
	    cnt = shortbuffered;
	    shortbuffered = 0;
	    bpx = bp - (STDCHAR*)SvPVX(sv); /* box up before relocation */
	    SvCUR_set(sv, bpx);
	    SvGROW(sv, SvLEN(sv) + append + cnt + 2);
	    bp = (STDCHAR*)SvPVX(sv) + bpx; /* unbox after relocation */
	    continue;
	}

	DEBUG_P(PerlIO_printf(Perl_debug_log,
			      "Screamer: going to getc, ptr=%"UVuf", cnt=%ld\n",
			      PTR2UV(ptr),(long)cnt));
	PerlIO_set_ptrcnt(fp, (STDCHAR*)ptr, cnt); /* deregisterize cnt and ptr */
#if 0
	DEBUG_P(PerlIO_printf(Perl_debug_log,
	    "Screamer: pre: FILE * thinks ptr=%"UVuf", cnt=%ld, base=%"UVuf"\n",
	    PTR2UV(PerlIO_get_ptr(fp)), (long)PerlIO_get_cnt(fp),
	    PTR2UV(PerlIO_has_base (fp) ? PerlIO_get_base(fp) : 0)));
#endif
	/* This used to call 'filbuf' in stdio form, but as that behaves like
	   getc when cnt <= 0 we use PerlIO_getc here to avoid introducing
	   another abstraction.  */
	i   = PerlIO_getc(fp);		/* get more characters */
#if 0
	DEBUG_P(PerlIO_printf(Perl_debug_log,
	    "Screamer: post: FILE * thinks ptr=%"UVuf", cnt=%ld, base=%"UVuf"\n",
	    PTR2UV(PerlIO_get_ptr(fp)), (long)PerlIO_get_cnt(fp),
	    PTR2UV(PerlIO_has_base (fp) ? PerlIO_get_base(fp) : 0)));
#endif
	cnt = PerlIO_get_cnt(fp);
	ptr = (STDCHAR*)PerlIO_get_ptr(fp);	/* reregisterize cnt and ptr */
	DEBUG_P(PerlIO_printf(Perl_debug_log,
	    "Screamer: after getc, ptr=%"UVuf", cnt=%ld\n",PTR2UV(ptr),(long)cnt));

	if (i == EOF)			/* all done for ever? */
	    goto thats_really_all_folks;

	bpx = bp - (STDCHAR*)SvPVX(sv);	/* box up before relocation */
	SvCUR_set(sv, bpx);
	SvGROW(sv, bpx + cnt + 2);
	bp = (STDCHAR*)SvPVX(sv) + bpx;	/* unbox after relocation */

	*bp++ = (STDCHAR)i;		/* store character from PerlIO_getc */

	if (rslen && (STDCHAR)i == rslast)  /* all done for now? */
	    goto thats_all_folks;
    }

thats_all_folks:
    if ((rslen > 1 && (STRLEN)(bp - (STDCHAR*)SvPVX(sv)) < rslen) ||
	  memNE((char*)bp - rslen, rsptr, rslen))
	goto screamer;				/* go back to the fray */
thats_really_all_folks:
    if (shortbuffered)
	cnt += shortbuffered;
	DEBUG_P(PerlIO_printf(Perl_debug_log,
	    "Screamer: quitting, ptr=%"UVuf", cnt=%ld\n",PTR2UV(ptr),(long)cnt));
    PerlIO_set_ptrcnt(fp, (STDCHAR*)ptr, cnt);	/* put these back or we're in trouble */
    DEBUG_P(PerlIO_printf(Perl_debug_log,
	"Screamer: end: FILE * thinks ptr=%"UVuf", cnt=%ld, base=%"UVuf"\n",
	PTR2UV(PerlIO_get_ptr(fp)), (long)PerlIO_get_cnt(fp),
	PTR2UV(PerlIO_has_base (fp) ? PerlIO_get_base(fp) : 0)));
    *bp = '\0';
    SvCUR_set(sv, bp - (STDCHAR*)SvPVX(sv));	/* set length */
    DEBUG_P(PerlIO_printf(Perl_debug_log,
	"Screamer: done, len=%ld, string=|%.*s|\n",
	(long)SvCUR(sv),(int)SvCUR(sv),SvPVX(sv)));
    }
   else
    {
       /*The big, slow, and stupid way. */

      /* Any stack-challenged places. */
#if defined(EPOC)
      /* EPOC: need to work around SDK features.         *
       * On WINS: MS VC5 generates calls to _chkstk,     *
       * if a "large" stack frame is allocated.          *
       * gcc on MARM does not generate calls like these. */
#   define USEHEAPINSTEADOFSTACK
#endif

#ifdef USEHEAPINSTEADOFSTACK
	STDCHAR *buf = 0;
	New(0, buf, 8192, STDCHAR);
	assert(buf);
#else
	STDCHAR buf[8192];
#endif

screamer2:
	if (rslen) {
	    register STDCHAR *bpe = buf + sizeof(buf);
	    bp = buf;
	    while ((i = PerlIO_getc(fp)) != EOF && (*bp++ = (STDCHAR)i) != rslast && bp < bpe)
		; /* keep reading */
	    cnt = bp - buf;
	}
	else {
	    cnt = PerlIO_read(fp,(char*)buf, sizeof(buf));
	    /* Accomodate broken VAXC compiler, which applies U8 cast to
	     * both args of ?: operator, causing EOF to change into 255
	     */
	    if (cnt > 0)
		 i = (U8)buf[cnt - 1];
	    else
		 i = EOF;
	}

	if (cnt < 0)
	    cnt = 0;  /* we do need to re-set the sv even when cnt <= 0 */
	if (append)
	     sv_catpvn(sv, (char *) buf, cnt);
	else
	     sv_setpvn(sv, (char *) buf, cnt);

	if (i != EOF &&			/* joy */
	    (!rslen ||
	     SvCUR(sv) < rslen ||
	     memNE(SvPVX(sv) + SvCUR(sv) - rslen, rsptr, rslen)))
	{
	    append = -1;
	    /*
	     * If we're reading from a TTY and we get a short read,
	     * indicating that the user hit his EOF character, we need
	     * to notice it now, because if we try to read from the TTY
	     * again, the EOF condition will disappear.
	     *
	     * The comparison of cnt to sizeof(buf) is an optimization
	     * that prevents unnecessary calls to feof().
	     *
	     * - jik 9/25/96
	     */
	    if (!(cnt < sizeof(buf) && PerlIO_eof(fp)))
		goto screamer2;
	}

#ifdef USEHEAPINSTEADOFSTACK
	Safefree(buf);
#endif
    }

    if (rspara) {		/* have to do this both before and after */
        while (i != EOF) {	/* to make sure file boundaries work right */
	    i = PerlIO_getc(fp);
	    if (i != '\n') {
		PerlIO_ungetc(fp,i);
		break;
	    }
	}
    }

return_string_or_null:
    return (SvCUR(sv) - append) ? SvPVX(sv) : Nullch;
}

/*
=for apidoc sv_inc

Auto-increment of the value in the SV, doing string to numeric conversion
if necessary. Handles 'get' magic.

=cut
*/

void
Perl_sv_inc(pTHX_ register SV *sv)
{
    register char *d;
    int flags;

    if (!sv)
	return;
    if (SvGMAGICAL(sv))
	mg_get(sv);
    if (SvTHINKFIRST(sv)) {
	if (SvREADONLY(sv) && SvFAKE(sv))
	    sv_force_normal(sv);
	if (SvREADONLY(sv)) {
	    if (IN_PERL_RUNTIME)
		Perl_croak(aTHX_ PL_no_modify);
	}
	if (SvROK(sv)) {
	    IV i;
	    if (SvAMAGIC(sv) && AMG_CALLun(sv,inc))
		return;
	    i = PTR2IV(SvRV(sv));
	    sv_unref(sv);
	    sv_setiv(sv, i);
	}
    }
    flags = SvFLAGS(sv);
    if ((flags & (SVp_NOK|SVp_IOK)) == SVp_NOK) {
	/* It's (privately or publicly) a float, but not tested as an
	   integer, so test it to see. */
	(void) SvIV(sv);
	flags = SvFLAGS(sv);
    }
    if ((flags & SVf_IOK) || ((flags & (SVp_IOK | SVp_NOK)) == SVp_IOK)) {
	/* It's publicly an integer, or privately an integer-not-float */
#ifdef PERL_PRESERVE_IVUV
      oops_its_int:
#endif
	if (SvIsUV(sv)) {
	    if (SvUVX(sv) == UV_MAX)
		sv_setnv(sv, UV_MAX_P1);
	    else
		(void)SvIOK_only_UV(sv);
		++SvUVX(sv);
	} else {
	    if (SvIVX(sv) == IV_MAX)
		sv_setuv(sv, (UV)IV_MAX + 1);
	    else {
		(void)SvIOK_only(sv);
		++SvIVX(sv);
	    }	
	}
	return;
    }
    if (flags & SVp_NOK) {
	(void)SvNOK_only(sv);
	SvNVX(sv) += 1.0;
	return;
    }

    if (!(flags & SVp_POK) || !*SvPVX(sv)) {
	if ((flags & SVTYPEMASK) < SVt_PVIV)
	    sv_upgrade(sv, SVt_IV);
	(void)SvIOK_only(sv);
	SvIVX(sv) = 1;
	return;
    }
    d = SvPVX(sv);
    while (isALPHA(*d)) d++;
    while (isDIGIT(*d)) d++;
    if (*d) {
#ifdef PERL_PRESERVE_IVUV
	/* Got to punt this as an integer if needs be, but we don't issue
	   warnings. Probably ought to make the sv_iv_please() that does
	   the conversion if possible, and silently.  */
	int numtype = grok_number(SvPVX(sv), SvCUR(sv), NULL);
	if (numtype && !(numtype & IS_NUMBER_INFINITY)) {
	    /* Need to try really hard to see if it's an integer.
	       9.22337203685478e+18 is an integer.
	       but "9.22337203685478e+18" + 0 is UV=9223372036854779904
	       so $a="9.22337203685478e+18"; $a+0; $a++
	       needs to be the same as $a="9.22337203685478e+18"; $a++
	       or we go insane. */
	
	    (void) sv_2iv(sv);
	    if (SvIOK(sv))
		goto oops_its_int;

	    /* sv_2iv *should* have made this an NV */
	    if (flags & SVp_NOK) {
		(void)SvNOK_only(sv);
		SvNVX(sv) += 1.0;
		return;
	    }
	    /* I don't think we can get here. Maybe I should assert this
	       And if we do get here I suspect that sv_setnv will croak. NWC
	       Fall through. */
#if defined(USE_LONG_DOUBLE)
	    DEBUG_c(PerlIO_printf(Perl_debug_log,"sv_inc punt failed to convert '%s' to IOK or NOKp, UV=0x%"UVxf" NV=%"PERL_PRIgldbl"\n",
				  SvPVX(sv), SvIVX(sv), SvNVX(sv)));
#else
	    DEBUG_c(PerlIO_printf(Perl_debug_log,"sv_inc punt failed to convert '%s' to IOK or NOKp, UV=0x%"UVxf" NV=%"NVgf"\n",
				  SvPVX(sv), SvIVX(sv), SvNVX(sv)));
#endif
	}
#endif /* PERL_PRESERVE_IVUV */
	sv_setnv(sv,Atof(SvPVX(sv)) + 1.0);
	return;
    }
    d--;
    while (d >= SvPVX(sv)) {
	if (isDIGIT(*d)) {
	    if (++*d <= '9')
		return;
	    *(d--) = '0';
	}
	else {
#ifdef EBCDIC
	    /* MKS: The original code here died if letters weren't consecutive.
	     * at least it didn't have to worry about non-C locales.  The
	     * new code assumes that ('z'-'a')==('Z'-'A'), letters are
	     * arranged in order (although not consecutively) and that only
	     * [A-Za-z] are accepted by isALPHA in the C locale.
	     */
	    if (*d != 'z' && *d != 'Z') {
		do { ++*d; } while (!isALPHA(*d));
		return;
	    }
	    *(d--) -= 'z' - 'a';
#else
	    ++*d;
	    if (isALPHA(*d))
		return;
	    *(d--) -= 'z' - 'a' + 1;
#endif
	}
    }
    /* oh,oh, the number grew */
    SvGROW(sv, SvCUR(sv) + 2);
    SvCUR(sv)++;
    for (d = SvPVX(sv) + SvCUR(sv); d > SvPVX(sv); d--)
	*d = d[-1];
    if (isDIGIT(d[1]))
	*d = '1';
    else
	*d = d[1];
}

/*
=for apidoc sv_dec

Auto-decrement of the value in the SV, doing string to numeric conversion
if necessary. Handles 'get' magic.

=cut
*/

void
Perl_sv_dec(pTHX_ register SV *sv)
{
    int flags;

    if (!sv)
	return;
    if (SvGMAGICAL(sv))
	mg_get(sv);
    if (SvTHINKFIRST(sv)) {
	if (SvREADONLY(sv) && SvFAKE(sv))
	    sv_force_normal(sv);
	if (SvREADONLY(sv)) {
	    if (IN_PERL_RUNTIME)
		Perl_croak(aTHX_ PL_no_modify);
	}
	if (SvROK(sv)) {
	    IV i;
	    if (SvAMAGIC(sv) && AMG_CALLun(sv,dec))
		return;
	    i = PTR2IV(SvRV(sv));
	    sv_unref(sv);
	    sv_setiv(sv, i);
	}
    }
    /* Unlike sv_inc we don't have to worry about string-never-numbers
       and keeping them magic. But we mustn't warn on punting */
    flags = SvFLAGS(sv);
    if ((flags & SVf_IOK) || ((flags & (SVp_IOK | SVp_NOK)) == SVp_IOK)) {
	/* It's publicly an integer, or privately an integer-not-float */
#ifdef PERL_PRESERVE_IVUV
      oops_its_int:
#endif
	if (SvIsUV(sv)) {
	    if (SvUVX(sv) == 0) {
		(void)SvIOK_only(sv);
		SvIVX(sv) = -1;
	    }
	    else {
		(void)SvIOK_only_UV(sv);
		--SvUVX(sv);
	    }	
	} else {
	    if (SvIVX(sv) == IV_MIN)
		sv_setnv(sv, (NV)IV_MIN - 1.0);
	    else {
		(void)SvIOK_only(sv);
		--SvIVX(sv);
	    }	
	}
	return;
    }
    if (flags & SVp_NOK) {
	SvNVX(sv) -= 1.0;
	(void)SvNOK_only(sv);
	return;
    }
    if (!(flags & SVp_POK)) {
	if ((flags & SVTYPEMASK) < SVt_PVNV)
	    sv_upgrade(sv, SVt_NV);
	SvNVX(sv) = -1.0;
	(void)SvNOK_only(sv);
	return;
    }
#ifdef PERL_PRESERVE_IVUV
    {
	int numtype = grok_number(SvPVX(sv), SvCUR(sv), NULL);
	if (numtype && !(numtype & IS_NUMBER_INFINITY)) {
	    /* Need to try really hard to see if it's an integer.
	       9.22337203685478e+18 is an integer.
	       but "9.22337203685478e+18" + 0 is UV=9223372036854779904
	       so $a="9.22337203685478e+18"; $a+0; $a--
	       needs to be the same as $a="9.22337203685478e+18"; $a--
	       or we go insane. */
	
	    (void) sv_2iv(sv);
	    if (SvIOK(sv))
		goto oops_its_int;

	    /* sv_2iv *should* have made this an NV */
	    if (flags & SVp_NOK) {
		(void)SvNOK_only(sv);
		SvNVX(sv) -= 1.0;
		return;
	    }
	    /* I don't think we can get here. Maybe I should assert this
	       And if we do get here I suspect that sv_setnv will croak. NWC
	       Fall through. */
#if defined(USE_LONG_DOUBLE)
	    DEBUG_c(PerlIO_printf(Perl_debug_log,"sv_dec punt failed to convert '%s' to IOK or NOKp, UV=0x%"UVxf" NV=%"PERL_PRIgldbl"\n",
				  SvPVX(sv), SvIVX(sv), SvNVX(sv)));
#else
	    DEBUG_c(PerlIO_printf(Perl_debug_log,"sv_dec punt failed to convert '%s' to IOK or NOKp, UV=0x%"UVxf" NV=%"NVgf"\n",
				  SvPVX(sv), SvIVX(sv), SvNVX(sv)));
#endif
	}
    }
#endif /* PERL_PRESERVE_IVUV */
    sv_setnv(sv,Atof(SvPVX(sv)) - 1.0);	/* punt */
}

/*
=for apidoc sv_mortalcopy

Creates a new SV which is a copy of the original SV (using C<sv_setsv>).
The new SV is marked as mortal. It will be destroyed "soon", either by an
explicit call to FREETMPS, or by an implicit call at places such as
statement boundaries.  See also C<sv_newmortal> and C<sv_2mortal>.

=cut
*/

/* Make a string that will exist for the duration of the expression
 * evaluation.  Actually, it may have to last longer than that, but
 * hopefully we won't free it until it has been assigned to a
 * permanent location. */

SV *
Perl_sv_mortalcopy(pTHX_ SV *oldstr)
{
    register SV *sv;

    new_SV(sv);
    sv_setsv(sv,oldstr);
    EXTEND_MORTAL(1);
    PL_tmps_stack[++PL_tmps_ix] = sv;
    SvTEMP_on(sv);
    return sv;
}

/*
=for apidoc sv_newmortal

Creates a new null SV which is mortal.  The reference count of the SV is
set to 1. It will be destroyed "soon", either by an explicit call to
FREETMPS, or by an implicit call at places such as statement boundaries.
See also C<sv_mortalcopy> and C<sv_2mortal>.

=cut
*/

SV *
Perl_sv_newmortal(pTHX)
{
    register SV *sv;

    new_SV(sv);
    SvFLAGS(sv) = SVs_TEMP;
    EXTEND_MORTAL(1);
    PL_tmps_stack[++PL_tmps_ix] = sv;
    return sv;
}

/*
=for apidoc sv_2mortal

Marks an existing SV as mortal.  The SV will be destroyed "soon", either
by an explicit call to FREETMPS, or by an implicit call at places such as
statement boundaries.  See also C<sv_newmortal> and C<sv_mortalcopy>.

=cut
*/

SV *
Perl_sv_2mortal(pTHX_ register SV *sv)
{
    if (!sv)
	return sv;
    if (SvREADONLY(sv) && SvIMMORTAL(sv))
	return sv;
    EXTEND_MORTAL(1);
    PL_tmps_stack[++PL_tmps_ix] = sv;
    SvTEMP_on(sv);
    return sv;
}

/*
=for apidoc newSVpv

Creates a new SV and copies a string into it.  The reference count for the
SV is set to 1.  If C<len> is zero, Perl will compute the length using
strlen().  For efficiency, consider using C<newSVpvn> instead.

=cut
*/

SV *
Perl_newSVpv(pTHX_ const char *s, STRLEN len)
{
    register SV *sv;

    new_SV(sv);
    if (!len)
	len = strlen(s);
    sv_setpvn(sv,s,len);
    return sv;
}

/*
=for apidoc newSVpvn

Creates a new SV and copies a string into it.  The reference count for the
SV is set to 1.  Note that if C<len> is zero, Perl will create a zero length
string.  You are responsible for ensuring that the source string is at least
C<len> bytes long.

=cut
*/

SV *
Perl_newSVpvn(pTHX_ const char *s, STRLEN len)
{
    register SV *sv;

    new_SV(sv);
    sv_setpvn(sv,s,len);
    return sv;
}

/*
=for apidoc newSVpvn_share

Creates a new SV with its SvPVX pointing to a shared string in the string
table. If the string does not already exist in the table, it is created
first.  Turns on READONLY and FAKE.  The string's hash is stored in the UV
slot of the SV; if the C<hash> parameter is non-zero, that value is used;
otherwise the hash is computed.  The idea here is that as the string table
is used for shared hash keys these strings will have SvPVX == HeKEY and
hash lookup will avoid string compare.

=cut
*/

SV *
Perl_newSVpvn_share(pTHX_ const char *src, I32 len, U32 hash)
{
    register SV *sv;
    bool is_utf8 = FALSE;
    if (len < 0) {
	STRLEN tmplen = -len;
        is_utf8 = TRUE;
	/* See the note in hv.c:hv_fetch() --jhi */
	src = (char*)bytes_from_utf8((U8*)src, &tmplen, &is_utf8);
	len = tmplen;
    }
    if (!hash)
	PERL_HASH(hash, src, len);
    new_SV(sv);
    sv_upgrade(sv, SVt_PVIV);
    SvPVX(sv) = sharepvn(src, is_utf8?-len:len, hash);
    SvCUR(sv) = len;
    SvUVX(sv) = hash;
    SvLEN(sv) = 0;
    SvREADONLY_on(sv);
    SvFAKE_on(sv);
    SvPOK_on(sv);
    if (is_utf8)
        SvUTF8_on(sv);
    return sv;
}


#if defined(PERL_IMPLICIT_CONTEXT)

/* pTHX_ magic can't cope with varargs, so this is a no-context
 * version of the main function, (which may itself be aliased to us).
 * Don't access this version directly.
 */

SV *
Perl_newSVpvf_nocontext(const char* pat, ...)
{
    dTHX;
    register SV *sv;
    va_list args;
    va_start(args, pat);
    sv = vnewSVpvf(pat, &args);
    va_end(args);
    return sv;
}
#endif

/*
=for apidoc newSVpvf

Creates a new SV and initializes it with the string formatted like
C<sprintf>.

=cut
*/

SV *
Perl_newSVpvf(pTHX_ const char* pat, ...)
{
    register SV *sv;
    va_list args;
    va_start(args, pat);
    sv = vnewSVpvf(pat, &args);
    va_end(args);
    return sv;
}

/* backend for newSVpvf() and newSVpvf_nocontext() */

SV *
Perl_vnewSVpvf(pTHX_ const char* pat, va_list* args)
{
    register SV *sv;
    new_SV(sv);
    sv_vsetpvfn(sv, pat, strlen(pat), args, Null(SV**), 0, Null(bool*));
    return sv;
}

/*
=for apidoc newSVnv

Creates a new SV and copies a floating point value into it.
The reference count for the SV is set to 1.

=cut
*/

SV *
Perl_newSVnv(pTHX_ NV n)
{
    register SV *sv;

    new_SV(sv);
    sv_setnv(sv,n);
    return sv;
}

/*
=for apidoc newSViv

Creates a new SV and copies an integer into it.  The reference count for the
SV is set to 1.

=cut
*/

SV *
Perl_newSViv(pTHX_ IV i)
{
    register SV *sv;

    new_SV(sv);
    sv_setiv(sv,i);
    return sv;
}

/*
=for apidoc newSVuv

Creates a new SV and copies an unsigned integer into it.
The reference count for the SV is set to 1.

=cut
*/

SV *
Perl_newSVuv(pTHX_ UV u)
{
    register SV *sv;

    new_SV(sv);
    sv_setuv(sv,u);
    return sv;
}

/*
=for apidoc newRV_noinc

Creates an RV wrapper for an SV.  The reference count for the original
SV is B<not> incremented.

=cut
*/

SV *
Perl_newRV_noinc(pTHX_ SV *tmpRef)
{
    register SV *sv;

    new_SV(sv);
    sv_upgrade(sv, SVt_RV);
    SvTEMP_off(tmpRef);
    SvRV(sv) = tmpRef;
    SvROK_on(sv);
    return sv;
}

/* newRV_inc is the official function name to use now.
 * newRV_inc is in fact #defined to newRV in sv.h
 */

SV *
Perl_newRV(pTHX_ SV *tmpRef)
{
    return newRV_noinc(SvREFCNT_inc(tmpRef));
}

/*
=for apidoc newSVsv

Creates a new SV which is an exact duplicate of the original SV.
(Uses C<sv_setsv>).

=cut
*/

SV *
Perl_newSVsv(pTHX_ register SV *old)
{
    register SV *sv;

    if (!old)
	return Nullsv;
    if (SvTYPE(old) == SVTYPEMASK) {
        if (ckWARN_d(WARN_INTERNAL))
	    Perl_warner(aTHX_ packWARN(WARN_INTERNAL), "semi-panic: attempt to dup freed string");
	return Nullsv;
    }
    new_SV(sv);
    if (SvTEMP(old)) {
	SvTEMP_off(old);
	sv_setsv(sv,old);
	SvTEMP_on(old);
    }
    else
	sv_setsv(sv,old);
    return sv;
}

/*
=for apidoc sv_reset

Underlying implementation for the C<reset> Perl function.
Note that the perl-level function is vaguely deprecated.

=cut
*/

void
Perl_sv_reset(pTHX_ register char *s, HV *stash)
{
    register HE *entry;
    register GV *gv;
    register SV *sv;
    register I32 i;
    register PMOP *pm;
    register I32 max;
    char todo[PERL_UCHAR_MAX+1];

    if (!stash)
	return;

    if (!*s) {		/* reset ?? searches */
	for (pm = HvPMROOT(stash); pm; pm = pm->op_pmnext) {
	    pm->op_pmdynflags &= ~PMdf_USED;
	}
	return;
    }

    /* reset variables */

    if (!HvARRAY(stash))
	return;

    Zero(todo, 256, char);
    while (*s) {
	i = (unsigned char)*s;
	if (s[1] == '-') {
	    s += 2;
	}
	max = (unsigned char)*s++;
	for ( ; i <= max; i++) {
	    todo[i] = 1;
	}
	for (i = 0; i <= (I32) HvMAX(stash); i++) {
	    for (entry = HvARRAY(stash)[i];
		 entry;
		 entry = HeNEXT(entry))
	    {
		if (!todo[(U8)*HeKEY(entry)])
		    continue;
		gv = (GV*)HeVAL(entry);
		sv = GvSV(gv);
		if (SvTHINKFIRST(sv)) {
		    if (!SvREADONLY(sv) && SvROK(sv))
			sv_unref(sv);
		    continue;
		}
		(void)SvOK_off(sv);
		if (SvTYPE(sv) >= SVt_PV) {
		    SvCUR_set(sv, 0);
		    if (SvPVX(sv) != Nullch)
			*SvPVX(sv) = '\0';
		    SvTAINT(sv);
		}
		if (GvAV(gv)) {
		    av_clear(GvAV(gv));
		}
		if (GvHV(gv) && !HvNAME(GvHV(gv))) {
		    hv_clear(GvHV(gv));
#ifndef PERL_MICRO
#ifdef USE_ENVIRON_ARRAY
		    if (gv == PL_envgv
#  ifdef USE_ITHREADS
			&& PL_curinterp == aTHX
#  endif
		    )
		    {
			environ[0] = Nullch;
		    }
#endif
#endif /* !PERL_MICRO */
		}
	    }
	}
    }
}

/*
=for apidoc sv_2io

Using various gambits, try to get an IO from an SV: the IO slot if its a
GV; or the recursive result if we're an RV; or the IO slot of the symbol
named after the PV if we're a string.

=cut
*/

IO*
Perl_sv_2io(pTHX_ SV *sv)
{
    IO* io;
    GV* gv;
    STRLEN n_a;

    switch (SvTYPE(sv)) {
    case SVt_PVIO:
	io = (IO*)sv;
	break;
    case SVt_PVGV:
	gv = (GV*)sv;
	io = GvIO(gv);
	if (!io)
	    Perl_croak(aTHX_ "Bad filehandle: %s", GvNAME(gv));
	break;
    default:
	if (!SvOK(sv))
	    Perl_croak(aTHX_ PL_no_usym, "filehandle");
	if (SvROK(sv))
	    return sv_2io(SvRV(sv));
	gv = gv_fetchpv(SvPV(sv,n_a), FALSE, SVt_PVIO);
	if (gv)
	    io = GvIO(gv);
	else
	    io = 0;
	if (!io)
	    Perl_croak(aTHX_ "Bad filehandle: %"SVf, sv);
	break;
    }
    return io;
}

/*
=for apidoc sv_2cv

Using various gambits, try to get a CV from an SV; in addition, try if
possible to set C<*st> and C<*gvp> to the stash and GV associated with it.

=cut
*/

CV *
Perl_sv_2cv(pTHX_ SV *sv, HV **st, GV **gvp, I32 lref)
{
    GV *gv = Nullgv;
    CV *cv = Nullcv;
    STRLEN n_a;

    if (!sv)
	return *gvp = Nullgv, Nullcv;
    switch (SvTYPE(sv)) {
    case SVt_PVCV:
	*st = CvSTASH(sv);
	*gvp = Nullgv;
	return (CV*)sv;
    case SVt_PVHV:
    case SVt_PVAV:
	*gvp = Nullgv;
	return Nullcv;
    case SVt_PVGV:
	gv = (GV*)sv;
	*gvp = gv;
	*st = GvESTASH(gv);
	goto fix_gv;

    default:
	if (SvGMAGICAL(sv))
	    mg_get(sv);
	if (SvROK(sv)) {
	    SV **sp = &sv;		/* Used in tryAMAGICunDEREF macro. */
	    tryAMAGICunDEREF(to_cv);

	    sv = SvRV(sv);
	    if (SvTYPE(sv) == SVt_PVCV) {
		cv = (CV*)sv;
		*gvp = Nullgv;
		*st = CvSTASH(cv);
		return cv;
	    }
	    else if(isGV(sv))
		gv = (GV*)sv;
	    else
		Perl_croak(aTHX_ "Not a subroutine reference");
	}
	else if (isGV(sv))
	    gv = (GV*)sv;
	else
	    gv = gv_fetchpv(SvPV(sv, n_a), lref, SVt_PVCV);
	*gvp = gv;
	if (!gv)
	    return Nullcv;
	*st = GvESTASH(gv);
    fix_gv:
	if (lref && !GvCVu(gv)) {
	    SV *tmpsv;
	    ENTER;
	    tmpsv = NEWSV(704,0);
	    gv_efullname3(tmpsv, gv, Nullch);
	    /* XXX this is probably not what they think they're getting.
	     * It has the same effect as "sub name;", i.e. just a forward
	     * declaration! */
	    newSUB(start_subparse(FALSE, 0),
		   newSVOP(OP_CONST, 0, tmpsv),
		   Nullop,
		   Nullop);
	    LEAVE;
	    if (!GvCVu(gv))
		Perl_croak(aTHX_ "Unable to create sub named \"%"SVf"\"",
			   sv);
	}
	return GvCVu(gv);
    }
}

/*
=for apidoc sv_true

Returns true if the SV has a true value by Perl's rules.
Use the C<SvTRUE> macro instead, which may call C<sv_true()> or may
instead use an in-line version.

=cut
*/

I32
Perl_sv_true(pTHX_ register SV *sv)
{
    if (!sv)
	return 0;
    if (SvPOK(sv)) {
	register XPV* tXpv;
	if ((tXpv = (XPV*)SvANY(sv)) &&
		(tXpv->xpv_cur > 1 ||
		(tXpv->xpv_cur && *tXpv->xpv_pv != '0')))
	    return 1;
	else
	    return 0;
    }
    else {
	if (SvIOK(sv))
	    return SvIVX(sv) != 0;
	else {
	    if (SvNOK(sv))
		return SvNVX(sv) != 0.0;
	    else
		return sv_2bool(sv);
	}
    }
}

/*
=for apidoc sv_iv

A private implementation of the C<SvIVx> macro for compilers which can't
cope with complex macro expressions. Always use the macro instead.

=cut
*/

IV
Perl_sv_iv(pTHX_ register SV *sv)
{
    if (SvIOK(sv)) {
	if (SvIsUV(sv))
	    return (IV)SvUVX(sv);
	return SvIVX(sv);
    }
    return sv_2iv(sv);
}

/*
=for apidoc sv_uv

A private implementation of the C<SvUVx> macro for compilers which can't
cope with complex macro expressions. Always use the macro instead.

=cut
*/

UV
Perl_sv_uv(pTHX_ register SV *sv)
{
    if (SvIOK(sv)) {
	if (SvIsUV(sv))
	    return SvUVX(sv);
	return (UV)SvIVX(sv);
    }
    return sv_2uv(sv);
}

/*
=for apidoc sv_nv

A private implementation of the C<SvNVx> macro for compilers which can't
cope with complex macro expressions. Always use the macro instead.

=cut
*/

NV
Perl_sv_nv(pTHX_ register SV *sv)
{
    if (SvNOK(sv))
	return SvNVX(sv);
    return sv_2nv(sv);
}

/* sv_pv() is now a macro using SvPV_nolen();
 * this function provided for binary compatibility only
 */

char *
Perl_sv_pv(pTHX_ SV *sv)
{
    STRLEN n_a;

    if (SvPOK(sv))
	return SvPVX(sv);

    return sv_2pv(sv, &n_a);
}

/*
=for apidoc sv_pv

Use the C<SvPV_nolen> macro instead

=for apidoc sv_pvn

A private implementation of the C<SvPV> macro for compilers which can't
cope with complex macro expressions. Always use the macro instead.

=cut
*/

char *
Perl_sv_pvn(pTHX_ SV *sv, STRLEN *lp)
{
    if (SvPOK(sv)) {
	*lp = SvCUR(sv);
	return SvPVX(sv);
    }
    return sv_2pv(sv, lp);
}


char *
Perl_sv_pvn_nomg(pTHX_ register SV *sv, STRLEN *lp)
{
    if (SvPOK(sv)) {
	*lp = SvCUR(sv);
	return SvPVX(sv);
    }
    return sv_2pv_flags(sv, lp, 0);
}

/* sv_pvn_force() is now a macro using Perl_sv_pvn_force_flags();
 * this function provided for binary compatibility only
 */

char *
Perl_sv_pvn_force(pTHX_ SV *sv, STRLEN *lp)
{
    return sv_pvn_force_flags(sv, lp, SV_GMAGIC);
}

/*
=for apidoc sv_pvn_force

Get a sensible string out of the SV somehow.
A private implementation of the C<SvPV_force> macro for compilers which
can't cope with complex macro expressions. Always use the macro instead.

=for apidoc sv_pvn_force_flags

Get a sensible string out of the SV somehow.
If C<flags> has C<SV_GMAGIC> bit set, will C<mg_get> on C<sv> if
appropriate, else not. C<sv_pvn_force> and C<sv_pvn_force_nomg> are
implemented in terms of this function.
You normally want to use the various wrapper macros instead: see
C<SvPV_force> and C<SvPV_force_nomg>

=cut
*/

char *
Perl_sv_pvn_force_flags(pTHX_ SV *sv, STRLEN *lp, I32 flags)
{
    char *s = NULL;

    if (SvTHINKFIRST(sv) && !SvROK(sv))
	sv_force_normal(sv);

    if (SvPOK(sv)) {
	*lp = SvCUR(sv);
    }
    else {
	if (SvTYPE(sv) > SVt_PVLV && SvTYPE(sv) != SVt_PVFM) {
	    Perl_croak(aTHX_ "Can't coerce %s to string in %s", sv_reftype(sv,0),
		OP_NAME(PL_op));
	}
	else
	    s = sv_2pv_flags(sv, lp, flags);
	if (s != SvPVX(sv)) {	/* Almost, but not quite, sv_setpvn() */
	    STRLEN len = *lp;
	
	    if (SvROK(sv))
		sv_unref(sv);
	    (void)SvUPGRADE(sv, SVt_PV);		/* Never FALSE */
	    SvGROW(sv, len + 1);
	    Move(s,SvPVX(sv),len,char);
	    SvCUR_set(sv, len);
	    *SvEND(sv) = '\0';
	}
	if (!SvPOK(sv)) {
	    SvPOK_on(sv);		/* validate pointer */
	    SvTAINT(sv);
	    DEBUG_c(PerlIO_printf(Perl_debug_log, "0x%"UVxf" 2pv(%s)\n",
				  PTR2UV(sv),SvPVX(sv)));
	}
    }
    return SvPVX(sv);
}

/* sv_pvbyte () is now a macro using Perl_sv_2pv_flags();
 * this function provided for binary compatibility only
 */

char *
Perl_sv_pvbyte(pTHX_ SV *sv)
{
    sv_utf8_downgrade(sv,0);
    return sv_pv(sv);
}

/*
=for apidoc sv_pvbyte

Use C<SvPVbyte_nolen> instead.

=for apidoc sv_pvbyten

A private implementation of the C<SvPVbyte> macro for compilers
which can't cope with complex macro expressions. Always use the macro
instead.

=cut
*/

char *
Perl_sv_pvbyten(pTHX_ SV *sv, STRLEN *lp)
{
    sv_utf8_downgrade(sv,0);
    return sv_pvn(sv,lp);
}

/*
=for apidoc sv_pvbyten_force

A private implementation of the C<SvPVbytex_force> macro for compilers
which can't cope with complex macro expressions. Always use the macro
instead.

=cut
*/

char *
Perl_sv_pvbyten_force(pTHX_ SV *sv, STRLEN *lp)
{
    sv_utf8_downgrade(sv,0);
    return sv_pvn_force(sv,lp);
}

/* sv_pvutf8 () is now a macro using Perl_sv_2pv_flags();
 * this function provided for binary compatibility only
 */

char *
Perl_sv_pvutf8(pTHX_ SV *sv)
{
    sv_utf8_upgrade(sv);
    return sv_pv(sv);
}

/*
=for apidoc sv_pvutf8

Use the C<SvPVutf8_nolen> macro instead

=for apidoc sv_pvutf8n

A private implementation of the C<SvPVutf8> macro for compilers
which can't cope with complex macro expressions. Always use the macro
instead.

=cut
*/

char *
Perl_sv_pvutf8n(pTHX_ SV *sv, STRLEN *lp)
{
    sv_utf8_upgrade(sv);
    return sv_pvn(sv,lp);
}

/*
=for apidoc sv_pvutf8n_force

A private implementation of the C<SvPVutf8_force> macro for compilers
which can't cope with complex macro expressions. Always use the macro
instead.

=cut
*/

char *
Perl_sv_pvutf8n_force(pTHX_ SV *sv, STRLEN *lp)
{
    sv_utf8_upgrade(sv);
    return sv_pvn_force(sv,lp);
}

/*
=for apidoc sv_reftype

Returns a string describing what the SV is a reference to.

=cut
*/

char *
Perl_sv_reftype(pTHX_ SV *sv, int ob)
{
    if (ob && SvOBJECT(sv)) {
        HV *svs = SvSTASH(sv);
        /* [20011101.072] This bandaid for C<package;> should eventually
           be removed. AMS 20011103 */
        return (svs ? HvNAME(svs) : "<none>");
    }
    else {
	switch (SvTYPE(sv)) {
	case SVt_NULL:
	case SVt_IV:
	case SVt_NV:
	case SVt_RV:
	case SVt_PV:
	case SVt_PVIV:
	case SVt_PVNV:
	case SVt_PVMG:
	case SVt_PVBM:
				if (SvROK(sv))
				    return "REF";
				else
				    return "SCALAR";
				
	case SVt_PVLV:		return SvROK(sv) ? "REF"
				/* tied lvalues should appear to be
				 * scalars for backwards compatitbility */
				: (LvTYPE(sv) == 't' || LvTYPE(sv) == 'T')
				    ? "SCALAR" : "LVALUE";
	case SVt_PVAV:		return "ARRAY";
	case SVt_PVHV:		return "HASH";
	case SVt_PVCV:		return "CODE";
	case SVt_PVGV:		return "GLOB";
	case SVt_PVFM:		return "FORMAT";
	case SVt_PVIO:		return "IO";
	default:		return "UNKNOWN";
	}
    }
}

/*
=for apidoc sv_isobject

Returns a boolean indicating whether the SV is an RV pointing to a blessed
object.  If the SV is not an RV, or if the object is not blessed, then this
will return false.

=cut
*/

int
Perl_sv_isobject(pTHX_ SV *sv)
{
    if (!sv)
	return 0;
    if (SvGMAGICAL(sv))
	mg_get(sv);
    if (!SvROK(sv))
	return 0;
    sv = (SV*)SvRV(sv);
    if (!SvOBJECT(sv))
	return 0;
    return 1;
}

/*
=for apidoc sv_isa

Returns a boolean indicating whether the SV is blessed into the specified
class.  This does not check for subtypes; use C<sv_derived_from> to verify
an inheritance relationship.

=cut
*/

int
Perl_sv_isa(pTHX_ SV *sv, const char *name)
{
    if (!sv)
	return 0;
    if (SvGMAGICAL(sv))
	mg_get(sv);
    if (!SvROK(sv))
	return 0;
    sv = (SV*)SvRV(sv);
    if (!SvOBJECT(sv))
	return 0;

    return strEQ(HvNAME(SvSTASH(sv)), name);
}

/*
=for apidoc newSVrv

Creates a new SV for the RV, C<rv>, to point to.  If C<rv> is not an RV then
it will be upgraded to one.  If C<classname> is non-null then the new SV will
be blessed in the specified package.  The new SV is returned and its
reference count is 1.

=cut
*/

SV*
Perl_newSVrv(pTHX_ SV *rv, const char *classname)
{
    SV *sv;

    new_SV(sv);

    SV_CHECK_THINKFIRST(rv);
    SvAMAGIC_off(rv);

    if (SvTYPE(rv) >= SVt_PVMG) {
	U32 refcnt = SvREFCNT(rv);
	SvREFCNT(rv) = 0;
	sv_clear(rv);
	SvFLAGS(rv) = 0;
	SvREFCNT(rv) = refcnt;
    }

    if (SvTYPE(rv) < SVt_RV)
	sv_upgrade(rv, SVt_RV);
    else if (SvTYPE(rv) > SVt_RV) {
	(void)SvOOK_off(rv);
	if (SvPVX(rv) && SvLEN(rv))
	    Safefree(SvPVX(rv));
	SvCUR_set(rv, 0);
	SvLEN_set(rv, 0);
    }

    (void)SvOK_off(rv);
    SvRV(rv) = sv;
    SvROK_on(rv);

    if (classname) {
	HV* stash = gv_stashpv(classname, TRUE);
	(void)sv_bless(rv, stash);
    }
    return sv;
}

/*
=for apidoc sv_setref_pv

Copies a pointer into a new SV, optionally blessing the SV.  The C<rv>
argument will be upgraded to an RV.  That RV will be modified to point to
the new SV.  If the C<pv> argument is NULL then C<PL_sv_undef> will be placed
into the SV.  The C<classname> argument indicates the package for the
blessing.  Set C<classname> to C<Nullch> to avoid the blessing.  The new SV
will have a reference count of 1, and the RV will be returned.

Do not use with other Perl types such as HV, AV, SV, CV, because those
objects will become corrupted by the pointer copy process.

Note that C<sv_setref_pvn> copies the string while this copies the pointer.

=cut
*/

SV*
Perl_sv_setref_pv(pTHX_ SV *rv, const char *classname, void *pv)
{
    if (!pv) {
	sv_setsv(rv, &PL_sv_undef);
	SvSETMAGIC(rv);
    }
    else
	sv_setiv(newSVrv(rv,classname), PTR2IV(pv));
    return rv;
}

/*
=for apidoc sv_setref_iv

Copies an integer into a new SV, optionally blessing the SV.  The C<rv>
argument will be upgraded to an RV.  That RV will be modified to point to
the new SV.  The C<classname> argument indicates the package for the
blessing.  Set C<classname> to C<Nullch> to avoid the blessing.  The new SV
will have a reference count of 1, and the RV will be returned.

=cut
*/

SV*
Perl_sv_setref_iv(pTHX_ SV *rv, const char *classname, IV iv)
{
    sv_setiv(newSVrv(rv,classname), iv);
    return rv;
}

/*
=for apidoc sv_setref_uv

Copies an unsigned integer into a new SV, optionally blessing the SV.  The C<rv>
argument will be upgraded to an RV.  That RV will be modified to point to
the new SV.  The C<classname> argument indicates the package for the
blessing.  Set C<classname> to C<Nullch> to avoid the blessing.  The new SV
will have a reference count of 1, and the RV will be returned.

=cut
*/

SV*
Perl_sv_setref_uv(pTHX_ SV *rv, const char *classname, UV uv)
{
    sv_setuv(newSVrv(rv,classname), uv);
    return rv;
}

/*
=for apidoc sv_setref_nv

Copies a double into a new SV, optionally blessing the SV.  The C<rv>
argument will be upgraded to an RV.  That RV will be modified to point to
the new SV.  The C<classname> argument indicates the package for the
blessing.  Set C<classname> to C<Nullch> to avoid the blessing.  The new SV
will have a reference count of 1, and the RV will be returned.

=cut
*/

SV*
Perl_sv_setref_nv(pTHX_ SV *rv, const char *classname, NV nv)
{
    sv_setnv(newSVrv(rv,classname), nv);
    return rv;
}

/*
=for apidoc sv_setref_pvn

Copies a string into a new SV, optionally blessing the SV.  The length of the
string must be specified with C<n>.  The C<rv> argument will be upgraded to
an RV.  That RV will be modified to point to the new SV.  The C<classname>
argument indicates the package for the blessing.  Set C<classname> to
C<Nullch> to avoid the blessing.  The new SV will have a reference count 
of 1, and the RV will be returned.

Note that C<sv_setref_pv> copies the pointer while this copies the string.

=cut
*/

SV*
Perl_sv_setref_pvn(pTHX_ SV *rv, const char *classname, char *pv, STRLEN n)
{
    sv_setpvn(newSVrv(rv,classname), pv, n);
    return rv;
}

/*
=for apidoc sv_bless

Blesses an SV into a specified package.  The SV must be an RV.  The package
must be designated by its stash (see C<gv_stashpv()>).  The reference count
of the SV is unaffected.

=cut
*/

SV*
Perl_sv_bless(pTHX_ SV *sv, HV *stash)
{
    SV *tmpRef;
    if (!SvROK(sv))
        Perl_croak(aTHX_ "Can't bless non-reference value");
    tmpRef = SvRV(sv);
    if (SvFLAGS(tmpRef) & (SVs_OBJECT|SVf_READONLY)) {
	if (SvREADONLY(tmpRef))
	    Perl_croak(aTHX_ PL_no_modify);
	if (SvOBJECT(tmpRef)) {
	    if (SvTYPE(tmpRef) != SVt_PVIO)
		--PL_sv_objcount;
	    SvREFCNT_dec(SvSTASH(tmpRef));
	}
    }
    SvOBJECT_on(tmpRef);
    if (SvTYPE(tmpRef) != SVt_PVIO)
	++PL_sv_objcount;
    (void)SvUPGRADE(tmpRef, SVt_PVMG);
    SvSTASH(tmpRef) = (HV*)SvREFCNT_inc(stash);

    if (Gv_AMG(stash))
	SvAMAGIC_on(sv);
    else
	SvAMAGIC_off(sv);

    if(SvSMAGICAL(tmpRef))
        if(mg_find(tmpRef, PERL_MAGIC_ext) || mg_find(tmpRef, PERL_MAGIC_uvar))
            mg_set(tmpRef);



    return sv;
}

/* Downgrades a PVGV to a PVMG.
 */

STATIC void
S_sv_unglob(pTHX_ SV *sv)
{
    void *xpvmg;

    assert(SvTYPE(sv) == SVt_PVGV);
    SvFAKE_off(sv);
    if (GvGP(sv))
	gp_free((GV*)sv);
    if (GvSTASH(sv)) {
	SvREFCNT_dec(GvSTASH(sv));
	GvSTASH(sv) = Nullhv;
    }
    sv_unmagic(sv, PERL_MAGIC_glob);
    Safefree(GvNAME(sv));
    GvMULTI_off(sv);

    /* need to keep SvANY(sv) in the right arena */
    xpvmg = new_XPVMG();
    StructCopy(SvANY(sv), xpvmg, XPVMG);
    del_XPVGV(SvANY(sv));
    SvANY(sv) = xpvmg;

    SvFLAGS(sv) &= ~SVTYPEMASK;
    SvFLAGS(sv) |= SVt_PVMG;
}

/*
=for apidoc sv_unref_flags

Unsets the RV status of the SV, and decrements the reference count of
whatever was being referenced by the RV.  This can almost be thought of
as a reversal of C<newSVrv>.  The C<cflags> argument can contain
C<SV_IMMEDIATE_UNREF> to force the reference count to be decremented
(otherwise the decrementing is conditional on the reference count being
different from one or the reference being a readonly SV).
See C<SvROK_off>.

=cut
*/

void
Perl_sv_unref_flags(pTHX_ SV *sv, U32 flags)
{
    SV* rv = SvRV(sv);

    if (SvWEAKREF(sv)) {
    	sv_del_backref(sv);
	SvWEAKREF_off(sv);
	SvRV(sv) = 0;
	return;
    }
    SvRV(sv) = 0;
    SvROK_off(sv);
    /* You can't have a || SvREADONLY(rv) here, as $a = $$a, where $a was
       assigned to as BEGIN {$a = \"Foo"} will fail.  */
    if (SvREFCNT(rv) != 1 || (flags & SV_IMMEDIATE_UNREF))
	SvREFCNT_dec(rv);
    else /* XXX Hack, but hard to make $a=$a->[1] work otherwise */
	sv_2mortal(rv);		/* Schedule for freeing later */
}

/*
=for apidoc sv_unref

Unsets the RV status of the SV, and decrements the reference count of
whatever was being referenced by the RV.  This can almost be thought of
as a reversal of C<newSVrv>.  This is C<sv_unref_flags> with the C<flag>
being zero.  See C<SvROK_off>.

=cut
*/

void
Perl_sv_unref(pTHX_ SV *sv)
{
    sv_unref_flags(sv, 0);
}

/*
=for apidoc sv_taint

Taint an SV. Use C<SvTAINTED_on> instead.
=cut
*/

void
Perl_sv_taint(pTHX_ SV *sv)
{
    sv_magic((sv), Nullsv, PERL_MAGIC_taint, Nullch, 0);
}

/*
=for apidoc sv_untaint

Untaint an SV. Use C<SvTAINTED_off> instead.
=cut
*/

void
Perl_sv_untaint(pTHX_ SV *sv)
{
    if (SvTYPE(sv) >= SVt_PVMG && SvMAGIC(sv)) {
	MAGIC *mg = mg_find(sv, PERL_MAGIC_taint);
	if (mg)
	    mg->mg_len &= ~1;
    }
}

/*
=for apidoc sv_tainted

Test an SV for taintedness. Use C<SvTAINTED> instead.
=cut
*/

bool
Perl_sv_tainted(pTHX_ SV *sv)
{
    if (SvTYPE(sv) >= SVt_PVMG && SvMAGIC(sv)) {
	MAGIC *mg = mg_find(sv, PERL_MAGIC_taint);
	if (mg && ((mg->mg_len & 1) || ((mg->mg_len & 2) && mg->mg_obj == sv)))
	    return TRUE;
    }
    return FALSE;
}

/*
=for apidoc sv_setpviv

Copies an integer into the given SV, also updating its string value.
Does not handle 'set' magic.  See C<sv_setpviv_mg>.

=cut
*/

void
Perl_sv_setpviv(pTHX_ SV *sv, IV iv)
{
    char buf[TYPE_CHARS(UV)];
    char *ebuf;
    char *ptr = uiv_2buf(buf, iv, 0, 0, &ebuf);

    sv_setpvn(sv, ptr, ebuf - ptr);
}

/*
=for apidoc sv_setpviv_mg

Like C<sv_setpviv>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setpviv_mg(pTHX_ SV *sv, IV iv)
{
    char buf[TYPE_CHARS(UV)];
    char *ebuf;
    char *ptr = uiv_2buf(buf, iv, 0, 0, &ebuf);

    sv_setpvn(sv, ptr, ebuf - ptr);
    SvSETMAGIC(sv);
}

#if defined(PERL_IMPLICIT_CONTEXT)

/* pTHX_ magic can't cope with varargs, so this is a no-context
 * version of the main function, (which may itself be aliased to us).
 * Don't access this version directly.
 */

void
Perl_sv_setpvf_nocontext(SV *sv, const char* pat, ...)
{
    dTHX;
    va_list args;
    va_start(args, pat);
    sv_vsetpvf(sv, pat, &args);
    va_end(args);
}

/* pTHX_ magic can't cope with varargs, so this is a no-context
 * version of the main function, (which may itself be aliased to us).
 * Don't access this version directly.
 */

void
Perl_sv_setpvf_mg_nocontext(SV *sv, const char* pat, ...)
{
    dTHX;
    va_list args;
    va_start(args, pat);
    sv_vsetpvf_mg(sv, pat, &args);
    va_end(args);
}
#endif

/*
=for apidoc sv_setpvf

Processes its arguments like C<sprintf> and sets an SV to the formatted
output.  Does not handle 'set' magic.  See C<sv_setpvf_mg>.

=cut
*/

void
Perl_sv_setpvf(pTHX_ SV *sv, const char* pat, ...)
{
    va_list args;
    va_start(args, pat);
    sv_vsetpvf(sv, pat, &args);
    va_end(args);
}

/* backend for C<sv_setpvf> and C<sv_setpvf_nocontext> */

void
Perl_sv_vsetpvf(pTHX_ SV *sv, const char* pat, va_list* args)
{
    sv_vsetpvfn(sv, pat, strlen(pat), args, Null(SV**), 0, Null(bool*));
}

/*
=for apidoc sv_setpvf_mg

Like C<sv_setpvf>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_setpvf_mg(pTHX_ SV *sv, const char* pat, ...)
{
    va_list args;
    va_start(args, pat);
    sv_vsetpvf_mg(sv, pat, &args);
    va_end(args);
}

/* backend for C<sv_setpvf_mg> C<setpvf_mg_nocontext> */

void
Perl_sv_vsetpvf_mg(pTHX_ SV *sv, const char* pat, va_list* args)
{
    sv_vsetpvfn(sv, pat, strlen(pat), args, Null(SV**), 0, Null(bool*));
    SvSETMAGIC(sv);
}

#if defined(PERL_IMPLICIT_CONTEXT)

/* pTHX_ magic can't cope with varargs, so this is a no-context
 * version of the main function, (which may itself be aliased to us).
 * Don't access this version directly.
 */

void
Perl_sv_catpvf_nocontext(SV *sv, const char* pat, ...)
{
    dTHX;
    va_list args;
    va_start(args, pat);
    sv_vcatpvf(sv, pat, &args);
    va_end(args);
}

/* pTHX_ magic can't cope with varargs, so this is a no-context
 * version of the main function, (which may itself be aliased to us).
 * Don't access this version directly.
 */

void
Perl_sv_catpvf_mg_nocontext(SV *sv, const char* pat, ...)
{
    dTHX;
    va_list args;
    va_start(args, pat);
    sv_vcatpvf_mg(sv, pat, &args);
    va_end(args);
}
#endif

/*
=for apidoc sv_catpvf

Processes its arguments like C<sprintf> and appends the formatted
output to an SV.  If the appended data contains "wide" characters
(including, but not limited to, SVs with a UTF-8 PV formatted with %s,
and characters >255 formatted with %c), the original SV might get
upgraded to UTF-8.  Handles 'get' magic, but not 'set' magic.
C<SvSETMAGIC()> must typically be called after calling this function
to handle 'set' magic.

=cut */

void
Perl_sv_catpvf(pTHX_ SV *sv, const char* pat, ...)
{
    va_list args;
    va_start(args, pat);
    sv_vcatpvf(sv, pat, &args);
    va_end(args);
}

/* backend for C<sv_catpvf> and C<catpvf_mg_nocontext> */

void
Perl_sv_vcatpvf(pTHX_ SV *sv, const char* pat, va_list* args)
{
    sv_vcatpvfn(sv, pat, strlen(pat), args, Null(SV**), 0, Null(bool*));
}

/*
=for apidoc sv_catpvf_mg

Like C<sv_catpvf>, but also handles 'set' magic.

=cut
*/

void
Perl_sv_catpvf_mg(pTHX_ SV *sv, const char* pat, ...)
{
    va_list args;
    va_start(args, pat);
    sv_vcatpvf_mg(sv, pat, &args);
    va_end(args);
}

/* backend for C<catpvf_mg> and C<catpvf_mg_nocontext> */

void
Perl_sv_vcatpvf_mg(pTHX_ SV *sv, const char* pat, va_list* args)
{
    sv_vcatpvfn(sv, pat, strlen(pat), args, Null(SV**), 0, Null(bool*));
    SvSETMAGIC(sv);
}

/*
=for apidoc sv_vsetpvfn

Works like C<vcatpvfn> but copies the text into the SV instead of
appending it.

Usually used via one of its frontends C<sv_setpvf> and C<sv_setpvf_mg>.

=cut
*/

void
Perl_sv_vsetpvfn(pTHX_ SV *sv, const char *pat, STRLEN patlen, va_list *args, SV **svargs, I32 svmax, bool *maybe_tainted)
{
    sv_setpvn(sv, "", 0);
    sv_vcatpvfn(sv, pat, patlen, args, svargs, svmax, maybe_tainted);
}

/* private function for use in sv_vcatpvfn via the EXPECT_NUMBER macro */

STATIC I32
S_expect_number(pTHX_ char** pattern)
{
    I32 var = 0;
    switch (**pattern) {
    case '1': case '2': case '3':
    case '4': case '5': case '6':
    case '7': case '8': case '9':
	while (isDIGIT(**pattern))
	    var = var * 10 + (*(*pattern)++ - '0');
    }
    return var;
}
#define EXPECT_NUMBER(pattern, var) (var = S_expect_number(aTHX_ &pattern))

/*
=for apidoc sv_vcatpvfn

Processes its arguments like C<vsprintf> and appends the formatted output
to an SV.  Uses an array of SVs if the C style variable argument list is
missing (NULL).  When running with taint checks enabled, indicates via
C<maybe_tainted> if results are untrustworthy (often due to the use of
locales).

Usually used via one of its frontends C<sv_catpvf> and C<sv_catpvf_mg>.

=cut
*/

void
Perl_sv_vcatpvfn(pTHX_ SV *sv, const char *pat, STRLEN patlen, va_list *args, SV **svargs, I32 svmax, bool *maybe_tainted)
{
    char *p;
    char *q;
    char *patend;
    STRLEN origlen;
    I32 svix = 0;
    static char nullstr[] = "(null)";
    SV *argsv = Nullsv;
    bool has_utf8; /* has the result utf8? */
    bool pat_utf8; /* the pattern is in utf8? */
    SV *nsv = Nullsv;

    has_utf8 = pat_utf8 = DO_UTF8(sv);

    /* no matter what, this is a string now */
    (void)SvPV_force(sv, origlen);

    /* special-case "", "%s", and "%_" */
    if (patlen == 0)
	return;
    if (patlen == 2 && pat[0] == '%') {
	switch (pat[1]) {
	case 's':
	    if (args) {
		char *s = va_arg(*args, char*);
		sv_catpv(sv, s ? s : nullstr);
	    }
	    else if (svix < svmax) {
		sv_catsv(sv, *svargs);
		if (DO_UTF8(*svargs))
		    SvUTF8_on(sv);
	    }
	    return;
	case '_':
	    if (args) {
		argsv = va_arg(*args, SV*);
		sv_catsv(sv, argsv);
		if (DO_UTF8(argsv))
		    SvUTF8_on(sv);
		return;
	    }
	    /* See comment on '_' below */
	    break;
	}
    }

    if (!args && svix < svmax && DO_UTF8(*svargs))
	has_utf8 = TRUE;

    patend = (char*)pat + patlen;
    for (p = (char*)pat; p < patend; p = q) {
	bool alt = FALSE;
	bool left = FALSE;
	bool vectorize = FALSE;
	bool vectorarg = FALSE;
	bool vec_utf8 = FALSE;
	char fill = ' ';
	char plus = 0;
	char intsize = 0;
	STRLEN width = 0;
	STRLEN zeros = 0;
	bool has_precis = FALSE;
	STRLEN precis = 0;
	I32 osvix = svix;
	bool is_utf8 = FALSE;  /* is this item utf8?   */
#ifdef HAS_LDBL_SPRINTF_BUG
	/* This is to try to fix a bug with irix/nonstop-ux/powerux and
	   with sfio - Allen <allens@cpan.org> */
	bool fix_ldbl_sprintf_bug = FALSE;
#endif

	char esignbuf[4];
	U8 utf8buf[UTF8_MAXLEN+1];
	STRLEN esignlen = 0;

	char *eptr = Nullch;
	STRLEN elen = 0;
	/* Times 4: a decimal digit takes more than 3 binary digits.
	 * NV_DIG: mantissa takes than many decimal digits.
	 * Plus 32: Playing safe. */
	char ebuf[IV_DIG * 4 + NV_DIG + 32];
	/* large enough for "%#.#f" --chip */
	/* what about long double NVs? --jhi */

	SV *vecsv = Nullsv;
	U8 *vecstr = Null(U8*);
	STRLEN veclen = 0;
	char c = 0;
	int i;
	unsigned base = 0;
	IV iv = 0;
	UV uv = 0;
	/* we need a long double target in case HAS_LONG_DOUBLE but
	   not USE_LONG_DOUBLE
	*/
#if defined(HAS_LONG_DOUBLE) && LONG_DOUBLESIZE > DOUBLESIZE
	long double nv;
#else
	NV nv;
#endif
	STRLEN have;
	STRLEN need;
	STRLEN gap;
	char *dotstr = ".";
	STRLEN dotstrlen = 1;
	I32 efix = 0; /* explicit format parameter index */
	I32 ewix = 0; /* explicit width index */
	I32 epix = 0; /* explicit precision index */
	I32 evix = 0; /* explicit vector index */
	bool asterisk = FALSE;

	/* echo everything up to the next format specification */
	for (q = p; q < patend && *q != '%'; ++q) ;
	if (q > p) {
	    if (has_utf8 && !pat_utf8)
		sv_catpvn_utf8_upgrade(sv, p, q - p, nsv);
	    else
		sv_catpvn(sv, p, q - p);
	    p = q;
	}
	if (q++ >= patend)
	    break;

/*
    We allow format specification elements in this order:
	\d+\$              explicit format parameter index
	[-+ 0#]+           flags
	v|\*(\d+\$)?v      vector with optional (optionally specified) arg
	0		   flag (as above): repeated to allow "v02" 	
	\d+|\*(\d+\$)?     width using optional (optionally specified) arg
	\.(\d*|\*(\d+\$)?) precision using optional (optionally specified) arg
	[hlqLV]            size
    [%bcdefginopsux_DFOUX] format (mandatory)
*/
	if (EXPECT_NUMBER(q, width)) {
	    if (*q == '$') {
		++q;
		efix = width;
	    } else {
		goto gotwidth;
	    }
	}

	/* FLAGS */

	while (*q) {
	    switch (*q) {
	    case ' ':
	    case '+':
		plus = *q++;
		continue;

	    case '-':
		left = TRUE;
		q++;
		continue;

	    case '0':
		fill = *q++;
		continue;

	    case '#':
		alt = TRUE;
		q++;
		continue;

	    default:
		break;
	    }
	    break;
	}

      tryasterisk:
	if (*q == '*') {
	    q++;
	    if (EXPECT_NUMBER(q, ewix))
		if (*q++ != '$')
		    goto unknown;
	    asterisk = TRUE;
	}
	if (*q == 'v') {
	    q++;
	    if (vectorize)
		goto unknown;
	    if ((vectorarg = asterisk)) {
		evix = ewix;
		ewix = 0;
		asterisk = FALSE;
	    }
	    vectorize = TRUE;
	    goto tryasterisk;
	}

	if (!asterisk)
	    if( *q == '0' ) 
		fill = *q++;
	    EXPECT_NUMBER(q, width);

	if (vectorize) {
	    if (vectorarg) {
		if (args)
		    vecsv = va_arg(*args, SV*);
		else
		    vecsv = (evix ? evix <= svmax : svix < svmax) ?
			svargs[evix ? evix-1 : svix++] : &PL_sv_undef;
		dotstr = SvPVx(vecsv, dotstrlen);
		if (DO_UTF8(vecsv))
		    is_utf8 = TRUE;
	    }
	    if (args) {
		vecsv = va_arg(*args, SV*);
		vecstr = (U8*)SvPVx(vecsv,veclen);
		vec_utf8 = DO_UTF8(vecsv);
	    }
	    else if (efix ? efix <= svmax : svix < svmax) {
		vecsv = svargs[efix ? efix-1 : svix++];
		vecstr = (U8*)SvPVx(vecsv,veclen);
		vec_utf8 = DO_UTF8(vecsv);
	    }
	    else {
		vecstr = (U8*)"";
		veclen = 0;
	    }
	}

	if (asterisk) {
	    if (args)
		i = va_arg(*args, int);
	    else
		i = (ewix ? ewix <= svmax : svix < svmax) ?
		    SvIVx(svargs[ewix ? ewix-1 : svix++]) : 0;
	    left |= (i < 0);
	    width = (i < 0) ? -i : i;
	}
      gotwidth:

	/* PRECISION */

	if (*q == '.') {
	    q++;
	    if (*q == '*') {
		q++;
		if (EXPECT_NUMBER(q, epix) && *q++ != '$')
		    goto unknown;
		/* XXX: todo, support specified precision parameter */
		if (epix)
		    goto unknown;
		if (args)
		    i = va_arg(*args, int);
		else
		    i = (ewix ? ewix <= svmax : svix < svmax)
			? SvIVx(svargs[ewix ? ewix-1 : svix++]) : 0;
		precis = (i < 0) ? 0 : i;
	    }
	    else {
		precis = 0;
		while (isDIGIT(*q))
		    precis = precis * 10 + (*q++ - '0');
	    }
	    has_precis = TRUE;
	}

	/* SIZE */

	switch (*q) {
#ifdef WIN32
	case 'I':			/* Ix, I32x, and I64x */
#  ifdef WIN64
	    if (q[1] == '6' && q[2] == '4') {
		q += 3;
		intsize = 'q';
		break;
	    }
#  endif
	    if (q[1] == '3' && q[2] == '2') {
		q += 3;
		break;
	    }
#  ifdef WIN64
	    intsize = 'q';
#  endif
	    q++;
	    break;
#endif
#if defined(HAS_QUAD) || defined(HAS_LONG_DOUBLE)
	case 'L':			/* Ld */
	    /* FALL THROUGH */
#ifdef HAS_QUAD
	case 'q':			/* qd */
#endif
	    intsize = 'q';
	    q++;
	    break;
#endif
	case 'l':
#if defined(HAS_QUAD) || defined(HAS_LONG_DOUBLE)
	    if (*(q + 1) == 'l') {	/* lld, llf */
		intsize = 'q';
		q += 2;
		break;
	     }
#endif
	    /* FALL THROUGH */
	case 'h':
	    /* FALL THROUGH */
	case 'V':
	    intsize = *q++;
	    break;
	}

	/* CONVERSION */

	if (*q == '%') {
	    eptr = q++;
	    elen = 1;
	    goto string;
	}

	if (vectorize)
	    argsv = vecsv;
	else if (!args)
	    argsv = (efix ? efix <= svmax : svix < svmax) ?
		    svargs[efix ? efix-1 : svix++] : &PL_sv_undef;

	switch (c = *q++) {

	    /* STRINGS */

	case 'c':
	    uv = (args && !vectorize) ? va_arg(*args, int) : SvIVx(argsv);
	    if ((uv > 255 ||
		 (!UNI_IS_INVARIANT(uv) && SvUTF8(sv)))
		&& !IN_BYTES) {
		eptr = (char*)utf8buf;
		elen = uvchr_to_utf8((U8*)eptr, uv) - utf8buf;
		is_utf8 = TRUE;
	    }
	    else {
		c = (char)uv;
		eptr = &c;
		elen = 1;
	    }
	    goto string;

	case 's':
	    if (args && !vectorize) {
		eptr = va_arg(*args, char*);
		if (eptr)
#ifdef MACOS_TRADITIONAL
		  /* On MacOS, %#s format is used for Pascal strings */
		  if (alt)
		    elen = *eptr++;
		  else
#endif
		    elen = strlen(eptr);
		else {
		    eptr = nullstr;
		    elen = sizeof nullstr - 1;
		}
	    }
	    else {
		eptr = SvPVx(argsv, elen);
		if (DO_UTF8(argsv)) {
		    if (has_precis && precis < elen) {
			I32 p = precis;
			sv_pos_u2b(argsv, &p, 0); /* sticks at end */
			precis = p;
		    }
		    if (width) { /* fudge width (can't fudge elen) */
			width += elen - sv_len_utf8(argsv);
		    }
		    is_utf8 = TRUE;
		}
	    }
	    goto string;

	case '_':
	    /*
	     * The "%_" hack might have to be changed someday,
	     * if ISO or ANSI decide to use '_' for something.
	     * So we keep it hidden from users' code.
	     */
	    if (!args || vectorize)
		goto unknown;
	    argsv = va_arg(*args, SV*);
	    eptr = SvPVx(argsv, elen);
	    if (DO_UTF8(argsv))
		is_utf8 = TRUE;

	string:
	    vectorize = FALSE;
	    if (has_precis && elen > precis)
		elen = precis;
	    break;

	    /* INTEGERS */

	case 'p':
	    if (alt || vectorize)
		goto unknown;
	    uv = PTR2UV(args ? va_arg(*args, void*) : argsv);
	    base = 16;
	    goto integer;

	case 'D':
#ifdef IV_IS_QUAD
	    intsize = 'q';
#else
	    intsize = 'l';
#endif
	    /* FALL THROUGH */
	case 'd':
	case 'i':
	    if (vectorize) {
		STRLEN ulen;
		if (!veclen)
		    continue;
		if (vec_utf8)
		    uv = utf8n_to_uvchr(vecstr, veclen, &ulen,
					UTF8_ALLOW_ANYUV);
		else {
		    uv = *vecstr;
		    ulen = 1;
		}
		vecstr += ulen;
		veclen -= ulen;
		if (plus)
		     esignbuf[esignlen++] = plus;
	    }
	    else if (args) {
		switch (intsize) {
		case 'h':	iv = (short)va_arg(*args, int); break;
		default:	iv = va_arg(*args, int); break;
		case 'l':	iv = va_arg(*args, long); break;
		case 'V':	iv = va_arg(*args, IV); break;
#ifdef HAS_QUAD
		case 'q':	iv = va_arg(*args, Quad_t); break;
#endif
		}
	    }
	    else {
		iv = SvIVx(argsv);
		switch (intsize) {
		case 'h':	iv = (short)iv; break;
		default:	break;
		case 'l':	iv = (long)iv; break;
		case 'V':	break;
#ifdef HAS_QUAD
		case 'q':	iv = (Quad_t)iv; break;
#endif
		}
	    }
	    if ( !vectorize )	/* we already set uv above */
	    {
		if (iv >= 0) {
		    uv = iv;
		    if (plus)
			esignbuf[esignlen++] = plus;
		}
		else {
		    uv = -iv;
		    esignbuf[esignlen++] = '-';
		}
	    }
	    base = 10;
	    goto integer;

	case 'U':
#ifdef IV_IS_QUAD
	    intsize = 'q';
#else
	    intsize = 'l';
#endif
	    /* FALL THROUGH */
	case 'u':
	    base = 10;
	    goto uns_integer;

	case 'b':
	    base = 2;
	    goto uns_integer;

	case 'O':
#ifdef IV_IS_QUAD
	    intsize = 'q';
#else
	    intsize = 'l';
#endif
	    /* FALL THROUGH */
	case 'o':
	    base = 8;
	    goto uns_integer;

	case 'X':
	case 'x':
	    base = 16;

	uns_integer:
	    if (vectorize) {
		STRLEN ulen;
	vector:
		if (!veclen)
		    continue;
		if (vec_utf8)
		    uv = utf8n_to_uvchr(vecstr, veclen, &ulen,
					UTF8_ALLOW_ANYUV);
		else {
		    uv = *vecstr;
		    ulen = 1;
		}
		vecstr += ulen;
		veclen -= ulen;
	    }
	    else if (args) {
		switch (intsize) {
		case 'h':  uv = (unsigned short)va_arg(*args, unsigned); break;
		default:   uv = va_arg(*args, unsigned); break;
		case 'l':  uv = va_arg(*args, unsigned long); break;
		case 'V':  uv = va_arg(*args, UV); break;
#ifdef HAS_QUAD
		case 'q':  uv = va_arg(*args, Quad_t); break;
#endif
		}
	    }
	    else {
		uv = SvUVx(argsv);
		switch (intsize) {
		case 'h':	uv = (unsigned short)uv; break;
		default:	break;
		case 'l':	uv = (unsigned long)uv; break;
		case 'V':	break;
#ifdef HAS_QUAD
		case 'q':	uv = (Quad_t)uv; break;
#endif
		}
	    }

	integer:
	    eptr = ebuf + sizeof ebuf;
	    switch (base) {
		unsigned dig;
	    case 16:
		if (!uv)
		    alt = FALSE;
		p = (char*)((c == 'X')
			    ? "0123456789ABCDEF" : "0123456789abcdef");
		do {
		    dig = uv & 15;
		    *--eptr = p[dig];
		} while (uv >>= 4);
		if (alt) {
		    esignbuf[esignlen++] = '0';
		    esignbuf[esignlen++] = c;  /* 'x' or 'X' */
		}
		break;
	    case 8:
		do {
		    dig = uv & 7;
		    *--eptr = '0' + dig;
		} while (uv >>= 3);
		if (alt && *eptr != '0')
		    *--eptr = '0';
		break;
	    case 2:
		do {
		    dig = uv & 1;
		    *--eptr = '0' + dig;
		} while (uv >>= 1);
		if (alt) {
		    esignbuf[esignlen++] = '0';
		    esignbuf[esignlen++] = 'b';
		}
		break;
	    default:		/* it had better be ten or less */
#if defined(PERL_Y2KWARN)
		if (ckWARN(WARN_Y2K)) {
		    STRLEN n;
		    char *s = SvPV(sv,n);
		    if (n >= 2 && s[n-2] == '1' && s[n-1] == '9'
			&& (n == 2 || !isDIGIT(s[n-3])))
		    {
			Perl_warner(aTHX_ packWARN(WARN_Y2K),
				    "Possible Y2K bug: %%%c %s",
				    c, "format string following '19'");
		    }
		}
#endif
		do {
		    dig = uv % base;
		    *--eptr = '0' + dig;
		} while (uv /= base);
		break;
	    }
	    elen = (ebuf + sizeof ebuf) - eptr;
	    if (has_precis) {
		if (precis > elen)
		    zeros = precis - elen;
		else if (precis == 0 && elen == 1 && *eptr == '0')
		    elen = 0;
	    }
	    break;

	    /* FLOATING POINT */

	case 'F':
	    c = 'f';		/* maybe %F isn't supported here */
	    /* FALL THROUGH */
	case 'e': case 'E':
	case 'f':
	case 'g': case 'G':

	    /* This is evil, but floating point is even more evil */

	    /* for SV-style calling, we can only get NV
	       for C-style calling, we assume %f is double;
	       for simplicity we allow any of %Lf, %llf, %qf for long double
	    */
	    switch (intsize) {
	    case 'V':
#if defined(USE_LONG_DOUBLE)
		intsize = 'q';
#endif
		break;
/* [perl #20339] - we should accept and ignore %lf rather than die */
	    case 'l':
		/* FALL THROUGH */
	    default:
#if defined(USE_LONG_DOUBLE)
		intsize = args ? 0 : 'q';
#endif
		break;
	    case 'q':
#if defined(HAS_LONG_DOUBLE)
		break;
#else
		/* FALL THROUGH */
#endif
	    case 'h':
		goto unknown;
	    }

	    /* now we need (long double) if intsize == 'q', else (double) */
	    nv = (args && !vectorize) ?
#if LONG_DOUBLESIZE > DOUBLESIZE
		intsize == 'q' ?
		    va_arg(*args, long double) :
		    va_arg(*args, double)
#else
		    va_arg(*args, double)
#endif
		: SvNVx(argsv);

	    need = 0;
	    vectorize = FALSE;
	    if (c != 'e' && c != 'E') {
		i = PERL_INT_MIN;
		/* FIXME: if HAS_LONG_DOUBLE but not USE_LONG_DOUBLE this
		   will cast our (long double) to (double) */
		(void)Perl_frexp(nv, &i);
		if (i == PERL_INT_MIN)
		    Perl_die(aTHX_ "panic: frexp");
		if (i > 0)
		    need = BIT_DIGITS(i);
	    }
	    need += has_precis ? precis : 6; /* known default */

	    if (need < width)
		need = width;

#ifdef HAS_LDBL_SPRINTF_BUG
	    /* This is to try to fix a bug with irix/nonstop-ux/powerux and
	       with sfio - Allen <allens@cpan.org> */

#  ifdef DBL_MAX
#    define MY_DBL_MAX DBL_MAX
#  else /* XXX guessing! HUGE_VAL may be defined as infinity, so not using */
#    if DOUBLESIZE >= 8
#      define MY_DBL_MAX 1.7976931348623157E+308L
#    else
#      define MY_DBL_MAX 3.40282347E+38L
#    endif
#  endif

#  ifdef HAS_LDBL_SPRINTF_BUG_LESS1 /* only between -1L & 1L - Allen */
#    define MY_DBL_MAX_BUG 1L
#  else
#    define MY_DBL_MAX_BUG MY_DBL_MAX
#  endif

#  ifdef DBL_MIN
#    define MY_DBL_MIN DBL_MIN
#  else  /* XXX guessing! -Allen */
#    if DOUBLESIZE >= 8
#      define MY_DBL_MIN 2.2250738585072014E-308L
#    else
#      define MY_DBL_MIN 1.17549435E-38L
#    endif
#  endif

	    if ((intsize == 'q') && (c == 'f') &&
		((nv < MY_DBL_MAX_BUG) && (nv > -MY_DBL_MAX_BUG)) &&
		(need < DBL_DIG)) {
		/* it's going to be short enough that
		 * long double precision is not needed */

		if ((nv <= 0L) && (nv >= -0L))
		    fix_ldbl_sprintf_bug = TRUE; /* 0 is 0 - easiest */
		else {
		    /* would use Perl_fp_class as a double-check but not
		     * functional on IRIX - see perl.h comments */

		    if ((nv >= MY_DBL_MIN) || (nv <= -MY_DBL_MIN)) {
			/* It's within the range that a double can represent */
#if defined(DBL_MAX) && !defined(DBL_MIN)
			if ((nv >= ((long double)1/DBL_MAX)) ||
			    (nv <= (-(long double)1/DBL_MAX)))
#endif
			fix_ldbl_sprintf_bug = TRUE;
		    }
		}
		if (fix_ldbl_sprintf_bug == TRUE) {
		    double temp;

		    intsize = 0;
		    temp = (double)nv;
		    nv = (NV)temp;
		}
	    }

#  undef MY_DBL_MAX
#  undef MY_DBL_MAX_BUG
#  undef MY_DBL_MIN

#endif /* HAS_LDBL_SPRINTF_BUG */

	    need += 20; /* fudge factor */
	    if (PL_efloatsize < need) {
		Safefree(PL_efloatbuf);
		PL_efloatsize = need + 20; /* more fudge */
		New(906, PL_efloatbuf, PL_efloatsize, char);
		PL_efloatbuf[0] = '\0';
	    }

	    eptr = ebuf + sizeof ebuf;
	    *--eptr = '\0';
	    *--eptr = c;
	    /* FIXME: what to do if HAS_LONG_DOUBLE but not PERL_PRIfldbl? */
#if defined(HAS_LONG_DOUBLE) && defined(PERL_PRIfldbl)
	    if (intsize == 'q') {
		/* Copy the one or more characters in a long double
		 * format before the 'base' ([efgEFG]) character to
		 * the format string. */
		static char const prifldbl[] = PERL_PRIfldbl;
		char const *p = prifldbl + sizeof(prifldbl) - 3;
		while (p >= prifldbl) { *--eptr = *p--; }
	    }
#endif
	    if (has_precis) {
		base = precis;
		do { *--eptr = '0' + (base % 10); } while (base /= 10);
		*--eptr = '.';
	    }
	    if (width) {
		base = width;
		do { *--eptr = '0' + (base % 10); } while (base /= 10);
	    }
	    if (fill == '0')
		*--eptr = fill;
	    if (left)
		*--eptr = '-';
	    if (plus)
		*--eptr = plus;
	    if (alt)
		*--eptr = '#';
	    *--eptr = '%';

	    /* No taint.  Otherwise we are in the strange situation
	     * where printf() taints but print($float) doesn't.
	     * --jhi */
#if defined(HAS_LONG_DOUBLE)
	    if (intsize == 'q')
		(void)sprintf(PL_efloatbuf, eptr, nv);
	    else
		(void)sprintf(PL_efloatbuf, eptr, (double)nv);
#else
	    (void)sprintf(PL_efloatbuf, eptr, nv);
#endif
	    eptr = PL_efloatbuf;
	    elen = strlen(PL_efloatbuf);
	    break;

	    /* SPECIAL */

	case 'n':
	    i = SvCUR(sv) - origlen;
	    if (args && !vectorize) {
		switch (intsize) {
		case 'h':	*(va_arg(*args, short*)) = i; break;
		default:	*(va_arg(*args, int*)) = i; break;
		case 'l':	*(va_arg(*args, long*)) = i; break;
		case 'V':	*(va_arg(*args, IV*)) = i; break;
#ifdef HAS_QUAD
		case 'q':	*(va_arg(*args, Quad_t*)) = i; break;
#endif
		}
	    }
	    else
		sv_setuv_mg(argsv, (UV)i);
	    vectorize = FALSE;
	    continue;	/* not "break" */

	    /* UNKNOWN */

	default:
      unknown:
	    if (!args && ckWARN(WARN_PRINTF) &&
		  (PL_op->op_type == OP_PRTF || PL_op->op_type == OP_SPRINTF)) {
		SV *msg = sv_newmortal();
		Perl_sv_setpvf(aTHX_ msg, "Invalid conversion in %sprintf: ",
			  (PL_op->op_type == OP_PRTF) ? "" : "s");
		if (c) {
		    if (isPRINT(c))
			Perl_sv_catpvf(aTHX_ msg,
				       "\"%%%c\"", c & 0xFF);
		    else
			Perl_sv_catpvf(aTHX_ msg,
				       "\"%%\\%03"UVof"\"",
				       (UV)c & 0xFF);
		} else
		    sv_catpv(msg, "end of string");
		Perl_warner(aTHX_ packWARN(WARN_PRINTF), "%"SVf, msg); /* yes, this is reentrant */
	    }

	    /* output mangled stuff ... */
	    if (c == '\0')
		--q;
	    eptr = p;
	    elen = q - p;

	    /* ... right here, because formatting flags should not apply */
	    SvGROW(sv, SvCUR(sv) + elen + 1);
	    p = SvEND(sv);
	    Copy(eptr, p, elen, char);
	    p += elen;
	    *p = '\0';
	    SvCUR(sv) = p - SvPVX(sv);
	    svix = osvix;
	    continue;	/* not "break" */
	}

	if (is_utf8 != has_utf8) {
	     if (is_utf8) {
		  if (SvCUR(sv))
		       sv_utf8_upgrade(sv);
	     }
	     else {
		  SV *nsv = sv_2mortal(newSVpvn(eptr, elen));
		  sv_utf8_upgrade(nsv);
		  eptr = SvPVX(nsv);
		  elen = SvCUR(nsv);
	     }
	     SvGROW(sv, SvCUR(sv) + elen + 1);
	     p = SvEND(sv);
	     *p = '\0';
	}
	/* Use memchr() instead of strchr(), as eptr is not guaranteed */
	/* to point to a null-terminated string.                       */
	if (left && ckWARN(WARN_PRINTF) && memchr(eptr, '\n', elen) && 
	    (PL_op->op_type == OP_PRTF || PL_op->op_type == OP_SPRINTF)) 
	    Perl_warner(aTHX_ packWARN(WARN_PRINTF),
		"Newline in left-justified string for %sprintf",
			(PL_op->op_type == OP_PRTF) ? "" : "s");
	
	have = esignlen + zeros + elen;
	need = (have > width ? have : width);
	gap = need - have;

	SvGROW(sv, SvCUR(sv) + need + dotstrlen + 1);
	p = SvEND(sv);
	if (esignlen && fill == '0') {
	    for (i = 0; i < (int)esignlen; i++)
		*p++ = esignbuf[i];
	}
	if (gap && !left) {
	    memset(p, fill, gap);
	    p += gap;
	}
	if (esignlen && fill != '0') {
	    for (i = 0; i < (int)esignlen; i++)
		*p++ = esignbuf[i];
	}
	if (zeros) {
	    for (i = zeros; i; i--)
		*p++ = '0';
	}
	if (elen) {
	    Copy(eptr, p, elen, char);
	    p += elen;
	}
	if (gap && left) {
	    memset(p, ' ', gap);
	    p += gap;
	}
	if (vectorize) {
	    if (veclen) {
		Copy(dotstr, p, dotstrlen, char);
		p += dotstrlen;
	    }
	    else
		vectorize = FALSE;		/* done iterating over vecstr */
	}
	if (is_utf8)
	    has_utf8 = TRUE;
	if (has_utf8)
	    SvUTF8_on(sv);
	*p = '\0';
	SvCUR(sv) = p - SvPVX(sv);
	if (vectorize) {
	    esignlen = 0;
	    goto vector;
	}
    }
}

/* =========================================================================

=head1 Cloning an interpreter

All the macros and functions in this section are for the private use of
the main function, perl_clone().

The foo_dup() functions make an exact copy of an existing foo thinngy.
During the course of a cloning, a hash table is used to map old addresses
to new addresses. The table is created and manipulated with the
ptr_table_* functions.

=cut

============================================================================*/


#if defined(USE_ITHREADS)

#if defined(USE_5005THREADS)
#  include "error: USE_5005THREADS and USE_ITHREADS are incompatible"
#endif

#ifndef GpREFCNT_inc
#  define GpREFCNT_inc(gp)	((gp) ? (++(gp)->gp_refcnt, (gp)) : (GP*)NULL)
#endif


#define sv_dup_inc(s,t)	SvREFCNT_inc(sv_dup(s,t))
#define av_dup(s,t)	(AV*)sv_dup((SV*)s,t)
#define av_dup_inc(s,t)	(AV*)SvREFCNT_inc(sv_dup((SV*)s,t))
#define hv_dup(s,t)	(HV*)sv_dup((SV*)s,t)
#define hv_dup_inc(s,t)	(HV*)SvREFCNT_inc(sv_dup((SV*)s,t))
#define cv_dup(s,t)	(CV*)sv_dup((SV*)s,t)
#define cv_dup_inc(s,t)	(CV*)SvREFCNT_inc(sv_dup((SV*)s,t))
#define io_dup(s,t)	(IO*)sv_dup((SV*)s,t)
#define io_dup_inc(s,t)	(IO*)SvREFCNT_inc(sv_dup((SV*)s,t))
#define gv_dup(s,t)	(GV*)sv_dup((SV*)s,t)
#define gv_dup_inc(s,t)	(GV*)SvREFCNT_inc(sv_dup((SV*)s,t))
#define SAVEPV(p)	(p ? savepv(p) : Nullch)
#define SAVEPVN(p,n)	(p ? savepvn(p,n) : Nullch)


/* Duplicate a regexp. Required reading: pregcomp() and pregfree() in
   regcomp.c. AMS 20010712 */

REGEXP *
Perl_re_dup(pTHX_ REGEXP *r, CLONE_PARAMS *param)
{
    REGEXP *ret;
    int i, len, npar;
    struct reg_substr_datum *s;

    if (!r)
	return (REGEXP *)NULL;

    if ((ret = (REGEXP *)ptr_table_fetch(PL_ptr_table, r)))
	return ret;

    len = r->offsets[0];
    npar = r->nparens+1;

    Newc(0, ret, sizeof(regexp) + (len+1)*sizeof(regnode), char, regexp);
    Copy(r->program, ret->program, len+1, regnode);

    New(0, ret->startp, npar, I32);
    Copy(r->startp, ret->startp, npar, I32);
    New(0, ret->endp, npar, I32);
    Copy(r->startp, ret->startp, npar, I32);

    New(0, ret->substrs, 1, struct reg_substr_data);
    for (s = ret->substrs->data, i = 0; i < 3; i++, s++) {
	s->min_offset = r->substrs->data[i].min_offset;
	s->max_offset = r->substrs->data[i].max_offset;
	s->substr     = sv_dup_inc(r->substrs->data[i].substr, param);
	s->utf8_substr = sv_dup_inc(r->substrs->data[i].utf8_substr, param);
    }

    ret->regstclass = NULL;
    if (r->data) {
	struct reg_data *d;
	int count = r->data->count;

	Newc(0, d, sizeof(struct reg_data) + count*sizeof(void *),
		char, struct reg_data);
	New(0, d->what, count, U8);

	d->count = count;
	for (i = 0; i < count; i++) {
	    d->what[i] = r->data->what[i];
	    switch (d->what[i]) {
	    case 's':
		d->data[i] = sv_dup_inc((SV *)r->data->data[i], param);
		break;
	    case 'p':
		d->data[i] = av_dup_inc((AV *)r->data->data[i], param);
		break;
	    case 'f':
		/* This is cheating. */
		New(0, d->data[i], 1, struct regnode_charclass_class);
		StructCopy(r->data->data[i], d->data[i],
			    struct regnode_charclass_class);
		ret->regstclass = (regnode*)d->data[i];
		break;
	    case 'o':
		/* Compiled op trees are readonly, and can thus be
		   shared without duplication. */
		d->data[i] = (void*)OpREFCNT_inc((OP*)r->data->data[i]);
		break;
	    case 'n':
		d->data[i] = r->data->data[i];
		break;
	    }
	}

	ret->data = d;
    }
    else
	ret->data = NULL;

    New(0, ret->offsets, 2*len+1, U32);
    Copy(r->offsets, ret->offsets, 2*len+1, U32);

    ret->precomp        = SAVEPV(r->precomp);
    ret->refcnt         = r->refcnt;
    ret->minlen         = r->minlen;
    ret->prelen         = r->prelen;
    ret->nparens        = r->nparens;
    ret->lastparen      = r->lastparen;
    ret->lastcloseparen = r->lastcloseparen;
    ret->reganch        = r->reganch;

    ret->sublen         = r->sublen;

    if (RX_MATCH_COPIED(ret))
	ret->subbeg  = SAVEPV(r->subbeg);
    else
	ret->subbeg = Nullch;

    ptr_table_store(PL_ptr_table, r, ret);
    return ret;
}

/* duplicate a file handle */

PerlIO *
Perl_fp_dup(pTHX_ PerlIO *fp, char type, CLONE_PARAMS *param)
{
    PerlIO *ret;
    if (!fp)
	return (PerlIO*)NULL;

    /* look for it in the table first */
    ret = (PerlIO*)ptr_table_fetch(PL_ptr_table, fp);
    if (ret)
	return ret;

    /* create anew and remember what it is */
    ret = PerlIO_fdupopen(aTHX_ fp, param, PERLIO_DUP_CLONE);
    ptr_table_store(PL_ptr_table, fp, ret);
    return ret;
}

/* duplicate a directory handle */

DIR *
Perl_dirp_dup(pTHX_ DIR *dp)
{
    if (!dp)
	return (DIR*)NULL;
    /* XXX TODO */
    return dp;
}

/* duplicate a typeglob */

GP *
Perl_gp_dup(pTHX_ GP *gp, CLONE_PARAMS* param)
{
    GP *ret;
    if (!gp)
	return (GP*)NULL;
    /* look for it in the table first */
    ret = (GP*)ptr_table_fetch(PL_ptr_table, gp);
    if (ret)
	return ret;

    /* create anew and remember what it is */
    Newz(0, ret, 1, GP);
    ptr_table_store(PL_ptr_table, gp, ret);

    /* clone */
    ret->gp_refcnt	= 0;			/* must be before any other dups! */
    ret->gp_sv		= sv_dup_inc(gp->gp_sv, param);
    ret->gp_io		= io_dup_inc(gp->gp_io, param);
    ret->gp_form	= cv_dup_inc(gp->gp_form, param);
    ret->gp_av		= av_dup_inc(gp->gp_av, param);
    ret->gp_hv		= hv_dup_inc(gp->gp_hv, param);
    ret->gp_egv	= gv_dup(gp->gp_egv, param);/* GvEGV is not refcounted */
    ret->gp_cv		= cv_dup_inc(gp->gp_cv, param);
    ret->gp_cvgen	= gp->gp_cvgen;
    ret->gp_flags	= gp->gp_flags;
    ret->gp_line	= gp->gp_line;
    ret->gp_file	= gp->gp_file;		/* points to COP.cop_file */
    return ret;
}

/* duplicate a chain of magic */

MAGIC *
Perl_mg_dup(pTHX_ MAGIC *mg, CLONE_PARAMS* param)
{
    MAGIC *mgprev = (MAGIC*)NULL;
    MAGIC *mgret;
    if (!mg)
	return (MAGIC*)NULL;
    /* look for it in the table first */
    mgret = (MAGIC*)ptr_table_fetch(PL_ptr_table, mg);
    if (mgret)
	return mgret;

    for (; mg; mg = mg->mg_moremagic) {
	MAGIC *nmg;
	Newz(0, nmg, 1, MAGIC);
	if (mgprev)
	    mgprev->mg_moremagic = nmg;
	else
	    mgret = nmg;
	nmg->mg_virtual	= mg->mg_virtual;	/* XXX copy dynamic vtable? */
	nmg->mg_private	= mg->mg_private;
	nmg->mg_type	= mg->mg_type;
	nmg->mg_flags	= mg->mg_flags;
	if (mg->mg_type == PERL_MAGIC_qr) {
	    nmg->mg_obj	= (SV*)re_dup((REGEXP*)mg->mg_obj, param);
	}
	else if(mg->mg_type == PERL_MAGIC_backref) {
	     AV *av = (AV*) mg->mg_obj;
	     SV **svp;
	     I32 i;
	     nmg->mg_obj = (SV*)newAV();
	     svp = AvARRAY(av);
	     i = AvFILLp(av);
	     while (i >= 0) {
		  av_push((AV*)nmg->mg_obj,sv_dup(svp[i],param));
		  i--;
	     }
	}
	else {
	    nmg->mg_obj	= (mg->mg_flags & MGf_REFCOUNTED)
			      ? sv_dup_inc(mg->mg_obj, param)
			      : sv_dup(mg->mg_obj, param);
	}
	nmg->mg_len	= mg->mg_len;
	nmg->mg_ptr	= mg->mg_ptr;	/* XXX random ptr? */
	if (mg->mg_ptr && mg->mg_type != PERL_MAGIC_regex_global) {
	    if (mg->mg_len > 0) {
		nmg->mg_ptr	= SAVEPVN(mg->mg_ptr, mg->mg_len);
		if (mg->mg_type == PERL_MAGIC_overload_table &&
			AMT_AMAGIC((AMT*)mg->mg_ptr))
		{
		    AMT *amtp = (AMT*)mg->mg_ptr;
		    AMT *namtp = (AMT*)nmg->mg_ptr;
		    I32 i;
		    for (i = 1; i < NofAMmeth; i++) {
			namtp->table[i] = cv_dup_inc(amtp->table[i], param);
		    }
		}
	    }
	    else if (mg->mg_len == HEf_SVKEY)
		nmg->mg_ptr	= (char*)sv_dup_inc((SV*)mg->mg_ptr, param);
	}
	if ((mg->mg_flags & MGf_DUP) && mg->mg_virtual && mg->mg_virtual->svt_dup) {
	    CALL_FPTR(nmg->mg_virtual->svt_dup)(aTHX_ nmg, param);
	}
	mgprev = nmg;
    }
    return mgret;
}

/* create a new pointer-mapping table */

PTR_TBL_t *
Perl_ptr_table_new(pTHX)
{
    PTR_TBL_t *tbl;
    Newz(0, tbl, 1, PTR_TBL_t);
    tbl->tbl_max	= 511;
    tbl->tbl_items	= 0;
    Newz(0, tbl->tbl_ary, tbl->tbl_max + 1, PTR_TBL_ENT_t*);
    return tbl;
}

/* map an existing pointer using a table */

void *
Perl_ptr_table_fetch(pTHX_ PTR_TBL_t *tbl, void *sv)
{
    PTR_TBL_ENT_t *tblent;
    UV hash = PTR2UV(sv);
    assert(tbl);
    tblent = tbl->tbl_ary[hash & tbl->tbl_max];
    for (; tblent; tblent = tblent->next) {
	if (tblent->oldval == sv)
	    return tblent->newval;
    }
    return (void*)NULL;
}

/* add a new entry to a pointer-mapping table */

void
Perl_ptr_table_store(pTHX_ PTR_TBL_t *tbl, void *oldv, void *newv)
{
    PTR_TBL_ENT_t *tblent, **otblent;
    /* XXX this may be pessimal on platforms where pointers aren't good
     * hash values e.g. if they grow faster in the most significant
     * bits */
    UV hash = PTR2UV(oldv);
    bool i = 1;

    assert(tbl);
    otblent = &tbl->tbl_ary[hash & tbl->tbl_max];
    for (tblent = *otblent; tblent; i=0, tblent = tblent->next) {
	if (tblent->oldval == oldv) {
	    tblent->newval = newv;
	    return;
	}
    }
    Newz(0, tblent, 1, PTR_TBL_ENT_t);
    tblent->oldval = oldv;
    tblent->newval = newv;
    tblent->next = *otblent;
    *otblent = tblent;
    tbl->tbl_items++;
    if (i && tbl->tbl_items > tbl->tbl_max)
	ptr_table_split(tbl);
}

/* double the hash bucket size of an existing ptr table */

void
Perl_ptr_table_split(pTHX_ PTR_TBL_t *tbl)
{
    PTR_TBL_ENT_t **ary = tbl->tbl_ary;
    UV oldsize = tbl->tbl_max + 1;
    UV newsize = oldsize * 2;
    UV i;

    Renew(ary, newsize, PTR_TBL_ENT_t*);
    Zero(&ary[oldsize], newsize-oldsize, PTR_TBL_ENT_t*);
    tbl->tbl_max = --newsize;
    tbl->tbl_ary = ary;
    for (i=0; i < oldsize; i++, ary++) {
	PTR_TBL_ENT_t **curentp, **entp, *ent;
	if (!*ary)
	    continue;
	curentp = ary + oldsize;
	for (entp = ary, ent = *ary; ent; ent = *entp) {
	    if ((newsize & PTR2UV(ent->oldval)) != i) {
		*entp = ent->next;
		ent->next = *curentp;
		*curentp = ent;
		continue;
	    }
	    else
		entp = &ent->next;
	}
    }
}

/* remove all the entries from a ptr table */

void
Perl_ptr_table_clear(pTHX_ PTR_TBL_t *tbl)
{
    register PTR_TBL_ENT_t **array;
    register PTR_TBL_ENT_t *entry;
    register PTR_TBL_ENT_t *oentry = Null(PTR_TBL_ENT_t*);
    UV riter = 0;
    UV max;

    if (!tbl || !tbl->tbl_items) {
        return;
    }

    array = tbl->tbl_ary;
    entry = array[0];
    max = tbl->tbl_max;

    for (;;) {
        if (entry) {
            oentry = entry;
            entry = entry->next;
            Safefree(oentry);
        }
        if (!entry) {
            if (++riter > max) {
                break;
            }
            entry = array[riter];
        }
    }

    tbl->tbl_items = 0;
}

/* clear and free a ptr table */

void
Perl_ptr_table_free(pTHX_ PTR_TBL_t *tbl)
{
    if (!tbl) {
        return;
    }
    ptr_table_clear(tbl);
    Safefree(tbl->tbl_ary);
    Safefree(tbl);
}

#ifdef DEBUGGING
char *PL_watch_pvx;
#endif

/* attempt to make everything in the typeglob readonly */

STATIC SV *
S_gv_share(pTHX_ SV *sstr, CLONE_PARAMS *param)
{
    GV *gv = (GV*)sstr;
    SV *sv = &param->proto_perl->Isv_no; /* just need SvREADONLY-ness */

    if (GvIO(gv) || GvFORM(gv)) {
        GvUNIQUE_off(gv); /* GvIOs cannot be shared. nor can GvFORMs */
    }
    else if (!GvCV(gv)) {
        GvCV(gv) = (CV*)sv;
    }
    else {
        /* CvPADLISTs cannot be shared */
        if (!SvREADONLY(GvCV(gv)) && !CvXSUB(GvCV(gv))) {
            GvUNIQUE_off(gv);
        }
    }

    if (!GvUNIQUE(gv)) {
#if 0
        PerlIO_printf(Perl_debug_log, "gv_share: unable to share %s::%s\n",
                      HvNAME(GvSTASH(gv)), GvNAME(gv));
#endif
        return Nullsv;
    }

    /*
     * write attempts will die with
     * "Modification of a read-only value attempted"
     */
    if (!GvSV(gv)) {
        GvSV(gv) = sv;
    }
    else {
        SvREADONLY_on(GvSV(gv));
    }

    if (!GvAV(gv)) {
        GvAV(gv) = (AV*)sv;
    }
    else {
        SvREADONLY_on(GvAV(gv));
    }

    if (!GvHV(gv)) {
        GvHV(gv) = (HV*)sv;
    }
    else {
        SvREADONLY_on(GvAV(gv));
    }

    return sstr; /* he_dup() will SvREFCNT_inc() */
}

/* duplicate an SV of any type (including AV, HV etc) */

void
Perl_rvpv_dup(pTHX_ SV *dstr, SV *sstr, CLONE_PARAMS* param)
{
    if (SvROK(sstr)) {
        SvRV(dstr) = SvWEAKREF(sstr)
		     ? sv_dup(SvRV(sstr), param)
		     : sv_dup_inc(SvRV(sstr), param);
    }
    else if (SvPVX(sstr)) {
	/* Has something there */
	if (SvLEN(sstr)) {
	    /* Normal PV - clone whole allocated space */
	    SvPVX(dstr) = SAVEPVN(SvPVX(sstr), SvLEN(sstr)-1);
	}
	else {
	    /* Special case - not normally malloced for some reason */
	    if (SvREADONLY(sstr) && SvFAKE(sstr)) {
		/* A "shared" PV - clone it as unshared string */
                if(SvPADTMP(sstr)) {
                    /* However, some of them live in the pad
                       and they should not have these flags
                       turned off */

                    SvPVX(dstr) = sharepvn(SvPVX(sstr), SvCUR(sstr),
                                           SvUVX(sstr));
                    SvUVX(dstr) = SvUVX(sstr);
                } else {

                    SvPVX(dstr) = SAVEPVN(SvPVX(sstr), SvCUR(sstr));
                    SvFAKE_off(dstr);
                    SvREADONLY_off(dstr);
                }
	    }
	    else {
		/* Some other special case - random pointer */
		SvPVX(dstr) = SvPVX(sstr);		
            }
	}
    }
    else {
	/* Copy the Null */
	SvPVX(dstr) = SvPVX(sstr);
    }
}

SV *
Perl_sv_dup(pTHX_ SV *sstr, CLONE_PARAMS* param)
{
    SV *dstr;

    if (!sstr || SvTYPE(sstr) == SVTYPEMASK)
	return Nullsv;
    /* look for it in the table first */
    dstr = (SV*)ptr_table_fetch(PL_ptr_table, sstr);
    if (dstr)
	return dstr;

    if(param->flags & CLONEf_JOIN_IN) {
        /** We are joining here so we don't want do clone
	    something that is bad **/

        if(SvTYPE(sstr) == SVt_PVHV &&
	   HvNAME(sstr)) {
	    /** don't clone stashes if they already exist **/
	    HV* old_stash = gv_stashpv(HvNAME(sstr),0);
	    return (SV*) old_stash;
        }
    }

    /* create anew and remember what it is */
    new_SV(dstr);
    ptr_table_store(PL_ptr_table, sstr, dstr);

    /* clone */
    SvFLAGS(dstr)	= SvFLAGS(sstr);
    SvFLAGS(dstr)	&= ~SVf_OOK;		/* don't propagate OOK hack */
    SvREFCNT(dstr)	= 0;			/* must be before any other dups! */

#ifdef DEBUGGING
    if (SvANY(sstr) && PL_watch_pvx && SvPVX(sstr) == PL_watch_pvx)
	PerlIO_printf(Perl_debug_log, "watch at %p hit, found string \"%s\"\n",
		      PL_watch_pvx, SvPVX(sstr));
#endif

    switch (SvTYPE(sstr)) {
    case SVt_NULL:
	SvANY(dstr)	= NULL;
	break;
    case SVt_IV:
	SvANY(dstr)	= new_XIV();
	SvIVX(dstr)	= SvIVX(sstr);
	break;
    case SVt_NV:
	SvANY(dstr)	= new_XNV();
	SvNVX(dstr)	= SvNVX(sstr);
	break;
    case SVt_RV:
	SvANY(dstr)	= new_XRV();
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	break;
    case SVt_PV:
	SvANY(dstr)	= new_XPV();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	break;
    case SVt_PVIV:
	SvANY(dstr)	= new_XPVIV();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	break;
    case SVt_PVNV:
	SvANY(dstr)	= new_XPVNV();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	break;
    case SVt_PVMG:
	SvANY(dstr)	= new_XPVMG();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	break;
    case SVt_PVBM:
	SvANY(dstr)	= new_XPVBM();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	BmRARE(dstr)	= BmRARE(sstr);
	BmUSEFUL(dstr)	= BmUSEFUL(sstr);
	BmPREVIOUS(dstr)= BmPREVIOUS(sstr);
	break;
    case SVt_PVLV:
	SvANY(dstr)	= new_XPVLV();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	LvTARGOFF(dstr)	= LvTARGOFF(sstr);	/* XXX sometimes holds PMOP* when DEBUGGING */
	LvTARGLEN(dstr)	= LvTARGLEN(sstr);
	if (LvTYPE(sstr) == 't') /* for tie: unrefcnted fake (SV**) */
	    LvTARG(dstr) = dstr;
	else if (LvTYPE(sstr) == 'T') /* for tie: fake HE */
	    LvTARG(dstr) = (SV*)he_dup((HE*)LvTARG(sstr), 0, param);
	else
	    LvTARG(dstr) = sv_dup_inc(LvTARG(sstr), param);
	LvTYPE(dstr)	= LvTYPE(sstr);
	break;
    case SVt_PVGV:
	if (GvUNIQUE((GV*)sstr)) {
            SV *share;
            if ((share = gv_share(sstr, param))) {
                del_SV(dstr);
                dstr = share;
                ptr_table_store(PL_ptr_table, sstr, dstr);
#if 0
                PerlIO_printf(Perl_debug_log, "sv_dup: sharing %s::%s\n",
                              HvNAME(GvSTASH(share)), GvNAME(share));
#endif
                break;
            }
	}
	SvANY(dstr)	= new_XPVGV();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	GvNAMELEN(dstr)	= GvNAMELEN(sstr);
	GvNAME(dstr)	= SAVEPVN(GvNAME(sstr), GvNAMELEN(sstr));
    	GvSTASH(dstr)	= hv_dup_inc(GvSTASH(sstr), param);
	GvFLAGS(dstr)	= GvFLAGS(sstr);
	GvGP(dstr)	= gp_dup(GvGP(sstr), param);
	(void)GpREFCNT_inc(GvGP(dstr));
	break;
    case SVt_PVIO:
	SvANY(dstr)	= new_XPVIO();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	IoIFP(dstr)	= fp_dup(IoIFP(sstr), IoTYPE(sstr), param);
	if (IoOFP(sstr) == IoIFP(sstr))
	    IoOFP(dstr) = IoIFP(dstr);
	else
	    IoOFP(dstr)	= fp_dup(IoOFP(sstr), IoTYPE(sstr), param);
	/* PL_rsfp_filters entries have fake IoDIRP() */
	if (IoDIRP(sstr) && !(IoFLAGS(sstr) & IOf_FAKE_DIRP))
	    IoDIRP(dstr)	= dirp_dup(IoDIRP(sstr));
	else
	    IoDIRP(dstr)	= IoDIRP(sstr);
	IoLINES(dstr)		= IoLINES(sstr);
	IoPAGE(dstr)		= IoPAGE(sstr);
	IoPAGE_LEN(dstr)	= IoPAGE_LEN(sstr);
	IoLINES_LEFT(dstr)	= IoLINES_LEFT(sstr);
        if(IoFLAGS(sstr) & IOf_FAKE_DIRP) { 
            /* I have no idea why fake dirp (rsfps)
               should be treaded differently but otherwise
               we end up with leaks -- sky*/
            IoTOP_GV(dstr)      = gv_dup_inc(IoTOP_GV(sstr), param);
            IoFMT_GV(dstr)      = gv_dup_inc(IoFMT_GV(sstr), param);
            IoBOTTOM_GV(dstr)   = gv_dup_inc(IoBOTTOM_GV(sstr), param);
        } else {
            IoTOP_GV(dstr)      = gv_dup(IoTOP_GV(sstr), param);
            IoFMT_GV(dstr)      = gv_dup(IoFMT_GV(sstr), param);
            IoBOTTOM_GV(dstr)   = gv_dup(IoBOTTOM_GV(sstr), param);
        }
	IoTOP_NAME(dstr)	= SAVEPV(IoTOP_NAME(sstr));
	IoFMT_NAME(dstr)	= SAVEPV(IoFMT_NAME(sstr));
	IoBOTTOM_NAME(dstr)	= SAVEPV(IoBOTTOM_NAME(sstr));
	IoSUBPROCESS(dstr)	= IoSUBPROCESS(sstr);
	IoTYPE(dstr)		= IoTYPE(sstr);
	IoFLAGS(dstr)		= IoFLAGS(sstr);
	break;
    case SVt_PVAV:
	SvANY(dstr)	= new_XPVAV();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	AvARYLEN((AV*)dstr) = sv_dup_inc(AvARYLEN((AV*)sstr), param);
	AvFLAGS((AV*)dstr) = AvFLAGS((AV*)sstr);
	if (AvARRAY((AV*)sstr)) {
	    SV **dst_ary, **src_ary;
	    SSize_t items = AvFILLp((AV*)sstr) + 1;

	    src_ary = AvARRAY((AV*)sstr);
	    Newz(0, dst_ary, AvMAX((AV*)sstr)+1, SV*);
	    ptr_table_store(PL_ptr_table, src_ary, dst_ary);
	    SvPVX(dstr)	= (char*)dst_ary;
	    AvALLOC((AV*)dstr) = dst_ary;
	    if (AvREAL((AV*)sstr)) {
		while (items-- > 0)
		    *dst_ary++ = sv_dup_inc(*src_ary++, param);
	    }
	    else {
		while (items-- > 0)
		    *dst_ary++ = sv_dup(*src_ary++, param);
	    }
	    items = AvMAX((AV*)sstr) - AvFILLp((AV*)sstr);
	    while (items-- > 0) {
		*dst_ary++ = &PL_sv_undef;
	    }
	}
	else {
	    SvPVX(dstr)		= Nullch;
	    AvALLOC((AV*)dstr)	= (SV**)NULL;
	}
	break;
    case SVt_PVHV:
	SvANY(dstr)	= new_XPVHV();
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	HvRITER((HV*)dstr)	= HvRITER((HV*)sstr);
	if (HvARRAY((HV*)sstr)) {
	    STRLEN i = 0;
	    XPVHV *dxhv = (XPVHV*)SvANY(dstr);
	    XPVHV *sxhv = (XPVHV*)SvANY(sstr);
	    Newz(0, dxhv->xhv_array,
		 PERL_HV_ARRAY_ALLOC_BYTES(dxhv->xhv_max+1), char);
	    while (i <= sxhv->xhv_max) {
		((HE**)dxhv->xhv_array)[i] = he_dup(((HE**)sxhv->xhv_array)[i],
						    (bool)!!HvSHAREKEYS(sstr),
						    param);
		++i;
	    }
	    dxhv->xhv_eiter = he_dup(sxhv->xhv_eiter,
				     (bool)!!HvSHAREKEYS(sstr), param);
	}
	else {
	    SvPVX(dstr)		= Nullch;
	    HvEITER((HV*)dstr)	= (HE*)NULL;
	}
	HvPMROOT((HV*)dstr)	= HvPMROOT((HV*)sstr);		/* XXX */
	HvNAME((HV*)dstr)	= SAVEPV(HvNAME((HV*)sstr));
    /* Record stashes for possible cloning in Perl_clone(). */
	if(HvNAME((HV*)dstr))
	    av_push(param->stashes, dstr);
	break;
    case SVt_PVFM:
	SvANY(dstr)	= new_XPVFM();
	FmLINES(dstr)	= FmLINES(sstr);
	goto dup_pvcv;
	/* NOTREACHED */
    case SVt_PVCV:
	SvANY(dstr)	= new_XPVCV();
        dup_pvcv:
	SvCUR(dstr)	= SvCUR(sstr);
	SvLEN(dstr)	= SvLEN(sstr);
	SvIVX(dstr)	= SvIVX(sstr);
	SvNVX(dstr)	= SvNVX(sstr);
	SvMAGIC(dstr)	= mg_dup(SvMAGIC(sstr), param);
	SvSTASH(dstr)	= hv_dup_inc(SvSTASH(sstr), param);
	Perl_rvpv_dup(aTHX_ dstr, sstr, param);
	CvSTASH(dstr)	= hv_dup(CvSTASH(sstr), param); /* NOTE: not refcounted */
	CvSTART(dstr)	= CvSTART(sstr);
	CvROOT(dstr)	= OpREFCNT_inc(CvROOT(sstr));
	CvXSUB(dstr)	= CvXSUB(sstr);
	CvXSUBANY(dstr)	= CvXSUBANY(sstr);
	if (CvCONST(sstr)) {
	    CvXSUBANY(dstr).any_ptr = GvUNIQUE(CvGV(sstr)) ?
                SvREFCNT_inc(CvXSUBANY(sstr).any_ptr) :
                sv_dup_inc(CvXSUBANY(sstr).any_ptr, param);
	}
	CvGV(dstr)	= gv_dup(CvGV(sstr), param);
	if (param->flags & CLONEf_COPY_STACKS) {
	  CvDEPTH(dstr)	= CvDEPTH(sstr);
	} else {
	  CvDEPTH(dstr) = 0;
	}
	PAD_DUP(CvPADLIST(dstr), CvPADLIST(sstr), param);
	CvOUTSIDE_SEQ(dstr) = CvOUTSIDE_SEQ(sstr);
	CvOUTSIDE(dstr)	=
		CvWEAKOUTSIDE(sstr)
			? cv_dup(    CvOUTSIDE(sstr), param)
			: cv_dup_inc(CvOUTSIDE(sstr), param);
	CvFLAGS(dstr)	= CvFLAGS(sstr);
	CvFILE(dstr) = CvXSUB(sstr) ? CvFILE(sstr) : SAVEPV(CvFILE(sstr));
	break;
    default:
	Perl_croak(aTHX_ "Bizarre SvTYPE [%" IVdf "]", (IV)SvTYPE(sstr));
	break;
    }

    if (SvOBJECT(dstr) && SvTYPE(dstr) != SVt_PVIO)
	++PL_sv_objcount;

    return dstr;
 }

/* duplicate a context */

PERL_CONTEXT *
Perl_cx_dup(pTHX_ PERL_CONTEXT *cxs, I32 ix, I32 max, CLONE_PARAMS* param)
{
    PERL_CONTEXT *ncxs;

    if (!cxs)
	return (PERL_CONTEXT*)NULL;

    /* look for it in the table first */
    ncxs = (PERL_CONTEXT*)ptr_table_fetch(PL_ptr_table, cxs);
    if (ncxs)
	return ncxs;

    /* create anew and remember what it is */
    Newz(56, ncxs, max + 1, PERL_CONTEXT);
    ptr_table_store(PL_ptr_table, cxs, ncxs);

    while (ix >= 0) {
	PERL_CONTEXT *cx = &cxs[ix];
	PERL_CONTEXT *ncx = &ncxs[ix];
	ncx->cx_type	= cx->cx_type;
	if (CxTYPE(cx) == CXt_SUBST) {
	    Perl_croak(aTHX_ "Cloning substitution context is unimplemented");
	}
	else {
	    ncx->blk_oldsp	= cx->blk_oldsp;
	    ncx->blk_oldcop	= cx->blk_oldcop;
	    ncx->blk_oldretsp	= cx->blk_oldretsp;
	    ncx->blk_oldmarksp	= cx->blk_oldmarksp;
	    ncx->blk_oldscopesp	= cx->blk_oldscopesp;
	    ncx->blk_oldpm	= cx->blk_oldpm;
	    ncx->blk_gimme	= cx->blk_gimme;
	    switch (CxTYPE(cx)) {
	    case CXt_SUB:
		ncx->blk_sub.cv		= (cx->blk_sub.olddepth == 0
					   ? cv_dup_inc(cx->blk_sub.cv, param)
					   : cv_dup(cx->blk_sub.cv,param));
		ncx->blk_sub.argarray	= (cx->blk_sub.hasargs
					   ? av_dup_inc(cx->blk_sub.argarray, param)
					   : Nullav);
		ncx->blk_sub.savearray	= av_dup_inc(cx->blk_sub.savearray, param);
		ncx->blk_sub.olddepth	= cx->blk_sub.olddepth;
		ncx->blk_sub.hasargs	= cx->blk_sub.hasargs;
		ncx->blk_sub.lval	= cx->blk_sub.lval;
		break;
	    case CXt_EVAL:
		ncx->blk_eval.old_in_eval = cx->blk_eval.old_in_eval;
		ncx->blk_eval.old_op_type = cx->blk_eval.old_op_type;
		ncx->blk_eval.old_namesv = sv_dup_inc(cx->blk_eval.old_namesv, param);
		ncx->blk_eval.old_eval_root = cx->blk_eval.old_eval_root;
		ncx->blk_eval.cur_text	= sv_dup(cx->blk_eval.cur_text, param);
		break;
	    case CXt_LOOP:
		ncx->blk_loop.label	= cx->blk_loop.label;
		ncx->blk_loop.resetsp	= cx->blk_loop.resetsp;
		ncx->blk_loop.redo_op	= cx->blk_loop.redo_op;
		ncx->blk_loop.next_op	= cx->blk_loop.next_op;
		ncx->blk_loop.last_op	= cx->blk_loop.last_op;
		ncx->blk_loop.iterdata	= (CxPADLOOP(cx)
					   ? cx->blk_loop.iterdata
					   : gv_dup((GV*)cx->blk_loop.iterdata, param));
		ncx->blk_loop.oldcomppad
		    = (PAD*)ptr_table_fetch(PL_ptr_table,
					    cx->blk_loop.oldcomppad);
		ncx->blk_loop.itersave	= sv_dup_inc(cx->blk_loop.itersave, param);
		ncx->blk_loop.iterlval	= sv_dup_inc(cx->blk_loop.iterlval, param);
		ncx->blk_loop.iterary	= av_dup_inc(cx->blk_loop.iterary, param);
		ncx->blk_loop.iterix	= cx->blk_loop.iterix;
		ncx->blk_loop.itermax	= cx->blk_loop.itermax;
		break;
	    case CXt_FORMAT:
		ncx->blk_sub.cv		= cv_dup(cx->blk_sub.cv, param);
		ncx->blk_sub.gv		= gv_dup(cx->blk_sub.gv, param);
		ncx->blk_sub.dfoutgv	= gv_dup_inc(cx->blk_sub.dfoutgv, param);
		ncx->blk_sub.hasargs	= cx->blk_sub.hasargs;
		break;
	    case CXt_BLOCK:
	    case CXt_NULL:
		break;
	    }
	}
	--ix;
    }
    return ncxs;
}

/* duplicate a stack info structure */

PERL_SI *
Perl_si_dup(pTHX_ PERL_SI *si, CLONE_PARAMS* param)
{
    PERL_SI *nsi;

    if (!si)
	return (PERL_SI*)NULL;

    /* look for it in the table first */
    nsi = (PERL_SI*)ptr_table_fetch(PL_ptr_table, si);
    if (nsi)
	return nsi;

    /* create anew and remember what it is */
    Newz(56, nsi, 1, PERL_SI);
    ptr_table_store(PL_ptr_table, si, nsi);

    nsi->si_stack	= av_dup_inc(si->si_stack, param);
    nsi->si_cxix	= si->si_cxix;
    nsi->si_cxmax	= si->si_cxmax;
    nsi->si_cxstack	= cx_dup(si->si_cxstack, si->si_cxix, si->si_cxmax, param);
    nsi->si_type	= si->si_type;
    nsi->si_prev	= si_dup(si->si_prev, param);
    nsi->si_next	= si_dup(si->si_next, param);
    nsi->si_markoff	= si->si_markoff;

    return nsi;
}

#define POPINT(ss,ix)	((ss)[--(ix)].any_i32)
#define TOPINT(ss,ix)	((ss)[ix].any_i32)
#define POPLONG(ss,ix)	((ss)[--(ix)].any_long)
#define TOPLONG(ss,ix)	((ss)[ix].any_long)
#define POPIV(ss,ix)	((ss)[--(ix)].any_iv)
#define TOPIV(ss,ix)	((ss)[ix].any_iv)
#define POPBOOL(ss,ix)	((ss)[--(ix)].any_bool)
#define TOPBOOL(ss,ix)	((ss)[ix].any_bool)
#define POPPTR(ss,ix)	((ss)[--(ix)].any_ptr)
#define TOPPTR(ss,ix)	((ss)[ix].any_ptr)
#define POPDPTR(ss,ix)	((ss)[--(ix)].any_dptr)
#define TOPDPTR(ss,ix)	((ss)[ix].any_dptr)
#define POPDXPTR(ss,ix)	((ss)[--(ix)].any_dxptr)
#define TOPDXPTR(ss,ix)	((ss)[ix].any_dxptr)

/* XXXXX todo */
#define pv_dup_inc(p)	SAVEPV(p)
#define pv_dup(p)	SAVEPV(p)
#define svp_dup_inc(p,pp)	any_dup(p,pp)

/* map any object to the new equivent - either something in the
 * ptr table, or something in the interpreter structure
 */

void *
Perl_any_dup(pTHX_ void *v, PerlInterpreter *proto_perl)
{
    void *ret;

    if (!v)
	return (void*)NULL;

    /* look for it in the table first */
    ret = ptr_table_fetch(PL_ptr_table, v);
    if (ret)
	return ret;

    /* see if it is part of the interpreter structure */
    if (v >= (void*)proto_perl && v < (void*)(proto_perl+1))
	ret = (void*)(((char*)aTHX) + (((char*)v) - (char*)proto_perl));
    else {
	ret = v;
    }

    return ret;
}

/* duplicate the save stack */

ANY *
Perl_ss_dup(pTHX_ PerlInterpreter *proto_perl, CLONE_PARAMS* param)
{
    ANY *ss	= proto_perl->Tsavestack;
    I32 ix	= proto_perl->Tsavestack_ix;
    I32 max	= proto_perl->Tsavestack_max;
    ANY *nss;
    SV *sv;
    GV *gv;
    AV *av;
    HV *hv;
    void* ptr;
    int intval;
    long longval;
    GP *gp;
    IV iv;
    I32 i;
    char *c = NULL;
    void (*dptr) (void*);
    void (*dxptr) (pTHX_ void*);
    OP *o;

    Newz(54, nss, max, ANY);

    while (ix > 0) {
	i = POPINT(ss,ix);
	TOPINT(nss,ix) = i;
	switch (i) {
	case SAVEt_ITEM:			/* normal string */
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    break;
        case SAVEt_SV:				/* scalar reference */
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    gv = (GV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = gv_dup_inc(gv, param);
	    break;
	case SAVEt_GENERIC_PVREF:		/* generic char* */
	    c = (char*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = pv_dup(c);
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    break;
	case SAVEt_SHARED_PVREF:		/* char* in shared space */
	    c = (char*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = savesharedpv(c);
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    break;
        case SAVEt_GENERIC_SVREF:		/* generic sv */
        case SAVEt_SVREF:			/* scalar reference */
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = svp_dup_inc((SV**)ptr, proto_perl);/* XXXXX */
	    break;
        case SAVEt_AV:				/* array reference */
	    av = (AV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = av_dup_inc(av, param);
	    gv = (GV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = gv_dup(gv, param);
	    break;
        case SAVEt_HV:				/* hash reference */
	    hv = (HV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = hv_dup_inc(hv, param);
	    gv = (GV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = gv_dup(gv, param);
	    break;
	case SAVEt_INT:				/* int reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    intval = (int)POPINT(ss,ix);
	    TOPINT(nss,ix) = intval;
	    break;
	case SAVEt_LONG:			/* long reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    longval = (long)POPLONG(ss,ix);
	    TOPLONG(nss,ix) = longval;
	    break;
	case SAVEt_I32:				/* I32 reference */
	case SAVEt_I16:				/* I16 reference */
	case SAVEt_I8:				/* I8 reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    i = POPINT(ss,ix);
	    TOPINT(nss,ix) = i;
	    break;
	case SAVEt_IV:				/* IV reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    iv = POPIV(ss,ix);
	    TOPIV(nss,ix) = iv;
	    break;
	case SAVEt_SPTR:			/* SV* reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup(sv, param);
	    break;
	case SAVEt_VPTR:			/* random* reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    break;
	case SAVEt_PPTR:			/* char* reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    c = (char*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = pv_dup(c);
	    break;
	case SAVEt_HPTR:			/* HV* reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    hv = (HV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = hv_dup(hv, param);
	    break;
	case SAVEt_APTR:			/* AV* reference */
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    av = (AV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = av_dup(av, param);
	    break;
	case SAVEt_NSTAB:
	    gv = (GV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = gv_dup(gv, param);
	    break;
	case SAVEt_GP:				/* scalar reference */
	    gp = (GP*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = gp = gp_dup(gp, param);
	    (void)GpREFCNT_inc(gp);
	    gv = (GV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = gv_dup_inc(gv, param);
            c = (char*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = pv_dup(c);
	    iv = POPIV(ss,ix);
	    TOPIV(nss,ix) = iv;
	    iv = POPIV(ss,ix);
	    TOPIV(nss,ix) = iv;
            break;
	case SAVEt_FREESV:
	case SAVEt_MORTALIZESV:
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    break;
	case SAVEt_FREEOP:
	    ptr = POPPTR(ss,ix);
	    if (ptr && (((OP*)ptr)->op_private & OPpREFCOUNTED)) {
		/* these are assumed to be refcounted properly */
		switch (((OP*)ptr)->op_type) {
		case OP_LEAVESUB:
		case OP_LEAVESUBLV:
		case OP_LEAVEEVAL:
		case OP_LEAVE:
		case OP_SCOPE:
		case OP_LEAVEWRITE:
		    TOPPTR(nss,ix) = ptr;
		    o = (OP*)ptr;
		    OpREFCNT_inc(o);
		    break;
		default:
		    TOPPTR(nss,ix) = Nullop;
		    break;
		}
	    }
	    else
		TOPPTR(nss,ix) = Nullop;
	    break;
	case SAVEt_FREEPV:
	    c = (char*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = pv_dup_inc(c);
	    break;
	case SAVEt_CLEARSV:
	    longval = POPLONG(ss,ix);
	    TOPLONG(nss,ix) = longval;
	    break;
	case SAVEt_DELETE:
	    hv = (HV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = hv_dup_inc(hv, param);
	    c = (char*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = pv_dup_inc(c);
	    i = POPINT(ss,ix);
	    TOPINT(nss,ix) = i;
	    break;
	case SAVEt_DESTRUCTOR:
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);	/* XXX quite arbitrary */
	    dptr = POPDPTR(ss,ix);
	    TOPDPTR(nss,ix) = (void (*)(void*))any_dup((void *)dptr, proto_perl);
	    break;
	case SAVEt_DESTRUCTOR_X:
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);	/* XXX quite arbitrary */
	    dxptr = POPDXPTR(ss,ix);
	    TOPDXPTR(nss,ix) = (void (*)(pTHX_ void*))any_dup((void *)dxptr, proto_perl);
	    break;
	case SAVEt_REGCONTEXT:
	case SAVEt_ALLOC:
	    i = POPINT(ss,ix);
	    TOPINT(nss,ix) = i;
	    ix -= i;
	    break;
	case SAVEt_STACK_POS:		/* Position on Perl stack */
	    i = POPINT(ss,ix);
	    TOPINT(nss,ix) = i;
	    break;
	case SAVEt_AELEM:		/* array element */
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    i = POPINT(ss,ix);
	    TOPINT(nss,ix) = i;
	    av = (AV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = av_dup_inc(av, param);
	    break;
	case SAVEt_HELEM:		/* hash element */
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup_inc(sv, param);
	    hv = (HV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = hv_dup_inc(hv, param);
	    break;
	case SAVEt_OP:
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = ptr;
	    break;
	case SAVEt_HINTS:
	    i = POPINT(ss,ix);
	    TOPINT(nss,ix) = i;
	    break;
	case SAVEt_COMPPAD:
	    av = (AV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = av_dup(av, param);
	    break;
	case SAVEt_PADSV:
	    longval = (long)POPLONG(ss,ix);
	    TOPLONG(nss,ix) = longval;
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    sv = (SV*)POPPTR(ss,ix);
	    TOPPTR(nss,ix) = sv_dup(sv, param);
	    break;
	case SAVEt_BOOL:
	    ptr = POPPTR(ss,ix);
	    TOPPTR(nss,ix) = any_dup(ptr, proto_perl);
	    longval = (long)POPBOOL(ss,ix);
	    TOPBOOL(nss,ix) = (bool)longval;
	    break;
	default:
	    Perl_croak(aTHX_ "panic: ss_dup inconsistency");
	}
    }

    return nss;
}

/*
=for apidoc perl_clone

Create and return a new interpreter by cloning the current one.

perl_clone takes these flags as parameters:

CLONEf_COPY_STACKS - is used to, well, copy the stacks also, 
without it we only clone the data and zero the stacks, 
with it we copy the stacks and the new perl interpreter is 
ready to run at the exact same point as the previous one. 
The pseudo-fork code uses COPY_STACKS while the 
threads->new doesn't.

CLONEf_KEEP_PTR_TABLE
perl_clone keeps a ptr_table with the pointer of the old 
variable as a key and the new variable as a value, 
this allows it to check if something has been cloned and not 
clone it again but rather just use the value and increase the 
refcount. If KEEP_PTR_TABLE is not set then perl_clone will kill 
the ptr_table using the function 
C<ptr_table_free(PL_ptr_table); PL_ptr_table = NULL;>, 
reason to keep it around is if you want to dup some of your own 
variable who are outside the graph perl scans, example of this 
code is in threads.xs create

CLONEf_CLONE_HOST
This is a win32 thing, it is ignored on unix, it tells perls 
win32host code (which is c++) to clone itself, this is needed on 
win32 if you want to run two threads at the same time, 
if you just want to do some stuff in a separate perl interpreter 
and then throw it away and return to the original one, 
you don't need to do anything.

=cut
*/

/* XXX the above needs expanding by someone who actually understands it ! */
EXTERN_C PerlInterpreter *
perl_clone_host(PerlInterpreter* proto_perl, UV flags);

PerlInterpreter *
perl_clone(PerlInterpreter *proto_perl, UV flags)
{
#ifdef PERL_IMPLICIT_SYS

   /* perlhost.h so we need to call into it
   to clone the host, CPerlHost should have a c interface, sky */

   if (flags & CLONEf_CLONE_HOST) {
       return perl_clone_host(proto_perl,flags);
   }
   return perl_clone_using(proto_perl, flags,
			    proto_perl->IMem,
			    proto_perl->IMemShared,
			    proto_perl->IMemParse,
			    proto_perl->IEnv,
			    proto_perl->IStdIO,
			    proto_perl->ILIO,
			    proto_perl->IDir,
			    proto_perl->ISock,
			    proto_perl->IProc);
}

PerlInterpreter *
perl_clone_using(PerlInterpreter *proto_perl, UV flags,
		 struct IPerlMem* ipM, struct IPerlMem* ipMS,
		 struct IPerlMem* ipMP, struct IPerlEnv* ipE,
		 struct IPerlStdIO* ipStd, struct IPerlLIO* ipLIO,
		 struct IPerlDir* ipD, struct IPerlSock* ipS,
		 struct IPerlProc* ipP)
{
    /* XXX many of the string copies here can be optimized if they're
     * constants; they need to be allocated as common memory and just
     * their pointers copied. */

    IV i;
    CLONE_PARAMS clone_params;
    CLONE_PARAMS* param = &clone_params;

    PerlInterpreter *my_perl = (PerlInterpreter*)(*ipM->pMalloc)(ipM, sizeof(PerlInterpreter));
    PERL_SET_THX(my_perl);

#  ifdef DEBUGGING
    Poison(my_perl, 1, PerlInterpreter);
    PL_markstack = 0;
    PL_scopestack = 0;
    PL_savestack = 0;
    PL_savestack_ix = 0;
    PL_savestack_max = -1;
    PL_retstack = 0;
    PL_sig_pending = 0;
    Zero(&PL_debug_pad, 1, struct perl_debug_pad);
#  else	/* !DEBUGGING */
    Zero(my_perl, 1, PerlInterpreter);
#  endif	/* DEBUGGING */

    /* host pointers */
    PL_Mem		= ipM;
    PL_MemShared	= ipMS;
    PL_MemParse		= ipMP;
    PL_Env		= ipE;
    PL_StdIO		= ipStd;
    PL_LIO		= ipLIO;
    PL_Dir		= ipD;
    PL_Sock		= ipS;
    PL_Proc		= ipP;
#else		/* !PERL_IMPLICIT_SYS */
    IV i;
    CLONE_PARAMS clone_params;
    CLONE_PARAMS* param = &clone_params;
    PerlInterpreter *my_perl = (PerlInterpreter*)PerlMem_malloc(sizeof(PerlInterpreter));
    PERL_SET_THX(my_perl);



#    ifdef DEBUGGING
    Poison(my_perl, 1, PerlInterpreter);
    PL_markstack = 0;
    PL_scopestack = 0;
    PL_savestack = 0;
    PL_savestack_ix = 0;
    PL_savestack_max = -1;
    PL_retstack = 0;
    PL_sig_pending = 0;
    Zero(&PL_debug_pad, 1, struct perl_debug_pad);
#    else	/* !DEBUGGING */
    Zero(my_perl, 1, PerlInterpreter);
#    endif	/* DEBUGGING */
#endif		/* PERL_IMPLICIT_SYS */
    param->flags = flags;
    param->proto_perl = proto_perl;

    /* arena roots */
    PL_xiv_arenaroot	= NULL;
    PL_xiv_root		= NULL;
    PL_xnv_arenaroot	= NULL;
    PL_xnv_root		= NULL;
    PL_xrv_arenaroot	= NULL;
    PL_xrv_root		= NULL;
    PL_xpv_arenaroot	= NULL;
    PL_xpv_root		= NULL;
    PL_xpviv_arenaroot	= NULL;
    PL_xpviv_root	= NULL;
    PL_xpvnv_arenaroot	= NULL;
    PL_xpvnv_root	= NULL;
    PL_xpvcv_arenaroot	= NULL;
    PL_xpvcv_root	= NULL;
    PL_xpvav_arenaroot	= NULL;
    PL_xpvav_root	= NULL;
    PL_xpvhv_arenaroot	= NULL;
    PL_xpvhv_root	= NULL;
    PL_xpvmg_arenaroot	= NULL;
    PL_xpvmg_root	= NULL;
    PL_xpvlv_arenaroot	= NULL;
    PL_xpvlv_root	= NULL;
    PL_xpvbm_arenaroot	= NULL;
    PL_xpvbm_root	= NULL;
    PL_he_arenaroot	= NULL;
    PL_he_root		= NULL;
    PL_nice_chunk	= NULL;
    PL_nice_chunk_size	= 0;
    PL_sv_count		= 0;
    PL_sv_objcount	= 0;
    PL_sv_root		= Nullsv;
    PL_sv_arenaroot	= Nullsv;

    PL_debug		= proto_perl->Idebug;

#ifdef USE_REENTRANT_API
    Perl_reentrant_init(aTHX);
#endif

    /* create SV map for pointer relocation */
    PL_ptr_table = ptr_table_new();

    /* initialize these special pointers as early as possible */
    SvANY(&PL_sv_undef)		= NULL;
    SvREFCNT(&PL_sv_undef)	= (~(U32)0)/2;
    SvFLAGS(&PL_sv_undef)	= SVf_READONLY|SVt_NULL;
    ptr_table_store(PL_ptr_table, &proto_perl->Isv_undef, &PL_sv_undef);

    SvANY(&PL_sv_no)		= new_XPVNV();
    SvREFCNT(&PL_sv_no)		= (~(U32)0)/2;
    SvFLAGS(&PL_sv_no)		= SVp_NOK|SVf_NOK|SVp_POK|SVf_POK|SVf_READONLY|SVt_PVNV;
    SvPVX(&PL_sv_no)		= SAVEPVN(PL_No, 0);
    SvCUR(&PL_sv_no)		= 0;
    SvLEN(&PL_sv_no)		= 1;
    SvNVX(&PL_sv_no)		= 0;
    ptr_table_store(PL_ptr_table, &proto_perl->Isv_no, &PL_sv_no);

    SvANY(&PL_sv_yes)		= new_XPVNV();
    SvREFCNT(&PL_sv_yes)	= (~(U32)0)/2;
    SvFLAGS(&PL_sv_yes)		= SVp_NOK|SVf_NOK|SVp_POK|SVf_POK|SVf_READONLY|SVt_PVNV;
    SvPVX(&PL_sv_yes)		= SAVEPVN(PL_Yes, 1);
    SvCUR(&PL_sv_yes)		= 1;
    SvLEN(&PL_sv_yes)		= 2;
    SvNVX(&PL_sv_yes)		= 1;
    ptr_table_store(PL_ptr_table, &proto_perl->Isv_yes, &PL_sv_yes);

    /* create (a non-shared!) shared string table */
    PL_strtab		= newHV();
    HvSHAREKEYS_off(PL_strtab);
    hv_ksplit(PL_strtab, 512);
    ptr_table_store(PL_ptr_table, proto_perl->Istrtab, PL_strtab);

    PL_compiling = proto_perl->Icompiling;

    /* These two PVs will be free'd special way so must set them same way op.c does */
    PL_compiling.cop_stashpv = savesharedpv(PL_compiling.cop_stashpv);
    ptr_table_store(PL_ptr_table, proto_perl->Icompiling.cop_stashpv, PL_compiling.cop_stashpv);

    PL_compiling.cop_file    = savesharedpv(PL_compiling.cop_file);
    ptr_table_store(PL_ptr_table, proto_perl->Icompiling.cop_file, PL_compiling.cop_file);

    ptr_table_store(PL_ptr_table, &proto_perl->Icompiling, &PL_compiling);
    if (!specialWARN(PL_compiling.cop_warnings))
	PL_compiling.cop_warnings = sv_dup_inc(PL_compiling.cop_warnings, param);
    if (!specialCopIO(PL_compiling.cop_io))
	PL_compiling.cop_io = sv_dup_inc(PL_compiling.cop_io, param);
    PL_curcop		= (COP*)any_dup(proto_perl->Tcurcop, proto_perl);

    /* pseudo environmental stuff */
    PL_origargc		= proto_perl->Iorigargc;
    PL_origargv		= proto_perl->Iorigargv;

    param->stashes      = newAV();  /* Setup array of objects to call clone on */

#ifdef PERLIO_LAYERS
    /* Clone PerlIO tables as soon as we can handle general xx_dup() */
    PerlIO_clone(aTHX_ proto_perl, param);
#endif

    PL_envgv		= gv_dup(proto_perl->Ienvgv, param);
    PL_incgv		= gv_dup(proto_perl->Iincgv, param);
    PL_hintgv		= gv_dup(proto_perl->Ihintgv, param);
    PL_origfilename	= SAVEPV(proto_perl->Iorigfilename);
    PL_diehook		= sv_dup_inc(proto_perl->Idiehook, param);
    PL_warnhook		= sv_dup_inc(proto_perl->Iwarnhook, param);

    /* switches */
    PL_minus_c		= proto_perl->Iminus_c;
    PL_patchlevel	= sv_dup_inc(proto_perl->Ipatchlevel, param);
    PL_localpatches	= proto_perl->Ilocalpatches;
    PL_splitstr		= proto_perl->Isplitstr;
    PL_preprocess	= proto_perl->Ipreprocess;
    PL_minus_n		= proto_perl->Iminus_n;
    PL_minus_p		= proto_perl->Iminus_p;
    PL_minus_l		= proto_perl->Iminus_l;
    PL_minus_a		= proto_perl->Iminus_a;
    PL_minus_F		= proto_perl->Iminus_F;
    PL_doswitches	= proto_perl->Idoswitches;
    PL_dowarn		= proto_perl->Idowarn;
    PL_doextract	= proto_perl->Idoextract;
    PL_sawampersand	= proto_perl->Isawampersand;
    PL_unsafe		= proto_perl->Iunsafe;
    PL_inplace		= SAVEPV(proto_perl->Iinplace);
    PL_e_script		= sv_dup_inc(proto_perl->Ie_script, param);
    PL_perldb		= proto_perl->Iperldb;
    PL_perl_destruct_level = proto_perl->Iperl_destruct_level;
    PL_exit_flags       = proto_perl->Iexit_flags;

    /* magical thingies */
    /* XXX time(&PL_basetime) when asked for? */
    PL_basetime		= proto_perl->Ibasetime;
    PL_formfeed		= sv_dup(proto_perl->Iformfeed, param);

    PL_maxsysfd		= proto_perl->Imaxsysfd;
    PL_multiline	= proto_perl->Imultiline;
    PL_statusvalue	= proto_perl->Istatusvalue;
#ifdef VMS
    PL_statusvalue_vms	= proto_perl->Istatusvalue_vms;
#endif
    PL_encoding		= sv_dup(proto_perl->Iencoding, param);

    sv_setpvn(PERL_DEBUG_PAD(0), "", 0);	/* For regex debugging. */
    sv_setpvn(PERL_DEBUG_PAD(1), "", 0);	/* ext/re needs these */
    sv_setpvn(PERL_DEBUG_PAD(2), "", 0);	/* even without DEBUGGING. */

    /* Clone the regex array */
    PL_regex_padav = newAV();
    {
	I32 len = av_len((AV*)proto_perl->Iregex_padav);
	SV** regexen = AvARRAY((AV*)proto_perl->Iregex_padav);
	av_push(PL_regex_padav,
		sv_dup_inc(regexen[0],param));
	for(i = 1; i <= len; i++) {
            if(SvREPADTMP(regexen[i])) {
	      av_push(PL_regex_padav, sv_dup_inc(regexen[i], param));
            } else {
	        av_push(PL_regex_padav,
                    SvREFCNT_inc(
                        newSViv(PTR2IV(re_dup(INT2PTR(REGEXP *,
                             SvIVX(regexen[i])), param)))
                       ));
	    }
	}
    }
    PL_regex_pad = AvARRAY(PL_regex_padav);

    /* shortcuts to various I/O objects */
    PL_stdingv		= gv_dup(proto_perl->Istdingv, param);
    PL_stderrgv		= gv_dup(proto_perl->Istderrgv, param);
    PL_defgv		= gv_dup(proto_perl->Idefgv, param);
    PL_argvgv		= gv_dup(proto_perl->Iargvgv, param);
    PL_argvoutgv	= gv_dup(proto_perl->Iargvoutgv, param);
    PL_argvout_stack	= av_dup_inc(proto_perl->Iargvout_stack, param);

    /* shortcuts to regexp stuff */
    PL_replgv		= gv_dup(proto_perl->Ireplgv, param);

    /* shortcuts to misc objects */
    PL_errgv		= gv_dup(proto_perl->Ierrgv, param);

    /* shortcuts to debugging objects */
    PL_DBgv		= gv_dup(proto_perl->IDBgv, param);
    PL_DBline		= gv_dup(proto_perl->IDBline, param);
    PL_DBsub		= gv_dup(proto_perl->IDBsub, param);
    PL_DBsingle		= sv_dup(proto_perl->IDBsingle, param);
    PL_DBtrace		= sv_dup(proto_perl->IDBtrace, param);
    PL_DBsignal		= sv_dup(proto_perl->IDBsignal, param);
    PL_lineary		= av_dup(proto_perl->Ilineary, param);
    PL_dbargs		= av_dup(proto_perl->Idbargs, param);

    /* symbol tables */
    PL_defstash		= hv_dup_inc(proto_perl->Tdefstash, param);
    PL_curstash		= hv_dup(proto_perl->Tcurstash, param);
    PL_nullstash       = hv_dup(proto_perl->Inullstash, param);
    PL_debstash		= hv_dup(proto_perl->Idebstash, param);
    PL_globalstash	= hv_dup(proto_perl->Iglobalstash, param);
    PL_curstname	= sv_dup_inc(proto_perl->Icurstname, param);

    PL_beginav		= av_dup_inc(proto_perl->Ibeginav, param);
    PL_beginav_save	= av_dup_inc(proto_perl->Ibeginav_save, param);
    PL_checkav_save	= av_dup_inc(proto_perl->Icheckav_save, param);
    PL_endav		= av_dup_inc(proto_perl->Iendav, param);
    PL_checkav		= av_dup_inc(proto_perl->Icheckav, param);
    PL_initav		= av_dup_inc(proto_perl->Iinitav, param);

    PL_sub_generation	= proto_perl->Isub_generation;

    /* funky return mechanisms */
    PL_forkprocess	= proto_perl->Iforkprocess;

    /* subprocess state */
    PL_fdpid		= av_dup_inc(proto_perl->Ifdpid, param);

    /* internal state */
    PL_tainting		= proto_perl->Itainting;
    PL_taint_warn       = proto_perl->Itaint_warn;
    PL_maxo		= proto_perl->Imaxo;
    if (proto_perl->Iop_mask)
	PL_op_mask	= SAVEPVN(proto_perl->Iop_mask, PL_maxo);
    else
	PL_op_mask 	= Nullch;

    /* current interpreter roots */
    PL_main_cv		= cv_dup_inc(proto_perl->Imain_cv, param);
    PL_main_root	= OpREFCNT_inc(proto_perl->Imain_root);
    PL_main_start	= proto_perl->Imain_start;
    PL_eval_root	= proto_perl->Ieval_root;
    PL_eval_start	= proto_perl->Ieval_start;

    /* runtime control stuff */
    PL_curcopdb		= (COP*)any_dup(proto_perl->Icurcopdb, proto_perl);
    PL_copline		= proto_perl->Icopline;

    PL_filemode		= proto_perl->Ifilemode;
    PL_lastfd		= proto_perl->Ilastfd;
    PL_oldname		= proto_perl->Ioldname;		/* XXX not quite right */
    PL_Argv		= NULL;
    PL_Cmd		= Nullch;
    PL_gensym		= proto_perl->Igensym;
    PL_preambled	= proto_perl->Ipreambled;
    PL_preambleav	= av_dup_inc(proto_perl->Ipreambleav, param);
    PL_laststatval	= proto_perl->Ilaststatval;
    PL_laststype	= proto_perl->Ilaststype;
    PL_mess_sv		= Nullsv;

    PL_ors_sv		= sv_dup_inc(proto_perl->Iors_sv, param);
    PL_ofmt		= SAVEPV(proto_perl->Iofmt);

    /* interpreter atexit processing */
    PL_exitlistlen	= proto_perl->Iexitlistlen;
    if (PL_exitlistlen) {
	New(0, PL_exitlist, PL_exitlistlen, PerlExitListEntry);
	Copy(proto_perl->Iexitlist, PL_exitlist, PL_exitlistlen, PerlExitListEntry);
    }
    else
	PL_exitlist	= (PerlExitListEntry*)NULL;
    PL_modglobal	= hv_dup_inc(proto_perl->Imodglobal, param);
    PL_custom_op_names  = hv_dup_inc(proto_perl->Icustom_op_names,param);
    PL_custom_op_descs  = hv_dup_inc(proto_perl->Icustom_op_descs,param);

    PL_profiledata	= NULL;
    PL_rsfp		= fp_dup(proto_perl->Irsfp, '<', param);
    /* PL_rsfp_filters entries have fake IoDIRP() */
    PL_rsfp_filters	= av_dup_inc(proto_perl->Irsfp_filters, param);

    PL_compcv			= cv_dup(proto_perl->Icompcv, param);

    PAD_CLONE_VARS(proto_perl, param);

#ifdef HAVE_INTERP_INTERN
    sys_intern_dup(&proto_perl->Isys_intern, &PL_sys_intern);
#endif

    /* more statics moved here */
    PL_generation	= proto_perl->Igeneration;
    PL_DBcv		= cv_dup(proto_perl->IDBcv, param);

    PL_in_clean_objs	= proto_perl->Iin_clean_objs;
    PL_in_clean_all	= proto_perl->Iin_clean_all;

    PL_uid		= proto_perl->Iuid;
    PL_euid		= proto_perl->Ieuid;
    PL_gid		= proto_perl->Igid;
    PL_egid		= proto_perl->Iegid;
    PL_nomemok		= proto_perl->Inomemok;
    PL_an		= proto_perl->Ian;
    PL_op_seqmax	= proto_perl->Iop_seqmax;
    PL_evalseq		= proto_perl->Ievalseq;
    PL_origenviron	= proto_perl->Iorigenviron;	/* XXX not quite right */
    PL_origalen		= proto_perl->Iorigalen;
    PL_pidstatus	= newHV();			/* XXX flag for cloning? */
    PL_osname		= SAVEPV(proto_perl->Iosname);
    PL_sh_path_compat	= proto_perl->Ish_path_compat; /* XXX never deallocated */
    PL_sighandlerp	= proto_perl->Isighandlerp;


    PL_runops		= proto_perl->Irunops;

    Copy(proto_perl->Itokenbuf, PL_tokenbuf, 256, char);

#ifdef CSH
    PL_cshlen		= proto_perl->Icshlen;
    PL_cshname		= proto_perl->Icshname; /* XXX never deallocated */
#endif

    PL_lex_state	= proto_perl->Ilex_state;
    PL_lex_defer	= proto_perl->Ilex_defer;
    PL_lex_expect	= proto_perl->Ilex_expect;
    PL_lex_formbrack	= proto_perl->Ilex_formbrack;
    PL_lex_dojoin	= proto_perl->Ilex_dojoin;
    PL_lex_starts	= proto_perl->Ilex_starts;
    PL_lex_stuff	= sv_dup_inc(proto_perl->Ilex_stuff, param);
    PL_lex_repl		= sv_dup_inc(proto_perl->Ilex_repl, param);
    PL_lex_op		= proto_perl->Ilex_op;
    PL_lex_inpat	= proto_perl->Ilex_inpat;
    PL_lex_inwhat	= proto_perl->Ilex_inwhat;
    PL_lex_brackets	= proto_perl->Ilex_brackets;
    i = (PL_lex_brackets < 120 ? 120 : PL_lex_brackets);
    PL_lex_brackstack	= SAVEPVN(proto_perl->Ilex_brackstack,i);
    PL_lex_casemods	= proto_perl->Ilex_casemods;
    i = (PL_lex_casemods < 12 ? 12 : PL_lex_casemods);
    PL_lex_casestack	= SAVEPVN(proto_perl->Ilex_casestack,i);

    Copy(proto_perl->Inextval, PL_nextval, 5, YYSTYPE);
    Copy(proto_perl->Inexttype, PL_nexttype, 5,	I32);
    PL_nexttoke		= proto_perl->Inexttoke;

    /* XXX This is probably masking the deeper issue of why
     * SvANY(proto_perl->Ilinestr) can be NULL at this point. For test case:
     * http://archive.develooper.com/perl5-porters%40perl.org/msg83298.html
     * (A little debugging with a watchpoint on it may help.)
     */
    if (SvANY(proto_perl->Ilinestr)) {
	PL_linestr		= sv_dup_inc(proto_perl->Ilinestr, param);
	i = proto_perl->Ibufptr - SvPVX(proto_perl->Ilinestr);
	PL_bufptr		= SvPVX(PL_linestr) + (i < 0 ? 0 : i);
	i = proto_perl->Ioldbufptr - SvPVX(proto_perl->Ilinestr);
	PL_oldbufptr	= SvPVX(PL_linestr) + (i < 0 ? 0 : i);
	i = proto_perl->Ioldoldbufptr - SvPVX(proto_perl->Ilinestr);
	PL_oldoldbufptr	= SvPVX(PL_linestr) + (i < 0 ? 0 : i);
	i = proto_perl->Ilinestart - SvPVX(proto_perl->Ilinestr);
	PL_linestart	= SvPVX(PL_linestr) + (i < 0 ? 0 : i);
    }
    else {
        PL_linestr = NEWSV(65,79);
        sv_upgrade(PL_linestr,SVt_PVIV);
        sv_setpvn(PL_linestr,"",0);
	PL_bufptr = PL_oldbufptr = PL_oldoldbufptr = PL_linestart = SvPVX(PL_linestr);
    }
    PL_bufend		= SvPVX(PL_linestr) + SvCUR(PL_linestr);
    PL_pending_ident	= proto_perl->Ipending_ident;
    PL_sublex_info	= proto_perl->Isublex_info;	/* XXX not quite right */

    PL_expect		= proto_perl->Iexpect;

    PL_multi_start	= proto_perl->Imulti_start;
    PL_multi_end	= proto_perl->Imulti_end;
    PL_multi_open	= proto_perl->Imulti_open;
    PL_multi_close	= proto_perl->Imulti_close;

    PL_error_count	= proto_perl->Ierror_count;
    PL_subline		= proto_perl->Isubline;
    PL_subname		= sv_dup_inc(proto_perl->Isubname, param);

    /* XXX See comment on SvANY(proto_perl->Ilinestr) above */
    if (SvANY(proto_perl->Ilinestr)) {
	i = proto_perl->Ilast_uni - SvPVX(proto_perl->Ilinestr);
	PL_last_uni		= SvPVX(PL_linestr) + (i < 0 ? 0 : i);
	i = proto_perl->Ilast_lop - SvPVX(proto_perl->Ilinestr);
	PL_last_lop		= SvPVX(PL_linestr) + (i < 0 ? 0 : i);
	PL_last_lop_op	= proto_perl->Ilast_lop_op;
    }
    else {
	PL_last_uni	= SvPVX(PL_linestr);
	PL_last_lop	= SvPVX(PL_linestr);
	PL_last_lop_op	= 0;
    }
    PL_in_my		= proto_perl->Iin_my;
    PL_in_my_stash	= hv_dup(proto_perl->Iin_my_stash, param);
#ifdef FCRYPT
    PL_cryptseen	= proto_perl->Icryptseen;
#endif

    PL_hints		= proto_perl->Ihints;

    PL_amagic_generation	= proto_perl->Iamagic_generation;

#ifdef USE_LOCALE_COLLATE
    PL_collation_ix	= proto_perl->Icollation_ix;
    PL_collation_name	= SAVEPV(proto_perl->Icollation_name);
    PL_collation_standard	= proto_perl->Icollation_standard;
    PL_collxfrm_base	= proto_perl->Icollxfrm_base;
    PL_collxfrm_mult	= proto_perl->Icollxfrm_mult;
#endif /* USE_LOCALE_COLLATE */

#ifdef USE_LOCALE_NUMERIC
    PL_numeric_name	= SAVEPV(proto_perl->Inumeric_name);
    PL_numeric_standard	= proto_perl->Inumeric_standard;
    PL_numeric_local	= proto_perl->Inumeric_local;
    PL_numeric_radix_sv	= sv_dup_inc(proto_perl->Inumeric_radix_sv, param);
#endif /* !USE_LOCALE_NUMERIC */

    /* utf8 character classes */
    PL_utf8_alnum	= sv_dup_inc(proto_perl->Iutf8_alnum, param);
    PL_utf8_alnumc	= sv_dup_inc(proto_perl->Iutf8_alnumc, param);
    PL_utf8_ascii	= sv_dup_inc(proto_perl->Iutf8_ascii, param);
    PL_utf8_alpha	= sv_dup_inc(proto_perl->Iutf8_alpha, param);
    PL_utf8_space	= sv_dup_inc(proto_perl->Iutf8_space, param);
    PL_utf8_cntrl	= sv_dup_inc(proto_perl->Iutf8_cntrl, param);
    PL_utf8_graph	= sv_dup_inc(proto_perl->Iutf8_graph, param);
    PL_utf8_digit	= sv_dup_inc(proto_perl->Iutf8_digit, param);
    PL_utf8_upper	= sv_dup_inc(proto_perl->Iutf8_upper, param);
    PL_utf8_lower	= sv_dup_inc(proto_perl->Iutf8_lower, param);
    PL_utf8_print	= sv_dup_inc(proto_perl->Iutf8_print, param);
    PL_utf8_punct	= sv_dup_inc(proto_perl->Iutf8_punct, param);
    PL_utf8_xdigit	= sv_dup_inc(proto_perl->Iutf8_xdigit, param);
    PL_utf8_mark	= sv_dup_inc(proto_perl->Iutf8_mark, param);
    PL_utf8_toupper	= sv_dup_inc(proto_perl->Iutf8_toupper, param);
    PL_utf8_totitle	= sv_dup_inc(proto_perl->Iutf8_totitle, param);
    PL_utf8_tolower	= sv_dup_inc(proto_perl->Iutf8_tolower, param);
    PL_utf8_tofold	= sv_dup_inc(proto_perl->Iutf8_tofold, param);
    PL_utf8_idstart	= sv_dup_inc(proto_perl->Iutf8_idstart, param);
    PL_utf8_idcont	= sv_dup_inc(proto_perl->Iutf8_idcont, param);

    /* Did the locale setup indicate UTF-8? */
    PL_utf8locale	= proto_perl->Iutf8locale;
    /* Unicode features (see perlrun/-C) */
    PL_unicode		= proto_perl->Iunicode;

    /* Pre-5.8 signals control */
    PL_signals		= proto_perl->Isignals;

    /* times() ticks per second */
    PL_clocktick	= proto_perl->Iclocktick;

    /* Recursion stopper for PerlIO_find_layer */
    PL_in_load_module	= proto_perl->Iin_load_module;

    /* sort() routine */
    PL_sort_RealCmp	= proto_perl->Isort_RealCmp;

    /* Not really needed/useful since the reenrant_retint is "volatile",
     * but do it for consistency's sake. */
    PL_reentrant_retint	= proto_perl->Ireentrant_retint;

    /* Hooks to shared SVs and locks. */
    PL_sharehook	= proto_perl->Isharehook;
    PL_lockhook		= proto_perl->Ilockhook;
    PL_unlockhook	= proto_perl->Iunlockhook;
    PL_threadhook	= proto_perl->Ithreadhook;

    PL_runops_std	= proto_perl->Irunops_std;
    PL_runops_dbg	= proto_perl->Irunops_dbg;

#ifdef THREADS_HAVE_PIDS
    PL_ppid		= proto_perl->Ippid;
#endif

    /* swatch cache */
    PL_last_swash_hv	= Nullhv;	/* reinits on demand */
    PL_last_swash_klen	= 0;
    PL_last_swash_key[0]= '\0';
    PL_last_swash_tmps	= (U8*)NULL;
    PL_last_swash_slen	= 0;

    /* perly.c globals */
    PL_yydebug		= proto_perl->Iyydebug;
    PL_yynerrs		= proto_perl->Iyynerrs;
    PL_yyerrflag	= proto_perl->Iyyerrflag;
    PL_yychar		= proto_perl->Iyychar;
    PL_yyval		= proto_perl->Iyyval;
    PL_yylval		= proto_perl->Iyylval;

    PL_glob_index	= proto_perl->Iglob_index;
    PL_srand_called	= proto_perl->Isrand_called;
    PL_hash_seed	= proto_perl->Ihash_seed;
    PL_uudmap['M']	= 0;		/* reinits on demand */
    PL_bitcount		= Nullch;	/* reinits on demand */

    if (proto_perl->Ipsig_pend) {
	Newz(0, PL_psig_pend, SIG_SIZE, int);
    }
    else {
	PL_psig_pend	= (int*)NULL;
    }

    if (proto_perl->Ipsig_ptr) {
	Newz(0, PL_psig_ptr,  SIG_SIZE, SV*);
	Newz(0, PL_psig_name, SIG_SIZE, SV*);
	for (i = 1; i < SIG_SIZE; i++) {
	    PL_psig_ptr[i]  = sv_dup_inc(proto_perl->Ipsig_ptr[i], param);
	    PL_psig_name[i] = sv_dup_inc(proto_perl->Ipsig_name[i], param);
	}
    }
    else {
	PL_psig_ptr	= (SV**)NULL;
	PL_psig_name	= (SV**)NULL;
    }

    /* thrdvar.h stuff */

    if (flags & CLONEf_COPY_STACKS) {
	/* next allocation will be PL_tmps_stack[PL_tmps_ix+1] */
	PL_tmps_ix		= proto_perl->Ttmps_ix;
	PL_tmps_max		= proto_perl->Ttmps_max;
	PL_tmps_floor		= proto_perl->Ttmps_floor;
	Newz(50, PL_tmps_stack, PL_tmps_max, SV*);
	i = 0;
	while (i <= PL_tmps_ix) {
	    PL_tmps_stack[i]	= sv_dup_inc(proto_perl->Ttmps_stack[i], param);
	    ++i;
	}

	/* next PUSHMARK() sets *(PL_markstack_ptr+1) */
	i = proto_perl->Tmarkstack_max - proto_perl->Tmarkstack;
	Newz(54, PL_markstack, i, I32);
	PL_markstack_max	= PL_markstack + (proto_perl->Tmarkstack_max
						  - proto_perl->Tmarkstack);
	PL_markstack_ptr	= PL_markstack + (proto_perl->Tmarkstack_ptr
						  - proto_perl->Tmarkstack);
	Copy(proto_perl->Tmarkstack, PL_markstack,
	     PL_markstack_ptr - PL_markstack + 1, I32);

	/* next push_scope()/ENTER sets PL_scopestack[PL_scopestack_ix]
	 * NOTE: unlike the others! */
	PL_scopestack_ix	= proto_perl->Tscopestack_ix;
	PL_scopestack_max	= proto_perl->Tscopestack_max;
	Newz(54, PL_scopestack, PL_scopestack_max, I32);
	Copy(proto_perl->Tscopestack, PL_scopestack, PL_scopestack_ix, I32);

	/* next push_return() sets PL_retstack[PL_retstack_ix]
	 * NOTE: unlike the others! */
	PL_retstack_ix		= proto_perl->Tretstack_ix;
	PL_retstack_max		= proto_perl->Tretstack_max;
	Newz(54, PL_retstack, PL_retstack_max, OP*);
	Copy(proto_perl->Tretstack, PL_retstack, PL_retstack_ix, OP*);

	/* NOTE: si_dup() looks at PL_markstack */
	PL_curstackinfo		= si_dup(proto_perl->Tcurstackinfo, param);

	/* PL_curstack		= PL_curstackinfo->si_stack; */
	PL_curstack		= av_dup(proto_perl->Tcurstack, param);
	PL_mainstack		= av_dup(proto_perl->Tmainstack, param);

	/* next PUSHs() etc. set *(PL_stack_sp+1) */
	PL_stack_base		= AvARRAY(PL_curstack);
	PL_stack_sp		= PL_stack_base + (proto_perl->Tstack_sp
						   - proto_perl->Tstack_base);
	PL_stack_max		= PL_stack_base + AvMAX(PL_curstack);

	/* next SSPUSHFOO() sets PL_savestack[PL_savestack_ix]
	 * NOTE: unlike the others! */
	PL_savestack_ix		= proto_perl->Tsavestack_ix;
	PL_savestack_max	= proto_perl->Tsavestack_max;
	/*Newz(54, PL_savestack, PL_savestack_max, ANY);*/
	PL_savestack		= ss_dup(proto_perl, param);
    }
    else {
	init_stacks();
	ENTER;			/* perl_destruct() wants to LEAVE; */
    }

    PL_start_env	= proto_perl->Tstart_env;	/* XXXXXX */
    PL_top_env		= &PL_start_env;

    PL_op		= proto_perl->Top;

    PL_Sv		= Nullsv;
    PL_Xpv		= (XPV*)NULL;
    PL_na		= proto_perl->Tna;

    PL_statbuf		= proto_perl->Tstatbuf;
    PL_statcache	= proto_perl->Tstatcache;
    PL_statgv		= gv_dup(proto_perl->Tstatgv, param);
    PL_statname		= sv_dup_inc(proto_perl->Tstatname, param);
#ifdef HAS_TIMES
    PL_timesbuf		= proto_perl->Ttimesbuf;
#endif

    PL_tainted		= proto_perl->Ttainted;
    PL_curpm		= proto_perl->Tcurpm;	/* XXX No PMOP ref count */
    PL_rs		= sv_dup_inc(proto_perl->Trs, param);
    PL_last_in_gv	= gv_dup(proto_perl->Tlast_in_gv, param);
    PL_ofs_sv		= sv_dup_inc(proto_perl->Tofs_sv, param);
    PL_defoutgv		= gv_dup_inc(proto_perl->Tdefoutgv, param);
    PL_chopset		= proto_perl->Tchopset;	/* XXX never deallocated */
    PL_toptarget	= sv_dup_inc(proto_perl->Ttoptarget, param);
    PL_bodytarget	= sv_dup_inc(proto_perl->Tbodytarget, param);
    PL_formtarget	= sv_dup(proto_perl->Tformtarget, param);

    PL_restartop	= proto_perl->Trestartop;
    PL_in_eval		= proto_perl->Tin_eval;
    PL_delaymagic	= proto_perl->Tdelaymagic;
    PL_dirty		= proto_perl->Tdirty;
    PL_localizing	= proto_perl->Tlocalizing;

#ifdef PERL_FLEXIBLE_EXCEPTIONS
    PL_protect		= proto_perl->Tprotect;
#endif
    PL_errors		= sv_dup_inc(proto_perl->Terrors, param);
    PL_hv_fetch_ent_mh	= Nullhe;
    PL_modcount		= proto_perl->Tmodcount;
    PL_lastgotoprobe	= Nullop;
    PL_dumpindent	= proto_perl->Tdumpindent;

    PL_sortcop		= (OP*)any_dup(proto_perl->Tsortcop, proto_perl);
    PL_sortstash	= hv_dup(proto_perl->Tsortstash, param);
    PL_firstgv		= gv_dup(proto_perl->Tfirstgv, param);
    PL_secondgv		= gv_dup(proto_perl->Tsecondgv, param);
    PL_sortcxix		= proto_perl->Tsortcxix;
    PL_efloatbuf	= Nullch;		/* reinits on demand */
    PL_efloatsize	= 0;			/* reinits on demand */

    /* regex stuff */

    PL_screamfirst	= NULL;
    PL_screamnext	= NULL;
    PL_maxscream	= -1;			/* reinits on demand */
    PL_lastscream	= Nullsv;

    PL_watchaddr	= NULL;
    PL_watchok		= Nullch;

    PL_regdummy		= proto_perl->Tregdummy;
    PL_regcomp_parse	= Nullch;
    PL_regxend		= Nullch;
    PL_regcode		= (regnode*)NULL;
    PL_regnaughty	= 0;
    PL_regsawback	= 0;
    PL_regprecomp	= Nullch;
    PL_regnpar		= 0;
    PL_regsize		= 0;
    PL_regflags		= 0;
    PL_regseen		= 0;
    PL_seen_zerolen	= 0;
    PL_seen_evals	= 0;
    PL_regcomp_rx	= (regexp*)NULL;
    PL_extralen		= 0;
    PL_colorset		= 0;		/* reinits PL_colors[] */
    /*PL_colors[6]	= {0,0,0,0,0,0};*/
    PL_reg_whilem_seen	= 0;
    PL_reginput		= Nullch;
    PL_regbol		= Nullch;
    PL_regeol		= Nullch;
    PL_regstartp	= (I32*)NULL;
    PL_regendp		= (I32*)NULL;
    PL_reglastparen	= (U32*)NULL;
    PL_reglastcloseparen	= (U32*)NULL;
    PL_regtill		= Nullch;
    PL_reg_start_tmp	= (char**)NULL;
    PL_reg_start_tmpl	= 0;
    PL_regdata		= (struct reg_data*)NULL;
    PL_bostr		= Nullch;
    PL_reg_flags	= 0;
    PL_reg_eval_set	= 0;
    PL_regnarrate	= 0;
    PL_regprogram	= (regnode*)NULL;
    PL_regindent	= 0;
    PL_regcc		= (CURCUR*)NULL;
    PL_reg_call_cc	= (struct re_cc_state*)NULL;
    PL_reg_re		= (regexp*)NULL;
    PL_reg_ganch	= Nullch;
    PL_reg_sv		= Nullsv;
    PL_reg_match_utf8	= FALSE;
    PL_reg_magic	= (MAGIC*)NULL;
    PL_reg_oldpos	= 0;
    PL_reg_oldcurpm	= (PMOP*)NULL;
    PL_reg_curpm	= (PMOP*)NULL;
    PL_reg_oldsaved	= Nullch;
    PL_reg_oldsavedlen	= 0;
    PL_reg_maxiter	= 0;
    PL_reg_leftiter	= 0;
    PL_reg_poscache	= Nullch;
    PL_reg_poscache_size= 0;

    /* RE engine - function pointers */
    PL_regcompp		= proto_perl->Tregcompp;
    PL_regexecp		= proto_perl->Tregexecp;
    PL_regint_start	= proto_perl->Tregint_start;
    PL_regint_string	= proto_perl->Tregint_string;
    PL_regfree		= proto_perl->Tregfree;

    PL_reginterp_cnt	= 0;
    PL_reg_starttry	= 0;

    /* Pluggable optimizer */
    PL_peepp		= proto_perl->Tpeepp;

    PL_stashcache       = newHV();

    if (!(flags & CLONEf_KEEP_PTR_TABLE)) {
        ptr_table_free(PL_ptr_table);
        PL_ptr_table = NULL;
    }

    /* Call the ->CLONE method, if it exists, for each of the stashes
       identified by sv_dup() above.
    */
    while(av_len(param->stashes) != -1) {
        HV* stash = (HV*) av_shift(param->stashes);
	GV* cloner = gv_fetchmethod_autoload(stash, "CLONE", 0);
	if (cloner && GvCV(cloner)) {
	    dSP;
	    ENTER;
	    SAVETMPS;
	    PUSHMARK(SP);
           XPUSHs(sv_2mortal(newSVpv(HvNAME(stash), 0)));
	    PUTBACK;
	    call_sv((SV*)GvCV(cloner), G_DISCARD);
	    FREETMPS;
	    LEAVE;
	}
    }

    SvREFCNT_dec(param->stashes);

    return my_perl;
}

#endif /* USE_ITHREADS */

/*
=head1 Unicode Support

=for apidoc sv_recode_to_utf8

The encoding is assumed to be an Encode object, on entry the PV
of the sv is assumed to be octets in that encoding, and the sv
will be converted into Unicode (and UTF-8).

If the sv already is UTF-8 (or if it is not POK), or if the encoding
is not a reference, nothing is done to the sv.  If the encoding is not
an C<Encode::XS> Encoding object, bad things will happen.
(See F<lib/encoding.pm> and L<Encode>).

The PV of the sv is returned.

=cut */

char *
Perl_sv_recode_to_utf8(pTHX_ SV *sv, SV *encoding)
{
    if (SvPOK(sv) && !SvUTF8(sv) && !IN_BYTES && SvROK(encoding)) {
	SV *uni;
	STRLEN len;
	char *s;
	dSP;
	ENTER;
	SAVETMPS;
	save_re_context();
	PUSHMARK(sp);
	EXTEND(SP, 3);
	XPUSHs(encoding);
	XPUSHs(sv);
/* 
  NI-S 2002/07/09
  Passing sv_yes is wrong - it needs to be or'ed set of constants
  for Encode::XS, while UTf-8 decode (currently) assumes a true value means 
  remove converted chars from source.

  Both will default the value - let them.
  
	XPUSHs(&PL_sv_yes);
*/
	PUTBACK;
	call_method("decode", G_SCALAR);
	SPAGAIN;
	uni = POPs;
	PUTBACK;
	s = SvPV(uni, len);
	if (s != SvPVX(sv)) {
	    SvGROW(sv, len + 1);
	    Move(s, SvPVX(sv), len, char);
	    SvCUR_set(sv, len);
	    SvPVX(sv)[len] = 0;	
	}
	FREETMPS;
	LEAVE;
	SvUTF8_on(sv);
    }
    return SvPVX(sv);
}

/*
=for apidoc sv_cat_decode

The encoding is assumed to be an Encode object, the PV of the ssv is
assumed to be octets in that encoding and decoding the input starts
from the position which (PV + *offset) pointed to.  The dsv will be
concatenated the decoded UTF-8 string from ssv.  Decoding will terminate
when the string tstr appears in decoding output or the input ends on
the PV of the ssv. The value which the offset points will be modified
to the last input position on the ssv.

Returns TRUE if the terminator was found, else returns FALSE.

=cut */

bool
Perl_sv_cat_decode(pTHX_ SV *dsv, SV *encoding,
		   SV *ssv, int *offset, char *tstr, int tlen)
{
    bool ret = FALSE;
    if (SvPOK(ssv) && SvPOK(dsv) && SvROK(encoding) && offset) {
	SV *offsv;
	dSP;
	ENTER;
	SAVETMPS;
	save_re_context();
	PUSHMARK(sp);
	EXTEND(SP, 6);
	XPUSHs(encoding);
	XPUSHs(dsv);
	XPUSHs(ssv);
	XPUSHs(offsv = sv_2mortal(newSViv(*offset)));
	XPUSHs(sv_2mortal(newSVpvn(tstr, tlen)));
	PUTBACK;
	call_method("cat_decode", G_SCALAR);
	SPAGAIN;
	ret = SvTRUE(TOPs);
	*offset = SvIV(offsv);
	PUTBACK;
	FREETMPS;
	LEAVE;
    }
    else
        Perl_croak(aTHX_ "Invalid argument to sv_cat_decode");
    return ret;
}

