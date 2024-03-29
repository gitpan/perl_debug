/*    pp_ctl.c
 *
 *    Copyright (C) 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
 *    2000, 2001, 2002, 2003, by Larry Wall and others
 *
 *    You may distribute under the terms of either the GNU General Public
 *    License or the Artistic License, as specified in the README file.
 *
 */

/*
 * Now far ahead the Road has gone,
 * And I must follow, if I can,
 * Pursuing it with eager feet,
 * Until it joins some larger way
 * Where many paths and errands meet.
 * And whither then?  I cannot say.
 */

#include "EXTERN.h"
#define PERL_IN_PP_CTL_C
#include "perl.h"

#ifndef WORD_ALIGN
#define WORD_ALIGN sizeof(U32)
#endif

#define DOCATCH(o) ((CATCH_GET == TRUE) ? docatch(o) : (o))

static I32 run_user_filter(pTHX_ int idx, SV *buf_sv, int maxlen);

PP(pp_wantarray)
{
    dSP;
    I32 cxix;
    EXTEND(SP, 1);

    cxix = dopoptosub(cxstack_ix);
    if (cxix < 0)
	RETPUSHUNDEF;

    switch (cxstack[cxix].blk_gimme) {
    case G_ARRAY:
	RETPUSHYES;
    case G_SCALAR:
	RETPUSHNO;
    default:
	RETPUSHUNDEF;
    }
}

PP(pp_regcmaybe)
{
    return NORMAL;
}

PP(pp_regcreset)
{
    /* XXXX Should store the old value to allow for tie/overload - and
       restore in regcomp, where marked with XXXX. */
    PL_reginterp_cnt = 0;
    return NORMAL;
}

PP(pp_regcomp)
{
    dSP;
    register PMOP *pm = (PMOP*)cLOGOP->op_other;
    register char *t;
    SV *tmpstr;
    STRLEN len;
    MAGIC *mg = Null(MAGIC*);
    
    tmpstr = POPs;

    /* prevent recompiling under /o and ithreads. */
#if defined(USE_ITHREADS) || defined(USE_5005THREADS)
    if (pm->op_pmflags & PMf_KEEP && PM_GETRE(pm))
	 RETURN;
#endif

    if (SvROK(tmpstr)) {
	SV *sv = SvRV(tmpstr);
	if(SvMAGICAL(sv))
	    mg = mg_find(sv, PERL_MAGIC_qr);
    }
    if (mg) {
	regexp *re = (regexp *)mg->mg_obj;
	ReREFCNT_dec(PM_GETRE(pm));
	PM_SETRE(pm, ReREFCNT_inc(re));
    }
    else {
	t = SvPV(tmpstr, len);

	/* Check against the last compiled regexp. */
	if (!PM_GETRE(pm) || !PM_GETRE(pm)->precomp ||
	    PM_GETRE(pm)->prelen != (I32)len ||
	    memNE(PM_GETRE(pm)->precomp, t, len))
	{
	    if (PM_GETRE(pm)) {
	        ReREFCNT_dec(PM_GETRE(pm));
		PM_SETRE(pm, Null(REGEXP*));	/* crucial if regcomp aborts */
	    }
	    if (PL_op->op_flags & OPf_SPECIAL)
		PL_reginterp_cnt = I32_MAX; /* Mark as safe.  */

	    pm->op_pmflags = pm->op_pmpermflags;	/* reset case sensitivity */
	    if (DO_UTF8(tmpstr))
		pm->op_pmdynflags |= PMdf_DYN_UTF8;
	    else {
		pm->op_pmdynflags &= ~PMdf_DYN_UTF8;
		if (pm->op_pmdynflags & PMdf_UTF8)
		    t = (char*)bytes_to_utf8((U8*)t, &len);
	    }
	    PM_SETRE(pm, CALLREGCOMP(aTHX_ t, t + len, pm));
	    if (!DO_UTF8(tmpstr) && (pm->op_pmdynflags & PMdf_UTF8))
		Safefree(t);
	    PL_reginterp_cnt = 0;	/* XXXX Be extra paranoid - needed
					   inside tie/overload accessors.  */
	}
    }

#ifndef INCOMPLETE_TAINTS
    if (PL_tainting) {
	if (PL_tainted)
	    pm->op_pmdynflags |= PMdf_TAINTED;
	else
	    pm->op_pmdynflags &= ~PMdf_TAINTED;
    }
#endif

    if (!PM_GETRE(pm)->prelen && PL_curpm)
	pm = PL_curpm;
    else if (strEQ("\\s+", PM_GETRE(pm)->precomp))
	pm->op_pmflags |= PMf_WHITE;
    else
	pm->op_pmflags &= ~PMf_WHITE;

    /* XXX runtime compiled output needs to move to the pad */
    if (pm->op_pmflags & PMf_KEEP) {
	pm->op_private &= ~OPpRUNTIME;	/* no point compiling again */
#if !defined(USE_ITHREADS) && !defined(USE_5005THREADS)
	/* XXX can't change the optree at runtime either */
	cLOGOP->op_first->op_next = PL_op->op_next;
#endif
    }
    RETURN;
}

PP(pp_substcont)
{
    dSP;
    register PMOP *pm = (PMOP*) cLOGOP->op_other;
    register PERL_CONTEXT *cx = &cxstack[cxstack_ix];
    register SV *dstr = cx->sb_dstr;
    register char *s = cx->sb_s;
    register char *m = cx->sb_m;
    char *orig = cx->sb_orig;
    register REGEXP *rx = cx->sb_rx;
    SV *nsv = Nullsv;

    { 
      REGEXP *old = PM_GETRE(pm);
      if(old != rx) {
	if(old) 
	  ReREFCNT_dec(old);
	PM_SETRE(pm,rx);
      }
    }

    rxres_restore(&cx->sb_rxres, rx);
    RX_MATCH_UTF8_set(rx, SvUTF8(cx->sb_targ));

    if (cx->sb_iters++) {
	I32 saviters = cx->sb_iters;
	if (cx->sb_iters > cx->sb_maxiters)
	    DIE(aTHX_ "Substitution loop");

	if (!(cx->sb_rxtainted & 2) && SvTAINTED(TOPs))
	    cx->sb_rxtainted |= 2;
	sv_catsv(dstr, POPs);

	/* Are we done */
	if (cx->sb_once || !CALLREGEXEC(aTHX_ rx, s, cx->sb_strend, orig,
				     s == m, cx->sb_targ, NULL,
				     ((cx->sb_rflags & REXEC_COPY_STR)
				      ? (REXEC_IGNOREPOS|REXEC_NOT_FIRST)
				      : (REXEC_COPY_STR|REXEC_IGNOREPOS|REXEC_NOT_FIRST))))
	{
	    SV *targ = cx->sb_targ;

	    if (DO_UTF8(dstr) && !SvUTF8(targ))
		sv_catpvn_utf8_upgrade(dstr, s, cx->sb_strend - s, nsv);
	    else
		sv_catpvn(dstr, s, cx->sb_strend - s);
	    cx->sb_rxtainted |= RX_MATCH_TAINTED(rx);

	    (void)SvOOK_off(targ);
	    if (SvLEN(targ))
		Safefree(SvPVX(targ));
	    SvPVX(targ) = SvPVX(dstr);
	    SvCUR_set(targ, SvCUR(dstr));
	    SvLEN_set(targ, SvLEN(dstr));
	    if (DO_UTF8(dstr))
		SvUTF8_on(targ);
	    SvPVX(dstr) = 0;
	    sv_free(dstr);

	    TAINT_IF(cx->sb_rxtainted & 1);
	    PUSHs(sv_2mortal(newSViv(saviters - 1)));

	    (void)SvPOK_only_UTF8(targ);
	    TAINT_IF(cx->sb_rxtainted);
	    SvSETMAGIC(targ);
	    SvTAINT(targ);

	    LEAVE_SCOPE(cx->sb_oldsave);
	    ReREFCNT_dec(rx);
	    POPSUBST(cx);
	    RETURNOP(pm->op_next);
	}
	cx->sb_iters = saviters;
    }
    if (RX_MATCH_COPIED(rx) && rx->subbeg != orig) {
	m = s;
	s = orig;
	cx->sb_orig = orig = rx->subbeg;
	s = orig + (m - s);
	cx->sb_strend = s + (cx->sb_strend - m);
    }
    cx->sb_m = m = rx->startp[0] + orig;
    if (m > s) {
	if (DO_UTF8(dstr) && !SvUTF8(cx->sb_targ)) 
	    sv_catpvn_utf8_upgrade(dstr, s, m - s, nsv);
	else
	    sv_catpvn(dstr, s, m-s);
    }
    cx->sb_s = rx->endp[0] + orig;
    { /* Update the pos() information. */
	SV *sv = cx->sb_targ;
	MAGIC *mg;
	I32 i;
	if (SvTYPE(sv) < SVt_PVMG)
	    (void)SvUPGRADE(sv, SVt_PVMG);
	if (!(mg = mg_find(sv, PERL_MAGIC_regex_global))) {
	    sv_magic(sv, Nullsv, PERL_MAGIC_regex_global, Nullch, 0);
	    mg = mg_find(sv, PERL_MAGIC_regex_global);
	}
	i = m - orig;
	if (DO_UTF8(sv))
	    sv_pos_b2u(sv, &i);
	mg->mg_len = i;
    }
    ReREFCNT_inc(rx);
    cx->sb_rxtainted |= RX_MATCH_TAINTED(rx);
    rxres_save(&cx->sb_rxres, rx);
    RETURNOP(pm->op_pmreplstart);
}

void
Perl_rxres_save(pTHX_ void **rsp, REGEXP *rx)
{
    UV *p = (UV*)*rsp;
    U32 i;

    if (!p || p[1] < rx->nparens) {
	i = 6 + rx->nparens * 2;
	if (!p)
	    New(501, p, i, UV);
	else
	    Renew(p, i, UV);
	*rsp = (void*)p;
    }

    *p++ = PTR2UV(RX_MATCH_COPIED(rx) ? rx->subbeg : Nullch);
    RX_MATCH_COPIED_off(rx);

    *p++ = rx->nparens;

    *p++ = PTR2UV(rx->subbeg);
    *p++ = (UV)rx->sublen;
    for (i = 0; i <= rx->nparens; ++i) {
	*p++ = (UV)rx->startp[i];
	*p++ = (UV)rx->endp[i];
    }
}

void
Perl_rxres_restore(pTHX_ void **rsp, REGEXP *rx)
{
    UV *p = (UV*)*rsp;
    U32 i;

    if (RX_MATCH_COPIED(rx))
	Safefree(rx->subbeg);
    RX_MATCH_COPIED_set(rx, *p);
    *p++ = 0;

    rx->nparens = *p++;

    rx->subbeg = INT2PTR(char*,*p++);
    rx->sublen = (I32)(*p++);
    for (i = 0; i <= rx->nparens; ++i) {
	rx->startp[i] = (I32)(*p++);
	rx->endp[i] = (I32)(*p++);
    }
}

void
Perl_rxres_free(pTHX_ void **rsp)
{
    UV *p = (UV*)*rsp;

    if (p) {
	Safefree(INT2PTR(char*,*p));
	Safefree(p);
	*rsp = Null(void*);
    }
}

PP(pp_formline)
{
    dSP; dMARK; dORIGMARK;
    register SV *tmpForm = *++MARK;
    register U32 *fpc;
    register char *t;
    register char *f;
    register char *s;
    register char *send;
    register I32 arg;
    register SV *sv = Nullsv;
    char *item = Nullch;
    I32 itemsize  = 0;
    I32 fieldsize = 0;
    I32 lines = 0;
    bool chopspace = (strchr(PL_chopset, ' ') != Nullch);
    char *chophere = Nullch;
    char *linemark = Nullch;
    NV value;
    bool gotsome = FALSE;
    STRLEN len;
    STRLEN fudge = SvCUR(tmpForm) * (IN_BYTES ? 1 : 3) + 1;
    bool item_is_utf8 = FALSE;
    bool targ_is_utf8 = FALSE;
    SV * nsv = Nullsv;

    if (!SvMAGICAL(tmpForm) || !SvCOMPILED(tmpForm)) {
	if (SvREADONLY(tmpForm)) {
	    SvREADONLY_off(tmpForm);
	    doparseform(tmpForm);
	    SvREADONLY_on(tmpForm);
	}
	else
	    doparseform(tmpForm);
    }
    SvPV_force(PL_formtarget, len);
    if (DO_UTF8(PL_formtarget))
	targ_is_utf8 = TRUE;
    t = SvGROW(PL_formtarget, len + fudge + 1);  /* XXX SvCUR bad */
    t += len;
    f = SvPV(tmpForm, len);
    /* need to jump to the next word */
    s = f + len + WORD_ALIGN - SvCUR(tmpForm) % WORD_ALIGN;

    fpc = (U32*)s;

    for (;;) {
	DEBUG_f( {
	    char *name = "???";
	    arg = -1;
	    switch (*fpc) {
	    case FF_LITERAL:	arg = fpc[1]; name = "LITERAL";	break;
	    case FF_BLANK:	arg = fpc[1]; name = "BLANK";	break;
	    case FF_SKIP:	arg = fpc[1]; name = "SKIP";	break;
	    case FF_FETCH:	arg = fpc[1]; name = "FETCH";	break;
	    case FF_DECIMAL:	arg = fpc[1]; name = "DECIMAL";	break;

	    case FF_CHECKNL:	name = "CHECKNL";	break;
	    case FF_CHECKCHOP:	name = "CHECKCHOP";	break;
	    case FF_SPACE:	name = "SPACE";		break;
	    case FF_HALFSPACE:	name = "HALFSPACE";	break;
	    case FF_ITEM:	name = "ITEM";		break;
	    case FF_CHOP:	name = "CHOP";		break;
	    case FF_LINEGLOB:	name = "LINEGLOB";	break;
	    case FF_NEWLINE:	name = "NEWLINE";	break;
	    case FF_MORE:	name = "MORE";		break;
	    case FF_LINEMARK:	name = "LINEMARK";	break;
	    case FF_END:	name = "END";		break;
            case FF_0DECIMAL:	name = "0DECIMAL";	break;
	    }
	    if (arg >= 0)
		PerlIO_printf(Perl_debug_log, "%-16s%ld\n", name, (long) arg);
	    else
		PerlIO_printf(Perl_debug_log, "%-16s\n", name);
	} );
	switch (*fpc++) {
	case FF_LINEMARK:
	    linemark = t;
	    lines++;
	    gotsome = FALSE;
	    break;

	case FF_LITERAL:
	    arg = *fpc++;
	    if (targ_is_utf8 && !SvUTF8(tmpForm)) {
		SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		*t = '\0';
		sv_catpvn_utf8_upgrade(PL_formtarget, f, arg, nsv);
		t = SvEND(PL_formtarget);
		break;
	    }
	    if (!targ_is_utf8 && DO_UTF8(tmpForm)) {
		SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		*t = '\0';
		sv_utf8_upgrade(PL_formtarget);
		SvGROW(PL_formtarget, SvCUR(PL_formtarget) + fudge + 1);
		t = SvEND(PL_formtarget);
		targ_is_utf8 = TRUE;
	    }
	    while (arg--)
		*t++ = *f++;
	    break;

	case FF_SKIP:
	    f += *fpc++;
	    break;

	case FF_FETCH:
	    arg = *fpc++;
	    f += arg;
	    fieldsize = arg;

	    if (MARK < SP)
		sv = *++MARK;
	    else {
		sv = &PL_sv_no;
		if (ckWARN(WARN_SYNTAX))
		    Perl_warner(aTHX_ packWARN(WARN_SYNTAX), "Not enough format arguments");
	    }
	    break;

	case FF_CHECKNL:
	    item = s = SvPV(sv, len);
	    itemsize = len;
	    if (DO_UTF8(sv)) {
		itemsize = sv_len_utf8(sv);
		if (itemsize != (I32)len) {
		    I32 itembytes;
		    if (itemsize > fieldsize) {
			itemsize = fieldsize;
			itembytes = itemsize;
			sv_pos_u2b(sv, &itembytes, 0);
		    }
		    else
			itembytes = len;
		    send = chophere = s + itembytes;
		    while (s < send) {
			if (*s & ~31)
			    gotsome = TRUE;
			else if (*s == '\n')
			    break;
			s++;
		    }
		    item_is_utf8 = TRUE;
		    itemsize = s - item;
		    sv_pos_b2u(sv, &itemsize);
		    break;
		}
	    }
	    item_is_utf8 = FALSE;
	    if (itemsize > fieldsize)
		itemsize = fieldsize;
	    send = chophere = s + itemsize;
	    while (s < send) {
		if (*s & ~31)
		    gotsome = TRUE;
		else if (*s == '\n')
		    break;
		s++;
	    }
	    itemsize = s - item;
	    break;

	case FF_CHECKCHOP:
	    item = s = SvPV(sv, len);
	    itemsize = len;
	    if (DO_UTF8(sv)) {
		itemsize = sv_len_utf8(sv);
		if (itemsize != (I32)len) {
		    I32 itembytes;
		    if (itemsize <= fieldsize) {
			send = chophere = s + itemsize;
			while (s < send) {
			    if (*s == '\r') {
				itemsize = s - item;
				break;
			    }
			    if (*s++ & ~31)
				gotsome = TRUE;
			}
		    }
		    else {
			itemsize = fieldsize;
			itembytes = itemsize;
			sv_pos_u2b(sv, &itembytes, 0);
			send = chophere = s + itembytes;
			while (s < send || (s == send && isSPACE(*s))) {
			    if (isSPACE(*s)) {
				if (chopspace)
				    chophere = s;
				if (*s == '\r')
				    break;
			    }
			    else {
				if (*s & ~31)
				    gotsome = TRUE;
				if (strchr(PL_chopset, *s))
				    chophere = s + 1;
			    }
			    s++;
			}
			itemsize = chophere - item;
			sv_pos_b2u(sv, &itemsize);
		    }
		    item_is_utf8 = TRUE;
		    break;
		}
	    }
	    item_is_utf8 = FALSE;
	    if (itemsize <= fieldsize) {
		send = chophere = s + itemsize;
		while (s < send) {
		    if (*s == '\r') {
			itemsize = s - item;
			break;
		    }
		    if (*s++ & ~31)
			gotsome = TRUE;
		}
	    }
	    else {
		itemsize = fieldsize;
		send = chophere = s + itemsize;
		while (s < send || (s == send && isSPACE(*s))) {
		    if (isSPACE(*s)) {
			if (chopspace)
			    chophere = s;
			if (*s == '\r')
			    break;
		    }
		    else {
			if (*s & ~31)
			    gotsome = TRUE;
			if (strchr(PL_chopset, *s))
			    chophere = s + 1;
		    }
		    s++;
		}
		itemsize = chophere - item;
	    }
	    break;

	case FF_SPACE:
	    arg = fieldsize - itemsize;
	    if (arg) {
		fieldsize -= arg;
		while (arg-- > 0)
		    *t++ = ' ';
	    }
	    break;

	case FF_HALFSPACE:
	    arg = fieldsize - itemsize;
	    if (arg) {
		arg /= 2;
		fieldsize -= arg;
		while (arg-- > 0)
		    *t++ = ' ';
	    }
	    break;

	case FF_ITEM:
	    arg = itemsize;
	    s = item;
	    if (item_is_utf8) {
		if (!targ_is_utf8) {
		    SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		    *t = '\0';
		    sv_utf8_upgrade(PL_formtarget);
		    SvGROW(PL_formtarget, SvCUR(PL_formtarget) + fudge + 1);
		    t = SvEND(PL_formtarget);
		    targ_is_utf8 = TRUE;
		}
		while (arg--) {
		    if (UTF8_IS_CONTINUED(*s)) {
			STRLEN skip = UTF8SKIP(s);
			switch (skip) {
			default:
			    Move(s,t,skip,char);
			    s += skip;
			    t += skip;
			    break;
			case 7: *t++ = *s++;
			case 6: *t++ = *s++;
			case 5: *t++ = *s++;
			case 4: *t++ = *s++;
			case 3: *t++ = *s++;
			case 2: *t++ = *s++;
			case 1: *t++ = *s++;
			}
		    }
		    else {
			if ( !((*t++ = *s++) & ~31) )
			    t[-1] = ' ';
		    }
		}
		break;
	    }
	    if (targ_is_utf8 && !item_is_utf8) {
		SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		*t = '\0';
		sv_catpvn_utf8_upgrade(PL_formtarget, s, arg, nsv);
		for (; t < SvEND(PL_formtarget); t++) {
#ifdef EBCDIC
		    int ch = *t++ = *s++;
		    if (iscntrl(ch))
#else
		    if (!(*t & ~31))
#endif
			*t = ' ';
		}
		break;
	    }
	    while (arg--) {
#ifdef EBCDIC
		int ch = *t++ = *s++;
		if (iscntrl(ch))
#else
		if ( !((*t++ = *s++) & ~31) )
#endif
		    t[-1] = ' ';
	    }
	    break;

	case FF_CHOP:
	    s = chophere;
	    if (chopspace) {
		while (*s && isSPACE(*s))
		    s++;
	    }
	    sv_chop(sv,s);
	    SvSETMAGIC(sv);
	    break;

	case FF_LINEGLOB:
	    item = s = SvPV(sv, len);
	    itemsize = len;
	    if ((item_is_utf8 = DO_UTF8(sv)))
		itemsize = sv_len_utf8(sv);	    
	    if (itemsize) {
		bool chopped = FALSE;
		gotsome = TRUE;
		send = s + len;
		while (s < send) {
		    if (*s++ == '\n') {
			if (s == send) {
			    itemsize--;
			    chopped = TRUE;
			}
			else
			    lines++;
		    }
		}
		SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		if (targ_is_utf8)
		    SvUTF8_on(PL_formtarget);
		sv_catsv(PL_formtarget, sv);
		if (chopped)
		    SvCUR_set(PL_formtarget, SvCUR(PL_formtarget) - 1);
		SvGROW(PL_formtarget, SvCUR(PL_formtarget) + fudge + 1);
		t = SvPVX(PL_formtarget) + SvCUR(PL_formtarget);
		if (item_is_utf8)
		    targ_is_utf8 = TRUE;
	    }
	    break;

	case FF_DECIMAL:
	    /* If the field is marked with ^ and the value is undefined,
	       blank it out. */
	    arg = *fpc++;
	    if ((arg & 512) && !SvOK(sv)) {
		arg = fieldsize;
		while (arg--)
		    *t++ = ' ';
		break;
	    }
	    gotsome = TRUE;
	    value = SvNV(sv);
	    /* Formats aren't yet marked for locales, so assume "yes". */
	    {
		STORE_NUMERIC_STANDARD_SET_LOCAL();
#if defined(USE_LONG_DOUBLE)
		if (arg & 256) {
		    sprintf(t, "%#*.*" PERL_PRIfldbl,
			    (int) fieldsize, (int) arg & 255, value);
		} else {
		    sprintf(t, "%*.0" PERL_PRIfldbl, (int) fieldsize, value);
		}
#else
		if (arg & 256) {
		    sprintf(t, "%#*.*f",
			    (int) fieldsize, (int) arg & 255, value);
		} else {
		    sprintf(t, "%*.0f",
			    (int) fieldsize, value);
		}
#endif
		RESTORE_NUMERIC_STANDARD();
	    }
	    t += fieldsize;
	    break;

	case FF_0DECIMAL:
	    /* If the field is marked with ^ and the value is undefined,
	       blank it out. */
	    arg = *fpc++;
	    if ((arg & 512) && !SvOK(sv)) {
		arg = fieldsize;
		while (arg--)
		    *t++ = ' ';
		break;
	    }
	    gotsome = TRUE;
	    value = SvNV(sv);
	    /* Formats aren't yet marked for locales, so assume "yes". */
	    {
		STORE_NUMERIC_STANDARD_SET_LOCAL();
#if defined(USE_LONG_DOUBLE)
		if (arg & 256) {
		    sprintf(t, "%#0*.*" PERL_PRIfldbl,
			    (int) fieldsize, (int) arg & 255, value);
/* is this legal? I don't have long doubles */
		} else {
		    sprintf(t, "%0*.0" PERL_PRIfldbl, (int) fieldsize, value);
		}
#else
		if (arg & 256) {
		    sprintf(t, "%#0*.*f",
			    (int) fieldsize, (int) arg & 255, value);
		} else {
		    sprintf(t, "%0*.0f",
			    (int) fieldsize, value);
		}
#endif
		RESTORE_NUMERIC_STANDARD();
	    }
	    t += fieldsize;
	    break;
	
	case FF_NEWLINE:
	    f++;
	    while (t-- > linemark && *t == ' ') ;
	    t++;
	    *t++ = '\n';
	    break;

	case FF_BLANK:
	    arg = *fpc++;
	    if (gotsome) {
		if (arg) {		/* repeat until fields exhausted? */
		    *t = '\0';
		    SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		    lines += FmLINES(PL_formtarget);
		    if (lines == 200) {
			arg = t - linemark;
			if (strnEQ(linemark, linemark - arg, arg))
			    DIE(aTHX_ "Runaway format");
		    }
		    if (targ_is_utf8)
			SvUTF8_on(PL_formtarget);
		    FmLINES(PL_formtarget) = lines;
		    SP = ORIGMARK;
		    RETURNOP(cLISTOP->op_first);
		}
	    }
	    else {
		t = linemark;
		lines--;
	    }
	    break;

	case FF_MORE:
	    s = chophere;
	    send = item + len;
	    if (chopspace) {
		while (*s && isSPACE(*s) && s < send)
		    s++;
	    }
	    if (s < send) {
		arg = fieldsize - itemsize;
		if (arg) {
		    fieldsize -= arg;
		    while (arg-- > 0)
			*t++ = ' ';
		}
		s = t - 3;
		if (strnEQ(s,"   ",3)) {
		    while (s > SvPVX(PL_formtarget) && isSPACE(s[-1]))
			s--;
		}
		*s++ = '.';
		*s++ = '.';
		*s++ = '.';
	    }
	    break;

	case FF_END:
	    *t = '\0';
	    SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
	    if (targ_is_utf8)
		SvUTF8_on(PL_formtarget);
	    FmLINES(PL_formtarget) += lines;
	    SP = ORIGMARK;
	    RETPUSHYES;
	}
    }
}

PP(pp_grepstart)
{
    dSP;
    SV *src;

    if (PL_stack_base + *PL_markstack_ptr == SP) {
	(void)POPMARK;
	if (GIMME_V == G_SCALAR)
	    XPUSHs(sv_2mortal(newSViv(0)));
	RETURNOP(PL_op->op_next->op_next);
    }
    PL_stack_sp = PL_stack_base + *PL_markstack_ptr + 1;
    pp_pushmark();				/* push dst */
    pp_pushmark();				/* push src */
    ENTER;					/* enter outer scope */

    SAVETMPS;
    /* SAVE_DEFSV does *not* suffice here for USE_5005THREADS */
    SAVESPTR(DEFSV);
    ENTER;					/* enter inner scope */
    SAVEVPTR(PL_curpm);

    src = PL_stack_base[*PL_markstack_ptr];
    SvTEMP_off(src);
    DEFSV = src;

    PUTBACK;
    if (PL_op->op_type == OP_MAPSTART)
	pp_pushmark();			/* push top */
    return ((LOGOP*)PL_op->op_next)->op_other;
}

PP(pp_mapstart)
{
    DIE(aTHX_ "panic: mapstart");	/* uses grepstart */
}

PP(pp_mapwhile)
{
    dSP;
    I32 gimme = GIMME_V;
    I32 items = (SP - PL_stack_base) - *PL_markstack_ptr; /* how many new items */
    I32 count;
    I32 shift;
    SV** src;
    SV** dst;

    /* first, move source pointer to the next item in the source list */
    ++PL_markstack_ptr[-1];

    /* if there are new items, push them into the destination list */
    if (items && gimme != G_VOID) {
	/* might need to make room back there first */
	if (items > PL_markstack_ptr[-1] - PL_markstack_ptr[-2]) {
	    /* XXX this implementation is very pessimal because the stack
	     * is repeatedly extended for every set of items.  Is possible
	     * to do this without any stack extension or copying at all
	     * by maintaining a separate list over which the map iterates
	     * (like foreach does). --gsar */

	    /* everything in the stack after the destination list moves
	     * towards the end the stack by the amount of room needed */
	    shift = items - (PL_markstack_ptr[-1] - PL_markstack_ptr[-2]);

	    /* items to shift up (accounting for the moved source pointer) */
	    count = (SP - PL_stack_base) - (PL_markstack_ptr[-1] - 1);

	    /* This optimization is by Ben Tilly and it does
	     * things differently from what Sarathy (gsar)
	     * is describing.  The downside of this optimization is
	     * that leaves "holes" (uninitialized and hopefully unused areas)
	     * to the Perl stack, but on the other hand this
	     * shouldn't be a problem.  If Sarathy's idea gets
	     * implemented, this optimization should become
	     * irrelevant.  --jhi */
            if (shift < count)
                shift = count; /* Avoid shifting too often --Ben Tilly */
	
	    EXTEND(SP,shift);
	    src = SP;
	    dst = (SP += shift);
	    PL_markstack_ptr[-1] += shift;
	    *PL_markstack_ptr += shift;
	    while (count--)
		*dst-- = *src--;
	}
	/* copy the new items down to the destination list */
	dst = PL_stack_base + (PL_markstack_ptr[-2] += items) - 1;
	while (items-- > 0)
	    *dst-- = SvTEMP(TOPs) ? POPs : sv_mortalcopy(POPs);
    }
    LEAVE;					/* exit inner scope */

    /* All done yet? */
    if (PL_markstack_ptr[-1] > *PL_markstack_ptr) {

	(void)POPMARK;				/* pop top */
	LEAVE;					/* exit outer scope */
	(void)POPMARK;				/* pop src */
	items = --*PL_markstack_ptr - PL_markstack_ptr[-1];
	(void)POPMARK;				/* pop dst */
	SP = PL_stack_base + POPMARK;		/* pop original mark */
	if (gimme == G_SCALAR) {
	    dTARGET;
	    XPUSHi(items);
	}
	else if (gimme == G_ARRAY)
	    SP += items;
	RETURN;
    }
    else {
	SV *src;

	ENTER;					/* enter inner scope */
	SAVEVPTR(PL_curpm);

	/* set $_ to the new source item */
	src = PL_stack_base[PL_markstack_ptr[-1]];
	SvTEMP_off(src);
	DEFSV = src;

	RETURNOP(cLOGOP->op_other);
    }
}

/* Range stuff. */

PP(pp_range)
{
    if (GIMME == G_ARRAY)
	return NORMAL;
    if (SvTRUEx(PAD_SV(PL_op->op_targ)))
	return cLOGOP->op_other;
    else
	return NORMAL;
}

PP(pp_flip)
{
    dSP;

    if (GIMME == G_ARRAY) {
	RETURNOP(((LOGOP*)cUNOP->op_first)->op_other);
    }
    else {
	dTOPss;
	SV *targ = PAD_SV(PL_op->op_targ);
 	int flip = 0;

 	if (PL_op->op_private & OPpFLIP_LINENUM) {
	    if (GvIO(PL_last_in_gv)) {
		flip = SvIV(sv) == (IV)IoLINES(GvIOp(PL_last_in_gv));
	    }
	    else {
		GV *gv = gv_fetchpv(".", TRUE, SVt_PV);
		if (gv && GvSV(gv)) flip = SvIV(sv) == SvIV(GvSV(gv));
	    }
 	} else {
 	    flip = SvTRUE(sv);
 	}
 	if (flip) {
	    sv_setiv(PAD_SV(cUNOP->op_first->op_targ), 1);
	    if (PL_op->op_flags & OPf_SPECIAL) {
		sv_setiv(targ, 1);
		SETs(targ);
		RETURN;
	    }
	    else {
		sv_setiv(targ, 0);
		SP--;
		RETURNOP(((LOGOP*)cUNOP->op_first)->op_other);
	    }
	}
	sv_setpv(TARG, "");
	SETs(targ);
	RETURN;
    }
}

PP(pp_flop)
{
    dSP;

    if (GIMME == G_ARRAY) {
	dPOPPOPssrl;
	register I32 i, j;
	register SV *sv;
	I32 max;

	if (SvGMAGICAL(left))
	    mg_get(left);
	if (SvGMAGICAL(right))
	    mg_get(right);

	/* This code tries to decide if "$left .. $right" should use the
	   magical string increment, or if the range is numeric (we make
	   an exception for .."0" [#18165]). AMS 20021031. */

	if (SvNIOKp(left) || !SvPOKp(left) ||
	    SvNIOKp(right) || !SvPOKp(right) ||
	    (looks_like_number(left) && *SvPVX(left) != '0' &&
	     looks_like_number(right)))
	{
	    if (SvNV(left) < IV_MIN || SvNV(right) > IV_MAX)
		DIE(aTHX_ "Range iterator outside integer range");
	    i = SvIV(left);
	    max = SvIV(right);
	    if (max >= i) {
		j = max - i + 1;
		EXTEND_MORTAL(j);
		EXTEND(SP, j);
	    }
	    else
		j = 0;
	    while (j--) {
		sv = sv_2mortal(newSViv(i++));
		PUSHs(sv);
	    }
	}
	else {
	    SV *final = sv_mortalcopy(right);
	    STRLEN len, n_a;
	    char *tmps = SvPV(final, len);

	    sv = sv_mortalcopy(left);
	    SvPV_force(sv,n_a);
	    while (!SvNIOKp(sv) && SvCUR(sv) <= len) {
		XPUSHs(sv);
	        if (strEQ(SvPVX(sv),tmps))
	            break;
		sv = sv_2mortal(newSVsv(sv));
		sv_inc(sv);
	    }
	}
    }
    else {
	dTOPss;
	SV *targ = PAD_SV(cUNOP->op_first->op_targ);
	int flop = 0;
	sv_inc(targ);

	if (PL_op->op_private & OPpFLIP_LINENUM) {
	    if (GvIO(PL_last_in_gv)) {
		flop = SvIV(sv) == (IV)IoLINES(GvIOp(PL_last_in_gv));
	    }
	    else {
		GV *gv = gv_fetchpv(".", TRUE, SVt_PV);
		if (gv && GvSV(gv)) flop = SvIV(sv) == SvIV(GvSV(gv));
	    }
	}
	else {
	    flop = SvTRUE(sv);
	}

	if (flop) {
	    sv_setiv(PAD_SV(((UNOP*)cUNOP->op_first)->op_first->op_targ), 0);
	    sv_catpv(targ, "E0");
	}
	SETs(targ);
    }

    RETURN;
}

/* Control. */

static char *context_name[] = {
    "pseudo-block",
    "subroutine",
    "eval",
    "loop",
    "substitution",
    "block",
    "format"
};

STATIC I32
S_dopoptolabel(pTHX_ char *label)
{
    register I32 i;
    register PERL_CONTEXT *cx;

    for (i = cxstack_ix; i >= 0; i--) {
	cx = &cxstack[i];
	switch (CxTYPE(cx)) {
	case CXt_SUBST:
	case CXt_SUB:
	case CXt_FORMAT:
	case CXt_EVAL:
	case CXt_NULL:
	    if (ckWARN(WARN_EXITING))
		Perl_warner(aTHX_ packWARN(WARN_EXITING), "Exiting %s via %s",
			context_name[CxTYPE(cx)], OP_NAME(PL_op));
	    if (CxTYPE(cx) == CXt_NULL)
		return -1;
	    break;
	case CXt_LOOP:
	    if (!cx->blk_loop.label ||
	      strNE(label, cx->blk_loop.label) ) {
		DEBUG_l(Perl_deb(aTHX_ "(Skipping label #%ld %s)\n",
			(long)i, cx->blk_loop.label));
		continue;
	    }
	    DEBUG_l( Perl_deb(aTHX_ "(Found label #%ld %s)\n", (long)i, label));
	    return i;
	}
    }
    return i;
}

I32
Perl_dowantarray(pTHX)
{
    I32 gimme = block_gimme();
    return (gimme == G_VOID) ? G_SCALAR : gimme;
}

I32
Perl_block_gimme(pTHX)
{
    I32 cxix;

    cxix = dopoptosub(cxstack_ix);
    if (cxix < 0)
	return G_VOID;

    switch (cxstack[cxix].blk_gimme) {
    case G_VOID:
	return G_VOID;
    case G_SCALAR:
	return G_SCALAR;
    case G_ARRAY:
	return G_ARRAY;
    default:
	Perl_croak(aTHX_ "panic: bad gimme: %d\n", cxstack[cxix].blk_gimme);
	/* NOTREACHED */
	return 0;
    }
}

I32
Perl_is_lvalue_sub(pTHX)
{
    I32 cxix;

    cxix = dopoptosub(cxstack_ix);
    assert(cxix >= 0);  /* We should only be called from inside subs */

    if (cxstack[cxix].blk_sub.lval && CvLVALUE(cxstack[cxix].blk_sub.cv))
	return cxstack[cxix].blk_sub.lval;
    else
	return 0;
}

STATIC I32
S_dopoptosub(pTHX_ I32 startingblock)
{
    return dopoptosub_at(cxstack, startingblock);
}

STATIC I32
S_dopoptosub_at(pTHX_ PERL_CONTEXT *cxstk, I32 startingblock)
{
    I32 i;
    register PERL_CONTEXT *cx;
    for (i = startingblock; i >= 0; i--) {
	cx = &cxstk[i];
	switch (CxTYPE(cx)) {
	default:
	    continue;
	case CXt_EVAL:
	case CXt_SUB:
	case CXt_FORMAT:
	    DEBUG_l( Perl_deb(aTHX_ "(Found sub #%ld)\n", (long)i));
	    return i;
	}
    }
    return i;
}

STATIC I32
S_dopoptoeval(pTHX_ I32 startingblock)
{
    I32 i;
    register PERL_CONTEXT *cx;
    for (i = startingblock; i >= 0; i--) {
	cx = &cxstack[i];
	switch (CxTYPE(cx)) {
	default:
	    continue;
	case CXt_EVAL:
	    DEBUG_l( Perl_deb(aTHX_ "(Found eval #%ld)\n", (long)i));
	    return i;
	}
    }
    return i;
}

STATIC I32
S_dopoptoloop(pTHX_ I32 startingblock)
{
    I32 i;
    register PERL_CONTEXT *cx;
    for (i = startingblock; i >= 0; i--) {
	cx = &cxstack[i];
	switch (CxTYPE(cx)) {
	case CXt_SUBST:
	case CXt_SUB:
	case CXt_FORMAT:
	case CXt_EVAL:
	case CXt_NULL:
	    if (ckWARN(WARN_EXITING))
		Perl_warner(aTHX_ packWARN(WARN_EXITING), "Exiting %s via %s",
			context_name[CxTYPE(cx)], OP_NAME(PL_op));
	    if ((CxTYPE(cx)) == CXt_NULL)
		return -1;
	    break;
	case CXt_LOOP:
	    DEBUG_l( Perl_deb(aTHX_ "(Found loop #%ld)\n", (long)i));
	    return i;
	}
    }
    return i;
}

void
Perl_dounwind(pTHX_ I32 cxix)
{
    register PERL_CONTEXT *cx;
    I32 optype;

    while (cxstack_ix > cxix) {
	SV *sv;
	cx = &cxstack[cxstack_ix];
	DEBUG_l(PerlIO_printf(Perl_debug_log, "Unwinding block %ld, type %s\n",
			      (long) cxstack_ix, PL_block_type[CxTYPE(cx)]));
	/* Note: we don't need to restore the base context info till the end. */
	switch (CxTYPE(cx)) {
	case CXt_SUBST:
	    POPSUBST(cx);
	    continue;  /* not break */
	case CXt_SUB:
	    POPSUB(cx,sv);
	    LEAVESUB(sv);
	    break;
	case CXt_EVAL:
	    POPEVAL(cx);
	    break;
	case CXt_LOOP:
	    POPLOOP(cx);
	    break;
	case CXt_NULL:
	    break;
	case CXt_FORMAT:
	    POPFORMAT(cx);
	    break;
	}
	cxstack_ix--;
    }
}

void
Perl_qerror(pTHX_ SV *err)
{
    if (PL_in_eval)
	sv_catsv(ERRSV, err);
    else if (PL_errors)
	sv_catsv(PL_errors, err);
    else
	Perl_warn(aTHX_ "%"SVf, err);
    ++PL_error_count;
}

OP *
Perl_die_where(pTHX_ char *message, STRLEN msglen)
{
    STRLEN n_a;

    if (PL_in_eval) {
	I32 cxix;
	register PERL_CONTEXT *cx;
	I32 gimme;
	SV **newsp;

	if (message) {
	    if (PL_in_eval & EVAL_KEEPERR) {
		static char prefix[] = "\t(in cleanup) ";
		SV *err = ERRSV;
		char *e = Nullch;
		if (!SvPOK(err))
		    sv_setpv(err,"");
		else if (SvCUR(err) >= sizeof(prefix)+msglen-1) {
		    e = SvPV(err, n_a);
		    e += n_a - msglen;
		    if (*e != *message || strNE(e,message))
			e = Nullch;
		}
		if (!e) {
		    SvGROW(err, SvCUR(err)+sizeof(prefix)+msglen);
		    sv_catpvn(err, prefix, sizeof(prefix)-1);
		    sv_catpvn(err, message, msglen);
		    if (ckWARN(WARN_MISC)) {
			STRLEN start = SvCUR(err)-msglen-sizeof(prefix)+1;
			Perl_warner(aTHX_ packWARN(WARN_MISC), SvPVX(err)+start);
		    }
		}
	    }
	    else {
		sv_setpvn(ERRSV, message, msglen);
	    }
	}

	while ((cxix = dopoptoeval(cxstack_ix)) < 0
	       && PL_curstackinfo->si_prev)
	{
	    dounwind(-1);
	    POPSTACK;
	}

	if (cxix >= 0) {
	    I32 optype;

	    if (cxix < cxstack_ix)
		dounwind(cxix);

	    POPBLOCK(cx,PL_curpm);
	    if (CxTYPE(cx) != CXt_EVAL) {
		if (!message)
		    message = SvPVx(ERRSV, msglen);
		PerlIO_write(Perl_error_log, "panic: die ", 11);
		PerlIO_write(Perl_error_log, message, msglen);
		my_exit(1);
	    }
	    POPEVAL(cx);

	    if (gimme == G_SCALAR)
		*++newsp = &PL_sv_undef;
	    PL_stack_sp = newsp;

	    LEAVE;

	    /* LEAVE could clobber PL_curcop (see save_re_context())
	     * XXX it might be better to find a way to avoid messing with
	     * PL_curcop in save_re_context() instead, but this is a more
	     * minimal fix --GSAR */
	    PL_curcop = cx->blk_oldcop;

	    if (optype == OP_REQUIRE) {
		char* msg = SvPVx(ERRSV, n_a);
		DIE(aTHX_ "%sCompilation failed in require",
		    *msg ? msg : "Unknown error\n");
	    }
	    return pop_return();
	}
    }
    if (!message)
	message = SvPVx(ERRSV, msglen);

    write_to_stderr(message, msglen);
    my_failure_exit();
    /* NOTREACHED */
    return 0;
}

PP(pp_xor)
{
    dSP; dPOPTOPssrl;
    if (SvTRUE(left) != SvTRUE(right))
	RETSETYES;
    else
	RETSETNO;
}

PP(pp_andassign)
{
    dSP;
    if (!SvTRUE(TOPs))
	RETURN;
    else
	RETURNOP(cLOGOP->op_other);
}

PP(pp_orassign)
{
    dSP;
    if (SvTRUE(TOPs))
	RETURN;
    else
	RETURNOP(cLOGOP->op_other);
}
	
PP(pp_caller)
{
    dSP;
    register I32 cxix = dopoptosub(cxstack_ix);
    register PERL_CONTEXT *cx;
    register PERL_CONTEXT *ccstack = cxstack;
    PERL_SI *top_si = PL_curstackinfo;
    I32 dbcxix;
    I32 gimme;
    char *stashname;
    SV *sv;
    I32 count = 0;

    if (MAXARG)
	count = POPi;

    for (;;) {
	/* we may be in a higher stacklevel, so dig down deeper */
	while (cxix < 0 && top_si->si_type != PERLSI_MAIN) {
	    top_si = top_si->si_prev;
	    ccstack = top_si->si_cxstack;
	    cxix = dopoptosub_at(ccstack, top_si->si_cxix);
	}
	if (cxix < 0) {
	    if (GIMME != G_ARRAY) {
		EXTEND(SP, 1);
		RETPUSHUNDEF;
            }
	    RETURN;
	}
	if (PL_DBsub && cxix >= 0 &&
		ccstack[cxix].blk_sub.cv == GvCV(PL_DBsub))
	    count++;
	if (!count--)
	    break;
	cxix = dopoptosub_at(ccstack, cxix - 1);
    }

    cx = &ccstack[cxix];
    if (CxTYPE(cx) == CXt_SUB || CxTYPE(cx) == CXt_FORMAT) {
        dbcxix = dopoptosub_at(ccstack, cxix - 1);
	/* We expect that ccstack[dbcxix] is CXt_SUB, anyway, the
	   field below is defined for any cx. */
	if (PL_DBsub && dbcxix >= 0 && ccstack[dbcxix].blk_sub.cv == GvCV(PL_DBsub))
	    cx = &ccstack[dbcxix];
    }

    stashname = CopSTASHPV(cx->blk_oldcop);
    if (GIMME != G_ARRAY) {
        EXTEND(SP, 1);
	if (!stashname)
	    PUSHs(&PL_sv_undef);
	else {
	    dTARGET;
	    sv_setpv(TARG, stashname);
	    PUSHs(TARG);
	}
	RETURN;
    }

    EXTEND(SP, 10);

    if (!stashname)
	PUSHs(&PL_sv_undef);
    else
	PUSHs(sv_2mortal(newSVpv(stashname, 0)));
    PUSHs(sv_2mortal(newSVpv(OutCopFILE(cx->blk_oldcop), 0)));
    PUSHs(sv_2mortal(newSViv((I32)CopLINE(cx->blk_oldcop))));
    if (!MAXARG)
	RETURN;
    if (CxTYPE(cx) == CXt_SUB || CxTYPE(cx) == CXt_FORMAT) {
	GV *cvgv = CvGV(ccstack[cxix].blk_sub.cv);
	/* So is ccstack[dbcxix]. */
	if (isGV(cvgv)) {
	    sv = NEWSV(49, 0);
	    gv_efullname3(sv, cvgv, Nullch);
	    PUSHs(sv_2mortal(sv));
	    PUSHs(sv_2mortal(newSViv((I32)cx->blk_sub.hasargs)));
	}
	else {
	    PUSHs(sv_2mortal(newSVpvn("(unknown)",9)));
	    PUSHs(sv_2mortal(newSViv((I32)cx->blk_sub.hasargs)));
	}
    }
    else {
	PUSHs(sv_2mortal(newSVpvn("(eval)",6)));
	PUSHs(sv_2mortal(newSViv(0)));
    }
    gimme = (I32)cx->blk_gimme;
    if (gimme == G_VOID)
	PUSHs(&PL_sv_undef);
    else
	PUSHs(sv_2mortal(newSViv(gimme & G_ARRAY)));
    if (CxTYPE(cx) == CXt_EVAL) {
	/* eval STRING */
	if (cx->blk_eval.old_op_type == OP_ENTEREVAL) {
	    PUSHs(cx->blk_eval.cur_text);
	    PUSHs(&PL_sv_no);
	}
	/* require */
	else if (cx->blk_eval.old_namesv) {
	    PUSHs(sv_2mortal(newSVsv(cx->blk_eval.old_namesv)));
	    PUSHs(&PL_sv_yes);
	}
	/* eval BLOCK (try blocks have old_namesv == 0) */
	else {
	    PUSHs(&PL_sv_undef);
	    PUSHs(&PL_sv_undef);
	}
    }
    else {
	PUSHs(&PL_sv_undef);
	PUSHs(&PL_sv_undef);
    }
    if (CxTYPE(cx) == CXt_SUB && cx->blk_sub.hasargs
	&& CopSTASH_eq(PL_curcop, PL_debstash))
    {
	AV *ary = cx->blk_sub.argarray;
	int off = AvARRAY(ary) - AvALLOC(ary);

	if (!PL_dbargs) {
	    GV* tmpgv;
	    PL_dbargs = GvAV(gv_AVadd(tmpgv = gv_fetchpv("DB::args", TRUE,
				SVt_PVAV)));
	    GvMULTI_on(tmpgv);
	    AvREAL_off(PL_dbargs);	/* XXX should be REIFY (see av.h) */
	}

	if (AvMAX(PL_dbargs) < AvFILLp(ary) + off)
	    av_extend(PL_dbargs, AvFILLp(ary) + off);
	Copy(AvALLOC(ary), AvARRAY(PL_dbargs), AvFILLp(ary) + 1 + off, SV*);
	AvFILLp(PL_dbargs) = AvFILLp(ary) + off;
    }
    /* XXX only hints propagated via op_private are currently
     * visible (others are not easily accessible, since they
     * use the global PL_hints) */
    PUSHs(sv_2mortal(newSViv((I32)cx->blk_oldcop->op_private &
			     HINT_PRIVATE_MASK)));
    {
	SV * mask ;
	SV * old_warnings = cx->blk_oldcop->cop_warnings ;

	if  (old_warnings == pWARN_NONE ||
		(old_warnings == pWARN_STD && (PL_dowarn & G_WARN_ON) == 0))
            mask = newSVpvn(WARN_NONEstring, WARNsize) ;
        else if (old_warnings == pWARN_ALL ||
		  (old_warnings == pWARN_STD && PL_dowarn & G_WARN_ON))
            mask = newSVpvn(WARN_ALLstring, WARNsize) ;
        else
            mask = newSVsv(old_warnings);
        PUSHs(sv_2mortal(mask));
    }
    RETURN;
}

PP(pp_reset)
{
    dSP;
    char *tmps;
    STRLEN n_a;

    if (MAXARG < 1)
	tmps = "";
    else
	tmps = POPpx;
    sv_reset(tmps, CopSTASH(PL_curcop));
    PUSHs(&PL_sv_yes);
    RETURN;
}

PP(pp_lineseq)
{
    return NORMAL;
}

/* like pp_nextstate, but used instead when the debugger is active */

PP(pp_dbstate)
{
    PL_curcop = (COP*)PL_op;
    TAINT_NOT;		/* Each statement is presumed innocent */
    PL_stack_sp = PL_stack_base + cxstack[cxstack_ix].blk_oldsp;
    FREETMPS;

    if (PL_op->op_flags & OPf_SPECIAL /* breakpoint */
	    || SvIV(PL_DBsingle) || SvIV(PL_DBsignal) || SvIV(PL_DBtrace))
    {
	dSP;
	register CV *cv;
	register PERL_CONTEXT *cx;
	I32 gimme = G_ARRAY;
	U8 hasargs;
	GV *gv;

	gv = PL_DBgv;
	cv = GvCV(gv);
	if (!cv)
	    DIE(aTHX_ "No DB::DB routine defined");

	if (CvDEPTH(cv) >= 1 && !(PL_debug & DEBUG_DB_RECURSE_FLAG))
	    /* don't do recursive DB::DB call */
	    return NORMAL;

	ENTER;
	SAVETMPS;

	SAVEI32(PL_debug);
	SAVESTACK_POS();
	PL_debug = 0;
	hasargs = 0;
	SPAGAIN;

	push_return(PL_op->op_next);
	PUSHBLOCK(cx, CXt_SUB, SP);
	PUSHSUB_DB(cx);
	CvDEPTH(cv)++;
	(void)SvREFCNT_inc(cv);
	PAD_SET_CUR(CvPADLIST(cv),1);
	RETURNOP(CvSTART(cv));
    }
    else
	return NORMAL;
}

PP(pp_scope)
{
    return NORMAL;
}

PP(pp_enteriter)
{
    dSP; dMARK;
    register PERL_CONTEXT *cx;
    I32 gimme = GIMME_V;
    SV **svp;
    U32 cxtype = CXt_LOOP;
#ifdef USE_ITHREADS
    void *iterdata;
#endif

    ENTER;
    SAVETMPS;

#ifdef USE_5005THREADS
    if (PL_op->op_flags & OPf_SPECIAL) {
	svp = &THREADSV(PL_op->op_targ);	/* per-thread variable */
	SAVEGENERICSV(*svp);
	*svp = NEWSV(0,0);
    }
    else
#endif /* USE_5005THREADS */
    if (PL_op->op_targ) {
#ifndef USE_ITHREADS
	svp = &PAD_SVl(PL_op->op_targ);		/* "my" variable */
	SAVESPTR(*svp);
#else
	SAVEPADSV(PL_op->op_targ);
	iterdata = INT2PTR(void*, PL_op->op_targ);
	cxtype |= CXp_PADVAR;
#endif
    }
    else {
	GV *gv = (GV*)POPs;
	svp = &GvSV(gv);			/* symbol table variable */
	SAVEGENERICSV(*svp);
	*svp = NEWSV(0,0);
#ifdef USE_ITHREADS
	iterdata = (void*)gv;
#endif
    }

    ENTER;

    PUSHBLOCK(cx, cxtype, SP);
#ifdef USE_ITHREADS
    PUSHLOOP(cx, iterdata, MARK);
#else
    PUSHLOOP(cx, svp, MARK);
#endif
    if (PL_op->op_flags & OPf_STACKED) {
	cx->blk_loop.iterary = (AV*)SvREFCNT_inc(POPs);
	if (SvTYPE(cx->blk_loop.iterary) != SVt_PVAV) {
	    dPOPss;
	    /* See comment in pp_flop() */
	    if (SvNIOKp(sv) || !SvPOKp(sv) ||
		SvNIOKp(cx->blk_loop.iterary) || !SvPOKp(cx->blk_loop.iterary) ||
		(looks_like_number(sv) && *SvPVX(sv) != '0' &&
		 looks_like_number((SV*)cx->blk_loop.iterary)))
	    {
		 if (SvNV(sv) < IV_MIN ||
		     SvNV((SV*)cx->blk_loop.iterary) >= IV_MAX)
		     DIE(aTHX_ "Range iterator outside integer range");
		 cx->blk_loop.iterix = SvIV(sv);
		 cx->blk_loop.itermax = SvIV((SV*)cx->blk_loop.iterary);
	    }
	    else
		cx->blk_loop.iterlval = newSVsv(sv);
	}
    }
    else {
	cx->blk_loop.iterary = PL_curstack;
	AvFILLp(PL_curstack) = SP - PL_stack_base;
	cx->blk_loop.iterix = MARK - PL_stack_base;
    }

    RETURN;
}

PP(pp_enterloop)
{
    dSP;
    register PERL_CONTEXT *cx;
    I32 gimme = GIMME_V;

    ENTER;
    SAVETMPS;
    ENTER;

    PUSHBLOCK(cx, CXt_LOOP, SP);
    PUSHLOOP(cx, 0, SP);

    RETURN;
}

PP(pp_leaveloop)
{
    dSP;
    register PERL_CONTEXT *cx;
    I32 gimme;
    SV **newsp;
    PMOP *newpm;
    SV **mark;

    POPBLOCK(cx,newpm);
    mark = newsp;
    newsp = PL_stack_base + cx->blk_loop.resetsp;

    TAINT_NOT;
    if (gimme == G_VOID)
	; /* do nothing */
    else if (gimme == G_SCALAR) {
	if (mark < SP)
	    *++newsp = sv_mortalcopy(*SP);
	else
	    *++newsp = &PL_sv_undef;
    }
    else {
	while (mark < SP) {
	    *++newsp = sv_mortalcopy(*++mark);
	    TAINT_NOT;		/* Each item is independent */
	}
    }
    SP = newsp;
    PUTBACK;

    POPLOOP(cx);	/* Stack values are safe: release loop vars ... */
    PL_curpm = newpm;	/* ... and pop $1 et al */

    LEAVE;
    LEAVE;

    return NORMAL;
}

PP(pp_return)
{
    dSP; dMARK;
    I32 cxix;
    register PERL_CONTEXT *cx;
    bool popsub2 = FALSE;
    bool clear_errsv = FALSE;
    I32 gimme;
    SV **newsp;
    PMOP *newpm;
    I32 optype = 0;
    SV *sv;

    if (PL_curstackinfo->si_type == PERLSI_SORT) {
	if (cxstack_ix == PL_sortcxix
	    || dopoptosub(cxstack_ix) <= PL_sortcxix)
	{
	    if (cxstack_ix > PL_sortcxix)
		dounwind(PL_sortcxix);
	    AvARRAY(PL_curstack)[1] = *SP;
	    PL_stack_sp = PL_stack_base + 1;
	    return 0;
	}
    }

    cxix = dopoptosub(cxstack_ix);
    if (cxix < 0)
	DIE(aTHX_ "Can't return outside a subroutine");
    if (cxix < cxstack_ix)
	dounwind(cxix);

    POPBLOCK(cx,newpm);
    switch (CxTYPE(cx)) {
    case CXt_SUB:
	popsub2 = TRUE;
	break;
    case CXt_EVAL:
	if (!(PL_in_eval & EVAL_KEEPERR))
	    clear_errsv = TRUE;
	POPEVAL(cx);
	if (CxTRYBLOCK(cx))
	    break;
	lex_end();
	if (optype == OP_REQUIRE &&
	    (MARK == SP || (gimme == G_SCALAR && !SvTRUE(*SP))) )
	{
	    /* Unassume the success we assumed earlier. */
	    SV *nsv = cx->blk_eval.old_namesv;
	    (void)hv_delete(GvHVn(PL_incgv), SvPVX(nsv), SvCUR(nsv), G_DISCARD);
	    DIE(aTHX_ "%"SVf" did not return a true value", nsv);
	}
	break;
    case CXt_FORMAT:
	POPFORMAT(cx);
	break;
    default:
	DIE(aTHX_ "panic: return");
    }

    TAINT_NOT;
    if (gimme == G_SCALAR) {
	if (MARK < SP) {
	    if (popsub2) {
		if (cx->blk_sub.cv && CvDEPTH(cx->blk_sub.cv) > 1) {
		    if (SvTEMP(TOPs)) {
			*++newsp = SvREFCNT_inc(*SP);
			FREETMPS;
			sv_2mortal(*newsp);
		    }
		    else {
			sv = SvREFCNT_inc(*SP);	/* FREETMPS could clobber it */
			FREETMPS;
			*++newsp = sv_mortalcopy(sv);
			SvREFCNT_dec(sv);
		    }
		}
		else
		    *++newsp = (SvTEMP(*SP)) ? *SP : sv_mortalcopy(*SP);
	    }
	    else
		*++newsp = sv_mortalcopy(*SP);
	}
	else
	    *++newsp = &PL_sv_undef;
    }
    else if (gimme == G_ARRAY) {
	while (++MARK <= SP) {
	    *++newsp = (popsub2 && SvTEMP(*MARK))
			? *MARK : sv_mortalcopy(*MARK);
	    TAINT_NOT;		/* Each item is independent */
	}
    }
    PL_stack_sp = newsp;

    /* Stack values are safe: */
    if (popsub2) {
	POPSUB(cx,sv);	/* release CV and @_ ... */
    }
    else
	sv = Nullsv;
    PL_curpm = newpm;	/* ... and pop $1 et al */

    LEAVE;
    LEAVESUB(sv);
    if (clear_errsv)
	sv_setpv(ERRSV,"");
    return pop_return();
}

PP(pp_last)
{
    dSP;
    I32 cxix;
    register PERL_CONTEXT *cx;
    I32 pop2 = 0;
    I32 gimme;
    I32 optype;
    OP *nextop;
    SV **newsp;
    PMOP *newpm;
    SV **mark;
    SV *sv = Nullsv;

    if (PL_op->op_flags & OPf_SPECIAL) {
	cxix = dopoptoloop(cxstack_ix);
	if (cxix < 0)
	    DIE(aTHX_ "Can't \"last\" outside a loop block");
    }
    else {
	cxix = dopoptolabel(cPVOP->op_pv);
	if (cxix < 0)
	    DIE(aTHX_ "Label not found for \"last %s\"", cPVOP->op_pv);
    }
    if (cxix < cxstack_ix)
	dounwind(cxix);

    POPBLOCK(cx,newpm);
    mark = newsp;
    switch (CxTYPE(cx)) {
    case CXt_LOOP:
	pop2 = CXt_LOOP;
	newsp = PL_stack_base + cx->blk_loop.resetsp;
	nextop = cx->blk_loop.last_op->op_next;
	break;
    case CXt_SUB:
	pop2 = CXt_SUB;
	nextop = pop_return();
	break;
    case CXt_EVAL:
	POPEVAL(cx);
	nextop = pop_return();
	break;
    case CXt_FORMAT:
	POPFORMAT(cx);
	nextop = pop_return();
	break;
    default:
	DIE(aTHX_ "panic: last");
    }

    TAINT_NOT;
    if (gimme == G_SCALAR) {
	if (MARK < SP)
	    *++newsp = ((pop2 == CXt_SUB) && SvTEMP(*SP))
			? *SP : sv_mortalcopy(*SP);
	else
	    *++newsp = &PL_sv_undef;
    }
    else if (gimme == G_ARRAY) {
	while (++MARK <= SP) {
	    *++newsp = ((pop2 == CXt_SUB) && SvTEMP(*MARK))
			? *MARK : sv_mortalcopy(*MARK);
	    TAINT_NOT;		/* Each item is independent */
	}
    }
    SP = newsp;
    PUTBACK;

    /* Stack values are safe: */
    switch (pop2) {
    case CXt_LOOP:
	POPLOOP(cx);	/* release loop vars ... */
	LEAVE;
	break;
    case CXt_SUB:
	POPSUB(cx,sv);	/* release CV and @_ ... */
	break;
    }
    PL_curpm = newpm;	/* ... and pop $1 et al */

    LEAVE;
    LEAVESUB(sv);
    return nextop;
}

PP(pp_next)
{
    I32 cxix;
    register PERL_CONTEXT *cx;
    I32 inner;

    if (PL_op->op_flags & OPf_SPECIAL) {
	cxix = dopoptoloop(cxstack_ix);
	if (cxix < 0)
	    DIE(aTHX_ "Can't \"next\" outside a loop block");
    }
    else {
	cxix = dopoptolabel(cPVOP->op_pv);
	if (cxix < 0)
	    DIE(aTHX_ "Label not found for \"next %s\"", cPVOP->op_pv);
    }
    if (cxix < cxstack_ix)
	dounwind(cxix);

    /* clear off anything above the scope we're re-entering, but
     * save the rest until after a possible continue block */
    inner = PL_scopestack_ix;
    TOPBLOCK(cx);
    if (PL_scopestack_ix < inner)
	leave_scope(PL_scopestack[PL_scopestack_ix]);
    return cx->blk_loop.next_op;
}

PP(pp_redo)
{
    I32 cxix;
    register PERL_CONTEXT *cx;
    I32 oldsave;

    if (PL_op->op_flags & OPf_SPECIAL) {
	cxix = dopoptoloop(cxstack_ix);
	if (cxix < 0)
	    DIE(aTHX_ "Can't \"redo\" outside a loop block");
    }
    else {
	cxix = dopoptolabel(cPVOP->op_pv);
	if (cxix < 0)
	    DIE(aTHX_ "Label not found for \"redo %s\"", cPVOP->op_pv);
    }
    if (cxix < cxstack_ix)
	dounwind(cxix);

    TOPBLOCK(cx);
    oldsave = PL_scopestack[PL_scopestack_ix - 1];
    LEAVE_SCOPE(oldsave);
    return cx->blk_loop.redo_op;
}

STATIC OP *
S_dofindlabel(pTHX_ OP *o, char *label, OP **opstack, OP **oplimit)
{
    OP *kid = Nullop;
    OP **ops = opstack;
    static char too_deep[] = "Target of goto is too deeply nested";

    if (ops >= oplimit)
	Perl_croak(aTHX_ too_deep);
    if (o->op_type == OP_LEAVE ||
	o->op_type == OP_SCOPE ||
	o->op_type == OP_LEAVELOOP ||
	o->op_type == OP_LEAVESUB ||
	o->op_type == OP_LEAVETRY)
    {
	*ops++ = cUNOPo->op_first;
	if (ops >= oplimit)
	    Perl_croak(aTHX_ too_deep);
    }
    *ops = 0;
    if (o->op_flags & OPf_KIDS) {
	/* First try all the kids at this level, since that's likeliest. */
	for (kid = cUNOPo->op_first; kid; kid = kid->op_sibling) {
	    if ((kid->op_type == OP_NEXTSTATE || kid->op_type == OP_DBSTATE) &&
		    kCOP->cop_label && strEQ(kCOP->cop_label, label))
		return kid;
	}
	for (kid = cUNOPo->op_first; kid; kid = kid->op_sibling) {
	    if (kid == PL_lastgotoprobe)
		continue;
	    if (kid->op_type == OP_NEXTSTATE || kid->op_type == OP_DBSTATE) {
	        if (ops == opstack)
		    *ops++ = kid;
		else if (ops[-1]->op_type == OP_NEXTSTATE ||
		         ops[-1]->op_type == OP_DBSTATE)
		    ops[-1] = kid;
		else
		    *ops++ = kid;
	    }
	    if ((o = dofindlabel(kid, label, ops, oplimit)))
		return o;
	}
    }
    *ops = 0;
    return 0;
}

PP(pp_dump)
{
    return pp_goto();
    /*NOTREACHED*/
}

PP(pp_goto)
{
    dSP;
    OP *retop = 0;
    I32 ix;
    register PERL_CONTEXT *cx;
#define GOTO_DEPTH 64
    OP *enterops[GOTO_DEPTH];
    char *label;
    int do_dump = (PL_op->op_type == OP_DUMP);
    static char must_have_label[] = "goto must have label";

    label = 0;
    if (PL_op->op_flags & OPf_STACKED) {
	SV *sv = POPs;
	STRLEN n_a;

	/* This egregious kludge implements goto &subroutine */
	if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVCV) {
	    I32 cxix;
	    register PERL_CONTEXT *cx;
	    CV* cv = (CV*)SvRV(sv);
	    SV** mark;
	    I32 items = 0;
	    I32 oldsave;

	retry:
	    if (!CvROOT(cv) && !CvXSUB(cv)) {
		GV *gv = CvGV(cv);
		GV *autogv;
		if (gv) {
		    SV *tmpstr;
		    /* autoloaded stub? */
		    if (cv != GvCV(gv) && (cv = GvCV(gv)))
			goto retry;
		    autogv = gv_autoload4(GvSTASH(gv), GvNAME(gv),
					  GvNAMELEN(gv), FALSE);
		    if (autogv && (cv = GvCV(autogv)))
			goto retry;
		    tmpstr = sv_newmortal();
		    gv_efullname3(tmpstr, gv, Nullch);
		    DIE(aTHX_ "Goto undefined subroutine &%"SVf"",tmpstr);
		}
		DIE(aTHX_ "Goto undefined subroutine");
	    }

	    /* First do some returnish stuff. */
	    SvREFCNT_inc(cv); /* avoid premature free during unwind */
	    FREETMPS;
	    cxix = dopoptosub(cxstack_ix);
	    if (cxix < 0)
		DIE(aTHX_ "Can't goto subroutine outside a subroutine");
	    if (cxix < cxstack_ix)
		dounwind(cxix);
	    TOPBLOCK(cx);
	    if (CxREALEVAL(cx))
		DIE(aTHX_ "Can't goto subroutine from an eval-string");
	    mark = PL_stack_sp;
	    if (CxTYPE(cx) == CXt_SUB && cx->blk_sub.hasargs) {
		/* put @_ back onto stack */
		AV* av = cx->blk_sub.argarray;
		
		items = AvFILLp(av) + 1;
		PL_stack_sp++;
		EXTEND(PL_stack_sp, items); /* @_ could have been extended. */
		Copy(AvARRAY(av), PL_stack_sp, items, SV*);
		PL_stack_sp += items;
#ifndef USE_5005THREADS
		SvREFCNT_dec(GvAV(PL_defgv));
		GvAV(PL_defgv) = cx->blk_sub.savearray;
#endif /* USE_5005THREADS */
		/* abandon @_ if it got reified */
		if (AvREAL(av)) {
		    (void)sv_2mortal((SV*)av);	/* delay until return */
		    av = newAV();
		    av_extend(av, items-1);
		    AvFLAGS(av) = AVf_REIFY;
		    PAD_SVl(0) = (SV*)(cx->blk_sub.argarray = av);
		}
		else
		    CLEAR_ARGARRAY(av);
	    }
	    else if (CvXSUB(cv)) {	/* put GvAV(defgv) back onto stack */
		AV* av;
#ifdef USE_5005THREADS
		av = (AV*)PAD_SVl(0);
#else
		av = GvAV(PL_defgv);
#endif
		items = AvFILLp(av) + 1;
		PL_stack_sp++;
		EXTEND(PL_stack_sp, items); /* @_ could have been extended. */
		Copy(AvARRAY(av), PL_stack_sp, items, SV*);
		PL_stack_sp += items;
	    }
	    if (CxTYPE(cx) == CXt_SUB &&
		!(CvDEPTH(cx->blk_sub.cv) = cx->blk_sub.olddepth))
		SvREFCNT_dec(cx->blk_sub.cv);
	    oldsave = PL_scopestack[PL_scopestack_ix - 1];
	    LEAVE_SCOPE(oldsave);

	    /* Now do some callish stuff. */
	    SAVETMPS;
	    SAVEFREESV(cv); /* later, undo the 'avoid premature free' hack */
	    if (CvXSUB(cv)) {
#ifdef PERL_XSUB_OLDSTYLE
		if (CvOLDSTYLE(cv)) {
		    I32 (*fp3)(int,int,int);
		    while (SP > mark) {
			SP[1] = SP[0];
			SP--;
		    }
		    fp3 = (I32(*)(int,int,int))CvXSUB(cv);
		    items = (*fp3)(CvXSUBANY(cv).any_i32,
		                   mark - PL_stack_base + 1,
				   items);
		    SP = PL_stack_base + items;
		}
		else
#endif /* PERL_XSUB_OLDSTYLE */
		{
		    SV **newsp;
		    I32 gimme;

		    PL_stack_sp--;		/* There is no cv arg. */
		    /* Push a mark for the start of arglist */
		    PUSHMARK(mark);
		    (void)(*CvXSUB(cv))(aTHX_ cv);
		    /* Pop the current context like a decent sub should */
		    POPBLOCK(cx, PL_curpm);
		    /* Do _not_ use PUTBACK, keep the XSUB's return stack! */
		}
		LEAVE;
		return pop_return();
	    }
	    else {
		AV* padlist = CvPADLIST(cv);
		if (CxTYPE(cx) == CXt_EVAL) {
		    PL_in_eval = cx->blk_eval.old_in_eval;
		    PL_eval_root = cx->blk_eval.old_eval_root;
		    cx->cx_type = CXt_SUB;
		    cx->blk_sub.hasargs = 0;
		}
		cx->blk_sub.cv = cv;
		cx->blk_sub.olddepth = (U16)CvDEPTH(cv);

		CvDEPTH(cv)++;
		if (CvDEPTH(cv) < 2)
		    (void)SvREFCNT_inc(cv);
		else {
		    if (CvDEPTH(cv) == 100 && ckWARN(WARN_RECURSION))
			sub_crush_depth(cv);
		    pad_push(padlist, CvDEPTH(cv), cx->blk_sub.hasargs);
		}
#ifdef USE_5005THREADS
		if (!cx->blk_sub.hasargs) {
		    AV* av = (AV*)PAD_SVl(0);
		
		    items = AvFILLp(av) + 1;
		    if (items) {
			/* Mark is at the end of the stack. */
			EXTEND(SP, items);
			Copy(AvARRAY(av), SP + 1, items, SV*);
			SP += items;
			PUTBACK ;		
		    }
		}
#endif /* USE_5005THREADS */
		PAD_SET_CUR(padlist, CvDEPTH(cv));
#ifndef USE_5005THREADS
		if (cx->blk_sub.hasargs)
#endif /* USE_5005THREADS */
		{
		    AV* av = (AV*)PAD_SVl(0);
		    SV** ary;

#ifndef USE_5005THREADS
		    cx->blk_sub.savearray = GvAV(PL_defgv);
		    GvAV(PL_defgv) = (AV*)SvREFCNT_inc(av);
#endif /* USE_5005THREADS */
		    CX_CURPAD_SAVE(cx->blk_sub);
		    cx->blk_sub.argarray = av;
		    ++mark;

		    if (items >= AvMAX(av) + 1) {
			ary = AvALLOC(av);
			if (AvARRAY(av) != ary) {
			    AvMAX(av) += AvARRAY(av) - AvALLOC(av);
			    SvPVX(av) = (char*)ary;
			}
			if (items >= AvMAX(av) + 1) {
			    AvMAX(av) = items - 1;
			    Renew(ary,items+1,SV*);
			    AvALLOC(av) = ary;
			    SvPVX(av) = (char*)ary;
			}
		    }
		    Copy(mark,AvARRAY(av),items,SV*);
		    AvFILLp(av) = items - 1;
		    assert(!AvREAL(av));
		    while (items--) {
			if (*mark)
			    SvTEMP_off(*mark);
			mark++;
		    }
		}
		if (PERLDB_SUB) {	/* Checking curstash breaks DProf. */
		    /*
		     * We do not care about using sv to call CV;
		     * it's for informational purposes only.
		     */
		    SV *sv = GvSV(PL_DBsub);
		    CV *gotocv;
		
		    if (PERLDB_SUB_NN) {
			(void)SvUPGRADE(sv, SVt_PVIV);
			(void)SvIOK_on(sv);
			SAVEIV(SvIVX(sv));
			SvIVX(sv) = PTR2IV(cv); /* Do it the quickest way */
		    } else {
			save_item(sv);
			gv_efullname3(sv, CvGV(cv), Nullch);
		    }
		    if (  PERLDB_GOTO
			  && (gotocv = get_cv("DB::goto", FALSE)) ) {
			PUSHMARK( PL_stack_sp );
			call_sv((SV*)gotocv, G_SCALAR | G_NODEBUG);
			PL_stack_sp--;
		    }
		}
		RETURNOP(CvSTART(cv));
	    }
	}
	else {
	    label = SvPV(sv,n_a);
	    if (!(do_dump || *label))
		DIE(aTHX_ must_have_label);
	}
    }
    else if (PL_op->op_flags & OPf_SPECIAL) {
	if (! do_dump)
	    DIE(aTHX_ must_have_label);
    }
    else
	label = cPVOP->op_pv;

    if (label && *label) {
	OP *gotoprobe = 0;
	bool leaving_eval = FALSE;
	bool in_block = FALSE;
        PERL_CONTEXT *last_eval_cx = 0;

	/* find label */

	PL_lastgotoprobe = 0;
	*enterops = 0;
	for (ix = cxstack_ix; ix >= 0; ix--) {
	    cx = &cxstack[ix];
	    switch (CxTYPE(cx)) {
	    case CXt_EVAL:
		leaving_eval = TRUE;
                if (!CxTRYBLOCK(cx)) {
		    gotoprobe = (last_eval_cx ?
				last_eval_cx->blk_eval.old_eval_root :
				PL_eval_root);
		    last_eval_cx = cx;
		    break;
                }
                /* else fall through */
	    case CXt_LOOP:
		gotoprobe = cx->blk_oldcop->op_sibling;
		break;
	    case CXt_SUBST:
		continue;
	    case CXt_BLOCK:
		if (ix) {
		    gotoprobe = cx->blk_oldcop->op_sibling;
		    in_block = TRUE;
		} else
		    gotoprobe = PL_main_root;
		break;
	    case CXt_SUB:
		if (CvDEPTH(cx->blk_sub.cv)) {
		    gotoprobe = CvROOT(cx->blk_sub.cv);
		    break;
		}
		/* FALL THROUGH */
	    case CXt_FORMAT:
	    case CXt_NULL:
		DIE(aTHX_ "Can't \"goto\" out of a pseudo block");
	    default:
		if (ix)
		    DIE(aTHX_ "panic: goto");
		gotoprobe = PL_main_root;
		break;
	    }
	    if (gotoprobe) {
		retop = dofindlabel(gotoprobe, label,
				    enterops, enterops + GOTO_DEPTH);
		if (retop)
		    break;
	    }
	    PL_lastgotoprobe = gotoprobe;
	}
	if (!retop)
	    DIE(aTHX_ "Can't find label %s", label);

	/* if we're leaving an eval, check before we pop any frames
           that we're not going to punt, otherwise the error
	   won't be caught */

	if (leaving_eval && *enterops && enterops[1]) {
	    I32 i;
            for (i = 1; enterops[i]; i++)
                if (enterops[i]->op_type == OP_ENTERITER)
                    DIE(aTHX_ "Can't \"goto\" into the middle of a foreach loop");
	}

	/* pop unwanted frames */

	if (ix < cxstack_ix) {
	    I32 oldsave;

	    if (ix < 0)
		ix = 0;
	    dounwind(ix);
	    TOPBLOCK(cx);
	    oldsave = PL_scopestack[PL_scopestack_ix];
	    LEAVE_SCOPE(oldsave);
	}

	/* push wanted frames */

	if (*enterops && enterops[1]) {
	    OP *oldop = PL_op;
	    ix = enterops[1]->op_type == OP_ENTER && in_block ? 2 : 1;
	    for (; enterops[ix]; ix++) {
		PL_op = enterops[ix];
		/* Eventually we may want to stack the needed arguments
		 * for each op.  For now, we punt on the hard ones. */
		if (PL_op->op_type == OP_ENTERITER)
		    DIE(aTHX_ "Can't \"goto\" into the middle of a foreach loop");
		CALL_FPTR(PL_op->op_ppaddr)(aTHX);
	    }
	    PL_op = oldop;
	}
    }

    if (do_dump) {
#ifdef VMS
	if (!retop) retop = PL_main_start;
#endif
	PL_restartop = retop;
	PL_do_undump = TRUE;

	my_unexec();

	PL_restartop = 0;		/* hmm, must be GNU unexec().. */
	PL_do_undump = FALSE;
    }

    RETURNOP(retop);
}

PP(pp_exit)
{
    dSP;
    I32 anum;

    if (MAXARG < 1)
	anum = 0;
    else {
	anum = SvIVx(POPs);
#ifdef VMS
        if (anum == 1 && (PL_op->op_private & OPpEXIT_VMSISH))
	    anum = 0;
        VMSISH_HUSHED  = VMSISH_HUSHED || (PL_op->op_private & OPpHUSH_VMSISH);
#endif
    }
    PL_exit_flags |= PERL_EXIT_EXPECTED;
    my_exit(anum);
    PUSHs(&PL_sv_undef);
    RETURN;
}

#ifdef NOTYET
PP(pp_nswitch)
{
    dSP;
    NV value = SvNVx(GvSV(cCOP->cop_gv));
    register I32 match = I_32(value);

    if (value < 0.0) {
	if (((NV)match) > value)
	    --match;		/* was fractional--truncate other way */
    }
    match -= cCOP->uop.scop.scop_offset;
    if (match < 0)
	match = 0;
    else if (match > cCOP->uop.scop.scop_max)
	match = cCOP->uop.scop.scop_max;
    PL_op = cCOP->uop.scop.scop_next[match];
    RETURNOP(PL_op);
}

PP(pp_cswitch)
{
    dSP;
    register I32 match;

    if (PL_multiline)
	PL_op = PL_op->op_next;			/* can't assume anything */
    else {
	STRLEN n_a;
	match = *(SvPVx(GvSV(cCOP->cop_gv), n_a)) & 255;
	match -= cCOP->uop.scop.scop_offset;
	if (match < 0)
	    match = 0;
	else if (match > cCOP->uop.scop.scop_max)
	    match = cCOP->uop.scop.scop_max;
	PL_op = cCOP->uop.scop.scop_next[match];
    }
    RETURNOP(PL_op);
}
#endif

/* Eval. */

STATIC void
S_save_lines(pTHX_ AV *array, SV *sv)
{
    register char *s = SvPVX(sv);
    register char *send = SvPVX(sv) + SvCUR(sv);
    register char *t;
    register I32 line = 1;

    while (s && s < send) {
	SV *tmpstr = NEWSV(85,0);

	sv_upgrade(tmpstr, SVt_PVMG);
	t = strchr(s, '\n');
	if (t)
	    t++;
	else
	    t = send;

	sv_setpvn(tmpstr, s, t - s);
	av_store(array, line++, tmpstr);
	s = t;
    }
}

#ifdef PERL_FLEXIBLE_EXCEPTIONS
STATIC void *
S_docatch_body(pTHX_ va_list args)
{
    return docatch_body();
}
#endif

STATIC void *
S_docatch_body(pTHX)
{
    CALLRUNOPS(aTHX);
    return NULL;
}

STATIC OP *
S_docatch(pTHX_ OP *o)
{
    int ret;
    OP *oldop = PL_op;
    OP *retop;
    volatile PERL_SI *cursi = PL_curstackinfo;
    dJMPENV;

#ifdef DEBUGGING
    assert(CATCH_GET == TRUE);
#endif
    PL_op = o;

    /* Normally, the leavetry at the end of this block of ops will
     * pop an op off the return stack and continue there. By setting
     * the op to Nullop, we force an exit from the inner runops()
     * loop. DAPM.
     */
    retop = pop_return();
    push_return(Nullop);

#ifdef PERL_FLEXIBLE_EXCEPTIONS
 redo_body:
    CALLPROTECT(aTHX_ pcur_env, &ret, MEMBER_TO_FPTR(S_docatch_body));
#else
    JMPENV_PUSH(ret);
#endif
    switch (ret) {
    case 0:
#ifndef PERL_FLEXIBLE_EXCEPTIONS
 redo_body:
	docatch_body();
#endif
	break;
    case 3:
	/* die caught by an inner eval - continue inner loop */
	if (PL_restartop && cursi == PL_curstackinfo) {
	    PL_op = PL_restartop;
	    PL_restartop = 0;
	    goto redo_body;
	}
	/* a die in this eval - continue in outer loop */
	if (!PL_restartop)
	    break;
	/* FALL THROUGH */
    default:
	JMPENV_POP;
	PL_op = oldop;
	JMPENV_JUMP(ret);
	/* NOTREACHED */
    }
    JMPENV_POP;
    PL_op = oldop;
    return retop;
}

OP *
Perl_sv_compile_2op(pTHX_ SV *sv, OP** startop, char *code, PAD** padp)
/* sv Text to convert to OP tree. */
/* startop op_free() this to undo. */
/* code Short string id of the caller. */
{
    dSP;				/* Make POPBLOCK work. */
    PERL_CONTEXT *cx;
    SV **newsp;
    I32 gimme = 0;   /* SUSPECT - INITIALZE TO WHAT?  NI-S */
    I32 optype;
    OP dummy;
    OP *rop;
    char tbuf[TYPE_DIGITS(long) + 12 + 10];
    char *tmpbuf = tbuf;
    char *safestr;
    int runtime;
    CV* runcv = Nullcv;	/* initialise to avoid compiler warnings */

    ENTER;
    lex_start(sv);
    SAVETMPS;
    /* switch to eval mode */

    if (IN_PERL_COMPILETIME) {
	SAVECOPSTASH_FREE(&PL_compiling);
	CopSTASH_set(&PL_compiling, PL_curstash);
    }
    if (PERLDB_NAMEEVAL && CopLINE(PL_curcop)) {
	SV *sv = sv_newmortal();
	Perl_sv_setpvf(aTHX_ sv, "_<(%.10seval %lu)[%s:%"IVdf"]",
		       code, (unsigned long)++PL_evalseq,
		       CopFILE(PL_curcop), (IV)CopLINE(PL_curcop));
	tmpbuf = SvPVX(sv);
    }
    else
	sprintf(tmpbuf, "_<(%.10s_eval %lu)", code, (unsigned long)++PL_evalseq);
    SAVECOPFILE_FREE(&PL_compiling);
    CopFILE_set(&PL_compiling, tmpbuf+2);
    SAVECOPLINE(&PL_compiling);
    CopLINE_set(&PL_compiling, 1);
    /* XXX For C<eval "...">s within BEGIN {} blocks, this ends up
       deleting the eval's FILEGV from the stash before gv_check() runs
       (i.e. before run-time proper). To work around the coredump that
       ensues, we always turn GvMULTI_on for any globals that were
       introduced within evals. See force_ident(). GSAR 96-10-12 */
    safestr = savepv(tmpbuf);
    SAVEDELETE(PL_defstash, safestr, strlen(safestr));
    SAVEHINTS();
#ifdef OP_IN_REGISTER
    PL_opsave = op;
#else
    SAVEVPTR(PL_op);
#endif
    PL_hints &= HINT_UTF8;

    /* we get here either during compilation, or via pp_regcomp at runtime */
    runtime = IN_PERL_RUNTIME;
    if (runtime)
	runcv = find_runcv(NULL);

    PL_op = &dummy;
    PL_op->op_type = OP_ENTEREVAL;
    PL_op->op_flags = 0;			/* Avoid uninit warning. */
    PUSHBLOCK(cx, CXt_EVAL|(IN_PERL_COMPILETIME ? 0 : CXp_REAL), SP);
    PUSHEVAL(cx, 0, Nullgv);

    if (runtime)
	rop = doeval(G_SCALAR, startop, runcv, PL_curcop->cop_seq);
    else
	rop = doeval(G_SCALAR, startop, PL_compcv, PL_cop_seqmax);
    POPBLOCK(cx,PL_curpm);
    POPEVAL(cx);

    (*startop)->op_type = OP_NULL;
    (*startop)->op_ppaddr = PL_ppaddr[OP_NULL];
    lex_end();
    /* XXX DAPM do this properly one year */
    *padp = (AV*)SvREFCNT_inc(PL_comppad);
    LEAVE;
    if (IN_PERL_COMPILETIME)
	PL_compiling.op_private = (U8)(PL_hints & HINT_PRIVATE_MASK);
#ifdef OP_IN_REGISTER
    op = PL_opsave;
#endif
    return rop;
}


/*
=for apidoc find_runcv

Locate the CV corresponding to the currently executing sub or eval.
If db_seqp is non_null, skip CVs that are in the DB package and populate
*db_seqp with the cop sequence number at the point that the DB:: code was
entered. (allows debuggers to eval in the scope of the breakpoint rather
than in in the scope of the debuger itself).

=cut
*/

CV*
Perl_find_runcv(pTHX_ U32 *db_seqp)
{
    I32		 ix;
    PERL_SI	 *si;
    PERL_CONTEXT *cx;

    if (db_seqp)
	*db_seqp = PL_curcop->cop_seq;
    for (si = PL_curstackinfo; si; si = si->si_prev) {
	for (ix = si->si_cxix; ix >= 0; ix--) {
	    cx = &(si->si_cxstack[ix]);
	    if (CxTYPE(cx) == CXt_SUB || CxTYPE(cx) == CXt_FORMAT) {
		CV *cv = cx->blk_sub.cv;
		/* skip DB:: code */
		if (db_seqp && PL_debstash && CvSTASH(cv) == PL_debstash) {
		    *db_seqp = cx->blk_oldcop->cop_seq;
		    continue;
		}
		return cv;
	    }
	    else if (CxTYPE(cx) == CXt_EVAL && !CxTRYBLOCK(cx))
		return PL_compcv;
	}
    }
    return PL_main_cv;
}


/* Compile a require/do, an eval '', or a /(?{...})/.
 * In the last case, startop is non-null, and contains the address of
 * a pointer that should be set to the just-compiled code.
 * outside is the lexically enclosing CV (if any) that invoked us.
 */

/* With USE_5005THREADS, eval_owner must be held on entry to doeval */
STATIC OP *
S_doeval(pTHX_ int gimme, OP** startop, CV* outside, U32 seq)
{
    dSP;
    OP *saveop = PL_op;

    PL_in_eval = ((saveop && saveop->op_type == OP_REQUIRE)
		  ? (EVAL_INREQUIRE | (PL_in_eval & EVAL_INEVAL))
		  : EVAL_INEVAL);

    PUSHMARK(SP);

    SAVESPTR(PL_compcv);
    PL_compcv = (CV*)NEWSV(1104,0);
    sv_upgrade((SV *)PL_compcv, SVt_PVCV);
    CvEVAL_on(PL_compcv);
    assert(CxTYPE(&cxstack[cxstack_ix]) == CXt_EVAL);
    cxstack[cxstack_ix].blk_eval.cv = PL_compcv;

#ifdef USE_5005THREADS
    CvOWNER(PL_compcv) = 0;
    New(666, CvMUTEXP(PL_compcv), 1, perl_mutex);
    MUTEX_INIT(CvMUTEXP(PL_compcv));
#endif /* USE_5005THREADS */

    CvOUTSIDE_SEQ(PL_compcv) = seq;
    CvOUTSIDE(PL_compcv) = (CV*)SvREFCNT_inc(outside);

    /* set up a scratch pad */

    CvPADLIST(PL_compcv) = pad_new(padnew_SAVE);


    SAVEMORTALIZESV(PL_compcv);	/* must remain until end of current statement */

    /* make sure we compile in the right package */

    if (CopSTASH_ne(PL_curcop, PL_curstash)) {
	SAVESPTR(PL_curstash);
	PL_curstash = CopSTASH(PL_curcop);
    }
    SAVESPTR(PL_beginav);
    PL_beginav = newAV();
    SAVEFREESV(PL_beginav);
    SAVEI32(PL_error_count);

    /* try to compile it */

    PL_eval_root = Nullop;
    PL_error_count = 0;
    PL_curcop = &PL_compiling;
    PL_curcop->cop_arybase = 0;
    if (saveop && saveop->op_flags & OPf_SPECIAL)
	PL_in_eval |= EVAL_KEEPERR;
    else
	sv_setpv(ERRSV,"");
    if (yyparse() || PL_error_count || !PL_eval_root) {
	SV **newsp;			/* Used by POPBLOCK. */
	PERL_CONTEXT *cx;
	I32 optype = 0;			/* Might be reset by POPEVAL. */
	STRLEN n_a;
	
	PL_op = saveop;
	if (PL_eval_root) {
	    op_free(PL_eval_root);
	    PL_eval_root = Nullop;
	}
	SP = PL_stack_base + POPMARK;		/* pop original mark */
	if (!startop) {
	    POPBLOCK(cx,PL_curpm);
	    POPEVAL(cx);
	    pop_return();
	}
	lex_end();
	LEAVE;
	if (optype == OP_REQUIRE) {
	    char* msg = SvPVx(ERRSV, n_a);
	    DIE(aTHX_ "%sCompilation failed in require",
		*msg ? msg : "Unknown error\n");
	}
	else if (startop) {
	    char* msg = SvPVx(ERRSV, n_a);

	    POPBLOCK(cx,PL_curpm);
	    POPEVAL(cx);
	    Perl_croak(aTHX_ "%sCompilation failed in regexp",
		       (*msg ? msg : "Unknown error\n"));
	}
	else {
	    char* msg = SvPVx(ERRSV, n_a);
	    if (!*msg) {
	        sv_setpv(ERRSV, "Compilation error");
	    }
	}
#ifdef USE_5005THREADS
	MUTEX_LOCK(&PL_eval_mutex);
	PL_eval_owner = 0;
	COND_SIGNAL(&PL_eval_cond);
	MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_5005THREADS */
	RETPUSHUNDEF;
    }
    CopLINE_set(&PL_compiling, 0);
    if (startop) {
	*startop = PL_eval_root;
    } else
	SAVEFREEOP(PL_eval_root);

    /* Set the context for this new optree.
     * If the last op is an OP_REQUIRE, force scalar context.
     * Otherwise, propagate the context from the eval(). */
    if (PL_eval_root->op_type == OP_LEAVEEVAL
	    && cUNOPx(PL_eval_root)->op_first->op_type == OP_LINESEQ
	    && cLISTOPx(cUNOPx(PL_eval_root)->op_first)->op_last->op_type
	    == OP_REQUIRE)
	scalar(PL_eval_root);
    else if (gimme & G_VOID)
	scalarvoid(PL_eval_root);
    else if (gimme & G_ARRAY)
	list(PL_eval_root);
    else
	scalar(PL_eval_root);

    DEBUG_x(dump_eval());

    /* Register with debugger: */
    if (PERLDB_INTER && saveop->op_type == OP_REQUIRE) {
	CV *cv = get_cv("DB::postponed", FALSE);
	if (cv) {
	    dSP;
	    PUSHMARK(SP);
	    XPUSHs((SV*)CopFILEGV(&PL_compiling));
	    PUTBACK;
	    call_sv((SV*)cv, G_DISCARD);
	}
    }

    /* compiled okay, so do it */

    CvDEPTH(PL_compcv) = 1;
    SP = PL_stack_base + POPMARK;		/* pop original mark */
    PL_op = saveop;			/* The caller may need it. */
    PL_lex_state = LEX_NOTPARSING;	/* $^S needs this. */
#ifdef USE_5005THREADS
    MUTEX_LOCK(&PL_eval_mutex);
    PL_eval_owner = 0;
    COND_SIGNAL(&PL_eval_cond);
    MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_5005THREADS */

    RETURNOP(PL_eval_start);
}

STATIC PerlIO *
S_doopen_pm(pTHX_ const char *name, const char *mode)
{
#ifndef PERL_DISABLE_PMC
    STRLEN namelen = strlen(name);
    PerlIO *fp;

    if (namelen > 3 && strEQ(name + namelen - 3, ".pm")) {
	SV *pmcsv = Perl_newSVpvf(aTHX_ "%s%c", name, 'c');
	char *pmc = SvPV_nolen(pmcsv);
	Stat_t pmstat;
	Stat_t pmcstat;
	if (PerlLIO_stat(pmc, &pmcstat) < 0) {
	    fp = PerlIO_open(name, mode);
	}
	else {
	    if (PerlLIO_stat(name, &pmstat) < 0 ||
	        pmstat.st_mtime < pmcstat.st_mtime)
	    {
		fp = PerlIO_open(pmc, mode);
	    }
	    else {
		fp = PerlIO_open(name, mode);
	    }
	}
	SvREFCNT_dec(pmcsv);
    }
    else {
	fp = PerlIO_open(name, mode);
    }
    return fp;
#else
    return PerlIO_open(name, mode);
#endif /* !PERL_DISABLE_PMC */
}

PP(pp_require)
{
    dSP;
    register PERL_CONTEXT *cx;
    SV *sv;
    char *name;
    STRLEN len;
    char *tryname = Nullch;
    SV *namesv = Nullsv;
    SV** svp;
    I32 gimme = GIMME_V;
    PerlIO *tryrsfp = 0;
    STRLEN n_a;
    int filter_has_file = 0;
    GV *filter_child_proc = 0;
    SV *filter_state = 0;
    SV *filter_sub = 0;
    SV *hook_sv = 0;
    SV *encoding;
    OP *op;

    sv = POPs;
    if (SvNIOKp(sv) && PL_op->op_type != OP_DOFILE) {
	if (SvPOK(sv) && SvNOK(sv) && SvNV(sv)) {		/* require v5.6.1 */
	    UV rev = 0, ver = 0, sver = 0;
	    STRLEN len;
	    U8 *s = (U8*)SvPVX(sv);
	    U8 *end = (U8*)SvPVX(sv) + SvCUR(sv);
	    if (s < end) {
		rev = utf8n_to_uvchr(s, end - s, &len, 0);
		s += len;
		if (s < end) {
		    ver = utf8n_to_uvchr(s, end - s, &len, 0);
		    s += len;
		    if (s < end)
			sver = utf8n_to_uvchr(s, end - s, &len, 0);
		}
	    }
	    if (PERL_REVISION < rev
		|| (PERL_REVISION == rev
		    && (PERL_VERSION < ver
			|| (PERL_VERSION == ver
			    && PERL_SUBVERSION < sver))))
	    {
		DIE(aTHX_ "Perl v%"UVuf".%"UVuf".%"UVuf" required--this is only "
		    "v%d.%d.%d, stopped", rev, ver, sver, PERL_REVISION,
		    PERL_VERSION, PERL_SUBVERSION);
	    }
	    RETPUSHYES;
	}
	else if (!SvPOKp(sv)) {			/* require 5.005_03 */
	    if ((NV)PERL_REVISION + ((NV)PERL_VERSION/(NV)1000)
		+ ((NV)PERL_SUBVERSION/(NV)1000000)
		+ 0.00000099 < SvNV(sv))
	    {
		NV nrev = SvNV(sv);
		UV rev = (UV)nrev;
		NV nver = (nrev - rev) * 1000;
		UV ver = (UV)(nver + 0.0009);
		NV nsver = (nver - ver) * 1000;
		UV sver = (UV)(nsver + 0.0009);

		/* help out with the "use 5.6" confusion */
		if (sver == 0 && (rev > 5 || (rev == 5 && ver >= 100))) {
		    DIE(aTHX_ "Perl v%"UVuf".%"UVuf".%"UVuf" required"
			" (did you mean v%"UVuf".%03"UVuf"?)--"
			"this is only v%d.%d.%d, stopped",
			rev, ver, sver, rev, ver/100,
			PERL_REVISION, PERL_VERSION, PERL_SUBVERSION);
		}
		else {
		    DIE(aTHX_ "Perl v%"UVuf".%"UVuf".%"UVuf" required--"
			"this is only v%d.%d.%d, stopped",
			rev, ver, sver, PERL_REVISION, PERL_VERSION,
			PERL_SUBVERSION);
		}
	    }
	    RETPUSHYES;
	}
    }
    name = SvPV(sv, len);
    if (!(name && len > 0 && *name))
	DIE(aTHX_ "Null filename used");
    TAINT_PROPER("require");
    if (PL_op->op_type == OP_REQUIRE &&
      (svp = hv_fetch(GvHVn(PL_incgv), name, len, 0)) &&
      *svp != &PL_sv_undef)
	RETPUSHYES;

    /* prepare to compile file */

    if (path_is_absolute(name)) {
	tryname = name;
	tryrsfp = doopen_pm(name,PERL_SCRIPT_MODE);
    }
#ifdef MACOS_TRADITIONAL
    if (!tryrsfp) {
	char newname[256];

	MacPerl_CanonDir(name, newname, 1);
	if (path_is_absolute(newname)) {
	    tryname = newname;
	    tryrsfp = doopen_pm(newname,PERL_SCRIPT_MODE);
	}
    }
#endif
    if (!tryrsfp) {
	AV *ar = GvAVn(PL_incgv);
	I32 i;
#ifdef VMS
	char *unixname;
	if ((unixname = tounixspec(name, Nullch)) != Nullch)
#endif
	{
	    namesv = NEWSV(806, 0);
	    for (i = 0; i <= AvFILL(ar); i++) {
		SV *dirsv = *av_fetch(ar, i, TRUE);

		if (SvROK(dirsv)) {
		    int count;
		    SV *loader = dirsv;

		    if (SvTYPE(SvRV(loader)) == SVt_PVAV
			&& !sv_isobject(loader))
		    {
			loader = *av_fetch((AV *)SvRV(loader), 0, TRUE);
		    }

		    Perl_sv_setpvf(aTHX_ namesv, "/loader/0x%"UVxf"/%s",
				   PTR2UV(SvRV(dirsv)), name);
		    tryname = SvPVX(namesv);
		    tryrsfp = 0;

		    ENTER;
		    SAVETMPS;
		    EXTEND(SP, 2);

		    PUSHMARK(SP);
		    PUSHs(dirsv);
		    PUSHs(sv);
		    PUTBACK;
		    if (sv_isobject(loader))
			count = call_method("INC", G_ARRAY);
		    else
			count = call_sv(loader, G_ARRAY);
		    SPAGAIN;

		    if (count > 0) {
			int i = 0;
			SV *arg;

			SP -= count - 1;
			arg = SP[i++];

			if (SvROK(arg) && SvTYPE(SvRV(arg)) == SVt_PVGV) {
			    arg = SvRV(arg);
			}

			if (SvTYPE(arg) == SVt_PVGV) {
			    IO *io = GvIO((GV *)arg);

			    ++filter_has_file;

			    if (io) {
				tryrsfp = IoIFP(io);
				if (IoTYPE(io) == IoTYPE_PIPE) {
				    /* reading from a child process doesn't
				       nest -- when returning from reading
				       the inner module, the outer one is
				       unreadable (closed?)  I've tried to
				       save the gv to manage the lifespan of
				       the pipe, but this didn't help. XXX */
				    filter_child_proc = (GV *)arg;
				    (void)SvREFCNT_inc(filter_child_proc);
				}
				else {
				    if (IoOFP(io) && IoOFP(io) != IoIFP(io)) {
					PerlIO_close(IoOFP(io));
				    }
				    IoIFP(io) = Nullfp;
				    IoOFP(io) = Nullfp;
				}
			    }

			    if (i < count) {
				arg = SP[i++];
			    }
			}

			if (SvROK(arg) && SvTYPE(SvRV(arg)) == SVt_PVCV) {
			    filter_sub = arg;
			    (void)SvREFCNT_inc(filter_sub);

			    if (i < count) {
				filter_state = SP[i];
				(void)SvREFCNT_inc(filter_state);
			    }

			    if (tryrsfp == 0) {
				tryrsfp = PerlIO_open("/dev/null",
						      PERL_SCRIPT_MODE);
			    }
			}
		    }

		    PUTBACK;
		    FREETMPS;
		    LEAVE;

		    if (tryrsfp) {
			hook_sv = dirsv;
			break;
		    }

		    filter_has_file = 0;
		    if (filter_child_proc) {
			SvREFCNT_dec(filter_child_proc);
			filter_child_proc = 0;
		    }
		    if (filter_state) {
			SvREFCNT_dec(filter_state);
			filter_state = 0;
		    }
		    if (filter_sub) {
			SvREFCNT_dec(filter_sub);
			filter_sub = 0;
		    }
		}
		else {
		  if (!path_is_absolute(name)
#ifdef MACOS_TRADITIONAL
			/* We consider paths of the form :a:b ambiguous and interpret them first
			   as global then as local
			*/
			|| (*name == ':' && name[1] != ':' && strchr(name+2, ':'))
#endif
		  ) {
		    char *dir = SvPVx(dirsv, n_a);
#ifdef MACOS_TRADITIONAL
		    char buf1[256];
		    char buf2[256];

		    MacPerl_CanonDir(name, buf2, 1);
		    Perl_sv_setpvf(aTHX_ namesv, "%s%s", MacPerl_CanonDir(dir, buf1, 0), buf2+(buf2[0] == ':'));
#else
#ifdef VMS
		    char *unixdir;
		    if ((unixdir = tounixpath(dir, Nullch)) == Nullch)
			continue;
		    sv_setpv(namesv, unixdir);
		    sv_catpv(namesv, unixname);
#else
		    Perl_sv_setpvf(aTHX_ namesv, "%s/%s", dir, name);
#endif
#endif
		    TAINT_PROPER("require");
		    tryname = SvPVX(namesv);
		    tryrsfp = doopen_pm(tryname, PERL_SCRIPT_MODE);
		    if (tryrsfp) {
			if (tryname[0] == '.' && tryname[1] == '/')
			    tryname += 2;
			break;
		    }
		  }
		}
	    }
	}
    }
    SAVECOPFILE_FREE(&PL_compiling);
    CopFILE_set(&PL_compiling, tryrsfp ? tryname : name);
    SvREFCNT_dec(namesv);
    if (!tryrsfp) {
	if (PL_op->op_type == OP_REQUIRE) {
	    char *msgstr = name;
	    if (namesv) {			/* did we lookup @INC? */
		SV *msg = sv_2mortal(newSVpv(msgstr,0));
		SV *dirmsgsv = NEWSV(0, 0);
		AV *ar = GvAVn(PL_incgv);
		I32 i;
		sv_catpvn(msg, " in @INC", 8);
		if (instr(SvPVX(msg), ".h "))
		    sv_catpv(msg, " (change .h to .ph maybe?)");
		if (instr(SvPVX(msg), ".ph "))
		    sv_catpv(msg, " (did you run h2ph?)");
		sv_catpv(msg, " (@INC contains:");
		for (i = 0; i <= AvFILL(ar); i++) {
		    char *dir = SvPVx(*av_fetch(ar, i, TRUE), n_a);
		    Perl_sv_setpvf(aTHX_ dirmsgsv, " %s", dir);
		    sv_catsv(msg, dirmsgsv);
		}
		sv_catpvn(msg, ")", 1);
		SvREFCNT_dec(dirmsgsv);
		msgstr = SvPV_nolen(msg);
	    }
	    DIE(aTHX_ "Can't locate %s", msgstr);
	}

	RETPUSHUNDEF;
    }
    else
	SETERRNO(0, SS_NORMAL);

    /* Assume success here to prevent recursive requirement. */
    len = strlen(name);
    /* Check whether a hook in @INC has already filled %INC */
    if (!hook_sv || !(svp = hv_fetch(GvHVn(PL_incgv), name, len, 0))) {
	(void)hv_store(GvHVn(PL_incgv), name, len,
		       (hook_sv ? SvREFCNT_inc(hook_sv)
				: newSVpv(CopFILE(&PL_compiling), 0)),
		       0 );
    }

    ENTER;
    SAVETMPS;
    lex_start(sv_2mortal(newSVpvn("",0)));
    SAVEGENERICSV(PL_rsfp_filters);
    PL_rsfp_filters = Nullav;

    PL_rsfp = tryrsfp;
    SAVEHINTS();
    PL_hints = 0;
    SAVESPTR(PL_compiling.cop_warnings);
    if (PL_dowarn & G_WARN_ALL_ON)
        PL_compiling.cop_warnings = pWARN_ALL ;
    else if (PL_dowarn & G_WARN_ALL_OFF)
        PL_compiling.cop_warnings = pWARN_NONE ;
    else if (PL_taint_warn)
        PL_compiling.cop_warnings = newSVpvn(WARN_TAINTstring, WARNsize);
    else
        PL_compiling.cop_warnings = pWARN_STD ;
    SAVESPTR(PL_compiling.cop_io);
    PL_compiling.cop_io = Nullsv;

    if (filter_sub || filter_child_proc) {
	SV *datasv = filter_add(run_user_filter, Nullsv);
	IoLINES(datasv) = filter_has_file;
	IoFMT_GV(datasv) = (GV *)filter_child_proc;
	IoTOP_GV(datasv) = (GV *)filter_state;
	IoBOTTOM_GV(datasv) = (GV *)filter_sub;
    }

    /* switch to eval mode */
    push_return(PL_op->op_next);
    PUSHBLOCK(cx, CXt_EVAL, SP);
    PUSHEVAL(cx, name, Nullgv);

    SAVECOPLINE(&PL_compiling);
    CopLINE_set(&PL_compiling, 0);

    PUTBACK;
#ifdef USE_5005THREADS
    MUTEX_LOCK(&PL_eval_mutex);
    if (PL_eval_owner && PL_eval_owner != thr)
	while (PL_eval_owner)
	    COND_WAIT(&PL_eval_cond, &PL_eval_mutex);
    PL_eval_owner = thr;
    MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_5005THREADS */

    /* Store and reset encoding. */
    encoding = PL_encoding;
    PL_encoding = Nullsv;

    op = DOCATCH(doeval(gimme, NULL, Nullcv, PL_curcop->cop_seq));
    
    /* Restore encoding. */
    PL_encoding = encoding;

    return op;
}

PP(pp_dofile)
{
    return pp_require();
}

PP(pp_entereval)
{
    dSP;
    register PERL_CONTEXT *cx;
    dPOPss;
    I32 gimme = GIMME_V, was = PL_sub_generation;
    char tbuf[TYPE_DIGITS(long) + 12];
    char *tmpbuf = tbuf;
    char *safestr;
    STRLEN len;
    OP *ret;
    CV* runcv;
    U32 seq;

    if (!SvPV(sv,len))
	RETPUSHUNDEF;
    TAINT_PROPER("eval");

    ENTER;
    lex_start(sv);
    SAVETMPS;

    /* switch to eval mode */

    if (PERLDB_NAMEEVAL && CopLINE(PL_curcop)) {
	SV *sv = sv_newmortal();
	Perl_sv_setpvf(aTHX_ sv, "_<(eval %lu)[%s:%"IVdf"]",
		       (unsigned long)++PL_evalseq,
		       CopFILE(PL_curcop), (IV)CopLINE(PL_curcop));
	tmpbuf = SvPVX(sv);
    }
    else
	sprintf(tmpbuf, "_<(eval %lu)", (unsigned long)++PL_evalseq);
    SAVECOPFILE_FREE(&PL_compiling);
    CopFILE_set(&PL_compiling, tmpbuf+2);
    SAVECOPLINE(&PL_compiling);
    CopLINE_set(&PL_compiling, 1);
    /* XXX For C<eval "...">s within BEGIN {} blocks, this ends up
       deleting the eval's FILEGV from the stash before gv_check() runs
       (i.e. before run-time proper). To work around the coredump that
       ensues, we always turn GvMULTI_on for any globals that were
       introduced within evals. See force_ident(). GSAR 96-10-12 */
    safestr = savepv(tmpbuf);
    SAVEDELETE(PL_defstash, safestr, strlen(safestr));
    SAVEHINTS();
    PL_hints = PL_op->op_targ;
    SAVESPTR(PL_compiling.cop_warnings);
    if (specialWARN(PL_curcop->cop_warnings))
        PL_compiling.cop_warnings = PL_curcop->cop_warnings;
    else {
        PL_compiling.cop_warnings = newSVsv(PL_curcop->cop_warnings);
        SAVEFREESV(PL_compiling.cop_warnings);
    }
    SAVESPTR(PL_compiling.cop_io);
    if (specialCopIO(PL_curcop->cop_io))
        PL_compiling.cop_io = PL_curcop->cop_io;
    else {
        PL_compiling.cop_io = newSVsv(PL_curcop->cop_io);
        SAVEFREESV(PL_compiling.cop_io);
    }
    /* special case: an eval '' executed within the DB package gets lexically
     * placed in the first non-DB CV rather than the current CV - this
     * allows the debugger to execute code, find lexicals etc, in the
     * scope of the code being debugged. Passing &seq gets find_runcv
     * to do the dirty work for us */
    runcv = find_runcv(&seq);

    push_return(PL_op->op_next);
    PUSHBLOCK(cx, (CXt_EVAL|CXp_REAL), SP);
    PUSHEVAL(cx, 0, Nullgv);

    /* prepare to compile string */

    if (PERLDB_LINE && PL_curstash != PL_debstash)
	save_lines(CopFILEAV(&PL_compiling), PL_linestr);
    PUTBACK;
#ifdef USE_5005THREADS
    MUTEX_LOCK(&PL_eval_mutex);
    if (PL_eval_owner && PL_eval_owner != thr)
	while (PL_eval_owner)
	    COND_WAIT(&PL_eval_cond, &PL_eval_mutex);
    PL_eval_owner = thr;
    MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_5005THREADS */
    ret = doeval(gimme, NULL, runcv, seq);
    if (PERLDB_INTER && was != (I32)PL_sub_generation /* Some subs defined here. */
	&& ret != PL_op->op_next) {	/* Successive compilation. */
	strcpy(safestr, "_<(eval )");	/* Anything fake and short. */
    }
    return DOCATCH(ret);
}

PP(pp_leaveeval)
{
    dSP;
    register SV **mark;
    SV **newsp;
    PMOP *newpm;
    I32 gimme;
    register PERL_CONTEXT *cx;
    OP *retop;
    U8 save_flags = PL_op -> op_flags;
    I32 optype;

    POPBLOCK(cx,newpm);
    POPEVAL(cx);
    retop = pop_return();

    TAINT_NOT;
    if (gimme == G_VOID)
	MARK = newsp;
    else if (gimme == G_SCALAR) {
	MARK = newsp + 1;
	if (MARK <= SP) {
	    if (SvFLAGS(TOPs) & SVs_TEMP)
		*MARK = TOPs;
	    else
		*MARK = sv_mortalcopy(TOPs);
	}
	else {
	    MEXTEND(mark,0);
	    *MARK = &PL_sv_undef;
	}
	SP = MARK;
    }
    else {
	/* in case LEAVE wipes old return values */
	for (mark = newsp + 1; mark <= SP; mark++) {
	    if (!(SvFLAGS(*mark) & SVs_TEMP)) {
		*mark = sv_mortalcopy(*mark);
		TAINT_NOT;	/* Each item is independent */
	    }
	}
    }
    PL_curpm = newpm;	/* Don't pop $1 et al till now */

#ifdef DEBUGGING
    assert(CvDEPTH(PL_compcv) == 1);
#endif
    CvDEPTH(PL_compcv) = 0;
    lex_end();

    if (optype == OP_REQUIRE &&
	!(gimme == G_SCALAR ? SvTRUE(*SP) : SP > newsp))
    {
	/* Unassume the success we assumed earlier. */
	SV *nsv = cx->blk_eval.old_namesv;
	(void)hv_delete(GvHVn(PL_incgv), SvPVX(nsv), SvCUR(nsv), G_DISCARD);
	retop = Perl_die(aTHX_ "%"SVf" did not return a true value", nsv);
	/* die_where() did LEAVE, or we won't be here */
    }
    else {
	LEAVE;
	if (!(save_flags & OPf_SPECIAL))
	    sv_setpv(ERRSV,"");
    }

    RETURNOP(retop);
}

PP(pp_entertry)
{
    dSP;
    register PERL_CONTEXT *cx;
    I32 gimme = GIMME_V;

    ENTER;
    SAVETMPS;

    push_return(cLOGOP->op_other->op_next);
    PUSHBLOCK(cx, (CXt_EVAL|CXp_TRYBLOCK), SP);
    PUSHEVAL(cx, 0, 0);

    PL_in_eval = EVAL_INEVAL;
    sv_setpv(ERRSV,"");
    PUTBACK;
    return DOCATCH(PL_op->op_next);
}

PP(pp_leavetry)
{
    dSP;
    register SV **mark;
    SV **newsp;
    PMOP *newpm;
    OP* retop;
    I32 gimme;
    register PERL_CONTEXT *cx;
    I32 optype;

    POPBLOCK(cx,newpm);
    POPEVAL(cx);
    retop = pop_return();

    TAINT_NOT;
    if (gimme == G_VOID)
	SP = newsp;
    else if (gimme == G_SCALAR) {
	MARK = newsp + 1;
	if (MARK <= SP) {
	    if (SvFLAGS(TOPs) & (SVs_PADTMP|SVs_TEMP))
		*MARK = TOPs;
	    else
		*MARK = sv_mortalcopy(TOPs);
	}
	else {
	    MEXTEND(mark,0);
	    *MARK = &PL_sv_undef;
	}
	SP = MARK;
    }
    else {
	/* in case LEAVE wipes old return values */
	for (mark = newsp + 1; mark <= SP; mark++) {
	    if (!(SvFLAGS(*mark) & (SVs_PADTMP|SVs_TEMP))) {
		*mark = sv_mortalcopy(*mark);
		TAINT_NOT;	/* Each item is independent */
	    }
	}
    }
    PL_curpm = newpm;	/* Don't pop $1 et al till now */

    LEAVE;
    sv_setpv(ERRSV,"");
    RETURNOP(retop);
}

STATIC void
S_doparseform(pTHX_ SV *sv)
{
    STRLEN len;
    register char *s = SvPV_force(sv, len);
    register char *send = s + len;
    register char *base = Nullch;
    register I32 skipspaces = 0;
    bool noblank   = FALSE;
    bool repeat    = FALSE;
    bool postspace = FALSE;
    U32 *fops;
    register U32 *fpc;
    U32 *linepc = 0;
    register I32 arg;
    bool ischop;
    int maxops = 2; /* FF_LINEMARK + FF_END) */

    if (len == 0)
	Perl_croak(aTHX_ "Null picture in formline");

    /* estimate the buffer size needed */
    for (base = s; s <= send; s++) {
	if (*s == '\n' || *s == '@' || *s == '^')
	    maxops += 10;
    }
    s = base;
    base = Nullch;

    New(804, fops, maxops, U32);
    fpc = fops;

    if (s < send) {
	linepc = fpc;
	*fpc++ = FF_LINEMARK;
	noblank = repeat = FALSE;
	base = s;
    }

    while (s <= send) {
	switch (*s++) {
	default:
	    skipspaces = 0;
	    continue;

	case '~':
	    if (*s == '~') {
		repeat = TRUE;
		*s = ' ';
	    }
	    noblank = TRUE;
	    s[-1] = ' ';
	    /* FALL THROUGH */
	case ' ': case '\t':
	    skipspaces++;
	    continue;
	
	case '\n': case 0:
	    arg = s - base;
	    skipspaces++;
	    arg -= skipspaces;
	    if (arg) {
		if (postspace)
		    *fpc++ = FF_SPACE;
		*fpc++ = FF_LITERAL;
		*fpc++ = (U16)arg;
	    }
	    postspace = FALSE;
	    if (s <= send)
		skipspaces--;
	    if (skipspaces) {
		*fpc++ = FF_SKIP;
		*fpc++ = (U16)skipspaces;
	    }
	    skipspaces = 0;
	    if (s <= send)
		*fpc++ = FF_NEWLINE;
	    if (noblank) {
		*fpc++ = FF_BLANK;
		if (repeat)
		    arg = fpc - linepc + 1;
		else
		    arg = 0;
		*fpc++ = (U16)arg;
	    }
	    if (s < send) {
		linepc = fpc;
		*fpc++ = FF_LINEMARK;
		noblank = repeat = FALSE;
		base = s;
	    }
	    else
		s++;
	    continue;

	case '@':
	case '^':
	    ischop = s[-1] == '^';

	    if (postspace) {
		*fpc++ = FF_SPACE;
		postspace = FALSE;
	    }
	    arg = (s - base) - 1;
	    if (arg) {
		*fpc++ = FF_LITERAL;
		*fpc++ = (U16)arg;
	    }

	    base = s - 1;
	    *fpc++ = FF_FETCH;
	    if (*s == '*') {
		s++;
		*fpc++ = 0;
		*fpc++ = FF_LINEGLOB;
	    }
	    else if (*s == '#' || (*s == '.' && s[1] == '#')) {
		arg = ischop ? 512 : 0;
		base = s - 1;
		while (*s == '#')
		    s++;
		if (*s == '.') {
		    char *f;
		    s++;
		    f = s;
		    while (*s == '#')
			s++;
		    arg |= 256 + (s - f);
		}
		*fpc++ = s - base;		/* fieldsize for FETCH */
		*fpc++ = FF_DECIMAL;
                *fpc++ = (U16)arg;
            }
            else if (*s == '0' && s[1] == '#') {  /* Zero padded decimals */
                arg = ischop ? 512 : 0;
		base = s - 1;
                s++;                                /* skip the '0' first */
                while (*s == '#')
                    s++;
                if (*s == '.') {
                    char *f;
                    s++;
                    f = s;
                    while (*s == '#')
                        s++;
                    arg |= 256 + (s - f);
                }
                *fpc++ = s - base;                /* fieldsize for FETCH */
                *fpc++ = FF_0DECIMAL;
		*fpc++ = (U16)arg;
	    }
	    else {
		I32 prespace = 0;
		bool ismore = FALSE;

		if (*s == '>') {
		    while (*++s == '>') ;
		    prespace = FF_SPACE;
		}
		else if (*s == '|') {
		    while (*++s == '|') ;
		    prespace = FF_HALFSPACE;
		    postspace = TRUE;
		}
		else {
		    if (*s == '<')
			while (*++s == '<') ;
		    postspace = TRUE;
		}
		if (*s == '.' && s[1] == '.' && s[2] == '.') {
		    s += 3;
		    ismore = TRUE;
		}
		*fpc++ = s - base;		/* fieldsize for FETCH */

		*fpc++ = ischop ? FF_CHECKCHOP : FF_CHECKNL;

		if (prespace)
		    *fpc++ = (U16)prespace;
		*fpc++ = FF_ITEM;
		if (ismore)
		    *fpc++ = FF_MORE;
		if (ischop)
		    *fpc++ = FF_CHOP;
	    }
	    base = s;
	    skipspaces = 0;
	    continue;
	}
    }
    *fpc++ = FF_END;

    assert (fpc <= fops + maxops); /* ensure our buffer estimate was valid */
    arg = fpc - fops;
    { /* need to jump to the next word */
        int z;
	z = WORD_ALIGN - SvCUR(sv) % WORD_ALIGN;
	SvGROW(sv, SvCUR(sv) + z + arg * sizeof(U32) + 4);
	s = SvPVX(sv) + SvCUR(sv) + z;
    }
    Copy(fops, s, arg, U32);
    Safefree(fops);
    sv_magic(sv, Nullsv, PERL_MAGIC_fm, Nullch, 0);
    SvCOMPILED_on(sv);
}

static I32
run_user_filter(pTHX_ int idx, SV *buf_sv, int maxlen)
{
    SV *datasv = FILTER_DATA(idx);
    int filter_has_file = IoLINES(datasv);
    GV *filter_child_proc = (GV *)IoFMT_GV(datasv);
    SV *filter_state = (SV *)IoTOP_GV(datasv);
    SV *filter_sub = (SV *)IoBOTTOM_GV(datasv);
    int len = 0;

    /* I was having segfault trouble under Linux 2.2.5 after a
       parse error occured.  (Had to hack around it with a test
       for PL_error_count == 0.)  Solaris doesn't segfault --
       not sure where the trouble is yet.  XXX */

    if (filter_has_file) {
	len = FILTER_READ(idx+1, buf_sv, maxlen);
    }

    if (filter_sub && len >= 0) {
	dSP;
	int count;

	ENTER;
	SAVE_DEFSV;
	SAVETMPS;
	EXTEND(SP, 2);

	DEFSV = buf_sv;
	PUSHMARK(SP);
	PUSHs(sv_2mortal(newSViv(maxlen)));
	if (filter_state) {
	    PUSHs(filter_state);
	}
	PUTBACK;
	count = call_sv(filter_sub, G_SCALAR);
	SPAGAIN;

	if (count > 0) {
	    SV *out = POPs;
	    if (SvOK(out)) {
		len = SvIV(out);
	    }
	}

	PUTBACK;
	FREETMPS;
	LEAVE;
    }

    if (len <= 0) {
	IoLINES(datasv) = 0;
	if (filter_child_proc) {
	    SvREFCNT_dec(filter_child_proc);
	    IoFMT_GV(datasv) = Nullgv;
	}
	if (filter_state) {
	    SvREFCNT_dec(filter_state);
	    IoTOP_GV(datasv) = Nullgv;
	}
	if (filter_sub) {
	    SvREFCNT_dec(filter_sub);
	    IoBOTTOM_GV(datasv) = Nullgv;
	}
	filter_del(run_user_filter);
    }

    return len;
}

/* perhaps someone can come up with a better name for
   this?  it is not really "absolute", per se ... */
static bool
S_path_is_absolute(pTHX_ char *name)
{
    if (PERL_FILE_IS_ABSOLUTE(name)
#ifdef MACOS_TRADITIONAL
	|| (*name == ':'))
#else
	|| (*name == '.' && (name[1] == '/' ||
			     (name[1] == '.' && name[2] == '/'))))
#endif
    {
	return TRUE;
    }
    else
    	return FALSE;
}
