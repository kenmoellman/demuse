/* help.h */
/* $Id: help.h,v 1.2 1992/10/11 15:15:06 nils Exp $ */

/* this used to be 90. 253 should be better */
#define  LINE_SIZE		253

#define  TOPIC_NAME_LEN		30

typedef struct {
    long pos;				/* index into help file */
    int len;				/* length of help entry */
    char topic[TOPIC_NAME_LEN+1];	/* topic of help entry */
} help_indx;
