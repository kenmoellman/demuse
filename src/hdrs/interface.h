/* interface.h */
/* $Id: interface.h,v 1.2 1992/10/11 15:15:09 nils Exp $ */

#include "copyright.h"

#include "db.h"

/* these symbols must be defined by the interface */
extern void notify(dbref player, const char *msg);
extern int shutdown_flag; /* if non-zero, interface should shut down */
extern void emergency_shutdown(void);
extern int boot_off(dbref player);	/* remove a player */

/* the following symbols are provided by game.c */

extern void process_command(dbref player, char *command, dbref cause);

extern dbref create_player(const char *name, const char *password, int class, dbref start);
extern dbref connect_player(const char *name, const char *password);
extern void do_look_around(dbref player);

extern int init_game(char *infile, char *outfile);
extern void dump_database(void);
extern void panic(char *message);
extern int depth;
