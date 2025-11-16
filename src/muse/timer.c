/* timer.c */
/* $Id: timer.c,v 1.9 1993/09/06 19:33:01 nils Exp $ */

/* ============================================================================
 * MODERNIZATION NOTES (2025)
 * ============================================================================
 * This file has been modernized with the following improvements:
 * 
 * SAFETY IMPROVEMENTS:
 * - Replaced strcpy() with strncpy() for command buffer safety
 * - Added bounds checking to prevent buffer overruns
 * - Improved signal handler safety
 *
 * CODE QUALITY:
 * - Full ANSI C compliance
 * - Better organization with section markers
 * - Comprehensive inline documentation
 * - Improved comments explaining timer intervals
 *
 * ANSI C COMPLIANCE:
 * - Proper signal handler declarations
 * - Consistent function prototypes
 *
 * SECURITY NOTES:
 * - Signal handlers are async-signal-safe
 * - Command buffer operations are bounded
 */

/* ============================================================================
 * INCLUDES
 * ============================================================================ */

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>

#ifdef XENIX
#include <sys/signal.h>
#else
#include <signal.h>
#endif

#include "db.h"
#include "sock.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static int alarm_triggered = 0;
extern char ccom[1024];  /* Current command name for logging */

/* ============================================================================
 * LOCAL FUNCTION PROTOTYPES
 * ============================================================================ */
void trig_idle_boot(void);


/* ============================================================================
 * @ATIME TRIGGER SYSTEM
 * ============================================================================ */

/**
 * Trigger @atime attributes on all objects that have them
 * 
 * This function is called periodically (every 5 minutes) to execute
 * any @atime attributes set on objects. @atime attributes are action
 * attributes that run on a timer.
 * 
 * SECURITY: Validates object references before triggering
 * 
 * PERFORMANCE NOTE: Iterates through entire database, so frequency
 * should be limited (currently 300 seconds).
 */
void trig_atime(void)
{
  dbref thing;

  for (thing = 0; thing < db_top; thing++) {
    if (GoodObject(thing) && *atr_get(thing, A_ATIME)) {
      did_it(thing, thing, NULL, NULL, NULL, NULL, A_ATIME);
    }
  }
}

/* ============================================================================
 * SIGNAL HANDLING
 * ============================================================================ */

/**
 * SIGALRM handler - sets flag when alarm fires
 * 
 * This function is called by the system when SIGALRM fires (every second).
 * It sets a flag that the main timer dispatch loop checks.
 * 
 * SAFETY: This is an async-signal-safe function. It only sets a flag
 * and does not perform any complex operations.
 * 
 * @param i Signal number (unused, required by signal handler signature)
 * @return void for void_signal_type systems, 0 otherwise
 */
static signal_type alarm_handler(int i)
{
  (void)i;  /* Unused parameter */
  alarm_triggered = 1;
  signal(SIGALRM, alarm_handler);
  
#ifdef void_signal_type
  return;
#else
  return 0;
#endif
}

/* ============================================================================
 * TIMER INITIALIZATION
 * ============================================================================ */

/**
 * Initialize the timer system
 * 
 * Sets up the SIGALRM handler and starts the 1-second alarm timer.
 * This should be called once during server startup.
 */
void init_timer(void)
{
  signal(SIGALRM, alarm_handler);
  signal(SIGHUP, alarm_handler);
  alarm(1);
}

/* ============================================================================
 * MAIN TIMER DISPATCH
 * ============================================================================ */

/**
 * Main timer dispatch function - called from main event loop
 * 
 * This function is called frequently from the main server loop.
 * When the alarm flag is set (once per second), it performs
 * various periodic maintenance tasks at different intervals.
 * 
 * TIMING INTERVALS:
 * - Every second: Queue processing (do_second)
 * - Every 60 seconds: New day check, idle boot checks
 * - Every 300 seconds (5 min): Resock, @atime triggers
 * - fixup_interval: Database consistency checks (dbck)
 * - dump_interval: Database dumps
 * - old_mail_interval: Stale mail deletion (if enabled)
 * 
 * SAFETY: All string operations use bounded functions
 */
void dispatch(void)
{
  static int ticks = 0;

  /* Only act when alarm has triggered */
  if (!alarm_triggered) {
    return;
  }

  alarm_triggered = 0;
  ticks++;

  /* === EVERY SECOND === */
  do_second();

#ifdef RESOCK
  /* === EVERY 5 MINUTES === */
  /* Re-establish socket connections if needed */
  if (!(ticks % 300)) {
    resock();
  }
#endif

  /* === FIXUP INTERVAL === */
  /* Database consistency check */
  if (!(ticks % fixup_interval)) {
    log_command("Dbcking...");
    
    strncpy(ccom, "dbck", sizeof(ccom) - 1);
    ccom[sizeof(ccom) - 1] = '\0';
    
    do_dbck(root);
    log_command("...Done.");
  }

  /* === DUMP INTERVAL === */
  /* Periodic database save */
  if (!(ticks % dump_interval)) {
    log_command("Dumping.");
    
    strncpy(ccom, "dump", sizeof(ccom) - 1);
    ccom[sizeof(ccom) - 1] = '\0';
    
    fork_and_dump();
  }

  /* === UPDATE BYTES === */
  /* Update byte usage tracking (spread across multiple ticks) */
  {
    int i;
    for (i = 0; i < ((db_top / 300) + 1); i++) {
      update_bytes();
    }
  }

#ifdef PURGE_OLDMAIL
  /* === OLD MAIL INTERVAL === */
  /* Delete stale mail periodically */
  {
    static int mticks = -1;
  
    if (mticks == -1) {
      mticks = old_mail_interval;
    }
  
    if (!mticks--) {
      log_command("Deleting old mail.\n");
      mticks = old_mail_interval;
      
      strncpy(ccom, "mail", sizeof(ccom) - 1);
      ccom[sizeof(ccom) - 1] = '\0';
      
      clear_old_mail();
      next_mail_clear = now + old_mail_interval;
    }
  }
#endif /* PURGE_OLDMAIL */

  /* === EVERY 5 MINUTES === */
  /* Trigger @atime attributes */
  if (!(ticks % 300)) {
    trig_atime();
  }

  /* === EVERY MINUTE === */
  /* Check for new day (for daily maintenance) */
  if (!(ticks % 60)) {
    check_newday();
  }

  /* Boot idle guests and unconnected descriptors */
  trig_idle_boot();

  /* === EVERY TICK === */
  /* Incremental garbage collection */
  strncpy(ccom, "garbage", sizeof(ccom) - 1);
  ccom[sizeof(ccom) - 1] = '\0';
  do_incremental();

  /* Topology processing */
  run_topology();

  /* Reset alarm for next second */
  alarm(1);
}



void trig_idle_boot(void)
{ 
  extern long guest_boot_time;
  struct descriptor_data *d, *dnext;

  if (!guest_boot_time) {
    return;
  }
  
  for (d = descriptor_list; d; d = dnext) { 
    dnext = d->next;

    /* Boot non-connected descriptors */
    if (d->state != CONNECTED) {
      /* Handle time initialization/wrapping */
      if (now - d->last_time <= 0) {
        d->last_time = now;
      }
      
      if (now - d->last_time > guest_boot_time) {
        queue_string(d, "You have been idle for too long. Sorry.\n");
        flush_all_output();
        
        log_io(tprintf("Concid %ld, host %s@%s, was idle booted.",
                       d->concid, d->user ? d->user : "unknown",
                       d->addr ? d->addr : "unknown"));
        
        shutdownsock(d);
      }
      continue;
    }

#ifdef BOOT_GUESTS
    /* Boot idle guest players */
    if (GoodObject(d->player) && Guest(d->player)) {
      if (now - d->last_time > guest_boot_time) {
        notify(d->player, "You have been idle for too long. Sorry.");
        flush_all_output();

        log_io(tprintf("Concid %ld (%s) was idle booted.",
                       d->concid, name(d->player)));

        shutdownsock(d);
      }
    }
#endif
  }
}



/* End of timer.c */
