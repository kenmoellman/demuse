


/* static long mdb_top; */
extern long mdb_top;

/* static struct mdb_entry *mdb; */
extern struct mdb_entry *mdb;


#define NOMAIL ((mdbref)-1)


/* from mail.c */
#ifdef __GNUC__
  __inline__
#endif
extern  mdbref get_mailk(dbref);

#ifdef __GNUC__
  __inline__
#endif
extern void set_mailk(dbref, mdbref);

#ifdef __GNUC__
__inline__
#endif
long mail_size(dbref);

extern void make_free_mail_slot(mdbref);
extern mdbref grab_free_mail_slot();

