/* speech.h - Basic player communication functions
 * 
 * This header defines only the basic communication functions
 * that remain in speech.c after reorganization.
 */

#ifndef __SPEECH_H__
#define __SPEECH_H__

#include <sys/types.h>
#include "db.h"

/* ===================================================================
 * Communication Tokens
 * These should probably move to config.h eventually
 * =================================================================== */

#ifndef POSE_TOKEN
#define POSE_TOKEN ':'      /* Start of pose */
#define NOSP_POSE ';'       /* Possessive pose */
#define THINK_TOKEN '.'     /* Think bubble */
#endif

/* ===================================================================
 * Utility Functions  
 * =================================================================== */

/**
 * Get speaking name for an object (spoof-protected)
 * @param thing Object to get name for
 * @return Colorized name from database
 */
char *spname(dbref thing);

/**
 * Reconstruct a message split by '=' in parsing
 * @param arg1 First part of message
 * @param arg2 Second part (after '=')
 * @return Complete reconstructed message (allocated)
 */
char *reconstruct_message(const char *arg1, const char *arg2);

/* ===================================================================
 * Basic Communication Commands
 * =================================================================== */

/**
 * SAY command - speak in current room
 * @param player Speaker
 * @param arg1 First part of message
 * @param arg2 Second part if split by =
 */
void do_say(dbref player, char *arg1, char *arg2);

/**
 * WHISPER command - private message to someone in same room
 * @param player Whisperer
 * @param arg1 Target player
 * @param arg2 Message to whisper
 */
void do_whisper(dbref player, char *arg1, char *arg2);

/**
 * POSE command - emote an action
 * @param player Actor
 * @param arg1 First part of pose
 * @param arg2 Second part if split by =
 * @param possessive 1 for possessive pose (:'s), 0 for normal
 */
void do_pose(dbref player, char *arg1, char *arg2, int possessive);

/**
 * THINK command - show a thought bubble
 * @param player Thinker
 * @param arg1 First part of thought
 * @param arg2 Second part if split by =
 */
void do_think(dbref player, char *arg1, char *arg2);

/**
 * TO command - directed speech with [to target] prefix
 * @param player Speaker
 * @param arg1 Target and message combined
 * @param arg2 Additional message part if split by =
 */
void do_to(dbref player, char *arg1, char *arg2);

/* ===================================================================
 * Simple Utility Commands
 * =================================================================== */

/**
 * ECHO command - echo text back to player
 * @param player Player to echo to
 * @param arg1 First part of message
 * @param arg2 Second part if split by =
 * @param type 0 for pronoun substitution, non-zero for literal
 */
void do_echo(dbref player, char *arg1, char *arg2, int type);

/**
 * USE command - use an object
 * @param player User
 * @param arg1 Object to use
 */
void do_use(dbref player, char *arg1);

#endif /* __SPEECH_H__ */
