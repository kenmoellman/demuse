/* cque.c */
/* $Id: cque.c,v 1.9 1993/08/16 01:57:18 nils Exp $ */

/*
 * ============================================================================
 * MODERNIZATION NOTES (2025)
 * ============================================================================
 * This file has been modernized with the following improvements:
 * 
 * SAFETY IMPROVEMENTS:
 * - Replaced all malloc() calls with SAFE_MALLOC for memory tracking
 * - Replaced all free() calls with SMART_FREE to prevent double-free
 * - Added buffer size limits to prevent overruns
 * - Enhanced validation with GoodObject() checks
 * - Changed strcpy() to strncpy() with size limits
 * - Changed sprintf() to snprintf() with bounds checking
 * - Added null pointer checks before dereferencing
 *
 * CODE QUALITY:
 * - All functions now use ANSI C prototypes
 * - Reorganized with clear === section markers
 * - Added comprehensive inline documentation
 * - Improved queue consistency checking
 * - Better error handling and logging
 *
 * QUEUE ARCHITECTURE:
 * - Priority-based command queue (lower pri = higher priority)
 * - Wait queue integrated into main queue
 * - PID system for tracking individual commands
 * - Per-player queue limits to prevent runaway objects
 *
 * SECURITY NOTES:
 * - All dbrefs validated with valid_player() or GoodObject()
 * - Queue cost enforced to prevent spam
 * - Runaway detection and automatic halting
 * - HAVEN flag prevents command execution
 */

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#ifdef XENIX
#include <sys/signal.h>
#else
#include <signal.h>
#endif /* xenix */

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"
#include "log.h"

/* ============================================================================
 * EXTERNAL GLOBALS
 * ============================================================================ */

extern char ccom[];
extern dbref cplr;

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

typedef struct bque BQUE;

/*
 * Queue Entry Structure
 * 
 * Represents a single command waiting to be executed.
 * Queue is maintained as a doubly-linked list sorted by priority and wait time.
 */
struct bque
{
  BQUE *next;               /* Next entry in queue */
  BQUE *prev;               /* Previous entry in queue */
  dbref player;             /* Player who will execute command */
  dbref cause;              /* Player causing command (for %n substitution) */
  char *env[10];            /* Environment variables from wild match */
  int pri;                  /* Priority of command (lower = higher priority) */
  time_t wait;              /* Timestamp - execute when now >= wait */
  int pid;                  /* Process ID for this command */
};

/* ============================================================================
 * QUEUE GLOBALS
 * ============================================================================ */

static BQUE *qfirst = NULL;   /* Head of command queue */
static BQUE *qlast = NULL;    /* Tail of command queue */

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

static void big_que(dbref player, char *command, dbref cause, int pri, time_t wait);
static int add_to(dbref player, int am);
static BQUE *find_wait(BQUE *queue);

void do_halt_player(dbref player, char *ncom);
void do_halt_process(dbref player, int pid);
int get_pid(void);
void free_pid(int pid);
void init_pid(void);

/* ============================================================================
 * COMMAND PARSING AND QUEUEING
 * ============================================================================ */

/*
 * parse_que_pri - Parse command string and queue with specified priority
 * 
 * Splits command on semicolons and queues each part separately.
 * 
 * SECURITY: Each command part is queued individually with proper validation
 */
void parse_que_pri(dbref player, char *command, dbref cause, int pri)
{
  char *s, *r;
  char temp_buf[MAX_COMMAND_LEN + 1];

  if (!command || !*command) {
    return;
  }

  /* Create safe copy of command string */
  strncpy(temp_buf, command, MAX_COMMAND_LEN);
  temp_buf[MAX_COMMAND_LEN] = '\0';
  
  s = temp_buf;
  while ((r = (char *)parse_up(&s, ';')))
  {
    big_que(player, r, cause, pri, 0);
  }
}

/*
 * parse_que - Parse and queue commands with automatic priority detection
 * 
 * Priority is determined by:
 * - A_NICE attribute if set
 * - 0 for player-caused commands
 * - 1 for object-caused commands
 * 
 * SECURITY: Validates player before queueing
 */
void parse_que(dbref player, char *command, dbref cause)
{
  int pri;
  char *p;

  if (!GoodObject(player)) {
    return;
  }

  /* Check for custom priority setting */
  p = atr_get(player, A_NICE);
  if (p && *p) {
    pri = (int)strtol(p, NULL, 10);
  } else {
    /* Default priority based on cause type */
    if (GoodObject(cause) && Typeof(cause) == TYPE_PLAYER) {
      pri = 0;
    } else {
      pri = 1;
    }
  }

  parse_que_pri(player, command, cause, pri);
}

/* ============================================================================
 * QUEUE ACCOUNTING
 * ============================================================================ */

/*
 * add_to - Adjust player's queue count
 * 
 * Updates A_QUEUE attribute to track number of commands in queue.
 * Returns new queue count.
 * 
 * SECURITY: Uses player's owner for accounting to prevent quota bypassing
 */
static int add_to(dbref player, int am)
{
  long num;
  char buff[200];

  if (!GoodObject(player)) {
    return 0;
  }

  player = db[player].owner;
  
  if (!GoodObject(player)) {
    return 0;
  }

  num = atol(atr_get(player, A_QUEUE));
  num += am;
  
  if (num > 0) {
    snprintf(buff, sizeof(buff), "%ld", num);
  } else {
    buff[0] = '\0';
  }
  
  atr_add(player, A_QUEUE, buff);
  return (num);
}

/* ============================================================================
 * CORE QUEUEING
 * ============================================================================ */

/*
 * big_que - Add command to priority queue
 * 
 * This is the core queueing function. It:
 * 1. Validates player can execute commands (not HAVEN)
 * 2. Charges queue cost
 * 3. Checks for runaway objects (queue limit)
 * 4. Allocates queue entry
 * 5. Inserts into sorted queue based on priority and wait time
 * 
 * SECURITY CRITICAL:
 * - Validates player with GoodObject()
 * - Enforces max_queue limit
 * - Sets HAVEN on runaway objects to prevent further damage
 * - Tracks memory allocations
 */
static void big_que(dbref player, char *command, dbref cause, int pri, time_t wait)
{
  int a, set;
  BQUE *tmp, *dah;

  /* Validate player */
  if (!GoodObject(player)) {
    return;
  }

  /* Check if player can execute commands */
  if (db[player].flags & HAVEN) {
    return;
  }

  /* Charge queue cost with random penalty */
  if (!payfor(player, queue_cost + (((random() & queue_loss) == 0) ? 1 : 0))) {
    if (GoodObject(db[player].owner)) {
      notify(db[player].owner, "Not enough money to queue command.");
    }
    return;
  }

  /* Check for runaway object */
  if (add_to(player, 1) > max_queue) {
    if (GoodObject(db[player].owner)) {
      notify(db[player].owner,
             tprintf("Run away object (%s), commands halted",
                     unparse_object(db[player].owner, player)));
    }
    log_important(
      tprintf("Run away object (%s), commands halted",
              unparse_object(db[player].owner, player)));
    do_halt_player(db[player].owner, "");

    /* Set HAVEN to prevent further command execution unless owner is director */
    if (GoodObject(player) && db[player].pows && db[player].pows[0] != CLASS_DIR) {
      db[player].flags |= HAVEN;
    }

    return;
  }

  /* Allocate queue entry - string data follows structure */
  SAFE_MALLOC(tmp, BQUE, sizeof(BQUE) + strlen(command) + 1);

  if (tmp == NULL) {
    add_to(player, -1); /* Rollback accounting */
    do_halt_player(player, "");
    log_io(tprintf("[%s] QUEUE: halted: %s (lack of memory).",
                   GoodObject(player) ? unparse_object(db[player].owner, player) : "#-1"));
    
    /* Set HAVEN on non-director objects */
    if (GoodObject(player) && db[player].pows && db[player].pows[0] != CLASS_DIR) {
      db[player].flags |= HAVEN;
    }
    return;
  }

  /* Initialize queue entry */
  {
    size_t cmd_len = strlen(command) + 1;
    strncpy(Astr(tmp), command, cmd_len - 1);
    Astr(tmp)[cmd_len - 1] = '\0';
  }
  tmp->player = player;
  tmp->next = NULL;
  tmp->prev = NULL;
  tmp->cause = cause;
  tmp->pri = pri;
  tmp->wait = now + wait;
  tmp->pid = get_pid();

  /* Copy environment variables */
  for (a = 0; a < 10; a++) {
    if (!wptr[a]) {
      tmp->env[a] = NULL;
    } else {
      char *p;
      size_t env_len = strlen(wptr[a]) + 1;
      
      SAFE_MALLOC(p, char, env_len);
      tmp->env[a] = p;
      
      if (p != NULL) {
        strncpy(p, wptr[a], env_len);
        p[env_len - 1] = '\0';
      } else {
        /* Out of memory - halt and cleanup */
        do_halt_player(player, "");
        log_io(tprintf("QUEUE: halted: %s (lack of memory).",
                       GoodObject(player) ? unparse_object(db[player].owner, player) : "#-1"));
        
        /* Free already-allocated env vars */
        for (int j = 0; j < a; j++) {
          if (tmp->env[j]) {
            SMART_FREE(tmp->env[j]);
          }
        }
        SMART_FREE(tmp);
        return;
      }
    }
  }

  /* ===== Insert into sorted queue ===== */
  
  if (qfirst) {
    BQUE *my_qlast = NULL; /* Track last processed node for repair */
    int flag = 0;

    for (dah = qfirst, set = 0; (flag != 1) && (set != 1); dah = dah->next) {
      /* Queue consistency check and repair */
      if ((dah != qfirst) && (!dah->prev)) {
        log_error("QUEUE: Broken queue detected - repairing prev pointer");
        if (my_qlast) {
          dah->prev = my_qlast;
        } else {
          qfirst = dah;
        }
      }

      /* Insert based on priority and wait time */
      if ((tmp->pri < dah->pri) || 
          ((tmp->pri == dah->pri) && (tmp->wait < dah->wait))) {
        
        if (dah == qfirst) {
          /* Insert at head */
          tmp->next = qfirst;
          qfirst->prev = tmp;
          qfirst = tmp;
          set = 1;
        } else {
          /* Insert in middle */
          if (dah->prev) {
            dah->prev->next = tmp;
            tmp->prev = dah->prev;
          }
          tmp->next = dah;
          dah->prev = tmp;
          set = 1;
        }
      }

      /* End of queue - append */
      if (dah->next == NULL && set == 0) {
        dah->next = tmp;
        tmp->prev = dah;
        qlast = tmp;
        set = 1;
      }
      
      my_qlast = dah;
    }

    /* Fallback - append to end if not inserted */
    if (set == 0) {
      if (qlast) {
        tmp->prev = qlast;
        qlast->next = tmp;
        qlast = tmp;
      } else {
        /* Queue was broken - reset */
        qfirst = qlast = tmp;
      }
    }
  } else {
    /* Empty queue - first entry */
    qlast = qfirst = tmp;
  }

  /* Process high-priority commands immediately */
  do_jobs(-20);
}

/* ============================================================================
 * QUEUE EXECUTION
 * ============================================================================ */

/*
 * do_jobs - Execute commands up to specified priority
 * 
 * Processes all commands with priority <= pri
 * 
 * USAGE: do_jobs(5) will execute all commands with priority 0-5
 */
void do_jobs(int pri)
{
  while (qfirst && (qfirst->pri <= pri) && do_top());
}

/*
 * test_top - Check if queue has entries ready to execute
 * 
 * Returns 1 if queue is not empty, 0 otherwise
 */
int test_top(void)
{
  return (qfirst ? 1 : 0);
}

/*
 * do_second - Called every second to process time-based queue entries
 * 
 * Executes one command from the queue
 */
void do_second(void)
{
  do_top();
  return;
}

/*
 * do_top - Execute one command from the queue
 * 
 * Finds first command ready to execute (wait time passed),
 * executes it, and removes it from queue.
 * 
 * SECURITY:
 * - Validates player with valid_player()
 * - Checks GOING flag
 * - Respects HAVEN flag
 * - Refunds queue cost
 * - Properly cleans up memory
 * 
 * Returns: 1 if command executed, 0 if no commands ready
 */
int do_top(void)
{
  int b = 0;
  BQUE *tmp;
  dbref player;

  tmp = qfirst;

  /* Find first command ready to execute */
  while ((tmp) && (tmp->wait > now)) {
    tmp = tmp->next;
  }

  if (!tmp) {
    return 0;
  }

  /* Validate player and execute command */
  if (valid_player(tmp->player) && !(db[tmp->player].flags & GOING)) {
    giveto(tmp->player, queue_cost);
    cplr = tmp->player;
    strncpy(ccom, Astr(tmp), sizeof(ccom) - 1);
    ccom[sizeof(ccom) - 1] = '\0';
    
    add_to(player = tmp->player, -1);
    tmp->player = NOTHING;
    
    if (!(db[player].flags & HAVEN)) {
      char buff[1030];
      int a;

      /* Set up environment variables */
      for (a = 0; a < 10; a++) {
        wptr[a] = tmp->env[a];
      }

      log_command(tprintf("Queue processing: %s (pri: %d)", Astr(tmp), tmp->pri));

      func_zerolev();
      pronoun_substitute(buff, tmp->cause, Astr(tmp), player);
      inc_qcmdc(); /* Increment command statistics */
      
      /* Execute the command */
      if (GoodObject(tmp->cause)) {
        size_t cause_name_len = strlen(db[tmp->cause].name);
        if (cause_name_len < sizeof(buff)) {
          process_command(player, buff + cause_name_len, tmp->cause);
        }
      }
    }
  }

  /* ===== Remove from queue ===== */

  if ((tmp == qfirst) && (tmp == qlast)) {
    /* Only entry in queue */
    qfirst = qlast = NULL;
  } else {
    if (tmp == qfirst) {
      /* Remove from head */
      qfirst = qfirst->next;
      if (qfirst) {
        qfirst->prev = NULL;
      }
      tmp->next = NULL;
    } else if (tmp == qlast) {
      /* Remove from tail */
      qlast = qlast->prev;
      tmp->prev = NULL;
      if (qlast) {
        qlast->next = NULL;
      }
    } else {
      /* Remove from middle */
      if (tmp->prev) {
        tmp->prev->next = tmp->next;
      }
      if (tmp->next) {
        tmp->next->prev = tmp->prev;
      }
      tmp->next = tmp->prev = NULL;
    }
  }

  /* Free resources */
  free_pid(tmp->pid);

  for (b = 0; b < 10; b++) {
    if (tmp->env[b]) {
      SMART_FREE(tmp->env[b]);
      tmp->env[b] = NULL;
    }
  }
  
  SMART_FREE(tmp);

  /* Consistency check */
  if (qfirst == NULL) {
    qlast = NULL;
  }

  return 1;
}

/* ============================================================================
 * WAIT QUEUE
 * ============================================================================ */

/*
 * wait_que - Queue a command with a delay
 * 
 * Similar to parse_que but adds a wait time before execution.
 * 
 * PARAMETERS:
 *   player - Object executing command
 *   wait - Seconds to wait before execution
 *   command - Command string to execute
 *   cause - Object that caused this command
 */
void wait_que(dbref player, int wait, char *command, dbref cause)
{
  int pri;
  char *p;

  if (!GoodObject(player)) {
    return;
  }

  /* Charge queue cost */
  if (!payfor(player, queue_cost + (((random() & queue_loss) == 0) ? 1 : 0))) {
    notify(player, "Not enough money to queue command.");
    return;
  }

  /* Determine priority - @wait commands get slightly lower priority */
  p = atr_get(player, A_NICE);
  if (p && *p) {
    pri = (int)strtol(p, NULL, 10) + 5;
    if (pri > 20) {
      pri = 20;
    }
  } else {
    if (GoodObject(cause) && Typeof(cause) == TYPE_PLAYER) {
      pri = 5;
    } else {
      pri = 6;
    }
  }

  big_que(player, command, cause, pri, wait);
}

/*
 * find_wait - Find first entry in queue with future wait time
 * 
 * Used by old queue display code (now deprecated)
 */
static BQUE *find_wait(BQUE *queue)
{
  BQUE *tmp;

  for (tmp = queue; tmp != NULL; tmp = tmp->next) {
    if (now < tmp->wait) {
      break;
    }
  }

  return tmp;
}

/* ============================================================================
 * QUEUE DISPLAY
 * ============================================================================ */

/*
 * do_queue - Display queue contents
 * 
 * Shows all queued commands visible to player.
 * Players see their own commands.
 * Players with POW_QUEUE see all commands.
 * 
 * SECURITY: Validates ownership before displaying
 */
void do_queue(dbref player)
{
  BQUE *tmp;
  int can_see = power(player, POW_QUEUE);
  char mytmp[30];

  if (!GoodObject(player)) {
    return;
  }

  if (qfirst) {
    notify(player, "PID   Player               Pr Wait  Command");
    
    for (tmp = qfirst; tmp != NULL; tmp = tmp->next) {
      if (!GoodObject(tmp->player)) {
        continue;
      }
      
      if ((db[tmp->player].owner == db[player].owner) || can_see) {
        /* Format player name with truncation */
        snprintf(mytmp, sizeof(mytmp), "[#%" DBREF_FMT " %-20.20s",
                 tmp->player,
                 GoodObject(tmp->player) ? db[tmp->player].name : "INVALID");
        mytmp[sizeof(mytmp) - 1] = '\0';
        
        if (strlen(mytmp) > 18) {
          mytmp[19] = '\0';
        }
        strncat(mytmp, "]", sizeof(mytmp) - strlen(mytmp) - 1);
        
        notify(player, tprintf("%5d %s %2d %5ld %s", 
                               tmp->pid, mytmp, tmp->pri, 
                               (long)(tmp->wait - now), Astr(tmp)));
      }
    }
  } else {
    notify(player, "@ps: No processes in the queue at this time.");
  }
}

/* ============================================================================
 * QUEUE HALTING
 * ============================================================================ */

/*
 * do_halt - Main halt command dispatcher
 * 
 * Handles:
 * - @halt - halt own commands
 * - @halt all - halt everything (requires POW_SECURITY)
 * - @halt <pid> - halt specific process
 * - @halt <player> - halt player's commands (requires POW_SECURITY)
 */
void do_halt(dbref player, char *arg1, char *arg2)
{
  if (!GoodObject(player)) {
    return;
  }

  if (!(arg1 && strlen(arg1))) {
    /* @halt with no args - halt own commands */
    do_halt_player(player, arg2);
  } else if (!strcmp(arg1, "all")) {
    /* @halt all - halt everything */
    do_haltall(player);
  } else if (((int)strtol(arg1, NULL, 10) > 0) || (!strncmp(arg1, "0", 2))) {
    /* @halt <number> - halt by PID */
    do_halt_process(player, (int)strtol(arg1, NULL, 10));
  } else {
    /* @halt <player> - halt another player */
    dbref lu = lookup_player(arg1);

    if (lu > -1 && GoodObject(lu)) {
      if (!power(player, POW_SECURITY)) {
        notify(player, "@halt: You do not have the power.");
        return;
      }
      do_halt_player(lu, arg2);
      notify(player, tprintf("@halt: Halted %s", unparse_object(player, lu)));
    } else {
      notify(player, "@halt: Invalid Syntax.");
    }
  }
}

/*
 * do_haltall - Halt entire queue
 * 
 * Clears all commands from queue and refunds costs.
 * 
 * SECURITY: Requires POW_SECURITY power
 */
void do_haltall(dbref player)
{
  BQUE *i, *next;
  int a;

  if (!GoodObject(player)) {
    return;
  }

  if (!db) {
    return;
  }

  if (!power(player, POW_SECURITY)) {
    notify(player, "You can't halt everything.");
    return;
  }

  if (!qlast || !qfirst) {
    qfirst = qlast = NULL;
    log_error("broken queue detected during haltall");
    return;
  }

  /* Free all queue entries */
  while (qfirst) {
    next = qfirst->next;
    i = qfirst;
    
    /* Free environment variables */
    for (a = 0; a < 10; a++) {
      if (qfirst->env[a]) {
        SMART_FREE(qfirst->env[a]);
        qfirst->env[a] = NULL;
      }
    }
    
    /* Refund queue cost */
    if (GoodObject(qfirst->player)) {
      giveto(qfirst->player, queue_cost);
    }
    
    free_pid(qfirst->pid);
    qfirst = next;
    SMART_FREE(i);
  }

  qfirst = NULL;
  qlast = NULL;
  notify(player, "@halt: Everything halted.");
}

/*
 * do_halt_process - Halt specific process by PID
 * 
 * SECURITY:
 * - Must own the object OR
 * - Have POW_SECURITY power
 */
void do_halt_process(dbref player, int pid)
{
  BQUE *point;

  if (!GoodObject(player)) {
    return;
  }

  for (point = qfirst; point; point = point->next) {
    if (point->pid == pid) {
      if (!GoodObject(point->player)) {
        notify(player, "@halt: Invalid process (bad player reference).");
        return;
      }

      /* Check permissions */
      if ((point->player == player) || 
          (real_owner(point->player) == real_owner(player)) || 
          (power(player, POW_SECURITY))) {
        
        int a;

        /* Remove from queue */
        if (point == qfirst) {
          qfirst = point->next;
          if (qfirst == NULL) {
            qlast = NULL;
          } else {
            qfirst->prev = NULL;
          }
        } else if (point == qlast) {
          qlast = qlast->prev;
          if (qlast) {
            qlast->next = NULL;
          }
        } else {
          if (point->next) {
            point->next->prev = point->prev;
          }
          if (point->prev) {
            point->prev->next = point->next;
          }
        }

        /* Free resources */
        for (a = 0; a < 10; a++) {
          if (point->env[a]) {
            SMART_FREE(point->env[a]);
            point->env[a] = NULL;
          }
        }
        
        point->player = NOTHING;
        free_pid(point->pid);
        SMART_FREE(point);
        
        notify(player, tprintf("@halt: Terminated process %d", pid));
        return;
      } else {
        notify(player, "@halt: Sorry. You don't control that process.");
        return;
      }
    }
  }
  
  notify(player, "@halt: Sorry. That process ID wasn't found.");
}

/*
 * do_halt_player - Halt all commands for a player
 * 
 * Removes all queue entries for player or objects owned by player.
 * Refunds queue costs and updates accounting.
 * 
 * PARAMETERS:
 *   player - Player whose commands to halt
 *   ncom - Optional command to queue after halting
 */
void do_halt_player(dbref player, char *ncom)
{
  BQUE *trail = NULL, *point, *next;
  int num = 0;

  if (!GoodObject(player)) {
    return;
  }

  /* Notify player unless QUIET */
  if (!(db[player].flags & QUIET)) {
    if (player == db[player].owner) {
      notify(db[player].owner, "@halt: Player halted.");
    } else if ((!(db[db[player].owner].flags & QUIET)) && ncom && *ncom) {
      notify(db[player].owner, 
             tprintf("@halt: %s halted.", unparse_object(db[player].owner, player)));
    }
  }

  /* Remove matching entries from queue */
  for (point = qfirst; point; point = next) {
    if (!GoodObject(point->player)) {
      next = point->next;
      continue;
    }

    if ((point->player == player) || (real_owner(point->player) == player)) {
      int a;

      num--;
      
      /* Refund cost */
      giveto(point->player, queue_cost);
      next = point->next;

      /* Remove from queue */
      if (point == qfirst) {
        qfirst = point->next;
        if (qfirst == NULL) {
          qlast = NULL;
        } else {
          qfirst->prev = NULL;
        }
      } else if (point == qlast) {
        qlast = qlast->prev;
        if (qlast) {
          qlast->next = NULL;
        }
      } else {
        if (point->next) {
          point->next->prev = point->prev;
        }
        if (point->prev) {
          point->prev->next = point->next;
        }
      }

      /* Free resources */
      for (a = 0; a < 10; a++) {
        if (point->env[a]) {
          SMART_FREE(point->env[a]);
          point->env[a] = NULL;
        }
      }
      
      point->player = NOTHING;
      free_pid(point->pid);
      SMART_FREE(point);
    } else {
      next = (trail = point)->next;
    }
  }

  /* Update queue accounting */
  if (db[player].owner == player) {
    atr_add(player, A_QUEUE, "");
  } else {
    add_to(player, num);
  }
  
  /* Queue new command if provided */
  if (ncom && *ncom) {
    parse_que(player, ncom, player);
  }
}

/* ============================================================================
 * PROCESS ID (PID) MANAGEMENT
 * ============================================================================
 */

static char pid_list[32768]; /* Bitmap of allocated PIDs */

/*
 * init_pid - Initialize PID allocation system
 * 
 * Clears all PIDs to unallocated state
 */
void init_pid(void)
{
  int x;

  for (x = 0; x < 32768; x++) {
    pid_list[x] = '\0';
  }
  return;
}

/*
 * get_pid - Allocate a new PID
 * 
 * Returns: PID number (0-32767) or -1 if none available
 * 
 * ALGORITHM: Simple linear search for first '\0' byte
 */
int get_pid(void)
{
  static char *p = NULL; /* Next PID to try */

  if (p == NULL) {
    init_pid();
    p = pid_list;
  }

  /* Find next free PID */
  while (*p != '\0') {
    p++;
    
    /* Wrap around if needed */
    if ((p - pid_list) >= 32768) {
      p = pid_list;
      
      /* Check if we wrapped back to start with no free PIDs */
      if (*p != '\0') {
        log_error("OUT OF PIDS! Critical queue error.");
        return -1;
      }
    }
  }

  /* Mark as allocated */
  *p = ' ';
  
  return (p - pid_list);
}

/*
 * free_pid - Free a PID for reuse
 * 
 * PARAMETERS:
 *   pid - PID to free (0-32767)
 */
void free_pid(int pid)
{
  if (pid >= 0 && pid < 32768) {
    pid_list[pid] = '\0';
  }
}
