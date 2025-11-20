/* $Id: log.h,v 1.4 1993/12/19 17:59:43 nils Exp $ */
/* log.h - extern definitions for logging things */


#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>

struct log {
  FILE *fptr;
  int counter;
  char *filename;
  char *com_channel;
};

extern struct log important_log, sensitive_log, error_log, 
  io_log, gripe_log, force_log, prayer_log, command_log, 
  combat_log, rlpage_log, suspect_log;

#define log_important(str) muse_log(&important_log, (str))
#define log_sensitive(str) muse_log(&sensitive_log, (str))
#define log_error(str) muse_log(&error_log, (str))
#define log_io(str) muse_log(&io_log, (str))
#define log_gripe(str) muse_log(&gripe_log, (str))
#define log_prayer(str) muse_log(&prayer_log, (str))
#define log_command(str) muse_log(&command_log, (str))
#define log_combat(str) muse_log(&combat_log, (str))
#define log_security(str) muse_log(&important_log, (str))
#define log_force(str) muse_log(&force_log, (str))
#define log_rlpage(str) muse_log(&rlpage_log, (str))
#define log_suspect(str) muse_log(&suspect_log, (str))

extern void muse_log (struct log *, const char *);

#endif /* __LOG_H */
