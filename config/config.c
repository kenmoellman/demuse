#include "config.h"


static char *perm_messages[] = {
  "Permission denied.",
  "Ummm... no.",
  "Lemme think about that.. No.",
  0
};


char *perm_denied()
{
  static int messageno = -1;
  if (perm_messages[++messageno] == 0)
    messageno = 0;
  return perm_messages[messageno]; 
}

/* the name of this muse */
char *muse_name = "YourMUSE";


/* name of +com channel to which db-info announcements should be
   sent to. */
char *dbinfo_chan = "dbinfo";
char *dc_chan = "*dc";

/* allow people to create new characters themselves */
int allow_create = 0;

/* combat-related stuff */
dbref paradox[] = { 0, 59, 1140, 1152, 1136, 55, 1164, 1169, 1173, 1177, -1};
static int combat = 3;


dbref player_start = 30;
dbref guest_start = 25;
dbref default_room = 0;  /* You need to make this something OTHER than player_start and guest_start! */
int initial_credits = 2000;
int allowance = 250;		/* credits gained per day */
char *start_quota = "100";
long default_idletime = 300;
long guest_boot_time = 300;
#ifdef USE_COMBAT
dbref graveyard = 12;
#endif

int number_guests = 30;
char *guest_prefix = "Guest";
char *guest_alias_prefix = "G";
char *guest_description = "You see a guest.";


long int max_pennies = 1000000;

int inet_port = 4208;

int fixup_interval = 1243;
int dump_interval = 2714;
int garbage_chunk = 3;

int max_output = 32767;		/* number of bytes until output flushed */

int max_input=1024;
int command_time_msec = 1000;	/* time slice length (milliseconds) */
int command_burst_size = 100;	/* number of commands allowed in a burst */
int commands_per_time = 1;	/* commands per slice after burst */

char *bad_object_doomsday = "600";
char *default_doomsday = "600";

int warning_chunk = 50;		/* number of non-checked objects that should
				 * be gone through when doing incremental
				 * warnings */
int warning_bonus = 30;		/* number of non-checked objects that one
				 * checked object counts as. */

/* you probably won't to change anything from here down. */
/* ----------------------------------------------------- */

/* general filenames */
#ifdef DBCOMP
char *def_db_in = "db/mdb.gz";
char *def_db_out = "db/mdb.gz";
#else
char *def_db_in = "db/mdb";
char *def_db_out = "db/mdb";
#endif

char *stdout_logfile = "logs/out.log";
char *wd_logfile = "logs/wd.log";

char *muse_pid_file = "logs/muse_pid";
char *wd_pid_file = "logs/wd_pid";

char *create_msg_file = "msgs/create.txt";
char *motd_msg_file = "msgs/motd.txt";
char *welcome_msg_file = "msgs/welcome.txt";
char *guest_msg_file = "msgs/guest.txt";
char *register_msg_file = "msgs/register.txt";
char *leave_msg_file = "msgs/leave.txt";
char *guest_lockout_file = "../config/guest-lockout";
char *welcome_lockout_file = "../config/welcome-lockout";
int enable_lockout=1;


dbref root = 1;

int thing_cost = 50;
int exit_cost = 1;
int room_cost = 100;
int robot_cost = 1000;
int channel_cost = 100;
int univ_cost = 100;

int link_cost = 1;

int find_cost = 10;
int search_cost = 10;
int page_cost = 1;
int announce_cost = 50;
int queue_cost = 100;		/* deposit */
int queue_loss = 150;		/* 1/queue_loss lost for each queue command */
int max_queue = 1000;		/* maximum queue commands per player */

int channel_name_limit = 32;	/* maximum length of channel name. */
int player_name_limit = 32;	/* maximum length of player name. */
int player_reference_limit = 5; /* longest name player can be
				 * referenced by. */
