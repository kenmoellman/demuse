
/* powerlist.c */
/* $Id: powerlist.c,v 1.13 1993/05/27 23:17:43 nils Exp $ */

#include "config.h"
#include "db.h"
#include "externs.h"

#include "powers.h"

#define NO PW_NO
#define YES PW_YES
#define YESLT PW_YESLT
#define YESEQ PW_YESEQ

/* Director Admin Judge  Counselor Citzen  Vistr Guest  Guide Builder VIP */
struct pow_list powers[] =
{
  {
    "Allquota", POW_ALLQUOTA, "Ability to alter everyone's quota at once",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
  {
    "Announce", POW_ANNOUNCE, "Ability to @announce for free",
    {YES, YES, YES, YES, NO, NO, NO, NO,  NO,  YES},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, YES}},
  {
    "Ban", POW_BAN, "Ability to ban/unban people from channels",
    {YES, YES, NO,  YES, NO, NO, NO, YES, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, NO, YES}},
  {
    "Board", POW_BOARD, "Ability to be chairman of the +board.",
    {YES, NO,  NO,  NO,  NO, NO, NO, NO,  NO,  YES},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, YES}},
  {
    "Boot", POW_BOOT, "Ability to @boot players off the game",
    {YES, YESLT, NO,    YESLT, NO, NO, NO, NO,    NO, NO},
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, YESLT, NO, YESLT}},
  {
    "Broadcast", POW_BROADCAST, "Ability to @broadcast a message",
    {YES, YES, NO,  NO,  NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}},
  {
    "Chown", POW_CHOWN, "Ability to change ownership of an object",
    {YESEQ, YESEQ, YESEQ, YESLT, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES,   YESEQ, YESEQ, YESLT, NO, NO, NO, YESLT, YESLT, YESLT}},
  {
    "Class", POW_CLASS, "Ability to re@classify somebody",
    {YESLT, YESLT, NO, NO, NO, NO, NO, NO,    NO,    YESLT},
    {YES,   YESEQ, NO, NO, NO, NO, NO, YESLT, YESLT, YESLT}},
  {
    "Database", POW_DB, "Ability to use @dbck and other database utilities",
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
#ifdef DBTOP_POW
  {
    "Dbtop", POW_DBTOP, "Abililty to do a @dbtop",
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}},
#else
  { 
    "NUTTIN0", POW_NUTTIN0, "Ability to do NUTTIN - Disabled POW_DBTOP",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
#endif /* DBTOP_POW */
  {
    "Examine", POW_EXAMINE, "Ability to see people's homes and locations",
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESEQ, YESLT, YESEQ}},
#ifdef ALLOW_EXEC
  {
    "Exec", POW_EXEC, "Power to execute external programs",
    {NO,  NO,  NO,  NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, NO, NO, NO, NO, NO, NO, NO}},
#else
  { 
    "NUTTIN1", POW_NUTTIN1, "Ability to do NUTTIN - Disabled EXEC",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
#endif /* allow_exec */
  {
    "Free", POW_FREE, "Ability to build, etc. for free",
    {YES, YES, YES, NO, NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, NO, NO, NO, NO, YES, YES, YES}},
  {
    "Functions", POW_FUNCTIONS, "Ability to get correct results from all functions",
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}},
  {
    "Combat", POW_COMBAT, "Ability to do change Combat",
    {NO,  NO,  NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}},
#ifdef USE_INCOMING
  {
    "Incoming", POW_INCOMING, "Ability to connect net to non-players",
    {YES, NO,  NO,  NO,  NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, NO}},
#else
  {
    "NUTTIN2", POW_NUTTIN2, "Ability to do NUTTIN - Disabled Incoming",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
#endif
  {
    "Join", POW_JOIN, "Ability to 'join' players",
    {YES, YES, YES, YES, NO, NO, NO, YESEQ, YESLT, YESEQ},
    {YES, YES, YES, YES, NO, NO, NO, YES,   YESEQ, YESEQ}},
  {
    "Member", POW_MEMBER, "Ability to change your name and password",
    {YES, YES, YES, YES, YES, YES, NO, YES, YES, YES},
    {YES, YES, YES, YES, YES, YES, NO, YES, YES, YES}},
  {
    "Modify", POW_MODIFY, "Ability to modify other people's objects",
    {YESEQ, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES,   YESEQ, YESEQ, YESEQ, NO, NO, NO, YESEQ, YESEQ, YESEQ}},
  {
    "Money", POW_MONEY, "Power to have INFINITE money",
    {YES, YES, YES, NO,  NO, NO, NO, NO, NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, YES, NO}},
  {
    "MOTD", POW_MOTD, "Ability to set the Message of the Day",
    {YES, YES, YES, NO,  NO,  NO,  NO, NO,  NO,  NO},
    {YES, YES, YES, YES, YES, YES, NO, YES, YES, YES}},
  {
    "Newpassword", POW_NEWPASS, "Ability to use the @newpassword command",
    {YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO}},
  {
    "Noslay", POW_NOSLAY, "Power to not be killed",
    {YES, YES, YES, YES, NO, NO, YES, NO,  YES, NO},
    {YES, YES, YES, YES, NO, NO, YES, YES, YES, YES}},
  {
    "Noquota", POW_NOQUOTA, "Power to have INFINITE quota",
    {YES, YES, YES, NO,  NO, NO, NO, NO, NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, YES, NO}},
  {
    "Nuke", POW_NUKE, "Power to @nuke other characters",
    {YESLT, NO,    NO, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, NO, NO, NO, NO, NO, NO, NO, NO}},
#ifdef USE_OUTGOING
  {
    "Outgoing", POW_OUTGOING, "Ability to initiate net connections.",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
#else
  {
    "NUTTIN3", POW_NUTTIN3, "Ability to do NUTTIN - Disabled Outgoing",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
#endif
  {
    "Pcreate", POW_PCREATE, "Power to create new characters",
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}},
  {
    "Poor", POW_POOR, "Power to use the @poor command",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
  {
    "Queue", POW_QUEUE, "Power to see everyone's commands in the queue",
    {YES, YESEQ, YESLT, YESLT, NO, NO, NO, NO, NO, NO},
    {YES, YES,   YES,   YES,   NO, NO, NO, NO, NO, NO}},
  {
    "Remote", POW_REMOTE, "Ability to do remote whisper, @pemit, etc.",
    {YES, YESEQ, YESLT, YESLT, NO, NO, NO, NO,  YESLT,  YESLT},
    {YES, YES,   YES,   YES,   NO, NO, NO, YES, YES,    YES}},
  {
  "Security", POW_SECURITY, "Ability to do various security-related things",
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
  {
 "Seeatr", POW_SEEATR, "Ability to see attributes on other people's things",
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES, YESEQ, YESEQ, YESEQ, NO, NO, NO, YESEQ, YESEQ, YESEQ}},
  {
    "Setpow", POW_SETPOW, "Ability to alter people's powers",
    {YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO}},
  {
    "Setquota", POW_SETQUOTA, "Ability to change people's quotas",
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO},
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, NO, NO, NO}},
  {
    "Slay", POW_SLAY, "Ability to use the 'slay' command",
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, YESLT, NO, NO},
    {YES, YES,   YES,   YES,   NO, NO, NO, YESLT, NO, NO}},
  {
    "Shutdown", POW_SHUTDOWN, "Ability to @shutdown the game",
    {YES, NO,  NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}},
  {
    "Summon", POW_SUMMON, "Ability to 'summon' other players",
    {YESLT, YESLT, YESLT, YESLT, NO, NO, NO, YESLT, YESLT, YESLT},
    {YES,   YES,   YES,   YESEQ, NO, NO, NO, YESLT, YESLT, YESLT}},
  {
    "Slave", POW_SLAVE, "Ability to set the slave flag.",
    {YESLT, YESLT, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES,   YESLT, NO, NO, NO, NO, NO, NO, NO, NO}},
#ifdef USE_SPACE
  {
    "Space", POW_SPACE, "Ability to control the cosmos",
    {NO,  NO,  NO,  NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, NO, NO, NO, NO, NO, NO, NO}},
#else
  {
    "NUTTIN4", POW_NUTTIN4, "Ability to do NUTTIN - Disabled Space",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
#endif
  {
    "NUTTIN5", POW_NUTTIN5, "Ability to do NUTTIN - Removed Spoof",
    {NO,  NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
  {
    "Stats", POW_STATS, "Ability to @stat other ppl",
    {YES, YES, YES, YES, NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, NO}},
  {
    "Steal", POW_STEAL, "Ability to give negative amounts of credits",
    {YES, YES, NO,  NO,  NO, NO, NO, NO,  NO,  NO},
    {YES, YES, YES, YES, NO, NO, NO, YES, YES, NO}},
  {
    "Teleport", POW_TELEPORT, "Ability to use unlimited @tel",
    {YES, YES, NO,  NO,  NO, NO, NO, YESLT, YESLT, YESLT},
    {YES, YES, YES, YES, NO, NO, NO, YESLT, YESLT, YESLT}},
  {
    "WizAttributes", POW_WATTR, "Ability to set Last, Queue, etc",
    {YES, YES, NO,  NO,  NO, NO, NO, NO, NO, NO},
    {YES, YES, YES, YES, NO, NO, NO, NO, NO, NO}},
  {
    "WizFlags", POW_WFLAGS, "Ability to set Temple, etc",
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}},
  {
    "Who", POW_WHO, "Ability to see classes and hidden players on the WHO list",
    {YES, YESLT, YESLT, YESLT, NO, NO, NO, NO,    NO, NO},
    {YES, YES,   YESEQ, YESEQ, NO, NO, NO, YESEQ, NO, NO}},
  {
    "Channel", POW_CHANNEL, "Ability to maintain all channels.",
    {YES, NO, NO, NO, NO, NO, NO, NO, NO, NO},
    {YES, YES, NO, NO, NO, NO, NO, NO, NO, NO}},
};

static char *classnames[] =
{
  " ?",
  "Guest", "Visitor", "Citizen",
  "Builder", "VIP", "Guide", "Counselor", "Judge",
  "Admin", "Director",
  NULL
};
static char *typenames[] =
{
"Room", "Thing", "Exit", "Universe", "Channel", " 0x5", " 0x6", " 0x7", "Player"
};
 
char *class_to_name(class)
int class;
{
  if (class >= NUM_CLASSES || class <= 0)
    return NULL;
  return classnames[class];
}

int name_to_class(name)
char *name;
{
  int k;

  for (k = 0; classnames[k]; k++)
    if (!string_compare(name, classnames[k]))
      return k;
  return 0;
}
char *type_to_name(type)
int type;
{
  if (type >= 0 && type < 9)
    return typenames[type];
  else
    return NULL;
}

int class_to_list_pos(type)
int type;
{
  switch (type)
  {
  case CLASS_DIR:
    return 0;
  case CLASS_ADMIN:
    return 1;
  case CLASS_BUILDER:
    return 2;
  case CLASS_OFFICIAL:
    return 3;
  case CLASS_CITIZEN:
    return 4;
  case CLASS_VISITOR:
    return 5;
  case CLASS_GUEST:
    return 6;
  case CLASS_JUNOFF:
    return 7;
  case CLASS_PCITIZEN:
  case CLASS_GROUP:
    return 8;
  default:
    return 5;
  }
}
