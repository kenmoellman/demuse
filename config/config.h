/* config.h */
/* config-dist,v 1.3 1993/02/06 18:00:09 nils Exp */

/* note: some of the options in here may not work/compile correctly.
 * please try compiling first without changing things like
 * REGISTRATION and RESTRICTED_BUILDING. */
#define ALPHA

/* if you're on an ancient SysV box, this is apparently needed. */
#undef RESOCK

/* can you go home across zones? */
#define HOME_ACROSS_ZONES

#ifndef _LOADED_CONFIG_
#define _LOADED_CONFIG_

/* change this only every once in a while.  it should be secure enough to not 
 * create a problem anymore.  But, if it does, just change this string.  -wm
 */
#define GUEST_PASSWORD "sjf\thdssd\ndsfg"

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


/* define whether or not you want reverse DNS.  This option is fine on most
 * systems, however, if your server has a slow DNS server or just does not 
 * reverse resolve, Reverse DNS lookups will be really slow.  If, when someone 
 * connects, the entire MUSE lags for 2 minutes, try turning this option off.  */
#define HOST_LOOKUPS 


/* Who list options */

/* Sort who list by idle time? */
/* #define SORT_BY_IDLE */

/* color output lines on who list for those who are idle? */
#define WHO_IDLE_COLOR

/* define if wish to allow objects to talk on com channels */
#define ALLOW_COM_NP

/* define Pueblo client support. defined = ON */
/* #define PUEBLO_CLIENT  */

/* message configuration */

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


/* The new Blacklisting feature - it's pretty cool, check out the help on it. */
#define USE_BLACKLIST

/* define this if you'd like to have teh system automatically purge +mail that   *
 * users delete out of other peoples' mailboxes ( +mail delete=wm:6 would delete *
 * AND PURGE message 6 if it was written by the issuer, and unread by wm )      */
#undef TARGET_DEL_PURGE

/* define this if you want dbtop to require powers. */
#define DBTOP_POW

/* Guests get kicked at reboot?  Guests get idlebooted? Define this to make it so.  */
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

/* define this if you want to use the Universe mods */
#define USE_UNIV

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

#endif /* _LOADED_CONFIG_ */
