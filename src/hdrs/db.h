/* db.h */
/* $Id: db.h,v 1.18 1993/10/18 01:11:48 nils Exp $ */


#ifndef __DB_H
#define __DB_H
#include "config.h"
/* #include "externs.h" */
#ifdef USE_COMBAT
#include "combat.h"
#endif /* USE_COMBAT */
#include <time.h>

/* Warning: some comments maybe out of date. read with caution. :) */
#include <string.h>
#include <stdio.h>

extern int depth;

#ifndef DEFDBREF
#define DEFDBREF
typedef long dbref;		/* offset into db */
#endif

#define IS(thing,type,flag) ((Typeof(thing)==type) && (db[thing].flags & flag))

extern dbref first_free;	/* pointer to free list */


#define FIX fix_free_list()
typedef long object_flag_type;

/* Macro for checking valid dbrefs - add to db.h eventually */
#ifndef GoodObject
#define GoodObject(x) ((x) >= 0 && (x) < db_top && \
                      (Typeof(x) != NOTYPE) && \
                      !(db[x].flags & GOING))
#endif

/* flag definitions */

#define TYPE_MASK 	0xF	/* room for expansion */
#define TYPE_ROOM 	0x0
#define TYPE_THING 	0x1
#define TYPE_EXIT 	0x2
#define TYPE_UNIVERSE	0x3
#define TYPE_CHANNEL	0x4
#define NOTYPE		0x7	/* no particular type */
#define TYPE_PLAYER	0x8
#define NUM_OBJ_TYPES	0x9	/* number of object types */

#define CHOWN_OK	0x20
#define DARK		0x40	/* contents of room are not printed */
#define STICKY		0x100	/* this object goes home when dropped */
#define	HAVEN		0x400	/* object can't execute commands */
#define INHERIT_POWERS	0x2000	/* gives object powers of its owner */
#define GOING		0x4000	/* object is available for recycling */
#define PUPPET		0x20000
#define LINK_OK		0x40000	/* anybody can link to this room */
#define ENTER_OK	0x80000
#define SEE_OK		0x100000
#define CONNECT		0x200000
#define OPAQUE		0x800000
#define QUIET		0x1000000
#define BEARING		0x8000000

/* type specific flags */
/* Thing flags */
#define THING_KEY	0x10
#define THING_LIGHT	0x80
#define THING_DEST_OK	0x200
#define THING_SACROK	0x1000
/* Exit flags */
#define EXIT_LIGHT	0x10
/* Player flags */
#define PLAYER_NEWBIE	0x10
#define PLAYER_SLAVE	0x80
#define PLAYER_ANSI	0x200
#define PLAYER_MORTAL	0x800
#define PLAYER_NOBEEP	0x1000
#define PLAYER_FREEZE	0x10000
#define PLAYER_TERSE	0x400000
#define PLAYER_NO_WALLS	0x2000000
#define PLAYER_WHEN	0x4000000   /* remove this much later when all players will have had this removed or been purged. */
#define PLAYER_SUSPECT	0x10000000
#define PLAYER_IDLE	0x20000000
/* Room flags */
#define ROOM_JUMP_OK	0x200
#define ROOM_AUDITORIUM	0x800
#define ROOM_FLOATING	0x1000
#define ROOM_SHOP    	0x10000
/* Channel Flags */
/* none right now */

/* internal flags. in .i_flags member. */
#define I_MARKED  	0x1	/* used for finding disconnected rooms */
#define I_QUOTAFULL     0x2	/* byte quota all used up. */
#define I_UPDATEBYTES   0x4	/* byte count needs to be updated. */

/* end of flags */

/* macro to make set string easier to use */
#define SET(astr,bstr) do { char **__a, *__b; __a=(&(astr)); __b=(bstr); if (*__a) free(*__a); if (!__b || !*__b) *__a=NULL; else { *__a = malloc(strlen(__b)+1); strcpy(*__a,__b); } } while(0)

/* Updates an objects age */
/*#define RLevel(x) (db[(x)].flags & TYPE_MASK)*/
#define Robot(x) ((Typeof(x) == TYPE_PLAYER) && ((x) != db[(x)].owner))
#define Guest(x) ((Typeof(x)==TYPE_PLAYER && *db[x].pows == CLASS_GUEST))
#define Dark(x) (((db[(x)].flags & DARK) != 0) && (Typeof(x)!=TYPE_PLAYER) && !(db[(x)].flags & PUPPET))
#define Alive(x) ((Typeof(x)==TYPE_PLAYER) || ((Typeof(x)==TYPE_THING) && (db[x].flags & PUPPET)))
#define Builder(x) ((db[db[(x)].owner].flags & PLAYER_BUILD) || (Typeof(x) == TYPE_PLAYER && *db[x].pows == CLASS_DIR) || Wizard(x)) 

char *atr_get();


struct attr {
  /* name of attribute */
  char *name;            
  int flags;
  dbref obj;			/* object where this is defined. NOTHING for
				 * builtin. */
  int refcount;			/* number of things that reference us. */
};

typedef struct attr ATTR;

/* Attribute flags */
#define AF_OSEE   1 /* players other than owner can see it */
#define AF_DARK   2 /* No one can see it */
#define AF_WIZARD 4 /* only wizards can change it */
#define AF_UNIMP  8 /* not important -- don't save it in the db */
#define AF_NOMOD  16 /* not even wizards can modify this */
#define AF_DATE   32 /* date stored in universal longint form */
#define AF_INHERIT 64 /* value inherited by childern */
#define AF_LOCK	  128 /* interpreted as a boolean expression */
#define AF_FUNC   256 /* this is a user defined function */
#define AF_BUILTIN 1024 /* server supplies value. not database. */
#define AF_DBREF 2048 /* value displayed as dbref. */
#define AF_NOMEM 4096 /* this isn't included in memory calculations */
#define AF_TIME  8192 /* a certain length of time */
#define AF_HAVEN 16384 /* haven attribute */


#define DECLARE_ATTR
#define DOATTR(var, name, flags, num) extern ATTR *var;
#include "attrib.h"
#undef DOATTR
#undef DECLARE_ATTR

/* attribute access macros */                     
#define Osucc(thing) atr_get(thing,A_OSUCC)
#define Ofail(thing) atr_get(thing,A_OFAIL)
#define Fail(thing) atr_get(thing,A_FAIL)
#define Succ(thing) atr_get(thing,A_SUCC)
#define Pass(thing) atr_get(thing,A_PASS)
#define Desc(thing) atr_get(thing,A_DESC)
#define Idle(thing) atr_get(thing,A_IDLE)
#define Away(thing) atr_get(thing,A_AWAY)
#define Pennies(thing) atol(atr_get(thing,A_PENNIES))
#define Home(thing) (db[thing].exits)
#define Exits(thing) (db[thing].exits)

#define s_Osucc(thing,s) atr_add(thing,A_OSUCC,s)
#define s_Ofail(thing,s) atr_add(thing,A_OFAIL,s)
#define s_Fail(thing,s) atr_add(thing,A_FAIL,s)
#define s_Succ(thing,s) atr_add(thing,A_SUCC,s)
#define s_Pass(thing,s) atr_add(thing,A_PASS,s)
#define s_Desc(thing,s) atr_add(thing,A_DESC,s)
#define s_Pennies(thing,pp) ((pp>max_pennies)?(atr_add(thing,A_PENNIES,tprintf("%ld",max_pennies)),1):(atr_add(thing,A_PENNIES,tprintf("%ld",pp)),1))
#define s_Exits(thing,pp) (db[thing].exits=(pp))
#define s_Home(thing,pp) (db[thing].exits=(pp))
extern char *int_to_str();


#define Astr(alist) ((char *)(&((alist)[1])))  

typedef struct alist ALIST;
typedef short atr_flag_t;
struct alist {            
  ALIST *next;
  ATTR *AL_type;
};

#define AL_TYPE(alist) ((alist)->AL_type)
#define AL_STR(alist) (Astr(alist))
#define AL_NEXT(alist) ((alist)->next)
#define AL_DISPOSE(alist) ((alist)->AL_type=0)

/* special dbref's */
#define NOTHING -1		/* null dbref */
#define AMBIGUOUS -2		/* multiple possibilities, for matchers */
#define HOME -3			/* virtual room, represents mover's home */
#define PASSWORD -4		/* incorrect password when one is required */
#define BACK -5 
typedef char ptype;

typedef struct atrdef ATRDEF;

struct atrdef {
  ATTR a;
  ATRDEF *next;
};

#ifndef USE_COMBAT
#define MAX_SKILLS 0
#endif

struct object {
  char *name;
  char *cname;			/* colorized name */
  dbref location;		/* pointer to container */
  /* for exits, pointer to destination */
  dbref  zone;			/* zone reference for object */
  dbref contents;		/* pointer to first item */
  dbref exits;			/* pointer to first exit for rooms */
  dbref fighting;		/* pointer to person currently being fought */
  dbref link;			/* pointer to home/destination */
  dbref next;			/* pointer to next in contents/exits chain */
  dbref next_fighting;		/* pointer to next person fighting */

  dbref universe;		/* pointer to which Universe obj to obey */
  char **ua_string;		/* Universe string variables */
  int *ua_int;			/* Universe integer variables */
  float *ua_float;		/* Universe floating point variables */
#ifdef USE_COMBAT
  long skills[MAX_SKILLS];	/* List of skill settings */
#endif


  char **paste;
  int paste_cnt;

  ptype *pows;

  /* the following are used for pickups for things, entry for exits */
  dbref owner;		/* who controls this object */
  object_flag_type flags;
  unsigned char i_flags;
  ALIST *list;
  
  struct atrdef *atrdefs;
  dbref *parents;
  dbref *children;
#ifdef USE_COMBAT
  struct main_spell_struct *spells;
#endif
  struct bank_acnt_struct *bank_acnts;
  long bitmap;
  unsigned long item_bitmap;
  long *items;
  long mod_time, create_time;
  long size;
};


///* begin +mail information */
//#define MF_DELETED 1
//#define MF_READ 2
//#define MF_NEW 4
//
//extern long mdb_alloc;
//extern long mdb_first_free;
//
//typedef long mdbref;
//extern struct mdb_entry
//{ 
//  dbref from;
//  long date;
//  int flags;                    /* see the following. */
//  char *message;                /* null if unused entry */
//  mdbref next;                  /* next garbage, or next message. */
//}
// *mdb;
///*  end +mail information  */



typedef struct mem_stack_t {
      void *ptr;    		/* information to be stored */
      size_t size;  		/* size of stuff allocated. */
      int timer;    		/* times through loops before killing. */
      int perm;			/* permanant flag */
      struct mem_stack_t *prev; /* pointer to last peice of stack. */
      struct mem_stack_t *next; /* pointer to next peice of stack. */
} MSTACK;
 
extern struct object *db;
extern dbref db_top;
extern MSTACK *mem_stack;
extern size_t number_stack_blocks;
extern size_t stack_size;
extern size_t text_block_size;
extern size_t text_block_num;

extern int dozonetemp;
#define DOZONE(var, first) for (dozonetemp=0,var=get_zone_first(first); var!=NOTHING && dozonetemp<10; var=get_zone_next(var), dozonetemp++)

#define DOLIST(var, first) for((var) = (first); (var) != NOTHING; (var) = db[(var)].next)
#define PUSH(thing, locative) ((db[(thing)].next = (locative)), (locative) = (thing))
#define getloc(thing) (db[thing].location)

#include "powers.h"
#define Typeof(x) (db[x].flags&TYPE_MASK)
#define is_root(x) ((x) == root)

/* stuff for doomsday */
#define IS_DOOMED(x) ((*atr_get((x),A_DOOMSDAY))&&(db[x].flags&GOING)&&(atoi(atr_get((x),A_DOOMSDAY))>0))
#define IS_GONE(x) ((db[(x)].flags&GOING)&&!*atr_get((x),A_DOOMSDAY))

#define PUSH_L(list,value) (push_list(&(list),(value)))
#define REMOVE_FIRST_L(list, value) (remove_first_list(&(list),(value)))

struct all_atr_list {
  ATTR *type;
  char *value;
  int numinherit;
  struct all_atr_list *next;
};

#define unref_atr(foo) do { if((foo)) if(0==--(foo)->refcount) { if((foo)->name) free((foo)->name); free((foo)); } } while(0)
#define ref_atr(foo) do { ((foo)->refcount++); } while(0)

//extern struct num_logins
//{ 
//  int highest_day;      /* Most logins in one day                       */
//  int highest_week;     /* Most logins in one week                      */
//  int highest_atonce;   /* Most players logged in at one time           */
//  char date_day[10];    /* Date of the day with the most logins         */
//  char date_week[10];   /* Date of the day that broke the week record   */
//  char date_atonce[10]; /* Date of the day that broke the atonce record */
//  int day;    /* day of week on last dump */
//  time_t time; /* time in secs of last dump */
//  long total;
//  int today;
//  int a_sun;
//  int a_mon;
//  int a_tue;
//  int a_wed;
//  int a_thu;
//  int a_fri;
//  int a_sat;
//  int b_sun;
//  int b_mon;
//  int b_tue;
//  int b_wed;
//  int b_thu;
//  int b_fri;
//  int b_sat;
//  int new_week_flag;
//} nl;

#define LOGINDBBUF 1024
#endif /* __DB_H */
