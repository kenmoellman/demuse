/* match.h */
/* $Id: match.h,v 1.2 1992/10/11 15:15:13 nils Exp $ */

#include "copyright.h"

#include "db.h"



/* match functions */
/* Usage: init_match(player, name, type); match_this(); match_that(); ... */
/* Then get value from match_result() */

/* initialize matcher */
extern void init_match(dbref player, const char *name, int type);
extern void init_match_check_keys(dbref player, char *name, int type);
extern void set_match_allow_deleted(int value);  /* allow matching deleted objects */


/* match (NUMBER_TOKEN)number */
extern void match_absolute(void);

/* match "me" */
extern void match_me(void);

/* match "here" */
extern void match_here(void);

/* match something player is carrying */
extern void match_possession(void);

/* match something in the same room as player */
extern void match_neighbor(void);

/* match a name exactly in the same room as player */
extern void match_perfect(void);

/* match an exit from player's room */
extern void match_exit(void);

/* all of the above, except only Wizards do match_absolute and match_player */
extern void match_everything(void);

/* return match results */
extern dbref match_result(void); /* returns AMBIGUOUS for multiple inexacts */
extern dbref last_match_result(void); /* returns last result */

/*#define NOMATCH_MESSAGE "I don't see that here."*/
#define NOMATCH_PATT "I don't see %s here."

#define AMBIGUOUS_MESSAGE "I don't know which one you mean!"

extern dbref noisy_match_result(void); /* wrapper for match_result */
                                /* noisily notifies player */
                                /* returns matched object or NOTHING */

