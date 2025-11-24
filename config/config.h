/* config.h */

/* note: some of the options in here may not work/compile correctly.
 * please try compiling first without changing things like
 * REGISTRATION and RESTRICTED_BUILDING. */

/* if you're on an ancient SysV box, this is apparently needed. */
#undef RESOCK

/* can you go home across zones? */
#define HOME_ACROSS_ZONES

#ifndef _LOADED_CONFIG_
#define _LOADED_CONFIG_

/* ============================================================================
 * GUEST_PASSWORD
 * ============================================================================
 * change this only every once in a while.  it should be secure enough to not 
 * create a problem anymore.  But, if it does, just change this string.  -wm
 */
#define GUEST_PASSWORD "sjf\thdssd\ndsfg"

/* ============================================================================
 * DBREF TYPEDEF, MAX DBREF NUMBER, AND DBREF SYMBOL FOR STRINGS
 * ============================================================================
 * dbref is defined as 'typedef long dbref' by default today but that can be
 * changed and macros allow for potential future changes to the dbref type.
 */

#ifndef DEFDBREF
#define DEFDBREF
typedef long dbref;  /* Database reference - offset into db array */

/* Determine the type characteristics of dbref at compile time */
#if LONG_MAX == 9223372036854775807L
    /* dbref is 64-bit long */
    #define DBREF_MAX LONG_MAX
    #define DBREF_MIN LONG_MIN
    #define DBREF_FMT "ld"
#else
    /* dbref is 32-bit long (or smaller) */
    #define DBREF_MAX LONG_MAX
    #define DBREF_MIN LONG_MIN
    #define DBREF_FMT "ld"
#endif

#endif /* DEFDBREF */



/* Network options */


/*  MULTIHOMING SECTION  */
/* decide whether machine is multihomed or not. */
#undef MULTIHOME 
#ifdef MULTIHOME

/* name of your game server's host */
#define HOSTNAME "kmserver.mgmt.moellman.com"

/* if you turn this option on, you will have to reboot the game with a new
sourcecode version any time you change IP addresses.  THat's pretty bothersome,
but, if you have a static IP, it prevents the game from having to reverse 
resolve the IP address of the game server upon every connection, causing twice
as much connection lag (which is caused by gethostbyname().  So, static IP
folks should define this, dynamic IP folks should not (but they shouldn't be
multi-homing either.) -wm 05/08/2000 */
#define CACHED_SERVER_HENT

#endif /* MULTIHOME */
/*  END MULTIHOMING SECTION  */

/* Define whether memory debug is turned on or not. You want this diabled    *
 * unless your game is crashing with memory errors.                          */
#define MEMORY_DEBUG_LOG
#ifdef MEMORY_DEBUG_LOG
#define MEMORY_DEBUG_FILE "./logs/malloc-debug.log"
#define MEMORY_DEBUG_SIZE 128
#endif
/* END memory debug section */

/* define whether or not you want reverse DNS.  This option is fine on most
 * systems, however, if your server has a slow DNS server or just does not 
 * reverse resolve, Reverse DNS lookups will be really slow.  If, when someone 
 * connects, the entire MUSE lags for 2 minutes, try turning this option off.  */
#define HOST_LOOKUPS 


/* Who list options */

/* Sort who list by idle time? */
/* #define SORT_BY_IDLE */

/* WHO idle color - always enabled */
/* WHO_IDLE_COLOR - removed, feature now always on */

/* Allow objects to talk on com channels - converted to power POW_COM_TALK */
/* ALLOW_COM_NP - removed, use POW_COM_TALK power instead */

/* Pueblo client support - always enabled */
/* PUEBLO_CLIENT - removed, feature now always on */

/* Universe system - always enabled */
/* USE_UNIV - removed, feature now always on */

/* message configuration */

/* Number of welcome message files (welcome000.txt to welcomeNNN.txt) */
#define NUM_WELCOME_MESSAGES 10

/* define these to your liking.  Be sure NOT to put your MUSE name in them. *
 * The MUSE name is now the first thing in each string.  Work it out.       *
 * WARNING DO  NOT USE ANSI CODES IN THESE STRINGS!                         */
#define FLUSHED_MESSAGE  "<Output Flushed>\n"
#define ONLINE_MESSAGE   "online.\n"
#define REBOOT_MESSAGE   "reloading, please hold.\n"
#define SHUTDOWN_MESSAGE "says 'This is your captain speaking. Light em up, cuz we're going down'\n"
#define LOCKOUT_MESSAGE  "is currently under restricted access conditions.\nPlease try again later.\n"
#define NOLOGINS_MESSAGE "is not allowing any connections at this time. Please try again later.\n"
/* message you want displayed to users the first time they log in. 
   make it short, preferably under 80 chars */
#define FIRST_LOGIN "First login: It always hurts the first time."

/* Set this to the appropriate time zone.  Valid US Timezones under linux are:
 EST EST5EDT
 CST CST6CDT
 MST MST7MDT
 PST PST8PDT
*/

#define OURTZ "EST5EDT"




/* defining the information for idle tineouts and whatnot. */
#define IDLE_TIMEOUT 1200
#define MIN_IDLE 1200
#define MAX_IDLE 3600

/* define this if you want certain users to be able to exec a shell.   *
 *  this is dangerous and stupid.  But if you want to, don't blame me. */
/* #define ALLOW_EXEC */

/* the following 3 are only applicable if ALLOW_EXEC is defined */
/* It's REALLY stupid and a big security risk to enable EXEC.  */
#ifdef ALLOW_EXEC
#define exec_config   "../config/ext_commands"
#define exec_shell    "/bin/bash"
#define exec_shellav0 "bash"
#endif /* ALLOW_EXEC */

/* should be the email address of someone who looks over crash
 * logs to try and figure out what the problem is. #undef if
 * you don't want to enable emailing of crash logs */
/* #define TECH_EMAIL "newmark@mu-net.org" */
#undef TECH_EMAIL


/* Blacklist feature - always enabled */
/* USE_BLACKLIST - removed, feature now always on */

/* define this if you'd like to have teh system automatically purge +mail that   *
 * users delete out of other peoples' mailboxes ( +mail delete=wm:6 would delete *
 * AND PURGE message 6 if it was written by the issuer, and unread by wm )      */
#undef TARGET_DEL_PURGE

/* @dbtop power requirement - always enabled */
/* DBTOP_POW - removed, feature now always on */

/* Guest idle boot - always enabled */
/* BOOT_GUEST - removed, feature now always on */
#define BOOT_GUEST

/* does a cariage return with no other input unidle a person? If yes, define */
/* #define CR_UNIDLE */

/* Use the 'compress' utility to compress the db at dump, and uncompress
 * it at startup.  This is kinda buggy, I wouldn't do it, unless you know 
 * what you're doing (disk space is cheap, man!)  */
/* #define DBCOMP */

/* use the memwatch malloc debugging package. this is recommended for
 * server developers. */
/* #define MEMWATCH */

/* If your machine doesn't have enough virtual memory to make a whole copy
 * of the netmud process, then define this. be warned that the netmud process
 * will hang while the database saves. */
#undef USE_VFORK

/* define whether or not you want RLPAGE email pageing */
#define USE_RLPAGE

/* define whether outgoing works or not. I'd recommend not. */
/* #define USE_OUTGOING */

/* define this if you want people to be able to connect as their objects. */
/* #define USE_INCOMING */

/* Universe mods - always enabled (see comments at top of file) */
/* #define USE_UNIV */

/* defines this if you are using the /proc filesystem. */
#define USE_PROC

/* define ConcID Play stuff. Turn this off generally. It's useless */
/* #define USE_CID_PLAY */

/* define if you want combat system - DONT TURN THIS ON! COMBAT IS NOT READY */
/* #define USE_COMBAT   */


/* define if want random welcome messages */
/* #define RANDOM_WELCOME */

/* define this if you want to shrink your database size. and only if.
   this will allow you to use the @shrink command which will move all
   @destroyed objects to be moved to the end of the database. Don't
   even try this unless you know what you're doing. It's dangerous.  */
#define SHRINK_DB  



/* You probably want to leave everything from here down alone. */

/* These are various characters that do special things. I recommend you
 * don't change these unless you won't have any people who are used
 * to tinymud using the muse. */
#define NOT_TOKEN '!'
#define AND_TOKEN '&'
#define OR_TOKEN '|'
#define THING_TOKEN 'x'
#define LOOKUP_TOKEN '*'
#define NUMBER_TOKEN '#'
#define AT_TOKEN '@'
#define ARG_DELIMITER '='
#define IS_TOKEN '='
#define CARRY_TOKEN '+'

/* These are various tokens that are abbreviations for special commands.
 * Again, i recommend you don't change these unless you won't have any people
 * who are used to tinymud. */
#define SAY_TOKEN '"'
#define POSE_TOKEN ':'
#define NOSP_POSE ';'
#define COM_TOKEN '='
#define TO_TOKEN '\''
#define THINK_TOKEN '.'

/* This is the maximum value of an object. You can @create <object>=a
 * value like 505, and it'll turn out to have a value of 100. use the
 * two formulas afterwrds. */
#define MAX_OBJECT_ENDOWMENT 100
#define OBJECT_ENDOWMENT(cost) (((cost)-5)/5)
#define OBJECT_DEPOSIT(pennies) ((pennies)*5+5)

/* This is the character that seperates different exit aliases. If you change
 * this, you'll probably have a lot of work fixing up your database. */
#define EXIT_DELIMITER ';'

/* special interface commands. i suggest you don't change these. */
#define QUIT_COMMAND "QUIT"

/* commands to set the output suffix and output prefix. */
#define PREFIX_COMMAND "OUTPUTPREFIX"
#define SUFFIX_COMMAND "OUTPUTSUFFIX"

/* If this is a usg-based system, redefine random and junk */
#if defined(XENIX) || defined(HPUX) || defined(SYSV) || defined(USG)
#define random rand
#define srandom srand
#endif

#define QUOTA_COST 1 /* why you would want to change this, i don't know */
#define MAX_ARG 100


#define MAX_BUFF_LEN 4096

/* Login Stats variables */
#define LOGINSTATS_FILE "db/loginstatsdb"
#define LOGINSTATS_BUF 256
#define LOGINSTATS_MAX_BACKUPS 3

/* for info.c, allow use of /proc data */
#define USE_PROC


/* email config - Customize these settings for your SMTP server */

#ifndef __EMAIL_CONFIG_H__
#define __EMAIL_CONFIG_H__

/* ===================================================================
 * SMTP Server Configuration
 * =================================================================== */

/* Option 1: Gmail (requires app-specific password) */
/*
#define SMTP_SERVER "smtp.gmail.com"
#define SMTP_PORT 587
#define SMTP_USE_SSL 1
#define SMTP_USERNAME "yourgame@gmail.com"
#define SMTP_PASSWORD "your-app-password"  // Not your regular password!
*/

/* Option 2: SendGrid */
/*
#define SMTP_SERVER "smtp.sendgrid.net"
#define SMTP_PORT 587
#define SMTP_USE_SSL 1
#define SMTP_USERNAME "apikey"  // Literally "apikey"
#define SMTP_PASSWORD "your-sendgrid-api-key"
*/

/* Option 3: Local Postfix/Sendmail relay (no auth) */
/*
#define SMTP_SERVER "localhost"
#define SMTP_PORT 25
#define SMTP_USE_SSL 0
// No username/password needed for local relay
*/

/* Option 4: AWS SES */
/*
#define SMTP_SERVER "email-smtp.us-east-1.amazonaws.com"
#define SMTP_PORT 587
#define SMTP_USE_SSL 1
#define SMTP_USERNAME "your-ses-smtp-username"
#define SMTP_PASSWORD "your-ses-smtp-password"
*/

/* Option 5: Mailgun */
/*
#define SMTP_SERVER "smtp.mailgun.org"
#define SMTP_PORT 587
#define SMTP_USE_SSL 1
#define SMTP_USERNAME "postmaster@your-domain.mailgun.org"
#define SMTP_PASSWORD "your-mailgun-password"
*/

/* Default Configuration - CHANGE THESE! */
#ifndef SMTP_SERVER
#define SMTP_SERVER "smtp.gmail.com"
#endif

#ifndef SMTP_PORT
#define SMTP_PORT 587
#endif

#ifndef SMTP_USE_SSL
#define SMTP_USE_SSL 1
#endif

#ifndef SMTP_USERNAME
#define SMTP_USERNAME "your-game@gmail.com"
#endif

#ifndef SMTP_PASSWORD
#define SMTP_PASSWORD "your-app-password"
#endif

#ifndef SMTP_FROM
#define SMTP_FROM "noreply@yourmud.com"
#endif

/* ===================================================================
 * Email Limits and Restrictions
 * =================================================================== */

/* Maximum number of emails per player per day */
#define MAX_EMAILS_PER_DAY 10

/* Minimum time between emails (seconds) */
#define EMAIL_COOLDOWN 60

/* Maximum message length */
#define MAX_EMAIL_LENGTH 4096

#endif /* __EMAIL_CONFIG_H__ */

/* change this if you add or delete a directory in the directory tree. */
#define MUSE_DIRECTORIES "src src/hdrs src/comm src/io src/db src/util run run/files run/files/p run/files/p/1 run/db run/msgs run/logs doc bin config"

/* max length of command argument to process_command */
#define MAX_COMMAND_LEN 1000
#define BUFFER_LEN ((MAX_COMMAND_LEN)*8)


/* definition of classes and powers formerly powers.h */
//typedef char ptype;  /* Power type for player powers/permissions */
typedef int ptype;  /* Power type for player powers/permissions */


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
#define POW_DBTOP        9
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
#define POW_NUTTIN4      38
#define POW_NUTTIN5      39
#define POW_STATS        40
#define POW_STEAL        41
#define POW_TELEPORT     42
#define POW_WATTR        43
#define POW_WFLAGS       44
#define POW_WHO          45
#define POW_COM_TALK     48


/* ============================================================================
 * POWER AND CLASS CONFIGURATION DATA
 * ============================================================================
 * Add this section to the END of config/config.h
 *
 * This data was moved from powerlist.c to enable future database migration.
 * These arrays define the power system and class hierarchy.
 *
 * MODERNIZATION NOTES:
 * - This data will eventually be moved to MySQL database tables
 * - Structure preserved for easy migration
 * - All data is configuration, not code logic
 */

/* Power value definitions (already in powers.h, but documented here) */
/* #define PW_NO    1  - No power */
/* #define PW_YESLT 2  - Yes if level < target */
/* #define PW_YESEQ 3  - Yes if level <= target */
/* #define PW_YES   4  - Yes always */

/* ============================================================================
 * CLASS NAME ARRAY
 * ============================================================================
 * Maps class constants to human-readable names.
 * Index corresponds to CLASS_* constants in powers.h
 */
static char *classnames[] =
{
  " ?",          /* CLASS_0 - Invalid/unknown */
  "Guest",       /* CLASS_GUEST - 1 */
  "Visitor",     /* CLASS_VISITOR - 2 */
  "Citizen",     /* CLASS_CITIZEN - 3 */
  "Builder",     /* CLASS_BUILDER - 4 (was CLASS_PCITIZEN) */
  "VIP",         /* CLASS_VIP - 5 (was CLASS_GROUP) */
  "Guide",       /* CLASS_GUIDE - 6 (was CLASS_JUNOFF) */
  "Counselor",   /* CLASS_OFFICIAL - 7 */
  "Judge",       /* CLASS_JUDGE - 8 (was part of builder) */
  "Admin",       /* CLASS_ADMIN - 9 */
  "Director" //,    /* CLASS_DIR - 10 */
//  NULL
};

/* ============================================================================
 * TYPE NAME ARRAY
 * ============================================================================
 * Maps type constants to human-readable names.
 * Index corresponds to TYPE_* constants in db.h
 */
static char *typenames[] =
{
  "Room",        /* TYPE_ROOM - 0x0 */
  "Thing",       /* TYPE_THING - 0x1 */
  "Exit",        /* TYPE_EXIT - 0x2 */
  "Universe",    /* TYPE_UNIVERSE - 0x3 */
  "Channel",     /* TYPE_CHANNEL - 0x4 */
  " 0x5",        /* Reserved */
  " 0x6",        /* Reserved */
  " 0x7",        /* Reserved */
  "Player"       /* TYPE_PLAYER - 0x8 */
};

/* ============================================================================
 * POWER DEFINITION ARRAY
 * ============================================================================
 * This is the main power configuration table.
 *
 * Each entry defines:
 * - name: Human-readable power name
 * - num: Power constant (POW_* from powers.h)
 * - description: What this power allows
 * - init[NUM_LIST_CLASSES]: Default power level for each class
 * - max[NUM_LIST_CLASSES]: Maximum power level for each class
 *
 * Array indices for init/max correspond to class_to_list_pos() return values:
 * [0] = Director
 * [1] = Admin
 * [2] = Builder/Judge
 * [3] = Counselor/Official
 * [4] = Citizen
 * [5] = Visitor
 * [6] = Guest
 * [7] = Guide/JunOff
 * [8] = Builder/VIP/Group
 * [9] = (unused)
 *
 * Power level values:
 * NO    = Cannot use this power
 * YES   = Can use this power unconditionally
 * YESLT = Can use on targets with level < own level
 * YESEQ = Can use on targets with level <= own level
 *
 * MIGRATION NOTE: This array maps directly to database structure:
 * CREATE TABLE powers (
 *   id INT PRIMARY KEY,
 *   name VARCHAR(32),
 *   pow_num INT,
 *   description TEXT,
 *   class_id INT,
 *   init_level ENUM('NO','YESLT','YESEQ','YES'),
 *   max_level ENUM('NO','YESLT','YESEQ','YES')
 * );
 */

/* Shorthand for readability */
#define NO PW_NO
#define YES PW_YES
#define YESLT PW_YESLT
#define YESEQ PW_YESEQ

/* Director Admin Builder Official Citizen Visitor Guest Guide/VIP Builder/VIP VIP */
static struct pow_list {
  char *name; /* name of power */
  ptype num; /* number of power */
  char *description; /* description of what the power is */
  int init[NUM_LIST_CLASSES];
  int max[NUM_LIST_CLASSES];
}  /* powers[]; */

/* static struct pow_list  */
powers[] =
{
  {
    "Allquota", POW_ALLQUOTA, "Ability to alter everyone's quota at once",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Announce", POW_ANNOUNCE, "Ability to @announce for free",
    {YES, YES, YES, YES, NO, NO, NO, NO,  NO,  YES},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, YES}
  },
  {
    "Ban", POW_BAN, "Ability to ban/unban people from channels",
    {YES, YES, NO,  YES, NO, NO, NO, YES, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, NO, YES}
  },
  {
    "Board", POW_BOARD, "Ability to be chairman of the +board.",
    {YES, NO,  NO,  NO,  NO, NO, NO, NO,  NO,  YES},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, YES}
  },
  {
    "Boot", POW_BOOT, "Ability to @boot players off the game",
    {YES, YESLT, NO,    YESLT, NO, NO, NO, NO,    NO, NO},
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, YESLT, NO, YESLT}
  },
  {
    "Broadcast", POW_BROADCAST, "Ability to @broadcast a message",
    {YES, YES, NO,  NO,  NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}
  },
  {
    "Chown", POW_CHOWN, "Ability to change ownership of an object",
    {YESEQ, YESEQ, YESEQ, YESLT, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES,   YESEQ, YESEQ, YESLT, NO, NO, NO, YESLT, YESLT, YESLT}
  },
  {
    "Class", POW_CLASS, "Ability to re@classify somebody",
    {YESLT, YESLT, NO, NO, NO, NO, NO, NO,    NO,    YESLT},
    {YES,   YESEQ, NO, NO, NO, NO, NO, YESLT, YESLT, YESLT}
  },
  {
    "Database", POW_DB, "Ability to use @dbck and other database utilities",
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Dbtop", POW_DBTOP, "Abililty to do a @dbtop",
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Examine", POW_EXAMINE, "Ability to see people's homes and locations",
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESEQ, YESLT, YESEQ}
  },
#ifdef ALLOW_EXEC
  {
    "Exec", POW_EXEC, "Power to execute external programs",
    {NO,  NO,  NO,  NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, NO, NO, NO, NO, NO, NO, NO}
  },
#else
  {
    "NUTTIN1", POW_NUTTIN1, "Ability to do NUTTIN - Disabled EXEC",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
#endif /* allow_exec */
  {
    "Free", POW_FREE, "Ability to build, etc. for free",
    {YES, YES, YES, NO, NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, NO, NO, NO, NO, YES, YES, YES}
  },
  {
    "Functions", POW_FUNCTIONS, "Ability to get correct results from all functions",
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}
  },
  {
    "Combat", POW_COMBAT, "Ability to do change Combat",
    {NO,  NO,  NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}
  },
#ifdef USE_INCOMING
  {
    "Incoming", POW_INCOMING, "Ability to connect net to non-players",
    {YES, NO,  NO,  NO,  NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, NO}
  },
#else
  {
    "NUTTIN2", POW_NUTTIN2, "Ability to do NUTTIN - Disabled Incoming",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
#endif
  {
    "Join", POW_JOIN, "Ability to 'join' players",
    {YES, YES, YES, YES, NO, NO, NO, YESEQ, YESLT, YESEQ},
    {YES, YES, YES, YES, NO, NO, NO, YES,   YESEQ, YESEQ}
  },
  {
    "Member", POW_MEMBER, "Ability to change your name and password",
    {YES, YES, YES, YES, YES, YES, NO, YES, YES, YES},
    {YES, YES, YES, YES, YES, YES, NO, YES, YES, YES}
  },
  {
    "Modify", POW_MODIFY, "Ability to modify other people's objects",
    {YESEQ, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES,   YESEQ, YESEQ, YESEQ, NO, NO, NO, YESEQ, YESEQ, YESEQ}
  },
  {
    "Money", POW_MONEY, "Power to have INFINITE money",
    {YES, YES, YES, NO,  NO, NO, NO, NO, NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, YES, NO}
  },
  {
    "MOTD", POW_MOTD, "Ability to set the Message of the Day",
    {YES, YES, YES, NO,  NO,  NO,  NO, NO,  NO,  NO},
    {YES, YES, YES, YES, YES, YES, NO, YES, YES, YES}
  },
  {
    "Newpassword", POW_NEWPASS, "Ability to use the @newpassword command",
    {YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Noslay", POW_NOSLAY, "Power to not be killed",
    {YES, YES, YES, YES, NO, NO, YES, NO,  YES, NO},
    {YES, YES, YES, YES, NO, NO, YES, YES, YES, YES}
  },
  {
    "Noquota", POW_NOQUOTA, "Power to have INFINITE quota",
    {YES, YES, YES, NO,  NO, NO, NO, NO, NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, YES, NO}
  },
  {
    "Nuke", POW_NUKE, "Power to @nuke other characters",
    {YESLT, NO,    NO, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, NO, NO, NO, NO, NO, NO, NO, NO}
  },
#ifdef USE_OUTGOING
  {
    "Outgoing", POW_OUTGOING, "Ability to initiate net connections.",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
#else
  {
    "NUTTIN3", POW_NUTTIN3, "Ability to do NUTTIN - Disabled Outgoing",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
#endif
  {
    "Pcreate", POW_PCREATE, "Power to create new characters",
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}
  },
  {
    "Poor", POW_POOR, "Power to use the @poor command",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Queue", POW_QUEUE, "Power to see everyone's commands in the queue",
    {YES, YESEQ, YESLT, YESLT, NO, NO, NO, NO, NO, NO},
    {YES, YES,   YES,   YES,   NO, NO, NO, NO, NO, NO}
  },
  {
    "Remote", POW_REMOTE, "Ability to do remote whisper, @pemit, etc.",
    {YES, YESEQ, YESLT, YESLT, NO, NO, NO, NO,  YESLT,  YESLT},
    {YES, YES,   YES,   YES,   NO, NO, NO, YES, YES,    YES}
  },
  {
    "Security", POW_SECURITY, "Ability to do various security-related things",
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Seeatr", POW_SEEATR, "Ability to see attributes on other people's things",
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESEQ, YESEQ, YESEQ}
  },
  {
    "Setpow", POW_SETPOW, "Ability to alter people's powers",
    {YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Setquota", POW_SETQUOTA, "Ability to change people's quotas",
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO},
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO}
  },
  {
    "Slay", POW_SLAY, "Ability to use the 'slay' command",
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, YESLT, NO, NO},
    {YES, YES,   YES,   YES,   NO, NO, NO, YESLT, NO, NO}
  },
  {
    "Shutdown", POW_SHUTDOWN, "Ability to @shutdown the game",
    {YES, NO,  NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Summon", POW_SUMMON, "Ability to 'summon' other players",
    {YESLT, YESLT, YESLT, YESLT, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES,   YES,   YES,   YESEQ, NO, NO, NO, YESLT, YESLT, YESLT}
  },
  {
    "Slave", POW_SLAVE, "Ability to set the slave flag.",
    {YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "NUTTIN4", POW_NUTTIN4, "Ability to do NUTTIN - Disabled Space",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "NUTTIN5", POW_NUTTIN5, "Ability to do NUTTIN - Removed Spoof",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Stats", POW_STATS, "Ability to @stat other ppl",
    {YES, YES, YES, YES, NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, NO}
  },
  {
    "Steal", POW_STEAL, "Ability to give negative amounts of credits",
    {YES, YES, NO,  NO,  NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, NO}
  },
  {
    "Teleport", POW_TELEPORT, "Ability to use unlimited @tel",
    {YES, YES, NO,  NO,  NO, NO, NO, YESLT, YESLT, YESLT},
    {YES, YES, YES, YES, NO, NO, NO, YESLT, YESLT, YESLT}
  },
  {
    "WizAttributes", POW_WATTR, "Ability to set Last, Queue, etc",
    {YES, YES, NO,  NO,  NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}
  },
  {
    "WizFlags", POW_WFLAGS, "Ability to set Temple, etc",
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "Who", POW_WHO, "Ability to see classes and hidden players on the WHO list",
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, NO,    NO, NO},
    {YES, YES,   YESEQ, YESEQ, NO, NO, NO, YESEQ, NO, NO}
  },
  {
    "Channel", POW_CHANNEL, "Ability to maintain all channels.",
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}
  },
  {
    "ComTalk", POW_COM_TALK, "Ability for non-player objects to talk on channels.",
    {NO, NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, YES, YES, YES, YES, YES, YES}
  },
};

/* Undefine shorthand */
#undef NO
#undef YES
#undef YESLT
#undef YESEQ



/* End of power configuration data */

#endif /* _LOADED_CONFIG_ */
