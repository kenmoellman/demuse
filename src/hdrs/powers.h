/* powers.h */
/* $Id: powers.h,v 1.8 1993/05/27 23:17:30 nils Exp $ */


#include "config.h"

#ifndef __POWERS_H__
#define __POWERS_H__

#define CLASS_GUEST 1
#define CLASS_VISITOR 2
#define CLASS_CITIZEN 3
#define CLASS_PCITIZEN 4
#define CLASS_GROUP 5
#define CLASS_JUNOFF 6
#define CLASS_OFFICIAL 7
#define CLASS_BUILDER 8
#define CLASS_ADMIN 9
#define CLASS_DIR 10

#define NUM_CLASSES 11
#define NUM_LIST_CLASSES 10
extern struct pow_list {
  char *name; /* name of power */
  ptype num; /* number of power */
  char *description; /* description of what the power is */
  int init[NUM_LIST_CLASSES];
  int max[NUM_LIST_CLASSES];
} powers[];

#define PW_NO 1
#define PW_YESLT 2
#define PW_YESEQ 3
#define PW_YES 4

/* this number will fluxuate depending on what powers you have turned on */
#define NUM_POWS         47
#define MAX_POWERNAMELEN 16

#define POW_ALLQUOTA     1
#define POW_ANNOUNCE     2
#define POW_BAN		 3
#define POW_BOARD        46
#define POW_BOOT         4
#define POW_BROADCAST    5
#define POW_CHANNEL      47
#define POW_CHOWN        6
#define POW_CLASS        7
#define POW_COMBAT       14
#define POW_DB           8
#ifdef DBTOP_POW
#define POW_DBTOP        9
#else
#define POW_NUTTIN0      9
#endif /* DBTOP_POW */
#define POW_EXAMINE      10
#ifdef ALLOW_EXEC
#define POW_EXEC         11
#else
#define POW_NUTTIN1      11
#endif /* ALLOW_EXEC */
#define POW_FREE         12
#define POW_FUNCTIONS    13
#ifdef USE_INCOMING
#define POW_INCOMING     15
#else
#define POW_NUTTIN2      15
#endif
#define POW_JOIN         16
#define POW_MEMBER       17
#define POW_MODIFY       18
#define POW_MONEY        19
#define POW_MOTD	 20
#define POW_NEWPASS      21
#define POW_NOSLAY       22
#define POW_NOQUOTA      23
#define POW_NUKE         24
#ifdef USE_OUTGOING
#define POW_OUTGOING     25
#else
#define POW_NUTTIN3      25
#endif
#define POW_PCREATE      26
#define POW_POOR         27
#define POW_QUEUE        28
#define POW_REMOTE       29
#define POW_SECURITY     30
#define POW_SEEATR       31
#define POW_SETPOW       32
#define POW_SETQUOTA     33
#define POW_SLAY         34
#define POW_SHUTDOWN     35
#define POW_SUMMON       36
#define POW_SLAVE        37
#ifdef USE_SPACE
#define POW_SPACE	 38
#else
#define POW_NUTTIN4      38
#endif
#define POW_NUTTIN5      39
#define POW_STATS        40
#define POW_STEAL        41
#define POW_TELEPORT     42
#define POW_WATTR        43
#define POW_WFLAGS       44
#define POW_WHO          45
#endif /* __POWERS_H_ */
