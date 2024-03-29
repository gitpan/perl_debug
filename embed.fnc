: Lines are of the form:
:    flags|return_type|function_name|arg1|arg2|...|argN
:
: A line may be continued on another by ending it with a backslash.
: Leading and trailing whitespace will be ignored in each component.
:
: flags are single letters with following meanings:
:	A		member of public API
:	m		Implemented as a macro - no export, no
:			proto, no #define
:	d		function has documentation with its source
:	s		static function, should have an S_ prefix in
:			source file
:	n		has no implicit interpreter/thread context argument
:	p		function has a Perl_ prefix
:	f		function takes printf style format string, varargs
:	r		function never returns
:	o		has no compatibility macro (#define foo Perl_foo)
:	x		not exported
:	X		explicitly exported
:	M		may change
:	E		visible to extensions included in the Perl core
:	b		binary backward compatibility; function is a macro
:			but has also Perl_ implementation (which is exported)
:
: Individual flags may be separated by whitespace.
:
: New global functions should be added at the end for binary compatibility
: in some configurations.

START_EXTERN_C

#if defined(PERL_IMPLICIT_SYS)
Ano	|PerlInterpreter*	|perl_alloc_using \
				|struct IPerlMem* m|struct IPerlMem* ms \
				|struct IPerlMem* mp|struct IPerlEnv* e \
				|struct IPerlStdIO* io|struct IPerlLIO* lio \
				|struct IPerlDir* d|struct IPerlSock* s \
				|struct IPerlProc* p
#endif
Anod	|PerlInterpreter*	|perl_alloc
Anod	|void	|perl_construct	|PerlInterpreter* interp
Anod	|int	|perl_destruct	|PerlInterpreter* interp
Anod	|void	|perl_free	|PerlInterpreter* interp
Anod	|int	|perl_run	|PerlInterpreter* interp
Anod	|int	|perl_parse	|PerlInterpreter* interp|XSINIT_t xsinit \
				|int argc|char** argv|char** env
Anp	|bool	|doing_taint	|int argc|char** argv|char** env
#if defined(USE_ITHREADS)
Anod	|PerlInterpreter*|perl_clone|PerlInterpreter* interp, UV flags
#  if defined(PERL_IMPLICIT_SYS)
Ano	|PerlInterpreter*|perl_clone_using|PerlInterpreter *interp|UV flags \
				|struct IPerlMem* m|struct IPerlMem* ms \
				|struct IPerlMem* mp|struct IPerlEnv* e \
				|struct IPerlStdIO* io|struct IPerlLIO* lio \
				|struct IPerlDir* d|struct IPerlSock* s \
				|struct IPerlProc* p
#  endif
#endif

Anop	|Malloc_t|malloc	|MEM_SIZE nbytes
Anop	|Malloc_t|calloc	|MEM_SIZE elements|MEM_SIZE size
Anop	|Malloc_t|realloc	|Malloc_t where|MEM_SIZE nbytes
Anop	|Free_t	|mfree		|Malloc_t where
#if defined(MYMALLOC)
np	|MEM_SIZE|malloced_size	|void *p
#endif

Anp	|void*	|get_context
Anp	|void	|set_context	|void *thx

END_EXTERN_C

/* functions with flag 'n' should come before here */
START_EXTERN_C
#  include "pp_proto.h"
Ap	|SV*	|amagic_call	|SV* left|SV* right|int method|int dir
Ap	|bool	|Gv_AMupdate	|HV* stash
Ap	|CV*	|gv_handler	|HV* stash|I32 id
p	|OP*	|append_elem	|I32 optype|OP* head|OP* tail
p	|OP*	|append_list	|I32 optype|LISTOP* first|LISTOP* last
p	|I32	|apply		|I32 type|SV** mark|SV** sp
ApM	|void	|apply_attrs_string|char *stashpv|CV *cv|char *attrstr|STRLEN len
Ap	|SV*	|avhv_delete_ent|AV *ar|SV* keysv|I32 flags|U32 hash
Ap	|bool	|avhv_exists_ent|AV *ar|SV* keysv|U32 hash
Ap	|SV**	|avhv_fetch_ent	|AV *ar|SV* keysv|I32 lval|U32 hash
Ap	|SV**	|avhv_store_ent	|AV *ar|SV* keysv|SV* val|U32 hash
Ap	|HE*	|avhv_iternext	|AV *ar
Ap	|SV*	|avhv_iterval	|AV *ar|HE* entry
Ap	|HV*	|avhv_keys	|AV *ar
Apd	|void	|av_clear	|AV* ar
Apd	|SV*	|av_delete	|AV* ar|I32 key|I32 flags
Apd	|bool	|av_exists	|AV* ar|I32 key
Apd	|void	|av_extend	|AV* ar|I32 key
p	|AV*	|av_fake	|I32 size|SV** svp
Apd	|SV**	|av_fetch	|AV* ar|I32 key|I32 lval
Apd	|void	|av_fill	|AV* ar|I32 fill
Apd	|I32	|av_len		|AV* ar
Apd	|AV*	|av_make	|I32 size|SV** svp
Apd	|SV*	|av_pop		|AV* ar
Apd	|void	|av_push	|AV* ar|SV* val
p	|void	|av_reify	|AV* ar
Apd	|SV*	|av_shift	|AV* ar
Apd	|SV**	|av_store	|AV* ar|I32 key|SV* val
Apd	|void	|av_undef	|AV* ar
Apd	|void	|av_unshift	|AV* ar|I32 num
p	|OP*	|bind_match	|I32 type|OP* left|OP* pat
p	|OP*	|block_end	|I32 floor|OP* seq
Ap	|I32	|block_gimme
p	|int	|block_start	|int full
p	|void	|boot_core_UNIVERSAL
p	|void	|boot_core_PerlIO
Ap	|void	|call_list	|I32 oldscope|AV* av_list
p	|bool	|cando		|Mode_t mode|Uid_t effective|Stat_t* statbufp
Ap	|U32	|cast_ulong	|NV f
Ap	|I32	|cast_i32	|NV f
Ap	|IV	|cast_iv	|NV f
Ap	|UV	|cast_uv	|NV f
#if !defined(HAS_TRUNCATE) && !defined(HAS_CHSIZE) && defined(F_FREESP)
Ap	|I32	|my_chsize	|int fd|Off_t length
#endif
#if defined(USE_5005THREADS)
Ap	|MAGIC*	|condpair_magic	|SV *sv
#endif
p	|OP*	|convert	|I32 optype|I32 flags|OP* o
Afprd	|void	|croak		|const char* pat|...
Apr	|void	|vcroak		|const char* pat|va_list* args
#if defined(PERL_IMPLICIT_CONTEXT)
Afnrp	|void	|croak_nocontext|const char* pat|...
Afnp	|OP*	|die_nocontext	|const char* pat|...
Afnp	|void	|deb_nocontext	|const char* pat|...
Afnp	|char*	|form_nocontext	|const char* pat|...
Anp	|void	|load_module_nocontext|U32 flags|SV* name|SV* ver|...
Afnp	|SV*	|mess_nocontext	|const char* pat|...
Afnp	|void	|warn_nocontext	|const char* pat|...
Afnp	|void	|warner_nocontext|U32 err|const char* pat|...
Afnp	|SV*	|newSVpvf_nocontext|const char* pat|...
Afnp	|void	|sv_catpvf_nocontext|SV* sv|const char* pat|...
Afnp	|void	|sv_setpvf_nocontext|SV* sv|const char* pat|...
Afnp	|void	|sv_catpvf_mg_nocontext|SV* sv|const char* pat|...
Afnp	|void	|sv_setpvf_mg_nocontext|SV* sv|const char* pat|...
Afnp	|int	|fprintf_nocontext|PerlIO* stream|const char* fmt|...
Afnp	|int	|printf_nocontext|const char* fmt|...
#endif
p	|void	|cv_ckproto	|CV* cv|GV* gv|char* p
pd	|CV*	|cv_clone	|CV* proto
Apd	|SV*	|cv_const_sv	|CV* cv
p	|SV*	|op_const_sv	|OP* o|CV* cv
Apd	|void	|cv_undef	|CV* cv
Ap	|void	|cx_dump	|PERL_CONTEXT* cs
Ap	|SV*	|filter_add	|filter_t funcp|SV* datasv
Ap	|void	|filter_del	|filter_t funcp
Ap	|I32	|filter_read	|int idx|SV* buffer|int maxlen
Ap	|char**	|get_op_descs
Ap	|char**	|get_op_names
p	|char*	|get_no_modify
p	|U32*	|get_opargs
Ap	|PPADDR_t*|get_ppaddr
Ep	|I32	|cxinc
Afp	|void	|deb		|const char* pat|...
Ap	|void	|vdeb		|const char* pat|va_list* args
Ap	|void	|debprofdump
Ap	|I32	|debop		|OP* o
Ap	|I32	|debstack
Ap	|I32	|debstackptrs
Ap	|char*	|delimcpy	|char* to|char* toend|char* from \
				|char* fromend|int delim|I32* retlen
p	|void	|deprecate	|char* s
p	|void	|deprecate_old	|char* s
Afp	|OP*	|die		|const char* pat|...
p	|OP*	|vdie		|const char* pat|va_list* args
p	|OP*	|die_where	|char* message|STRLEN msglen
Ap	|void	|dounwind	|I32 cxix
p	|bool	|do_aexec	|SV* really|SV** mark|SV** sp
p	|bool	|do_aexec5	|SV* really|SV** mark|SV** sp|int fd|int flag
Ap	|int	|do_binmode	|PerlIO *fp|int iotype|int mode
p	|void	|do_chop	|SV* asv|SV* sv
Ap	|bool	|do_close	|GV* gv|bool not_implicit
p	|bool	|do_eof		|GV* gv
p	|bool	|do_exec	|char* cmd
#if defined(WIN32)
Ap	|int	|do_aspawn	|SV* really|SV** mark|SV** sp
Ap	|int	|do_spawn	|char* cmd
Ap	|int	|do_spawn_nowait|char* cmd
#endif
#if !defined(WIN32)
p	|bool	|do_exec3	|char* cmd|int fd|int flag
#endif
p	|void	|do_execfree
#if defined(HAS_MSG) || defined(HAS_SEM) || defined(HAS_SHM)
p	|I32	|do_ipcctl	|I32 optype|SV** mark|SV** sp
p	|I32	|do_ipcget	|I32 optype|SV** mark|SV** sp
p	|I32	|do_msgrcv	|SV** mark|SV** sp
p	|I32	|do_msgsnd	|SV** mark|SV** sp
p	|I32	|do_semop	|SV** mark|SV** sp
p	|I32	|do_shmio	|I32 optype|SV** mark|SV** sp
#endif
Ap	|void	|do_join	|SV* sv|SV* del|SV** mark|SV** sp
p	|OP*	|do_kv
Ap	|bool	|do_open	|GV* gv|char* name|I32 len|int as_raw \
				|int rawmode|int rawperm|PerlIO* supplied_fp
Ap	|bool	|do_open9	|GV *gv|char *name|I32 len|int as_raw \
				|int rawmode|int rawperm|PerlIO *supplied_fp \
				|SV *svs|I32 num
Ap	|bool	|do_openn	|GV *gv|char *name|I32 len|int as_raw \
				|int rawmode|int rawperm|PerlIO *supplied_fp \
				|SV **svp|I32 num
p	|void	|do_pipe	|SV* sv|GV* rgv|GV* wgv
p	|bool	|do_print	|SV* sv|PerlIO* fp
p	|OP*	|do_readline
p	|I32	|do_chomp	|SV* sv
p	|bool	|do_seek	|GV* gv|Off_t pos|int whence
Ap	|void	|do_sprintf	|SV* sv|I32 len|SV** sarg
p	|Off_t	|do_sysseek	|GV* gv|Off_t pos|int whence
p	|Off_t	|do_tell	|GV* gv
p	|I32	|do_trans	|SV* sv
p	|UV	|do_vecget	|SV* sv|I32 offset|I32 size
p	|void	|do_vecset	|SV* sv
p	|void	|do_vop		|I32 optype|SV* sv|SV* left|SV* right
p	|OP*	|dofile		|OP* term
Ap	|I32	|dowantarray
Ap	|void	|dump_all
Ap	|void	|dump_eval
#if defined(DUMP_FDS)
Ap	|void	|dump_fds	|char* s
#endif
Ap	|void	|dump_form	|GV* gv
Ap	|void	|gv_dump	|GV* gv
Ap	|void	|op_dump	|OP* arg
Ap	|void	|pmop_dump	|PMOP* pm
Ap	|void	|dump_packsubs	|HV* stash
Ap	|void	|dump_sub	|GV* gv
Apd	|void	|fbm_compile	|SV* sv|U32 flags
Apd	|char*	|fbm_instr	|unsigned char* big|unsigned char* bigend \
				|SV* littlesv|U32 flags
p	|char*	|find_script	|char *scriptname|bool dosearch \
				|char **search_ext|I32 flags
#if defined(USE_5005THREADS)
p	|PADOFFSET|find_threadsv|const char *name
#endif
p	|OP*	|force_list	|OP* arg
p	|OP*	|fold_constants	|OP* arg
Afpd	|char*	|form		|const char* pat|...
Ap	|char*	|vform		|const char* pat|va_list* args
Ap	|void	|free_tmps
p	|OP*	|gen_constant_list|OP* o
#if !defined(HAS_GETENV_LEN)
p	|char*	|getenv_len	|const char* key|unsigned long *len
#endif
Ap	|void	|gp_free	|GV* gv
Ap	|GP*	|gp_ref		|GP* gp
Ap	|GV*	|gv_AVadd	|GV* gv
Ap	|GV*	|gv_HVadd	|GV* gv
Ap	|GV*	|gv_IOadd	|GV* gv
Ap	|GV*	|gv_autoload4	|HV* stash|const char* name|STRLEN len \
				|I32 method
Ap	|void	|gv_check	|HV* stash
Ap	|void	|gv_efullname	|SV* sv|GV* gv
Ap	|void	|gv_efullname3	|SV* sv|GV* gv|const char* prefix
Ap	|void	|gv_efullname4	|SV* sv|GV* gv|const char* prefix|bool keepmain
Ap	|GV*	|gv_fetchfile	|const char* name
Apd	|GV*	|gv_fetchmeth	|HV* stash|const char* name|STRLEN len \
				|I32 level
Apd	|GV*	|gv_fetchmeth_autoload	|HV* stash|const char* name|STRLEN len \
				|I32 level
Apd	|GV*	|gv_fetchmethod	|HV* stash|const char* name
Apd	|GV*	|gv_fetchmethod_autoload|HV* stash|const char* name \
				|I32 autoload
Ap	|GV*	|gv_fetchpv	|const char* name|I32 add|I32 sv_type
Ap	|void	|gv_fullname	|SV* sv|GV* gv
Ap	|void	|gv_fullname3	|SV* sv|GV* gv|const char* prefix
Ap	|void	|gv_fullname4	|SV* sv|GV* gv|const char* prefix|bool keepmain
Ap	|void	|gv_init	|GV* gv|HV* stash|const char* name \
				|STRLEN len|int multi
Apd	|HV*	|gv_stashpv	|const char* name|I32 create
Ap	|HV*	|gv_stashpvn	|const char* name|U32 namelen|I32 create
Apd	|HV*	|gv_stashsv	|SV* sv|I32 create
Apd	|void	|hv_clear	|HV* tb
Ap	|void	|hv_delayfree_ent|HV* hv|HE* entry
Apd	|SV*	|hv_delete	|HV* tb|const char* key|I32 klen|I32 flags
Apd	|SV*	|hv_delete_ent	|HV* tb|SV* key|I32 flags|U32 hash
Apd	|bool	|hv_exists	|HV* tb|const char* key|I32 klen
Apd	|bool	|hv_exists_ent	|HV* tb|SV* key|U32 hash
Apd	|SV**	|hv_fetch	|HV* tb|const char* key|I32 klen|I32 lval
Apd	|HE*	|hv_fetch_ent	|HV* tb|SV* key|I32 lval|U32 hash
Ap	|void	|hv_free_ent	|HV* hv|HE* entry
Apd	|I32	|hv_iterinit	|HV* tb
Apd	|char*	|hv_iterkey	|HE* entry|I32* retlen
Apd	|SV*	|hv_iterkeysv	|HE* entry
Apd	|HE*	|hv_iternext	|HV* tb
Apd	|SV*	|hv_iternextsv	|HV* hv|char** key|I32* retlen
ApMd	|HE*	|hv_iternext_flags|HV* tb|I32 flags
Apd	|SV*	|hv_iterval	|HV* tb|HE* entry
Ap	|void	|hv_ksplit	|HV* hv|IV newmax
Apd	|void	|hv_magic	|HV* hv|GV* gv|int how
Apd	|SV**	|hv_store	|HV* tb|const char* key|I32 klen|SV* val \
				|U32 hash
Apd	|HE*	|hv_store_ent	|HV* tb|SV* key|SV* val|U32 hash
ApM	|SV**	|hv_store_flags	|HV* tb|const char* key|I32 klen|SV* val \
				|U32 hash|int flags
Apd	|void	|hv_undef	|HV* tb
Ap	|I32	|ibcmp		|const char* a|const char* b|I32 len
Ap	|I32	|ibcmp_locale	|const char* a|const char* b|I32 len
Apd	|I32	|ibcmp_utf8	|const char* a|char **pe1|UV l1|bool u1|const char* b|char **pe2|UV l2|bool u2
p	|bool	|ingroup	|Gid_t testgid|Uid_t effective
p	|void	|init_argv_symbols|int|char **
p	|void	|init_debugger
Ap	|void	|init_stacks
Ap	|void	|init_tm	|struct tm *ptm
pd	|U32	|intro_my
Ap	|char*	|instr		|const char* big|const char* little
p	|bool	|io_close	|IO* io|bool not_implicit
p	|OP*	|invert		|OP* cmd
dp	|bool	|is_gv_magical	|char *name|STRLEN len|U32 flags
Ap	|I32	|is_lvalue_sub
Ap	|U32	|to_uni_upper_lc|U32 c
Ap	|U32	|to_uni_title_lc|U32 c
Ap	|U32	|to_uni_lower_lc|U32 c
Ap	|bool	|is_uni_alnum	|UV c
Ap	|bool	|is_uni_alnumc	|UV c
Ap	|bool	|is_uni_idfirst	|UV c
Ap	|bool	|is_uni_alpha	|UV c
Ap	|bool	|is_uni_ascii	|UV c
Ap	|bool	|is_uni_space	|UV c
Ap	|bool	|is_uni_cntrl	|UV c
Ap	|bool	|is_uni_graph	|UV c
Ap	|bool	|is_uni_digit	|UV c
Ap	|bool	|is_uni_upper	|UV c
Ap	|bool	|is_uni_lower	|UV c
Ap	|bool	|is_uni_print	|UV c
Ap	|bool	|is_uni_punct	|UV c
Ap	|bool	|is_uni_xdigit	|UV c
Ap	|UV	|to_uni_upper	|UV c|U8 *p|STRLEN *lenp
Ap	|UV	|to_uni_title	|UV c|U8 *p|STRLEN *lenp
Ap	|UV	|to_uni_lower	|UV c|U8 *p|STRLEN *lenp
Ap	|UV	|to_uni_fold	|UV c|U8 *p|STRLEN *lenp
Ap	|bool	|is_uni_alnum_lc|UV c
Ap	|bool	|is_uni_alnumc_lc|UV c
Ap	|bool	|is_uni_idfirst_lc|UV c
Ap	|bool	|is_uni_alpha_lc|UV c
Ap	|bool	|is_uni_ascii_lc|UV c
Ap	|bool	|is_uni_space_lc|UV c
Ap	|bool	|is_uni_cntrl_lc|UV c
Ap	|bool	|is_uni_graph_lc|UV c
Ap	|bool	|is_uni_digit_lc|UV c
Ap	|bool	|is_uni_upper_lc|UV c
Ap	|bool	|is_uni_lower_lc|UV c
Ap	|bool	|is_uni_print_lc|UV c
Ap	|bool	|is_uni_punct_lc|UV c
Ap	|bool	|is_uni_xdigit_lc|UV c
Apd	|STRLEN	|is_utf8_char	|U8 *p
Apd	|bool	|is_utf8_string	|U8 *s|STRLEN len
Apd	|bool	|is_utf8_string_loc|U8 *s|STRLEN len|U8 **p
Ap	|bool	|is_utf8_alnum	|U8 *p
Ap	|bool	|is_utf8_alnumc	|U8 *p
Ap	|bool	|is_utf8_idfirst|U8 *p
Ap	|bool	|is_utf8_idcont	|U8 *p
Ap	|bool	|is_utf8_alpha	|U8 *p
Ap	|bool	|is_utf8_ascii	|U8 *p
Ap	|bool	|is_utf8_space	|U8 *p
Ap	|bool	|is_utf8_cntrl	|U8 *p
Ap	|bool	|is_utf8_digit	|U8 *p
Ap	|bool	|is_utf8_graph	|U8 *p
Ap	|bool	|is_utf8_upper	|U8 *p
Ap	|bool	|is_utf8_lower	|U8 *p
Ap	|bool	|is_utf8_print	|U8 *p
Ap	|bool	|is_utf8_punct	|U8 *p
Ap	|bool	|is_utf8_xdigit	|U8 *p
Ap	|bool	|is_utf8_mark	|U8 *p
p	|OP*	|jmaybe		|OP* arg
p	|I32	|keyword	|char* d|I32 len
Ap	|void	|leave_scope	|I32 base
p	|void	|lex_end
p	|void	|lex_start	|SV* line
Ap |void   |op_null    |OP* o
p	|void	|op_clear	|OP* o
p	|OP*	|linklist	|OP* o
p	|OP*	|list		|OP* o
p	|OP*	|listkids	|OP* o
Apd	|void	|load_module|U32 flags|SV* name|SV* ver|...
Ap	|void	|vload_module|U32 flags|SV* name|SV* ver|va_list* args
p	|OP*	|localize	|OP* arg|I32 lexical
Apd	|I32	|looks_like_number|SV* sv
Apd	|UV	|grok_bin	|char* start|STRLEN* len|I32* flags|NV *result
Apd	|UV	|grok_hex	|char* start|STRLEN* len|I32* flags|NV *result
Apd	|int	|grok_number	|const char *pv|STRLEN len|UV *valuep
Apd	|bool	|grok_numeric_radix|const char **sp|const char *send
Apd	|UV	|grok_oct	|char* start|STRLEN* len|I32* flags|NV *result
p	|int	|magic_clearenv	|SV* sv|MAGIC* mg
p	|int	|magic_clear_all_env|SV* sv|MAGIC* mg
p	|int	|magic_clearpack|SV* sv|MAGIC* mg
p	|int	|magic_clearsig	|SV* sv|MAGIC* mg
p	|int	|magic_existspack|SV* sv|MAGIC* mg
p	|int	|magic_freeregexp|SV* sv|MAGIC* mg
p	|int	|magic_freeovrld|SV* sv|MAGIC* mg
p	|int	|magic_get	|SV* sv|MAGIC* mg
p	|int	|magic_getarylen|SV* sv|MAGIC* mg
p	|int	|magic_getdefelem|SV* sv|MAGIC* mg
p	|int	|magic_getglob	|SV* sv|MAGIC* mg
p	|int	|magic_getnkeys	|SV* sv|MAGIC* mg
p	|int	|magic_getpack	|SV* sv|MAGIC* mg
p	|int	|magic_getpos	|SV* sv|MAGIC* mg
p	|int	|magic_getsig	|SV* sv|MAGIC* mg
p	|int	|magic_getsubstr|SV* sv|MAGIC* mg
p	|int	|magic_gettaint	|SV* sv|MAGIC* mg
p	|int	|magic_getuvar	|SV* sv|MAGIC* mg
p	|int	|magic_getvec	|SV* sv|MAGIC* mg
p	|U32	|magic_len	|SV* sv|MAGIC* mg
#if defined(USE_5005THREADS)
p	|int	|magic_mutexfree|SV* sv|MAGIC* mg
#endif
p	|int	|magic_nextpack	|SV* sv|MAGIC* mg|SV* key
p	|U32	|magic_regdata_cnt|SV* sv|MAGIC* mg
p	|int	|magic_regdatum_get|SV* sv|MAGIC* mg
p	|int	|magic_regdatum_set|SV* sv|MAGIC* mg
p	|int	|magic_set	|SV* sv|MAGIC* mg
p	|int	|magic_setamagic|SV* sv|MAGIC* mg
p	|int	|magic_setarylen|SV* sv|MAGIC* mg
p	|int	|magic_setbm	|SV* sv|MAGIC* mg
p	|int	|magic_setdbline|SV* sv|MAGIC* mg
#if defined(USE_LOCALE_COLLATE)
p	|int	|magic_setcollxfrm|SV* sv|MAGIC* mg
#endif
p	|int	|magic_setdefelem|SV* sv|MAGIC* mg
p	|int	|magic_setenv	|SV* sv|MAGIC* mg
p	|int	|magic_setfm	|SV* sv|MAGIC* mg
p	|int	|magic_setisa	|SV* sv|MAGIC* mg
p	|int	|magic_setglob	|SV* sv|MAGIC* mg
p	|int	|magic_setmglob	|SV* sv|MAGIC* mg
p	|int	|magic_setnkeys	|SV* sv|MAGIC* mg
p	|int	|magic_setpack	|SV* sv|MAGIC* mg
p	|int	|magic_setpos	|SV* sv|MAGIC* mg
p	|int	|magic_setregexp|SV* sv|MAGIC* mg
p	|int	|magic_setsig	|SV* sv|MAGIC* mg
p	|int	|magic_setsubstr|SV* sv|MAGIC* mg
p	|int	|magic_settaint	|SV* sv|MAGIC* mg
p	|int	|magic_setuvar	|SV* sv|MAGIC* mg
p	|int	|magic_setvec	|SV* sv|MAGIC* mg
p	|int	|magic_setutf8	|SV* sv|MAGIC* mg
p	|int	|magic_set_all_env|SV* sv|MAGIC* mg
p	|U32	|magic_sizepack	|SV* sv|MAGIC* mg
p	|int	|magic_wipepack	|SV* sv|MAGIC* mg
p	|void	|magicname	|char* sym|char* name|I32 namlen
Ap	|void	|markstack_grow
#if defined(USE_LOCALE_COLLATE)
p	|char*	|mem_collxfrm	|const char* s|STRLEN len|STRLEN* xlen
#endif
Afp	|SV*	|mess		|const char* pat|...
Ap	|SV*	|vmess		|const char* pat|va_list* args
p	|void	|qerror		|SV* err
Apd     |void   |sortsv         |SV ** array|size_t num_elts|SVCOMPARE_t cmp
Apd	|int	|mg_clear	|SV* sv
Apd	|int	|mg_copy	|SV* sv|SV* nsv|const char* key|I32 klen
Apd	|MAGIC*	|mg_find	|SV* sv|int type
Apd	|int	|mg_free	|SV* sv
Apd	|int	|mg_get		|SV* sv
Apd	|U32	|mg_length	|SV* sv
Apd	|void	|mg_magical	|SV* sv
Apd	|int	|mg_set		|SV* sv
Ap	|I32	|mg_size	|SV* sv
Ap	|void	|mini_mktime	|struct tm *pm
p	|OP*	|mod		|OP* o|I32 type
p	|int	|mode_from_discipline|SV* discp
Ap	|char*	|moreswitches	|char* s
p	|OP*	|my		|OP* o
Ap	|NV	|my_atof	|const char *s
#if (!defined(HAS_MEMCPY) && !defined(HAS_BCOPY)) || (!defined(HAS_MEMMOVE) && !defined(HAS_SAFE_MEMCPY) && !defined(HAS_SAFE_BCOPY))
Anp	|char*	|my_bcopy	|const char* from|char* to|I32 len
#endif
#if !defined(HAS_BZERO) && !defined(HAS_MEMSET)
Anp	|char*	|my_bzero	|char* loc|I32 len
#endif
Apr	|void	|my_exit	|U32 status
Apr	|void	|my_failure_exit
Ap	|I32	|my_fflush_all
Anp	|Pid_t	|my_fork
Anp	|void	|atfork_lock
Anp	|void	|atfork_unlock
Ap	|I32	|my_lstat
#if !defined(HAS_MEMCMP) || !defined(HAS_SANE_MEMCMP)
Anp	|I32	|my_memcmp	|const char* s1|const char* s2|I32 len
#endif
#if !defined(HAS_MEMSET)
Anp	|void*	|my_memset	|char* loc|I32 ch|I32 len
#endif
Ap	|I32	|my_pclose	|PerlIO* ptr
Ap	|PerlIO*|my_popen	|char* cmd|char* mode
Ap	|PerlIO*|my_popen_list	|char* mode|int n|SV ** args
Ap	|void	|my_setenv	|char* nam|char* val
Ap	|I32	|my_stat
Ap	|char *	|my_strftime	|char *fmt|int sec|int min|int hour|int mday|int mon|int year|int wday|int yday|int isdst
#if defined(MYSWAP)
Ap	|short	|my_swap	|short s
Ap	|long	|my_htonl	|long l
Ap	|long	|my_ntohl	|long l
#endif
p	|void	|my_unexec
Ap	|OP*	|newANONLIST	|OP* o
Ap	|OP*	|newANONHASH	|OP* o
Ap	|OP*	|newANONSUB	|I32 floor|OP* proto|OP* block
Ap	|OP*	|newASSIGNOP	|I32 flags|OP* left|I32 optype|OP* right
Ap	|OP*	|newCONDOP	|I32 flags|OP* expr|OP* trueop|OP* falseop
Apd	|CV*	|newCONSTSUB	|HV* stash|char* name|SV* sv
Ap	|void	|newFORM	|I32 floor|OP* o|OP* block
Ap	|OP*	|newFOROP	|I32 flags|char* label|line_t forline \
				|OP* sclr|OP* expr|OP*block|OP*cont
Ap	|OP*	|newLOGOP	|I32 optype|I32 flags|OP* left|OP* right
Ap	|OP*	|newLOOPEX	|I32 type|OP* label
Ap	|OP*	|newLOOPOP	|I32 flags|I32 debuggable|OP* expr|OP* block
Ap	|OP*	|newNULLLIST
Ap	|OP*	|newOP		|I32 optype|I32 flags
Ap	|void	|newPROG	|OP* o
Ap	|OP*	|newRANGE	|I32 flags|OP* left|OP* right
Ap	|OP*	|newSLICEOP	|I32 flags|OP* subscript|OP* listop
Ap	|OP*	|newSTATEOP	|I32 flags|char* label|OP* o
Ap	|CV*	|newSUB		|I32 floor|OP* o|OP* proto|OP* block
Apd	|CV*	|newXS		|char* name|XSUBADDR_t f|char* filename
Apd	|AV*	|newAV
Ap	|OP*	|newAVREF	|OP* o
Ap	|OP*	|newBINOP	|I32 type|I32 flags|OP* first|OP* last
Ap	|OP*	|newCVREF	|I32 flags|OP* o
Ap	|OP*	|newGVOP	|I32 type|I32 flags|GV* gv
Ap	|GV*	|newGVgen	|char* pack
Ap	|OP*	|newGVREF	|I32 type|OP* o
Ap	|OP*	|newHVREF	|OP* o
Apd	|HV*	|newHV
Ap	|HV*	|newHVhv	|HV* hv
Ap	|IO*	|newIO
Ap	|OP*	|newLISTOP	|I32 type|I32 flags|OP* first|OP* last
Ap	|OP*	|newPADOP	|I32 type|I32 flags|SV* sv
Ap	|OP*	|newPMOP	|I32 type|I32 flags
Ap	|OP*	|newPVOP	|I32 type|I32 flags|char* pv
Ap	|SV*	|newRV		|SV* pref
Apd	|SV*	|newRV_noinc	|SV *sv
Apd	|SV*	|newSV		|STRLEN len
Ap	|OP*	|newSVREF	|OP* o
Ap	|OP*	|newSVOP	|I32 type|I32 flags|SV* sv
Apd	|SV*	|newSViv	|IV i
Apd	|SV*	|newSVuv	|UV u
Apd	|SV*	|newSVnv	|NV n
Apd	|SV*	|newSVpv	|const char* s|STRLEN len
Apd	|SV*	|newSVpvn	|const char* s|STRLEN len
Apd	|SV*	|newSVpvn_share	|const char* s|I32 len|U32 hash
Afpd	|SV*	|newSVpvf	|const char* pat|...
Ap	|SV*	|vnewSVpvf	|const char* pat|va_list* args
Apd	|SV*	|newSVrv	|SV* rv|const char* classname
Apd	|SV*	|newSVsv	|SV* old
Ap	|OP*	|newUNOP	|I32 type|I32 flags|OP* first
Ap	|OP*	|newWHILEOP	|I32 flags|I32 debuggable|LOOP* loop \
				|I32 whileline|OP* expr|OP* block|OP* cont

Ap	|PERL_SI*|new_stackinfo|I32 stitems|I32 cxitems
Ap	|char*	|scan_vstring	|char *vstr|SV *sv
p	|PerlIO*|nextargv	|GV* gv
Ap	|char*	|ninstr		|const char* big|const char* bigend \
				|const char* little|const char* lend
p	|OP*	|oopsCV		|OP* o
Ap	|void	|op_free	|OP* arg
p	|void	|package	|OP* o
pd	|PADOFFSET|pad_alloc	|I32 optype|U32 tmptype
p	|PADOFFSET|allocmy	|char* name
pd	|PADOFFSET|pad_findmy	|char* name
p	|OP*	|oopsAV		|OP* o
p	|OP*	|oopsHV		|OP* o
pd	|void	|pad_leavemy
Apd	|SV*	|pad_sv		|PADOFFSET po
pd	|void	|pad_free	|PADOFFSET po
pd	|void	|pad_reset
pd	|void	|pad_swipe	|PADOFFSET po|bool refadjust
p	|void	|peep		|OP* o
dopM	|PerlIO*|start_glob	|SV* pattern|IO *io
#if defined(USE_5005THREADS)
Ap	|struct perl_thread*	|new_struct_thread|struct perl_thread *t
#endif
#if defined(USE_REENTRANT_API)
Ap	|void	|reentrant_size
Ap	|void	|reentrant_init
Ap	|void	|reentrant_free
Anp	|void*	|reentrant_retry|const char*|...
#endif
Ap	|void	|call_atexit	|ATEXIT_t fn|void *ptr
Apd	|I32	|call_argv	|const char* sub_name|I32 flags|char** argv
Apd	|I32	|call_method	|const char* methname|I32 flags
Apd	|I32	|call_pv	|const char* sub_name|I32 flags
Apd	|I32	|call_sv	|SV* sv|I32 flags
Ap	|void	|despatch_signals
Apd	|SV*	|eval_pv	|const char* p|I32 croak_on_error
Apd	|I32	|eval_sv	|SV* sv|I32 flags
Apd	|SV*	|get_sv		|const char* name|I32 create
Apd	|AV*	|get_av		|const char* name|I32 create
Apd	|HV*	|get_hv		|const char* name|I32 create
Apd	|CV*	|get_cv		|const char* name|I32 create
Ap	|int	|init_i18nl10n	|int printwarn
Ap	|int	|init_i18nl14n	|int printwarn
Ap	|void	|new_collate	|char* newcoll
Ap	|void	|new_ctype	|char* newctype
Ap	|void	|new_numeric	|char* newcoll
Ap	|void	|set_numeric_local
Ap	|void	|set_numeric_radix
Ap	|void	|set_numeric_standard
Apd	|void	|require_pv	|const char* pv
Apd	|void	|pack_cat	|SV *cat|char *pat|char *patend|SV **beglist|SV **endlist|SV ***next_in_list|U32 flags
Apd	|void	|packlist 	|SV *cat|char *pat|char *patend|SV **beglist|SV **endlist
p	|void	|pidgone	|Pid_t pid|int status
Ap	|void	|pmflag		|U32* pmfl|int ch
p	|OP*	|pmruntime	|OP* pm|OP* expr|OP* repl
p	|OP*	|pmtrans	|OP* o|OP* expr|OP* repl
p	|OP*	|pop_return
Ap	|void	|pop_scope
p	|OP*	|prepend_elem	|I32 optype|OP* head|OP* tail
p	|void	|push_return	|OP* o
Ap	|void	|push_scope
p	|OP*	|ref		|OP* o|I32 type
p	|OP*	|refkids	|OP* o|I32 type
Ap	|void	|regdump	|regexp* r
Ap	|SV*	|regclass_swash	|struct regnode *n|bool doinit|SV **listsvp|SV **altsvp
Ap	|I32	|pregexec	|regexp* prog|char* stringarg \
				|char* strend|char* strbeg|I32 minend \
				|SV* screamer|U32 nosave
Ap	|void	|pregfree	|struct regexp* r
Ap	|regexp*|pregcomp	|char* exp|char* xend|PMOP* pm
Ap	|char*	|re_intuit_start|regexp* prog|SV* sv|char* strpos \
				|char* strend|U32 flags \
				|struct re_scream_pos_data_s *data
Ap	|SV*	|re_intuit_string|regexp* prog
Ap	|I32	|regexec_flags	|regexp* prog|char* stringarg \
				|char* strend|char* strbeg|I32 minend \
				|SV* screamer|void* data|U32 flags
Ap	|regnode*|regnext	|regnode* p
Ep	|void	|regprop	|SV* sv|regnode* o
Ap	|void	|repeatcpy	|char* to|const char* from|I32 len|I32 count
Ap	|char*	|rninstr	|const char* big|const char* bigend \
				|const char* little|const char* lend
Ap	|Sighandler_t|rsignal	|int i|Sighandler_t t
p	|int	|rsignal_restore|int i|Sigsave_t* t
p	|int	|rsignal_save	|int i|Sighandler_t t1|Sigsave_t* t2
Ap	|Sighandler_t|rsignal_state|int i
p	|void	|rxres_free	|void** rsp
p	|void	|rxres_restore	|void** rsp|REGEXP* prx
p	|void	|rxres_save	|void** rsp|REGEXP* prx
#if !defined(HAS_RENAME)
p	|I32	|same_dirent	|char* a|char* b
#endif
Apd	|char*	|savepv		|const char* pv
Apd	|char*	|savesharedpv	|const char* pv
Apd	|char*	|savepvn	|const char* pv|I32 len
Ap	|void	|savestack_grow
Ap	|void	|savestack_grow_cnt	|I32 need
Ap	|void	|save_aelem	|AV* av|I32 idx|SV **sptr
Ap	|I32	|save_alloc	|I32 size|I32 pad
Ap	|void	|save_aptr	|AV** aptr
Ap	|AV*	|save_ary	|GV* gv
Ap	|void	|save_bool	|bool* boolp
Ap	|void	|save_clearsv	|SV** svp
Ap	|void	|save_delete	|HV* hv|char* key|I32 klen
Ap	|void	|save_destructor|DESTRUCTORFUNC_NOCONTEXT_t f|void* p
Ap	|void	|save_destructor_x|DESTRUCTORFUNC_t f|void* p
Ap	|void	|save_freesv	|SV* sv
p	|void	|save_freeop	|OP* o
Ap	|void	|save_freepv	|char* pv
Ap	|void	|save_generic_svref|SV** sptr
Ap	|void	|save_generic_pvref|char** str
Ap	|void	|save_shared_pvref|char** str
Ap	|void	|save_gp	|GV* gv|I32 empty
Ap	|HV*	|save_hash	|GV* gv
Ap	|void	|save_helem	|HV* hv|SV *key|SV **sptr
Ap	|void	|save_hints
Ap	|void	|save_hptr	|HV** hptr
Ap	|void	|save_I16	|I16* intp
Ap	|void	|save_I32	|I32* intp
Ap	|void	|save_I8	|I8* bytep
Ap	|void	|save_int	|int* intp
Ap	|void	|save_item	|SV* item
Ap	|void	|save_iv	|IV* iv
Ap	|void	|save_list	|SV** sarg|I32 maxsarg
Ap	|void	|save_long	|long* longp
Ap	|void	|save_mortalizesv|SV* sv
Ap	|void	|save_nogv	|GV* gv
p	|void	|save_op
Ap	|SV*	|save_scalar	|GV* gv
Ap	|void	|save_pptr	|char** pptr
Ap	|void	|save_vptr	|void* pptr
Ap	|void	|save_re_context
Ap	|void	|save_padsv	|PADOFFSET off
Ap	|void	|save_sptr	|SV** sptr
Ap	|SV*	|save_svref	|SV** sptr
Ap	|SV**	|save_threadsv	|PADOFFSET i
p	|OP*	|sawparens	|OP* o
p	|OP*	|scalar		|OP* o
p	|OP*	|scalarkids	|OP* o
p	|OP*	|scalarseq	|OP* o
p	|OP*	|scalarvoid	|OP* o
Apd	|NV	|scan_bin	|char* start|STRLEN len|STRLEN* retlen
Apd	|NV	|scan_hex	|char* start|STRLEN len|STRLEN* retlen
Ap	|char*	|scan_num	|char* s|YYSTYPE *lvalp
Apd	|NV	|scan_oct	|char* start|STRLEN len|STRLEN* retlen
p	|OP*	|scope		|OP* o
Ap	|char*	|screaminstr	|SV* bigsv|SV* littlesv|I32 start_shift \
				|I32 end_shift|I32 *state|I32 last
#if !defined(VMS)
p	|I32	|setenv_getix	|char* nam
#endif
p	|void	|setdefout	|GV* gv
p	|HEK*	|share_hek	|const char* sv|I32 len|U32 hash
np	|Signal_t |sighandler	|int sig
Anp	|Signal_t |csighandler	|int sig
Ap	|SV**	|stack_grow	|SV** sp|SV**p|int n
Ap	|I32	|start_subparse	|I32 is_format|U32 flags
p	|void	|sub_crush_depth|CV* cv
Apd	|bool	|sv_2bool	|SV* sv
Apd	|CV*	|sv_2cv		|SV* sv|HV** st|GV** gvp|I32 lref
Apd	|IO*	|sv_2io		|SV* sv
Apd	|IV	|sv_2iv		|SV* sv
Apd	|SV*	|sv_2mortal	|SV* sv
Apd	|NV	|sv_2nv		|SV* sv
Amb	|char*	|sv_2pv		|SV* sv|STRLEN* lp
Apd	|char*	|sv_2pvutf8	|SV* sv|STRLEN* lp
Apd	|char*	|sv_2pvbyte	|SV* sv|STRLEN* lp
Ap	|char*	|sv_pvn_nomg	|SV* sv|STRLEN* lp
Apd	|UV	|sv_2uv		|SV* sv
Apd	|IV	|sv_iv		|SV* sv
Apd	|UV	|sv_uv		|SV* sv
Apd	|NV	|sv_nv		|SV* sv
Apd	|char*	|sv_pvn		|SV *sv|STRLEN *len
Apd	|char*	|sv_pvutf8n	|SV *sv|STRLEN *len
Apd	|char*	|sv_pvbyten	|SV *sv|STRLEN *len
Apd	|I32	|sv_true	|SV *sv
pd	|void	|sv_add_arena	|char* ptr|U32 size|U32 flags
Apd	|int	|sv_backoff	|SV* sv
Apd	|SV*	|sv_bless	|SV* sv|HV* stash
Afpd	|void	|sv_catpvf	|SV* sv|const char* pat|...
Ap	|void	|sv_vcatpvf	|SV* sv|const char* pat|va_list* args
Apd	|void	|sv_catpv	|SV* sv|const char* ptr
Amdb	|void	|sv_catpvn	|SV* sv|const char* ptr|STRLEN len
Amdb	|void	|sv_catsv	|SV* dsv|SV* ssv
Apd	|void	|sv_chop	|SV* sv|char* ptr
pd	|I32	|sv_clean_all
pd	|void	|sv_clean_objs
Apd	|void	|sv_clear	|SV* sv
Apd	|I32	|sv_cmp		|SV* sv1|SV* sv2
Apd	|I32	|sv_cmp_locale	|SV* sv1|SV* sv2
#if defined(USE_LOCALE_COLLATE)
Apd	|char*	|sv_collxfrm	|SV* sv|STRLEN* nxp
#endif
Ap	|OP*	|sv_compile_2op	|SV* sv|OP** startp|char* code|PAD** padp
Apd	|int	|getcwd_sv	|SV* sv
Apd	|void	|sv_dec		|SV* sv
Ap	|void	|sv_dump	|SV* sv
Apd	|bool	|sv_derived_from|SV* sv|const char* name
Apd	|I32	|sv_eq		|SV* sv1|SV* sv2
Apd	|void	|sv_free	|SV* sv
pd	|void	|sv_free_arenas
Apd	|char*	|sv_gets	|SV* sv|PerlIO* fp|I32 append
Apd	|char*	|sv_grow	|SV* sv|STRLEN newlen
Apd	|void	|sv_inc		|SV* sv
Apd	|void	|sv_insert	|SV* bigsv|STRLEN offset|STRLEN len \
				|char* little|STRLEN littlelen
Apd	|int	|sv_isa		|SV* sv|const char* name
Apd	|int	|sv_isobject	|SV* sv
Apd	|STRLEN	|sv_len		|SV* sv
Apd	|STRLEN	|sv_len_utf8	|SV* sv
Apd	|void	|sv_magic	|SV* sv|SV* obj|int how|const char* name \
				|I32 namlen
Apd	|MAGIC *|sv_magicext	|SV* sv|SV* obj|int how|MGVTBL *vtbl \
				| const char* name|I32 namlen	
Apd	|SV*	|sv_mortalcopy	|SV* oldsv
Apd	|SV*	|sv_newmortal
Apd	|SV*	|sv_newref	|SV* sv
Ap	|char*	|sv_peek	|SV* sv
Apd	|void	|sv_pos_u2b	|SV* sv|I32* offsetp|I32* lenp
Apd	|void	|sv_pos_b2u	|SV* sv|I32* offsetp
Amdb	|char*	|sv_pvn_force	|SV* sv|STRLEN* lp
Apd	|char*	|sv_pvutf8n_force|SV* sv|STRLEN* lp
Apd	|char*	|sv_pvbyten_force|SV* sv|STRLEN* lp
Apd	|char*	|sv_recode_to_utf8	|SV* sv|SV *encoding
Apd	|bool	|sv_cat_decode	|SV* dsv|SV *encoding|SV *ssv|int *offset \
				|char* tstr|int tlen
Apd	|char*	|sv_reftype	|SV* sv|int ob
Apd	|void	|sv_replace	|SV* sv|SV* nsv
Apd	|void	|sv_report_used
Apd	|void	|sv_reset	|char* s|HV* stash
Afpd	|void	|sv_setpvf	|SV* sv|const char* pat|...
Ap	|void	|sv_vsetpvf	|SV* sv|const char* pat|va_list* args
Apd	|void	|sv_setiv	|SV* sv|IV num
Apdb	|void	|sv_setpviv	|SV* sv|IV num
Apd	|void	|sv_setuv	|SV* sv|UV num
Apd	|void	|sv_setnv	|SV* sv|NV num
Apd	|SV*	|sv_setref_iv	|SV* rv|const char* classname|IV iv
Apd	|SV*	|sv_setref_uv	|SV* rv|const char* classname|UV uv
Apd	|SV*	|sv_setref_nv	|SV* rv|const char* classname|NV nv
Apd	|SV*	|sv_setref_pv	|SV* rv|const char* classname|void* pv
Apd	|SV*	|sv_setref_pvn	|SV* rv|const char* classname|char* pv \
				|STRLEN n
Apd	|void	|sv_setpv	|SV* sv|const char* ptr
Apd	|void	|sv_setpvn	|SV* sv|const char* ptr|STRLEN len
Amdb	|void	|sv_setsv	|SV* dsv|SV* ssv
Apd	|void	|sv_taint	|SV* sv
Apd	|bool	|sv_tainted	|SV* sv
Apd	|int	|sv_unmagic	|SV* sv|int type
Apd	|void	|sv_unref	|SV* sv
Apd	|void	|sv_unref_flags	|SV* sv|U32 flags
Apd	|void	|sv_untaint	|SV* sv
Apd	|bool	|sv_upgrade	|SV* sv|U32 mt
Apd	|void	|sv_usepvn	|SV* sv|char* ptr|STRLEN len
Apd	|void	|sv_vcatpvfn	|SV* sv|const char* pat|STRLEN patlen \
				|va_list* args|SV** svargs|I32 svmax \
				|bool *maybe_tainted
Apd	|void	|sv_vsetpvfn	|SV* sv|const char* pat|STRLEN patlen \
				|va_list* args|SV** svargs|I32 svmax \
				|bool *maybe_tainted
Ap	|NV	|str_to_version	|SV *sv
Ap	|SV*	|swash_init	|char* pkg|char* name|SV* listsv \
				|I32 minbits|I32 none
Ap	|UV	|swash_fetch	|SV *sv|U8 *ptr|bool do_utf8
Ap	|void	|taint_env
Ap	|void	|taint_proper	|const char* f|const char* s
Apd	|UV	|to_utf8_case	|U8 *p|U8* ustrp|STRLEN *lenp \
				|SV **swash|char *normal|char *special
Apd	|UV	|to_utf8_lower	|U8 *p|U8* ustrp|STRLEN *lenp
Apd	|UV	|to_utf8_upper	|U8 *p|U8* ustrp|STRLEN *lenp
Apd	|UV	|to_utf8_title	|U8 *p|U8* ustrp|STRLEN *lenp
Apd	|UV	|to_utf8_fold	|U8 *p|U8* ustrp|STRLEN *lenp
#if defined(UNLINK_ALL_VERSIONS)
Ap	|I32	|unlnk		|char* f
#endif
#if defined(USE_5005THREADS)
Ap	|void	|unlock_condpair|void* svv
#endif
Apd	|I32	|unpack_str	|char *pat|char *patend|char *s|char *strbeg|char *strend|char **new_s|I32 ocnt|U32 flags
Apd	|I32	|unpackstring	|char *pat|char *patend|char *s|char *strend|U32 flags
Ap	|void	|unsharepvn	|const char* sv|I32 len|U32 hash
p	|void	|unshare_hek	|HEK* hek
p	|void	|utilize	|int aver|I32 floor|OP* version|OP* idop|OP* arg
Ap	|U8*	|utf16_to_utf8	|U8* p|U8 *d|I32 bytelen|I32 *newlen
Ap	|U8*	|utf16_to_utf8_reversed|U8* p|U8 *d|I32 bytelen|I32 *newlen
Adp	|STRLEN	|utf8_length	|U8* s|U8 *e
Apd	|IV	|utf8_distance	|U8 *a|U8 *b
Apd	|U8*	|utf8_hop	|U8 *s|I32 off
ApMd	|U8*	|utf8_to_bytes	|U8 *s|STRLEN *len
ApMd	|U8*	|bytes_from_utf8|U8 *s|STRLEN *len|bool *is_utf8
ApMd	|U8*	|bytes_to_utf8	|U8 *s|STRLEN *len
Apd	|UV	|utf8_to_uvchr	|U8 *s|STRLEN* retlen
Apd	|UV	|utf8_to_uvuni	|U8 *s|STRLEN* retlen
Adp	|UV	|utf8n_to_uvchr	|U8 *s|STRLEN curlen|STRLEN* retlen|U32 flags
Adp	|UV	|utf8n_to_uvuni	|U8 *s|STRLEN curlen|STRLEN* retlen|U32 flags
Apd	|U8*	|uvchr_to_utf8	|U8 *d|UV uv
Ap	|U8*	|uvuni_to_utf8	|U8 *d|UV uv
Ap	|U8*	|uvchr_to_utf8_flags	|U8 *d|UV uv|UV flags
Apd	|U8*	|uvuni_to_utf8_flags	|U8 *d|UV uv|UV flags
Apd	|char*	|pv_uni_display	|SV *dsv|U8 *spv|STRLEN len \
				|STRLEN pvlim|UV flags
Apd	|char*	|sv_uni_display	|SV *dsv|SV *ssv|STRLEN pvlim|UV flags
p	|void	|vivify_defelem	|SV* sv
p	|void	|vivify_ref	|SV* sv|U32 to_what
p	|I32	|wait4pid	|Pid_t pid|int* statusp|int flags
p	|U32	|parse_unicode_opts|char **popt
p	|U32	|seed
p	|UV	|get_hash_seed
p	|void	|report_evil_fh	|GV *gv|IO *io|I32 op
pd	|void	|report_uninit
Afpd	|void	|warn		|const char* pat|...
Ap	|void	|vwarn		|const char* pat|va_list* args
Afp	|void	|warner		|U32 err|const char* pat|...
Ap	|void	|vwarner	|U32 err|const char* pat|va_list* args
p	|void	|watch		|char** addr
Ap	|I32	|whichsig	|char* sig
p	|void	|write_to_stderr|const char* message|int msglen
p	|int	|yyerror	|char* s
#ifdef USE_PURE_BISON
p	|int	|yylex_r	|YYSTYPE *lvalp|int *lcharp
#endif
p	|int	|yylex
p	|int	|yyparse
p	|int	|yywarn		|char* s
#if defined(MYMALLOC)
Ap	|void	|dump_mstats	|char* s
Ap	|int	|get_mstats	|perl_mstats_t *buf|int buflen|int level
#endif
Anp	|Malloc_t|safesysmalloc	|MEM_SIZE nbytes
Anp	|Malloc_t|safesyscalloc	|MEM_SIZE elements|MEM_SIZE size
Anp	|Malloc_t|safesysrealloc|Malloc_t where|MEM_SIZE nbytes
Anp	|Free_t	|safesysfree	|Malloc_t where
#if defined(PERL_GLOBAL_STRUCT)
Ap	|struct perl_vars *|GetVars
#endif
Ap	|int	|runops_standard
Ap	|int	|runops_debug
#if defined(USE_5005THREADS)
Ap	|SV*	|sv_lock	|SV *sv
#endif
Afpd	|void	|sv_catpvf_mg	|SV *sv|const char* pat|...
Ap	|void	|sv_vcatpvf_mg	|SV* sv|const char* pat|va_list* args
Apd	|void	|sv_catpv_mg	|SV *sv|const char *ptr
Apd	|void	|sv_catpvn_mg	|SV *sv|const char *ptr|STRLEN len
Apd	|void	|sv_catsv_mg	|SV *dstr|SV *sstr
Afpd	|void	|sv_setpvf_mg	|SV *sv|const char* pat|...
Ap	|void	|sv_vsetpvf_mg	|SV* sv|const char* pat|va_list* args
Apd	|void	|sv_setiv_mg	|SV *sv|IV i
Apdb	|void	|sv_setpviv_mg	|SV *sv|IV iv
Apd	|void	|sv_setuv_mg	|SV *sv|UV u
Apd	|void	|sv_setnv_mg	|SV *sv|NV num
Apd	|void	|sv_setpv_mg	|SV *sv|const char *ptr
Apd	|void	|sv_setpvn_mg	|SV *sv|const char *ptr|STRLEN len
Apd	|void	|sv_setsv_mg	|SV *dstr|SV *sstr
Apd	|void	|sv_usepvn_mg	|SV *sv|char *ptr|STRLEN len
Ap	|MGVTBL*|get_vtbl	|int vtbl_id
Ap	|char*	|pv_display	|SV *dsv|char *pv|STRLEN cur|STRLEN len \
				|STRLEN pvlim
Afp	|void	|dump_indent	|I32 level|PerlIO *file|const char* pat|...
Ap	|void	|dump_vindent	|I32 level|PerlIO *file|const char* pat \
				|va_list *args
Ap	|void	|do_gv_dump	|I32 level|PerlIO *file|char *name|GV *sv
Ap	|void	|do_gvgv_dump	|I32 level|PerlIO *file|char *name|GV *sv
Ap	|void	|do_hv_dump	|I32 level|PerlIO *file|char *name|HV *sv
Ap	|void	|do_magic_dump	|I32 level|PerlIO *file|MAGIC *mg|I32 nest \
				|I32 maxnest|bool dumpops|STRLEN pvlim
Ap	|void	|do_op_dump	|I32 level|PerlIO *file|OP *o
Ap	|void	|do_pmop_dump	|I32 level|PerlIO *file|PMOP *pm
Ap	|void	|do_sv_dump	|I32 level|PerlIO *file|SV *sv|I32 nest \
				|I32 maxnest|bool dumpops|STRLEN pvlim
Ap	|void	|magic_dump	|MAGIC *mg
#if defined(PERL_FLEXIBLE_EXCEPTIONS)
Ap	|void*	|default_protect|volatile JMPENV *je|int *excpt \
				|protect_body_t body|...
Ap	|void*	|vdefault_protect|volatile JMPENV *je|int *excpt \
				|protect_body_t body|va_list *args
#endif
Ap	|void	|reginitcolors
Apd	|char*	|sv_2pv_nolen	|SV* sv
Apd	|char*	|sv_2pvutf8_nolen|SV* sv
Apd	|char*	|sv_2pvbyte_nolen|SV* sv
Amdb	|char*	|sv_pv		|SV *sv
Amdb	|char*	|sv_pvutf8	|SV *sv
Amdb	|char*	|sv_pvbyte	|SV *sv
Amdb	|STRLEN	|sv_utf8_upgrade|SV *sv
ApdM	|bool	|sv_utf8_downgrade|SV *sv|bool fail_ok
Apd	|void	|sv_utf8_encode |SV *sv
ApdM	|bool	|sv_utf8_decode |SV *sv
Apd	|void	|sv_force_normal|SV *sv
Apd	|void	|sv_force_normal_flags|SV *sv|U32 flags
Ap	|void	|tmps_grow	|I32 n
Apd	|SV*	|sv_rvweaken	|SV *sv
p	|int	|magic_killbackrefs|SV *sv|MAGIC *mg
Ap	|OP*	|newANONATTRSUB	|I32 floor|OP *proto|OP *attrs|OP *block
Ap	|CV*	|newATTRSUB	|I32 floor|OP *o|OP *proto|OP *attrs|OP *block
Ap	|void	|newMYSUB	|I32 floor|OP *o|OP *proto|OP *attrs|OP *block
p	|OP *	|my_attrs	|OP *o|OP *attrs
p	|void	|boot_core_xsutils
#if defined(USE_ITHREADS)
Ap	|PERL_CONTEXT*|cx_dup	|PERL_CONTEXT* cx|I32 ix|I32 max|CLONE_PARAMS* param
Ap	|PERL_SI*|si_dup	|PERL_SI* si|CLONE_PARAMS* param
Ap	|ANY*	|ss_dup		|PerlInterpreter* proto_perl|CLONE_PARAMS* param
Ap	|void*	|any_dup	|void* v|PerlInterpreter* proto_perl
Ap	|HE*	|he_dup		|HE* e|bool shared|CLONE_PARAMS* param
Ap	|REGEXP*|re_dup		|REGEXP* r|CLONE_PARAMS* param
Ap	|PerlIO*|fp_dup		|PerlIO* fp|char type|CLONE_PARAMS* param
Ap	|DIR*	|dirp_dup	|DIR* dp
Ap	|GP*	|gp_dup		|GP* gp|CLONE_PARAMS* param
Ap	|MAGIC*	|mg_dup		|MAGIC* mg|CLONE_PARAMS* param
Ap	|SV*	|sv_dup		|SV* sstr|CLONE_PARAMS* param
#if defined(HAVE_INTERP_INTERN)
Ap	|void	|sys_intern_dup	|struct interp_intern* src \
				|struct interp_intern* dst
#endif
Ap	|PTR_TBL_t*|ptr_table_new
Ap	|void*	|ptr_table_fetch|PTR_TBL_t *tbl|void *sv
Ap	|void	|ptr_table_store|PTR_TBL_t *tbl|void *oldsv|void *newsv
Ap	|void	|ptr_table_split|PTR_TBL_t *tbl
Ap	|void	|ptr_table_clear|PTR_TBL_t *tbl
Ap	|void	|ptr_table_free|PTR_TBL_t *tbl
#endif
#if defined(HAVE_INTERP_INTERN)
Ap	|void	|sys_intern_clear
Ap	|void	|sys_intern_init
#endif

Ap	|char *	|custom_op_name	|OP* op
Ap	|char *	|custom_op_desc	|OP* op

Adp	|void	|sv_nosharing	|SV *
Adp	|void	|sv_nolocking	|SV *
Adp	|void	|sv_nounlocking	|SV *
Adp	|int	|nothreadhook

END_EXTERN_C

#if defined(PERL_IN_AV_C) || defined(PERL_DECL_PROT)
s	|I32	|avhv_index_sv	|SV* sv
s	|I32	|avhv_index	|AV* av|SV* sv|U32 hash
#endif

#if defined(PERL_IN_DOOP_C) || defined(PERL_DECL_PROT)
s	|I32	|do_trans_simple	|SV *sv
s	|I32	|do_trans_count		|SV *sv
s	|I32	|do_trans_complex	|SV *sv
s	|I32	|do_trans_simple_utf8	|SV *sv
s	|I32	|do_trans_count_utf8	|SV *sv
s	|I32	|do_trans_complex_utf8	|SV *sv
#endif

#if defined(PERL_IN_GV_C) || defined(PERL_DECL_PROT)
s	|void	|gv_init_sv	|GV *gv|I32 sv_type
s	|void	|require_errno	|GV *gv
#endif

#if defined(PERL_IN_HV_C) || defined(PERL_DECL_PROT)
s	|void	|hsplit		|HV *hv
s	|void	|hfreeentries	|HV *hv
s	|void	|more_he
s	|HE*	|new_he
s	|void	|del_he		|HE *p
s	|HEK*	|save_hek_flags	|const char *str|I32 len|U32 hash|int flags
s	|void	|hv_magic_check	|HV *hv|bool *needs_copy|bool *needs_store
s	|void	|unshare_hek_or_pvn|HEK* hek|const char* sv|I32 len|U32 hash
s	|HEK*	|share_hek_flags|const char* sv|I32 len|U32 hash|int flags
s	|SV**	|hv_fetch_flags	|HV* tb|const char* key|I32 klen|I32 lval \
                                |int flags
s	|void	|hv_notallowed	|int flags|const char *key|I32 klen|const char *msg
#endif

#if defined(PERL_IN_MG_C) || defined(PERL_DECL_PROT)
s	|void	|save_magic	|I32 mgs_ix|SV *sv
s	|int	|magic_methpack	|SV *sv|MAGIC *mg|char *meth
s	|int	|magic_methcall	|SV *sv|MAGIC *mg|char *meth|I32 f \
				|int n|SV *val
#endif

#if defined(PERL_IN_OP_C) || defined(PERL_DECL_PROT)
s	|I32	|list_assignment|OP *o
s	|void	|bad_type	|I32 n|char *t|char *name|OP *kid
s	|void	|cop_free	|COP *cop
s	|OP*	|modkids	|OP *o|I32 type
s	|void	|no_bareword_allowed|OP *o
s	|OP*	|no_fh_allowed	|OP *o
s	|OP*	|scalarboolean	|OP *o
s	|OP*	|too_few_arguments|OP *o|char* name
s	|OP*	|too_many_arguments|OP *o|char* name
s	|OP*	|newDEFSVOP
s	|OP*	|new_logop	|I32 type|I32 flags|OP **firstp|OP **otherp
s	|void	|simplify_sort	|OP *o
s	|bool	|is_handle_constructor	|OP *o|I32 argnum
s	|char*	|gv_ename	|GV *gv
s	|bool	|scalar_mod_type|OP *o|I32 type
s	|OP *	|my_kid		|OP *o|OP *attrs|OP **imopsp
s	|OP *	|dup_attrlist	|OP *o
s	|void	|apply_attrs	|HV *stash|SV *target|OP *attrs|bool for_my
s	|void	|apply_attrs_my	|HV *stash|OP *target|OP *attrs|OP **imopsp
#endif
#if defined(PL_OP_SLAB_ALLOC)
Ap	|void*	|Slab_Alloc	|int m|size_t sz
Ap	|void	|Slab_Free	|void *op
#endif

#if defined(PERL_IN_PERL_C) || defined(PERL_DECL_PROT)
s	|void	|find_beginning
s	|void	|forbid_setid	|char *
s	|void	|incpush	|char *|int|int|int
s	|void	|init_interp
s	|void	|init_ids
s	|void	|init_lexer
s	|void	|init_main_stash
s	|void	|init_perllib
s	|void	|init_postdump_symbols|int|char **|char **
s	|void	|init_predump_symbols
rs	|void	|my_exit_jump
s	|void	|nuke_stacks
s	|void	|open_script	|char *|bool|SV *|int *fd
s	|void	|usage		|char *
s	|void	|validate_suid	|char *|char*|int
#  if defined(IAMSUID)
s	|int	|fd_on_nosuid_fs|int fd
#  endif
s	|void*	|parse_body	|char **env|XSINIT_t xsinit
s	|void*	|run_body	|I32 oldscope
s	|void	|call_body	|OP *myop|int is_eval
s	|void*	|call_list_body	|CV *cv
#if defined(PERL_FLEXIBLE_EXCEPTIONS)
s	|void*	|vparse_body	|va_list args
s	|void*	|vrun_body	|va_list args
s	|void*	|vcall_body	|va_list args
s	|void*	|vcall_list_body|va_list args
#endif
#  if defined(USE_5005THREADS)
s	|struct perl_thread *	|init_main_thread
#  endif
#endif

#if defined(PERL_IN_PP_C) || defined(PERL_DECL_PROT)
s	|SV*	|refto		|SV* sv
#endif

#if defined(PERL_IN_PP_PACK_C) || defined(PERL_DECL_PROT)
s	|I32	|unpack_rec	|tempsym_t* symptr|char *s|char *strbeg|char *strend|char **new_s
s	|SV **	|pack_rec	|SV *cat|tempsym_t* symptr|SV **beglist|SV **endlist
s	|SV*	|mul128		|SV *sv|U8 m
s	|I32	|measure_struct	|tempsym_t* symptr
s	|char *	|group_end	|char *pat|char *patend|char ender
s	|char *	|get_num	|char *ppat|I32 *
s	|bool	|next_symbol	|tempsym_t* symptr
s	|void	|doencodes	|SV* sv|char* s|I32 len
s	|SV*	|is_an_int	|char *s|STRLEN l
s	|int	|div128		|SV *pnum|bool *done
#endif

#if defined(PERL_IN_PP_CTL_C) || defined(PERL_DECL_PROT)
s	|OP*	|docatch	|OP *o
s	|void*	|docatch_body
#if defined(PERL_FLEXIBLE_EXCEPTIONS)
s	|void*	|vdocatch_body	|va_list args
#endif
s	|OP*	|dofindlabel	|OP *o|char *label|OP **opstack|OP **oplimit
s	|void	|doparseform	|SV *sv
s	|I32	|dopoptoeval	|I32 startingblock
s	|I32	|dopoptolabel	|char *label
s	|I32	|dopoptoloop	|I32 startingblock
s	|I32	|dopoptosub	|I32 startingblock
s	|I32	|dopoptosub_at	|PERL_CONTEXT* cxstk|I32 startingblock
s	|void	|save_lines	|AV *array|SV *sv
s	|OP*	|doeval		|int gimme|OP** startop|CV* outside|U32 seq
s	|PerlIO *|doopen_pm	|const char *name|const char *mode
s	|bool	|path_is_absolute|char *name
#endif

#if defined(PERL_IN_PP_HOT_C) || defined(PERL_DECL_PROT)
s	|int	|do_maybe_phash	|AV *ary|SV **lelem|SV **firstlelem \
				|SV **relem|SV **lastrelem
s	|void	|do_oddball	|HV *hash|SV **relem|SV **firstrelem
s	|CV*	|get_db_sub	|SV **svp|CV *cv
s	|SV*	|method_common	|SV* meth|U32* hashp
#endif

#if defined(PERL_IN_PP_SYS_C) || defined(PERL_DECL_PROT)
s	|OP*	|doform		|CV *cv|GV *gv|OP *retop
s	|int	|emulate_eaccess|const char* path|Mode_t mode
#  if !defined(HAS_MKDIR) || !defined(HAS_RMDIR)
s	|int	|dooneliner	|char *cmd|char *filename
#  endif
#endif

#if defined(PERL_IN_REGCOMP_C) || defined(PERL_DECL_PROT)
Es	|regnode*|reg		|struct RExC_state_t*|I32|I32 *
Es	|regnode*|reganode	|struct RExC_state_t*|U8|U32
Es	|regnode*|regatom	|struct RExC_state_t*|I32 *
Es	|regnode*|regbranch	|struct RExC_state_t*|I32 *|I32
Es	|void	|reguni		|struct RExC_state_t*|UV|char *|STRLEN*
Es	|regnode*|regclass	|struct RExC_state_t*
Es	|I32	|regcurly	|char *
Es	|regnode*|reg_node	|struct RExC_state_t*|U8
Es	|regnode*|regpiece	|struct RExC_state_t*|I32 *
Es	|void	|reginsert	|struct RExC_state_t*|U8|regnode *
Es	|void	|regoptail	|struct RExC_state_t*|regnode *|regnode *
Es	|void	|regtail	|struct RExC_state_t*|regnode *|regnode *
Es	|char*|regwhite	|char *|char *
Es	|char*|nextchar	|struct RExC_state_t*
#  ifdef DEBUGGING
Es	|regnode*|dumpuntil	|regnode *start|regnode *node \
				|regnode *last|SV* sv|I32 l
Es	|void	|put_byte	|SV* sv|int c
#  endif
Es	|void	|scan_commit	|struct RExC_state_t*|struct scan_data_t *data
Es	|void	|cl_anything	|struct RExC_state_t*|struct regnode_charclass_class *cl
Es	|int	|cl_is_anything	|struct regnode_charclass_class *cl
Es	|void	|cl_init	|struct RExC_state_t*|struct regnode_charclass_class *cl
Es	|void	|cl_init_zero	|struct RExC_state_t*|struct regnode_charclass_class *cl
Es	|void	|cl_and		|struct regnode_charclass_class *cl \
				|struct regnode_charclass_class *and_with
Es	|void	|cl_or		|struct RExC_state_t*|struct regnode_charclass_class *cl \
				|struct regnode_charclass_class *or_with
Es	|I32	|study_chunk	|struct RExC_state_t*|regnode **scanp|I32 *deltap \
				|regnode *last|struct scan_data_t *data \
				|U32 flags
Es	|I32	|add_data	|struct RExC_state_t*|I32 n|char *s
rs	|void|re_croak2	|const char* pat1|const char* pat2|...
Es	|I32	|regpposixcc	|struct RExC_state_t*|I32 value
Es	|void	|checkposixcc	|struct RExC_state_t*
#endif

#if defined(PERL_IN_REGEXEC_C) || defined(PERL_DECL_PROT)
Es	|I32	|regmatch	|regnode *prog
Es	|I32	|regrepeat	|regnode *p|I32 max
Es	|I32	|regrepeat_hard	|regnode *p|I32 max|I32 *lp
Es	|I32	|regtry		|regexp *prog|char *startpos
Es	|bool	|reginclass	|regnode *n|U8 *p|STRLEN *lenp|bool do_utf8sv_is_utf8
Es	|CHECKPOINT|regcppush	|I32 parenfloor
Es	|char*|regcppop
Es	|char*|regcp_set_to	|I32 ss
Es	|void	|cache_re	|regexp *prog
Es	|U8*	|reghop		|U8 *pos|I32 off
Es	|U8*	|reghop3	|U8 *pos|I32 off|U8 *lim
Es	|U8*	|reghopmaybe	|U8 *pos|I32 off
Es	|U8*	|reghopmaybe3	|U8 *pos|I32 off|U8 *lim
Es	|char*	|find_byclass	|regexp * prog|regnode *c|char *s|char *strend|char *startpos|I32 norun
Es	|void	|to_utf8_substr	|regexp * prog
Es	|void	|to_byte_substr	|regexp * prog
#endif

#if defined(PERL_IN_DUMP_C) || defined(PERL_DECL_PROT)
s	|CV*	|deb_curcv	|I32 ix
s	|void	|debprof	|OP *o
#endif

#if defined(PERL_IN_SCOPE_C) || defined(PERL_DECL_PROT)
s	|SV*	|save_scalar_at	|SV **sptr
#endif

#if defined(PERL_IN_SV_C) || defined(PERL_DECL_PROT)
s	|IV	|asIV		|SV* sv
s	|UV	|asUV		|SV* sv
s	|SV*	|more_sv
s	|void	|more_xiv
s	|void	|more_xnv
s	|void	|more_xpv
s	|void	|more_xpviv
s	|void	|more_xpvnv
s	|void	|more_xpvcv
s	|void	|more_xpvav
s	|void	|more_xpvhv
s	|void	|more_xpvmg
s	|void	|more_xpvlv
s	|void	|more_xpvbm
s	|void	|more_xrv
s	|XPVIV*	|new_xiv
s	|XPVNV*	|new_xnv
s	|XPV*	|new_xpv
s	|XPVIV*	|new_xpviv
s	|XPVNV*	|new_xpvnv
s	|XPVCV*	|new_xpvcv
s	|XPVAV*	|new_xpvav
s	|XPVHV*	|new_xpvhv
s	|XPVMG*	|new_xpvmg
s	|XPVLV*	|new_xpvlv
s	|XPVBM*	|new_xpvbm
s	|XRV*	|new_xrv
s	|void	|del_xiv	|XPVIV* p
s	|void	|del_xnv	|XPVNV* p
s	|void	|del_xpv	|XPV* p
s	|void	|del_xpviv	|XPVIV* p
s	|void	|del_xpvnv	|XPVNV* p
s	|void	|del_xpvcv	|XPVCV* p
s	|void	|del_xpvav	|XPVAV* p
s	|void	|del_xpvhv	|XPVHV* p
s	|void	|del_xpvmg	|XPVMG* p
s	|void	|del_xpvlv	|XPVLV* p
s	|void	|del_xpvbm	|XPVBM* p
s	|void	|del_xrv	|XRV* p
s	|void	|sv_unglob	|SV* sv
s	|void	|not_a_number	|SV *sv
s	|I32	|visit		|SVFUNC_t f
s	|void	|sv_add_backref	|SV *tsv|SV *sv
s	|void	|sv_del_backref	|SV *sv
#  ifdef DEBUGGING
s	|void	|del_sv	|SV *p
#  endif
#  if !defined(NV_PRESERVES_UV)
s	|int	|sv_2iuv_non_preserve	|SV *sv|I32 numtype
#  endif
s	|I32	|expect_number	|char** pattern
#
#  if defined(USE_ITHREADS)
s	|SV*	|gv_share	|SV *sv|CLONE_PARAMS *param
#  endif
s 	|bool	|utf8_mg_pos	|SV *sv|MAGIC **mgp|STRLEN **cachep|I32 i|I32 *offsetp|I32 uoff|U8 **sp|U8 *start|U8 *send
s	|bool	|utf8_mg_pos_init	|SV *sv|MAGIC **mgp|STRLEN **cachep|I32 i|I32 *offsetp|U8 *s|U8 *start
#endif

#if defined(PERL_IN_TOKE_C) || defined(PERL_DECL_PROT)
s	|void	|check_uni
s	|void	|force_next	|I32 type
s	|char*	|force_version	|char *start|int guessing
s	|char*	|force_word	|char *start|int token|int check_keyword \
				|int allow_pack|int allow_tick
s	|SV*	|tokeq		|SV *sv
s	|int	|pending_ident
s	|char*	|scan_const	|char *start
s	|char*	|scan_formline	|char *s
s	|char*	|scan_heredoc	|char *s
s	|char*	|scan_ident	|char *s|char *send|char *dest \
				|STRLEN destlen|I32 ck_uni
s	|char*	|scan_inputsymbol|char *start
s	|char*	|scan_pat	|char *start|I32 type
s	|char*	|scan_str	|char *start|int keep_quoted|int keep_delims
s	|char*	|scan_subst	|char *start
s	|char*	|scan_trans	|char *start
s	|char*	|scan_word	|char *s|char *dest|STRLEN destlen \
				|int allow_package|STRLEN *slp
s	|char*	|skipspace	|char *s
s	|char*	|swallow_bom	|U8 *s
s	|void	|checkcomma	|char *s|char *name|char *what
s	|void	|force_ident	|char *s|int kind
s	|void	|incline	|char *s
s	|int	|intuit_method	|char *s|GV *gv
s	|int	|intuit_more	|char *s
s	|I32	|lop		|I32 f|int x|char *s
s	|void	|missingterm	|char *s
s	|void	|no_op		|char *what|char *s
s	|void	|set_csh
s	|I32	|sublex_done
s	|I32	|sublex_push
s	|I32	|sublex_start
s	|char *	|filter_gets	|SV *sv|PerlIO *fp|STRLEN append
s	|HV *	|find_in_my_stash|char *pkgname|I32 len
s	|SV*	|new_constant	|char *s|STRLEN len|const char *key|SV *sv \
				|SV *pv|const char *type
#  if defined(DEBUGGING)
s	|void	|tokereport	|char *thing|char *s|I32 rv
#  endif
s	|int	|ao		|int toketype
s	|void	|depcom
s	|char*	|incl_perldb
#if 0
s	|I32	|utf16_textfilter|int idx|SV *sv|int maxlen
s	|I32	|utf16rev_textfilter|int idx|SV *sv|int maxlen
#endif
#  if defined(PERL_CR_FILTER)
s	|I32	|cr_textfilter	|int idx|SV *sv|int maxlen
#  endif
#endif

#if defined(PERL_IN_UNIVERSAL_C) || defined(PERL_DECL_PROT)
s	|SV*|isa_lookup	|HV *stash|const char *name|HV *name_stash|int len|int level
#endif

#if defined(PERL_IN_LOCALE_C) || defined(PERL_DECL_PROT)
s	|char*	|stdize_locale	|char* locs
#endif

#if defined(PERL_IN_UTIL_C) || defined(PERL_DECL_PROT)
s	|COP*	|closest_cop	|COP *cop|OP *o
s	|SV*	|mess_alloc
#endif

#if defined(PERL_IN_NUMERIC_C) || defined(PERL_DECL_PROT)
sn	|NV|mulexp10	|NV value|I32 exponent
#endif

START_EXTERN_C

Apd	|void	|sv_setsv_flags	|SV* dsv|SV* ssv|I32 flags
Apd	|void	|sv_catpvn_flags|SV* sv|const char* ptr|STRLEN len|I32 flags
Apd	|void	|sv_catsv_flags	|SV* dsv|SV* ssv|I32 flags
Apd	|STRLEN	|sv_utf8_upgrade_flags|SV *sv|I32 flags
Apd	|char*	|sv_pvn_force_flags|SV* sv|STRLEN* lp|I32 flags
Apd	|char*	|sv_2pv_flags	|SV* sv|STRLEN* lp|I32 flags
Apd	|void	|sv_copypv	|SV* dsv|SV* ssv
Ap	|char*	|my_atof2	|const char *s|NV* value
Apn	|int	|my_socketpair	|int family|int type|int protocol|int fd[2]

#if defined(USE_PERLIO) && !defined(USE_SFIO)
Ap	|int	|PerlIO_close		|PerlIO *
Ap	|int	|PerlIO_fill		|PerlIO *
Ap	|int	|PerlIO_fileno		|PerlIO *
Ap	|int	|PerlIO_eof		|PerlIO *
Ap	|int	|PerlIO_error		|PerlIO *
Ap	|int	|PerlIO_flush		|PerlIO *
Ap	|void	|PerlIO_clearerr	|PerlIO *
Ap	|void	|PerlIO_set_cnt		|PerlIO *|int
Ap	|void	|PerlIO_set_ptrcnt	|PerlIO *|STDCHAR *|int
Ap	|void	|PerlIO_setlinebuf	|PerlIO *
Ap	|SSize_t|PerlIO_read		|PerlIO *|void *|Size_t
Ap	|SSize_t|PerlIO_write		|PerlIO *|const void *|Size_t
Ap	|SSize_t|PerlIO_unread		|PerlIO *|const void *|Size_t
Ap	|Off_t	|PerlIO_tell		|PerlIO *
Ap	|int	|PerlIO_seek		|PerlIO *|Off_t|int

Ap	|STDCHAR *|PerlIO_get_base	|PerlIO *
Ap	|STDCHAR *|PerlIO_get_ptr	|PerlIO *
Ap	|int	  |PerlIO_get_bufsiz	|PerlIO *
Ap	|int	  |PerlIO_get_cnt	|PerlIO *

Ap	|PerlIO *|PerlIO_stdin
Ap	|PerlIO *|PerlIO_stdout
Ap	|PerlIO *|PerlIO_stderr
#endif /* PERLIO_LAYERS */

p	|void	|deb_stack_all
#ifdef PERL_IN_DEB_C
s	|void	|deb_stack_n	|SV** stack_base|I32 stack_min \
				|I32 stack_max|I32 mark_min|I32 mark_max
#endif

pd	|PADLIST*|pad_new	|int flags
pd	|void	|pad_undef	|CV* cv
pd	|PADOFFSET|pad_add_name	|char *name\
				|HV* typestash|HV* ourstash \
				|bool clone
pd	|PADOFFSET|pad_add_anon	|SV* sv|OPCODE op_type
pd	|void	|pad_check_dup	|char* name|bool is_our|HV* ourstash
#ifdef DEBUGGING
pd	|void	|pad_setsv	|PADOFFSET po|SV* sv
#endif
pd	|void	|pad_block_start|int full
pd	|void	|pad_tidy	|padtidy_type type
pd 	|void	|do_dump_pad	|I32 level|PerlIO *file \
				|PADLIST *padlist|int full
pd	|void	|pad_fixup_inner_anons|PADLIST *padlist|CV *old_cv|CV *new_cv

pd	|void	|pad_push	|PADLIST *padlist|int depth|int has_args

#if defined(PERL_IN_PAD_C) || defined(PERL_DECL_PROT)
sd	|PADOFFSET|pad_findlex	|char* name|PADOFFSET newoff|CV* innercv
#  if defined(DEBUGGING)
sd	|void	|cv_dump	|CV *cv|char *title
#  endif
s	|CV*	|cv_clone2	|CV *proto|CV *outside
#endif
pd 	|CV*	|find_runcv	|U32 *db_seqp
p	|void	|free_tied_hv_pool
#if defined(DEBUGGING)
p	|int	|get_debug_opts	|char **s
#endif



END_EXTERN_C

