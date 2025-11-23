/* externs.h */
/* $Id: externs.h,v 1.36 1994/01/26 22:28:39 nils Exp $ */

#ifndef DEFDBREF
#define DEFDBREF
typedef long dbref;
#define DBREF_FMT "ld"
#endif


#ifndef _EXTERNS_H_
#define _EXTERNS_H_

#define __USE_XOPEN
#define _XOPEN_SOURCE


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
/* for struct timeval */
#include <sys/time.h>
/* for the time_t type */
#include <time.h>
/* for struct sockaddr */
/* for struct stat */
#ifndef S_IFMT
#include <sys/stat.h>
#endif
#ifndef FD_SETSIZE
#include <sys/types.h>
#endif

#ifdef linux
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef NeXT
#include <sys/wait.h>
#endif

#include "config.h"
#include "db.h"
/* for the text_queue and descriptor_list and stuff */
#include "net.h"
/* for FIFO * */
#include "fifo.h"
#include "log.h"

#ifndef SOCK_STREAM
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/net.h>
#endif

#if defined(ultrix) || defined(linux)
#define void_signal_type
#endif

#ifdef void_signal_type
#define signal_type void
#else
#define signal_type int
#endif

#ifdef USE_UNIV
#include "universe.h"
#endif

/* Macros for checking valid dbrefs */
#ifndef ValidObject
#define ValidObject(x) ((x) >= 0 && (x) < db_top && \
                        (Typeof(x) != NOTYPE))
#endif

#ifndef GoodObject
#define GoodObject(x) (ValidObject(x) && \
                      !(db[x].flags & GOING))
#endif


/* and all the definitions */
/* From admin.c */
extern void do_su (dbref, char *, char *, dbref);
extern void do_swap (dbref, char *, char *);
extern dbref match_controlled (dbref, char *, int);
extern void calc_stats (dbref, long *, long *, long *);
extern void do_allquota (dbref, char *);
extern void do_boot (dbref, char *, char *);
extern void do_cboot (dbref, char *);
extern void do_chownall (dbref, char *, char *);
extern void do_force (dbref, char *, char *);
extern void do_join (dbref, char *);
extern void do_newpassword (dbref, char *, char *);
extern void do_poor (dbref, char *);
extern void do_pstats (dbref, char *);
extern void do_search (dbref, char *, char *);
extern void do_stats (dbref, char *);
extern void do_summon (dbref, char *);
extern void do_teleport (dbref, char *, char *);
extern void do_uconfig (dbref, char *, char *);
extern void do_uinfo (dbref, char *);
extern void do_wipeout (dbref, char *, char *);
extern int owns_stuff (dbref);
extern int try_force (dbref, char *);
extern void do_nologins (dbref, char *);
extern void do_usercap (dbref, char *);
extern void do_lockout (dbref, char *);
extern void do_plusmotd (dbref, char *,char *);

/* From ansi.c */
extern char *parse_color (const char *, int);
extern char *parse_color_nobeep (char *, int);
extern char *strip_color (const char *);
extern char *strip_color_nobeep (char *);
extern char *truncate_color (char *, int);


/* From boolexp.c */
extern int eval_boolexp (dbref, dbref, char *, dbref);
extern char *process_lock (dbref, char *);
extern char *unprocess_lock (dbref, char *);

/* From bsd.c */
void free_text_block (struct text_block *);
extern char *spc (int);
extern void outgoing_setupfd (dbref, int);
extern int loading_db;
extern time_t now;
extern void do_ctrace (dbref);
extern void announce_connect (dbref);
extern void announce_disconnect (dbref);
extern int boot_off (dbref);
extern void emergency_shutdown (void);
extern int getdtablesize (void);
extern int queue_string (struct descriptor_data *, const char *);
extern int queue_write (struct descriptor_data *, const char *, int);
extern void raw_notify (dbref, char *);
extern void raw_notify_noc (dbref, char *);
extern void remove_muse_pid (void);
extern void welcome_user (struct descriptor_data *);
extern void connect_message (struct descriptor_data *,char *,int);
extern void flush_all_output (void);
extern int process_output (struct descriptor_data *);
extern char *add_pre_suf (dbref, int, char *, int);
extern void shutdown_stack (void);


/* From com.c */
extern void com_send (char *,char *);
extern void com_send_as (char *channel, char *message, dbref player);
extern void com_send_as_hidden (char *channel, char *message, dbref player);
extern void do_com (dbref, char *, char *);
extern void do_channel (dbref, char *, char *);
extern void do_ban (dbref, char *, char *);
extern void do_unban (dbref, char *, char *);
extern void make_default_channel (dbref,char *);
extern int is_on_channel (dbref, const char *);
extern char *add_stamp (char *);
extern void com_send_int (char *, char *, dbref, int);
extern void do_channel_destroy(dbref, char *);
extern void add_channel (dbref);
extern void clear_channels (void);
extern void delete_channel (dbref);
extern dbref lookup_channel (const char *);
extern void channel_talk (dbref, char *, char *, char *);
extern int ok_channel_name (char *);

/* From conf.c */
extern void info_config (dbref);
void info_pid (dbref);

/* From config.c */
extern char *perm_denied (void);
extern void do_config (dbref, char *, char *);
#define DO_NUM(nam,var) extern int var;
#define DO_STR(nam,var) extern char *var;
/* okay. this pisses me off. I don't know why, but the compiler
    is throwing a fit over using dbref, wanting to make it an 
    int instead of a long.  So I'll force it. *sigh*           
#define DO_REF(nam,var) extern dbref var;  */
#define DO_REF(nam,var) DO_LNG(nam,var)
#define DO_LNG(nam,var) extern long var;
#include "conf.h"
#undef DO_NUM
#undef DO_STR
#undef DO_REF
#undef DO_LNG

/* From cque.c */
extern void do_jobs (int);
extern void do_haltall (dbref);
extern void do_halt (dbref, char *, char *);
extern void do_queue (dbref);
extern void do_second (void);
extern int do_top (void);
extern void parse_que (dbref, char *, dbref);
extern void parse_que_pri (dbref, char *, dbref, int);
extern int test_top (void);
extern void wait_que (dbref, int, char *, dbref);

/* From create.c */
extern void init_universe (struct object *);
extern void do_clone (dbref, char *,char *);
extern void do_create (dbref, char *, int);
extern void do_ucreate (dbref, char *, int);
/* extern void check_spoofobj (dbref, dbref); */
extern void do_dig (dbref, char *, char **);
extern void do_link (dbref, char *, char *);
extern void do_ulink (dbref, char *, char *);
extern void do_unulink (dbref, char *);
extern void do_zlink (dbref, char *, char *);
extern void do_unzlink (dbref, char *);
extern void do_gzone (dbref, char *);
extern void do_guniverse (dbref, char *);
extern void do_open (dbref, char *, char *, dbref);
extern void do_robot (dbref, char *, char *);

/* From db.c */

extern void atr_fputs (char *, FILE *);
extern char *atr_fgets (char *, int, FILE *);
extern void update_bytes (void);
extern dbref get_zone_first (dbref);
extern dbref get_zone_next (dbref);
extern void db_set_read (FILE *);
extern void load_more_db (void);
extern void free_database (void);
extern void init_attributes (void);
extern char *unparse_attr (ATTR *, int dep);
extern void atr_add (dbref, ATTR *, char *);
extern void atr_clr (dbref, ATTR *);
extern ATTR *builtin_atr_str (char *);
extern void atr_collect (dbref);
extern void atr_cpy_noninh (dbref, dbref);
extern void atr_free (dbref);
extern char *atr_get (dbref, ATTR *);
extern ATTR *atr_str (dbref, dbref, char *);
extern dbref db_write (FILE *);
extern dbref getref (FILE *);
extern dbref new_object (void);
extern dbref parse_dbref (char *);
extern void putref (FILE *, dbref);
extern void putstring (FILE *, char *);
extern void remove_temp_dbs (void);
extern void get_univ_info (FILE *, struct object *);
extern char *getstring_noalloc (FILE *);

/* From dbtop.c */
extern void do_dbtop (dbref, char *);

/* From decompress.c */

/* From destroy.c */
extern void info_db (dbref);
extern void do_check (dbref, char *);
extern void do_incremental (void);
extern void do_dbck (dbref);
extern void do_empty (dbref);
extern void fix_free_list (void);
extern dbref free_get (void);
extern void do_undestroy (dbref,char *);
extern void do_poof (dbref, char *);
extern void do_upfront (dbref, char *);
#ifdef SHRINK_DB
extern void do_shrinkdbuse (dbref,char *);
#endif

/* From eval.c */
extern void info_funcs (dbref);
extern char *wptr[10];
extern void museexec (char **, char *, dbref, dbref, int);
extern void func_zerolev (void);
extern dbref match_thing (dbref, const char *);
extern char *parse_up (char **, int);
extern int mem_usage (dbref);

/* From game.c */
extern void exit_nicely (int);
extern void dest_info (dbref, dbref);
extern int Live_Player (dbref);
extern int Live_Puppet (dbref);
extern int Listener (dbref);
extern int Commer (dbref);
extern int Hearer (dbref);
extern void dump_database (void);
extern void fork_and_dump (void);
extern int init_game (char *, char *);
extern void notify (dbref, const char *);
extern void notify_all (char *, dbref, int);
extern void notify_noc (dbref, char *);
extern void panic (char *);
extern void process_command (dbref, char *, dbref);
extern void report (void);
//extern void do_shutdown (dbref, char *);
//extern void do_reload (dbref, char *);
//extern void do_purge (dbref);

/* From hash.c */
extern void do_showhash (dbref, char *);
extern void free_hash (void);

/* From help.c */
extern void do_text (dbref, char *, char *, ATTR *);
extern void do_motd (dbref);

/* From info.c */
extern void do_info (dbref, char *);

/* From inherit.c */
extern void do_undefattr (dbref, char *);
extern int is_a (dbref, dbref);
extern void do_defattr (dbref, char *, char *);
extern void do_addparent (dbref, char *, char *);
extern void do_delparent (dbref, char *, char *);
extern void put_atrdefs (FILE *, ATRDEF *);
extern ATRDEF *get_atrdefs (FILE *, ATRDEF *);

/* from io_globals.c */
extern int sock;
extern int reserved;
extern int maxd;
extern int ndescriptors;
extern int shutdown_flag;
extern int exit_status;
extern int sig_caught;
extern int nologins;
extern int restrict_connect_class;
extern time_t muse_up_time;
extern time_t muse_reboot_time;
extern time_t now;
extern char motd[2048];
extern char motd_who[11];
extern char ccom[1024];
extern dbref cplr;
void init_io_globals (void);

/* From look.c */
extern char *flag_description (dbref);
extern struct all_atr_list *all_attributes (dbref);
extern void do_examine (dbref, char *, char *);
extern void do_find (dbref, char *);
extern void do_inventory (dbref);
extern void do_laston (dbref, char *);
extern void do_look_around (dbref);
extern void do_look_at (dbref, char *);
extern void do_score (dbref);
extern void do_sweep (dbref, char *);
extern void do_whereis (dbref, char *);
extern void look_room (dbref, dbref);

/* from lstats.c */
extern void check_newday (void);
extern void write_loginstats (long);
extern void read_loginstats (void);
extern void do_loginstats (dbref);
extern void add_login (dbref);
extern void give_allowances (void);

/* From match.c */
extern dbref exact_match;    /* holds result of exact match */
extern char *match_name;       /* name to match */
extern dbref it;               /* the *IT*! */

extern void init_match (dbref, const char *, int);
extern void init_match_check_keys (dbref, char *, int);
extern dbref last_match_result (void);
extern void match_absolute (void);
extern void match_everything (void);
extern void match_exit (void);
extern void match_here (void);
extern void match_me (void);
extern void match_neighbor (void);
extern void match_perfect (void);
extern dbref match_player (dbref player, const char *name);
extern void match_channel (void);
extern void match_possession (void);
extern dbref match_result (void);
extern dbref noisy_match_result (void);
extern dbref pref_match (dbref, dbref, char *);

/* from maze.c */
extern char *comma (char *);
#define name(x) db[x].cname
#define Wizard(x) (*db[x].pows == CLASS_DIR)

/* from messaging.c */
extern void check_mail (dbref,char *);
extern long check_mail_internal (dbref,char *);
extern void do_board (dbref, char *, char *);
extern void do_mail (dbref, char *, char *);
extern long dt_mail (dbref obj);  /* Defined in mail.c */
extern void info_mail(dbref player);
extern long mail_size (dbref);
extern void read_mail (FILE *);
extern void write_mail (FILE *);
#ifdef SHRINK_DB
extern void remove_all_mail (void);
#endif


/* From move.c */
extern int can_move (dbref, char *);
extern void do_drop (dbref, char *);
extern void do_enter (dbref, char *);
extern void do_get (dbref, char *);
extern void do_leave (dbref);
extern void do_move (dbref, char *);
extern int  enter_room (dbref, dbref);
extern int  moveto (dbref, dbref);
extern void safe_tel (dbref, dbref);
extern dbref get_room (dbref);
extern void moveit (dbref, dbref);

/* From nalloc.c */
extern void* safe_malloc (size_t, const char *, int);
extern void safe_free (void *, const char *file, int);
extern void smart_free (void *, const char *, int);
extern void safe_memory_init (void);
extern void safe_memory_cleanup (void);
extern void *stack_em_fun (size_t);
extern void *stack_em (size_t);
extern void clear_stack (void);
extern char *stralloc (const char *);
extern char *stralloc_p (char *);
extern void strfree_p (char *);
extern char *funalloc (char *);
#define SAFE_MALLOC(result, type, number)  do {  (result) = (type *) safe_malloc((number) * sizeof(type), __FILE__, __LINE__);  } while (0)
#define SAFE_FREE(x)  do {  safe_free((void *)(x), __FILE__, __LINE__);  (x) = NULL;  } while (0)
#define SMART_FREE(x) do {  smart_free((void*)(x), __FILE__, __LINE__); (x) = NULL; } while(0)
/* Logging control - only available when MEMORY_DEBUG_LOG is defined */

#ifdef MEMORY_DEBUG_LOG
extern void safe_memory_set_log_file(const char *filename);
extern void safe_memory_set_content_log_size(size_t max_bytes);

extern void memdebug_log (const char *format, ...);
extern void memdebug_log_ts (const char *format, ...);
extern void memdebug_log_hex_dump (const void *data, size_t size);
extern int memdebug_is_active (void);
#else
/* No-op macros when MEMORY_DEBUG_LOG is not defined */
#define memdebug_log(...)
#define memdebug_log_ts(...)
#define memdebug_log_hex_dump(data, size)
#define memdebug_is_active() 0
#endif /* MEMORY_DEBUG_LOG */




/* From page.c */
extern void do_page (dbref, char *, char *);

/* From paste.c */
extern char is_pasting (dbref);
extern void remove_paste (dbref);
extern void do_paste (dbref, char *, char *);
extern void do_pastecode (dbref, char *, char *);
extern void add_more_paste (dbref, char *);
extern void do_pastestats (dbref, char *);

/* From pcmds.c */
extern void do_at (dbref, const char *, const char *);
extern void do_as (dbref, const char *, const char *);
extern void do_exec (dbref, const char *, const char *);
extern void do_version (dbref);
extern void do_uptime (dbref);
extern void do_cmdav (dbref);
extern void inc_pcmdc (void);
extern void inc_qcmdc (void);

/* From player.c */
extern dbref *match_things (dbref, const char *);
extern ptype name_to_pow (const char *);
extern void do_powers (dbref, const char *);
extern void do_misc (dbref, const char *, const char *);
extern void do_empower (dbref, const char *, const char *);
extern dbref connect_player (const char *, const char *);
extern dbref create_guest (const char *, const char *, const char *);
extern dbref create_player (const char *, const char *, int, dbref);
extern void destroy_guest (dbref);
extern void do_class (dbref, const char *, const char *);
extern void do_nopow_class (dbref, const char *, const char *);
extern void do_money (dbref, const char *, const char *);
extern void do_nuke (dbref, const char *);
extern void do_password (dbref, const char *, const char *);
extern void do_pcreate (dbref, const char *, const char *);
extern void do_quota (dbref, const char *, const char *);
extern char *get_class (dbref);
extern dbref *lookup_players (dbref, const char *);
extern char *title (dbref player);
extern char *pow_to_name (ptype pow);
extern int old_to_new_class (int lev);
extern int is_connected (dbref viewer, dbref target);
#define IS_CONNECTED(player) is_connected(NOTHING, (player))  /* For old single-argument calls */
#define CAN_SEE_CONNECTED(viewer, target) is_connected((viewer), (target))   /* For checking if viewer can see target is connected */
#define IS_CONNECTED_RAW(player) is_connected(NOTHING, (player))   /* For raw connection check */

/* From player_list.c */
extern void add_player (dbref);
extern void clear_players (void);
extern void delete_player (dbref);
extern dbref lookup_player (const char *);

/* From predicates.c */
extern dbref check_zone (dbref, dbref, dbref, int);
extern char *safe_name (dbref);
extern dbref starts_with_player (char *);
extern int can_see_atr (dbref, dbref, ATTR *);
extern int can_set_atr (dbref, dbref, ATTR *);
extern void push_list (dbref **, dbref);
extern void remove_first_list (dbref **, dbref);
extern int Levnm (dbref);
extern int inf_mon (dbref);
extern int inf_quota (dbref);
extern int Level (dbref);
extern void add_quota (dbref, int);
extern void recalc_bytes (dbref);
extern void add_bytesused (dbref, int);
extern int can_link (dbref, dbref, int);
extern int can_link_to (dbref, dbref, int);
extern int can_pay_fees (dbref, int, int);
extern int can_see (dbref, dbref, int);
extern int controls_a_zone (dbref, dbref, int);
extern int controls (dbref, dbref, int);
extern int is_in_zone (dbref, dbref);
extern dbref def_owner (dbref);
extern int could_doit (dbref, dbref, ATTR *);
extern void did_it (dbref, dbref, ATTR *, char *, ATTR *, char *, ATTR *);
extern void did_it_now (dbref, dbref, ATTR *, char *, ATTR *, char *, ATTR *);
extern void giveto (dbref, int);
extern int ok_attribute_name (char *);
extern int ok_object_name (dbref, char *);
extern int ok_room_name (const char *);
extern int ok_exit_name (const char *);
extern int ok_thing_name (const char *);
extern int ok_player_name (dbref, char *, char *);
extern int ok_password (char *);
extern int pay_quota (dbref, int);
extern int payfor (dbref, int);
extern int power (dbref, int);
extern void pronoun_substitute (char *, dbref, char *, dbref);
extern char *main_exit_name (dbref);
extern int sub_quota (dbref, int);
extern char *ljust (char *, int);
extern char *tprintf (char *, ...);
extern dbref real_owner (dbref);
extern int valid_player (dbref);
extern int ok_name (char *);
extern int group_controls (dbref, dbref);
/*
#ifndef NO_PROTO_VARARGS
extern char *tprintf (char *, ...) 
#ifdef __GNUC__
__attribute__ ((format (printf, 1,2)))
#endif */ /* __GNUC__ */ /*
;
#endif */ /* NO_PROTO_VARARGS */ /*
*/


/* From rlpage.c */
extern void do_rlpage (dbref, char *, char *);
extern void rlpage_tick (void);

/* From cntl.c */
extern void do_switch (dbref, char *, char **, dbref);
extern void do_foreach (dbref, char *, char *, dbref);
extern void do_trigger (dbref, char *, char **);
extern void do_trigger_as (dbref, char *, char **);
extern void do_decompile (dbref, char *, char *);
extern void do_cycle (dbref, char *, char **);

/* From rob.c */
extern void do_giveto (dbref, char *, char *);
extern void do_give (dbref, char *, char *);

/* From set.c */
void mark_hearing (dbref);
void check_hearing (void);
extern void do_haven (dbref, char *);
extern void do_away (dbref, char *);
extern void do_chown (dbref, char *, char *);
extern void do_describe (dbref, char *, char *);
extern void destroy_obj (dbref, int);
extern void do_destroy (dbref, char *);
extern void do_edit (dbref, char *, char **);
extern void do_fail (dbref, char *, char *);
extern void do_hide (dbref);
extern void do_idle (dbref, char *);
extern void do_name (dbref, char *, char *, int);
extern void do_cname (dbref, char *, char *);
extern void do_ofail (dbref, char *, char *);
extern void do_osuccess (dbref, char *, char *);
extern void do_set (dbref, char *, char *, int);
extern void do_unhide (dbref);
extern void do_unlink (dbref, char *);
extern void do_unlock (dbref, char *);
extern int parse_attrib (dbref, char *, dbref *, ATTR **, int);
extern int test_set (dbref, char *, char *, char *, int);
extern void set_idle_command (dbref, char *, char *);
extern void set_idle (dbref, dbref, time_t, char *);
extern void set_unidle (dbref, time_t);


/* From signal.c */
extern void set_signals (void);


/* From sock.c */
#ifdef RESOCK
extern void resock (void);
#endif
#ifdef USE_OUTGOING
extern void do_outgoing (dbref, char *, char *);
#endif

/* From speech.c */
extern void do_use (dbref, char *);
extern void do_announce (dbref, char *, char *);
extern void do_broadcast (dbref, char *, char *);
extern void do_emit (dbref, char *, char *, int);
extern void do_cemit (dbref, char *, char *);
extern void do_chemit (dbref, char *, char *);
extern void do_wemit (dbref, char *, char *);
extern void do_echo (dbref, char *, char *, int);
extern void do_gripe (dbref, char *, char *);
extern void do_pray (dbref, char *, char *);
extern void do_general_emit (dbref, char *, char *, int);
extern void do_pose (dbref, char *, char *, int);
extern void do_say (dbref, char *, char *);
extern void do_to (dbref, char *, char *);
extern void do_think (dbref, char *, char *);
extern void do_whisper (dbref, char *, char *);
extern void notify_in (dbref, dbref, char *);
extern void notify_in2 (dbref, dbref, dbref, char *);
extern char *spname (dbref);
extern char *reconstruct_message (char *, char *);

/* From stringutil.c */
extern char *str_index (char *, int);
extern int string_compare (const char *, const char *);
extern char *string_match (char *, char *);
extern int string_prefix (const char *, char *);
extern char to_lower (int);
extern char to_upper (int);
extern char *int_to_str (int);


/* From timer.c */
extern void dispatch (void);
extern void init_timer (void);
extern void trig_atime (void);

/* from time.c */
extern char *time_format_1 (time_t);
extern char *time_format_2 (time_t);
extern char *time_format_3 (time_t);
extern char *time_format_4 (time_t);
extern char *time_stamp (time_t);
extern char *get_day (time_t);
extern char *mil_to_stndrd (time_t);




/* From topology.c */
extern void run_topology (void);

/* From unparse.c */
extern char *unparse_object_a (dbref, dbref);
extern char *unparse_flags (dbref);
extern char *unparse_object (dbref, dbref);
extern char *unparse_object_caption (dbref, dbref);

/* From utils.c */
long mkxtime (char *, dbref, char *);
extern dbref find_entrance (dbref);
extern int member (dbref, dbref);
extern dbref remove_first (dbref, dbref);
extern dbref remove_first_fighting (dbref, dbref);
extern dbref reverse (dbref);
extern char *mktm (time_t, char *, dbref);

/* From who.c */
extern void dump_users (dbref, char *, char *, struct descriptor_data *);

/* From wild.c */
extern long wild_match (char *, char *);

/* From log.c */
extern void close_logs (void);
extern void suspectlog (dbref, char *);

/* From powerlist.c */
extern char *class_to_name (int);
//extern int name_to_class (char *);
extern int name_to_class (const char *);
extern int class_to_list_pos (int);

/* From powers.c */
extern void put_powers (FILE *, dbref);
extern void get_powers (dbref, char *);
extern int old_to_new_class (int);
extern void set_pow (dbref, int, int);
extern int has_pow (dbref, dbref, int);
extern int get_pow (dbref, int);

/* newconc.c */
extern long make_concid (void);
#ifdef USE_CID_PLAY
extern void do_becomeconc (struct descriptor_data *, char *);
extern void do_makeid (struct descriptor_data *);
extern void do_connectid (struct descriptor_data *, long, char *);
extern void do_killid (struct descriptor_data *, long);
#endif

#ifdef MALLOCDEBUG
#include "mnemosyne.h"
#elseif MEMWATCH
#include "memwatch.h"
#endif



#endif /* _EXTERNS_H_ */
