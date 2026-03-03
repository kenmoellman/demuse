/* convert_db.c - External database conversion tool for deMUSE
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * General-purpose tool for converting legacy flat-file data to MariaDB.
 * Supports converting +mail, +board messages, and channel data.
 *
 * Usage: convert_db [--mail | --board | --channels | --all] <database_file>
 *
 * The tool:
 *   1. Reads MariaDB credentials from run/db/mariadb.conf (or db/mariadb.conf)
 *   2. Connects to MariaDB and ensures tables exist
 *   3. For --mail/--board/--all: skips to after ***END OF DUMP*** and
 *      parses +from:to:date:flags:message lines
 *   4. For --channels: parses flat-file database objects to extract
 *      TYPE_CHANNEL objects and player A_CHANNEL/A_BANNED attributes
 *   5. Reports conversion results
 *
 * Can be run standalone for manual re-import, or is called automatically
 * by the server when legacy messages are detected during database load.
 *
 * Channel conversion is also performed automatically by the server during
 * startup (channel_convert_legacy in mariadb_channel.c). This standalone
 * mode is useful for pre-conversion or recovery from backups.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Suppress conversion warnings from MariaDB headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <mysql.h>
#pragma GCC diagnostic pop

/* Message flags - must match messaging.c values */
#define MF_DELETED  0x01
#define MF_READ     0x02
#define MF_NEW      0x04
#define MF_BOARD    0x08

/* Object type flags - must match db.h values */
#define TYPE_MASK       0xF
#define TYPE_THING      0x1
#define TYPE_CHANNEL    0x4
#define TYPE_PLAYER     0x8
#define GOING           0x4000

/* Attribute numbers - must match attrib.h DOATTR definitions */
#define A_PASS      5
#define A_DESC      6
#define A_OENTER    34
#define A_OLEAVE    48
#define A_CHANNEL   50
#define A_LOCK      129
#define A_LHIDE     158
#define A_SLOCK     165
#define A_USERS     170
#define A_BANNED    1016

/* DB format constants */
#define DB_MSGLEN   1040
#define DB_LOGICAL  0x15  /* Ctrl-U - newline escaping in attributes */
#define NOTHING     (-1)

/* Credential storage */
#define CRED_MAXLEN 256
static char db_host[CRED_MAXLEN];
static char db_user[CRED_MAXLEN];
static char db_pass[CRED_MAXLEN];
static char db_name[CRED_MAXLEN];
static unsigned int db_port = 3306;

/* Conversion statistics */
static long mail_converted = 0;
static long board_converted = 0;
static long channels_converted = 0;
static long members_converted = 0;
static long bans_converted = 0;
static long operators_converted = 0;
static long errors = 0;
static long skipped = 0;

/* Conversion mode flags */
#define CONV_MAIL     0x01
#define CONV_BOARD    0x02
#define CONV_CHANNELS 0x04
#define CONV_ALL      (CONV_MAIL | CONV_BOARD)

/* ============================================================================
 * CREDENTIAL PARSING
 * ============================================================================ */

static char *trim_whitespace(char *str)
{
    char *end;

    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';

    return str;
}

/*
 * parse_credentials - Read MariaDB credentials from config file
 *
 * Tries both "db/mariadb.conf" (from run/ directory) and
 * "run/db/mariadb.conf" (from project root).
 *
 * Returns: 1 on success, 0 on failure
 */
static int parse_credentials(void)
{
    FILE *fp;
    char line[512];
    char *key, *value, *eq;

    /* Set defaults */
    strncpy(db_host, "localhost", CRED_MAXLEN);
    db_host[CRED_MAXLEN - 1] = '\0';
    strncpy(db_user, "demuse", CRED_MAXLEN);
    db_user[CRED_MAXLEN - 1] = '\0';
    db_pass[0] = '\0';
    strncpy(db_name, "demuse", CRED_MAXLEN);
    db_name[CRED_MAXLEN - 1] = '\0';
    db_port = 3306;

    /* Try multiple paths */
    fp = fopen("db/mariadb.conf", "r");
    if (!fp) {
        fp = fopen("run/db/mariadb.conf", "r");
    }
    if (!fp) {
        fprintf(stderr, "Error: Cannot find mariadb.conf\n");
        fprintf(stderr, "Tried: db/mariadb.conf, run/db/mariadb.conf\n");
        return 0;
    }

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        key = trim_whitespace(line);
        if (*key == '#' || *key == '\0') continue;

        eq = strchr(key, '=');
        if (!eq) continue;

        *eq = '\0';
        value = trim_whitespace(eq + 1);
        key = trim_whitespace(key);

        if (strcmp(key, "host") == 0) {
            strncpy(db_host, value, CRED_MAXLEN);
            db_host[CRED_MAXLEN - 1] = '\0';
        } else if (strcmp(key, "port") == 0) {
            db_port = (unsigned int)strtoul(value, NULL, 10);
        } else if (strcmp(key, "user") == 0) {
            strncpy(db_user, value, CRED_MAXLEN);
            db_user[CRED_MAXLEN - 1] = '\0';
        } else if (strcmp(key, "password") == 0) {
            strncpy(db_pass, value, CRED_MAXLEN);
            db_pass[CRED_MAXLEN - 1] = '\0';
        } else if (strcmp(key, "database") == 0) {
            strncpy(db_name, value, CRED_MAXLEN);
            db_name[CRED_MAXLEN - 1] = '\0';
        }
    }

    fclose(fp);
    return 1;
}

/* ============================================================================
 * TABLE CREATION
 * ============================================================================ */

static int ensure_tables(MYSQL *conn)
{
    const char *mail_sql =
        "CREATE TABLE IF NOT EXISTS mail ("
        "  mail_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  sender       BIGINT       NOT NULL,"
        "  recipient    BIGINT       NOT NULL,"
        "  sent_date    BIGINT       NOT NULL,"
        "  flags        INT          NOT NULL DEFAULT 0,"
        "  message      TEXT         NOT NULL,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_recipient       (recipient),"
        "  INDEX idx_sender          (sender),"
        "  INDEX idx_recipient_flags (recipient, flags)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        "  COMMENT='deMUSE private player-to-player mail'";

    const char *board_sql =
        "CREATE TABLE IF NOT EXISTS board ("
        "  post_id      BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  author       BIGINT       NOT NULL,"
        "  board_room   BIGINT       NOT NULL DEFAULT 0,"
        "  posted_date  BIGINT       NOT NULL,"
        "  flags        INT          NOT NULL DEFAULT 0,"
        "  message      TEXT         NOT NULL,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_board_room (board_room),"
        "  INDEX idx_author     (author)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        "  COMMENT='deMUSE public board posts'";

    if (mysql_query(conn, mail_sql)) {
        fprintf(stderr, "Error creating mail table: %s\n",
                mysql_error(conn));
        return 0;
    }

    if (mysql_query(conn, board_sql)) {
        fprintf(stderr, "Error creating board table: %s\n",
                mysql_error(conn));
        return 0;
    }

    return 1;
}

static int ensure_channel_tables(MYSQL *conn)
{
    const char *channels_sql =
        "CREATE TABLE IF NOT EXISTS channels ("
        "  channel_id   BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  name         VARCHAR(128) NOT NULL,"
        "  cname        VARCHAR(256) NOT NULL,"
        "  owner        BIGINT       NOT NULL,"
        "  flags        BIGINT       NOT NULL DEFAULT 0,"
        "  is_system    TINYINT      NOT NULL DEFAULT 0,"
        "  min_level    TINYINT      NOT NULL DEFAULT 0,"
        "  password     VARCHAR(128) DEFAULT NULL,"
        "  topic        TEXT         DEFAULT NULL,"
        "  join_msg     TEXT         DEFAULT NULL,"
        "  leave_msg    TEXT         DEFAULT NULL,"
        "  speak_lock   TEXT         DEFAULT NULL,"
        "  join_lock    TEXT         DEFAULT NULL,"
        "  hide_lock    TEXT         DEFAULT NULL,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE INDEX idx_name (name),"
        "  INDEX idx_owner (owner)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    const char *members_sql =
        "CREATE TABLE IF NOT EXISTS channel_members ("
        "  member_id    BIGINT       AUTO_INCREMENT PRIMARY KEY,"
        "  channel_id   BIGINT       NOT NULL,"
        "  player       BIGINT       NOT NULL,"
        "  alias        VARCHAR(128) NOT NULL DEFAULT '',"
        "  color_name   VARCHAR(256) NOT NULL,"
        "  muted        TINYINT      NOT NULL DEFAULT 0,"
        "  is_default   TINYINT      NOT NULL DEFAULT 0,"
        "  is_operator  TINYINT      NOT NULL DEFAULT 0,"
        "  is_banned    TINYINT      NOT NULL DEFAULT 0,"
        "  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE INDEX idx_channel_player (channel_id, player),"
        "  INDEX idx_player (player),"
        "  INDEX idx_active (channel_id, is_banned, muted),"
        "  FOREIGN KEY (channel_id) REFERENCES channels(channel_id)"
        "    ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (mysql_query(conn, channels_sql)) {
        fprintf(stderr, "Error creating channels table: %s\n",
                mysql_error(conn));
        return 0;
    }

    if (mysql_query(conn, members_sql)) {
        fprintf(stderr, "Error creating channel_members table: %s\n",
                mysql_error(conn));
        return 0;
    }

    return 1;
}

/* ============================================================================
 * MESSAGE CONVERSION
 * ============================================================================ */

/*
 * insert_mail - Insert a private mail message into MariaDB
 */
static int insert_mail(MYSQL *conn, long from, long to, long date,
                       int flags, const char *message)
{
    char query[8192];
    char *escaped_msg;
    size_t msg_len;

    msg_len = strlen(message);
    escaped_msg = (char *)malloc(msg_len * 2 + 1);
    if (!escaped_msg) {
        fprintf(stderr, "Error: Out of memory\n");
        return 0;
    }

    mysql_real_escape_string(conn, escaped_msg, message,
                             (unsigned long)msg_len);

    snprintf(query, sizeof(query),
             "INSERT INTO mail (sender, recipient, sent_date, flags, message) "
             "VALUES (%ld, %ld, %ld, %d, '%s')",
             from, to, date, flags, escaped_msg);

    free(escaped_msg);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Error inserting mail: %s\n", mysql_error(conn));
        return 0;
    }

    return 1;
}

/*
 * insert_board - Insert a board post into MariaDB
 */
static int insert_board(MYSQL *conn, long author, long board_room, long date,
                        int flags, const char *message)
{
    char query[8192];
    char *escaped_msg;
    size_t msg_len;

    msg_len = strlen(message);
    escaped_msg = (char *)malloc(msg_len * 2 + 1);
    if (!escaped_msg) {
        fprintf(stderr, "Error: Out of memory\n");
        return 0;
    }

    mysql_real_escape_string(conn, escaped_msg, message,
                             (unsigned long)msg_len);

    snprintf(query, sizeof(query),
             "INSERT INTO board (author, board_room, posted_date, flags, message) "
             "VALUES (%ld, %ld, %ld, %d, '%s')",
             author, board_room, date, flags, escaped_msg);

    free(escaped_msg);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Error inserting board post: %s\n", mysql_error(conn));
        return 0;
    }

    return 1;
}

/*
 * convert_messages - Parse and convert legacy message lines from flat-file
 *
 * Reads from current file position, looking for lines starting with '+'.
 * Format: +from:to:date:flags:message
 *
 * Routes to mail or board table based on MF_BOARD flag (0x08).
 */
static int convert_messages(MYSQL *conn, FILE *fp, int mode)
{
    char buf[4096];

    while (fgets(buf, (int)sizeof(buf), fp) != NULL) {
        /* Strip trailing newline */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }

        if (buf[0] != '+') {
            skipped++;
            continue;
        }

        /* Parse: +from:to:date:flags:message */
        char *s = buf + 1;
        long from, to, date;
        int flags;

        from = strtol(s, NULL, 10);

        s = strchr(s, ':');
        if (!s) { errors++; continue; }
        s++;
        to = strtol(s, NULL, 10);

        s = strchr(s, ':');
        if (!s) { errors++; continue; }
        s++;
        date = strtol(s, NULL, 10);

        s = strchr(s, ':');
        if (!s) { errors++; continue; }
        s++;
        flags = (int)strtol(s, NULL, 10);

        s = strchr(s, ':');
        if (!s) { errors++; continue; }
        s++;  /* s now points to message text */

        /* Route based on MF_BOARD flag and conversion mode */
        if (flags & MF_BOARD) {
            if (mode & CONV_BOARD) {
                int clean_flags = flags & ~MF_BOARD;
                if (insert_board(conn, from, to, date, clean_flags, s)) {
                    board_converted++;
                } else {
                    errors++;
                }
            } else {
                skipped++;
            }
        } else {
            if (mode & CONV_MAIL) {
                if (insert_mail(conn, from, to, date, flags, s)) {
                    mail_converted++;
                } else {
                    errors++;
                }
            } else {
                skipped++;
            }
        }
    }

    return 1;
}

/* ============================================================================
 * FLAT-FILE DATABASE PARSING HELPERS
 * ============================================================================
 * Simplified versions of db_io.c functions for standalone use.
 * These parse the deMUSE flat-file database format (version 14).
 */

/*
 * db_read_ref - Read a number from the next line of the file
 * Equivalent to getref() in db_io.c
 */
static long db_read_ref(FILE *fp)
{
    char buf[DB_MSGLEN];

    if (!fgets(buf, DB_MSGLEN, fp)) {
        return NOTHING;
    }

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    return atol(buf);
}

/*
 * db_read_string - Read a string from the next line, handling DB_LOGICAL
 * escaping for multi-line attribute values.
 *
 * Stores result in the provided buffer.
 */
static void db_read_string(FILE *fp, char *dest, size_t dest_size)
{
    char buf[DB_MSGLEN];

    dest[0] = '\0';

    if (!fgets(buf, DB_MSGLEN, fp)) {
        return;
    }

    /* Handle DB_LOGICAL continuation lines */
    while (strlen(buf) > 1 && buf[strlen(buf) - 2] == DB_LOGICAL) {
        char cont[DB_MSGLEN];
        char *s = buf + strlen(buf) - 1;

        if (*s == '\n') {
            *s-- = '\0';
            *s = '\n';
        }

        if (fgets(cont, DB_MSGLEN, fp)) {
            size_t remaining = sizeof(buf) - strlen(buf);
            if (remaining > 1) {
                strncat(buf, cont, remaining - 1);
                buf[sizeof(buf) - 1] = '\0';
            }
        } else {
            break;
        }
    }

    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    strncpy(dest, buf, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

/*
 * db_skip_string - Read and discard a string line (with DB_LOGICAL handling)
 */
static void db_skip_string(FILE *fp)
{
    char buf[DB_MSGLEN];
    db_read_string(fp, buf, sizeof(buf));
}

/*
 * db_skip_list - Skip a getlist() format section (count + refs)
 */
static void db_skip_list(FILE *fp)
{
    long count = db_read_ref(fp);

    if (count > 0 && count < 100000) {
        long j;
        for (j = 0; j < count; j++) {
            db_read_ref(fp);
        }
    }
}

/*
 * db_skip_atrdefs - Skip an attribute definition section (ends with '\')
 */
static void db_skip_atrdefs(FILE *fp)
{
    int c;

    for (;;) {
        c = fgetc(fp);
        if (c == '\\') {
            fgetc(fp);  /* consume newline */
            return;
        } else if (c == '/') {
            db_read_ref(fp);     /* flags */
            db_read_ref(fp);     /* obj */
            db_skip_string(fp);  /* name */
        } else {
            return;  /* unexpected char */
        }
    }
}

/*
 * db_skip_univ_info - Skip universe info section (ends with '\')
 */
static void db_skip_univ_info(FILE *fp)
{
    int c;

    for (;;) {
        c = fgetc(fp);
        if (c == '\\') {
            fgetc(fp);  /* consume newline */
            return;
        } else if (c == '>') {
            db_read_ref(fp);  /* universe dbref */
        } else if (c == '/') {
            db_skip_string(fp);  /* attr index:value */
        } else {
            return;
        }
    }
}

/* ============================================================================
 * CHANNEL CONVERSION
 * ============================================================================ */

/* Channel prefix helpers */
static int chan_name_to_level(const char *name)
{
    if (!name || !*name) return 0;
    switch (*name) {
        case '*': return 3;
        case '.': return 2;
        case '_': return 1;
        default:  return 0;
    }
}

static const char *chan_strip_prefix(const char *name)
{
    if (!name || !*name) return name;
    if (*name == '*' || *name == '.' || *name == '_') return name + 1;
    return name;
}

/* Per-object storage for extracted attributes */
typedef struct {
    long objnum;
    char name[256];
    char cname[512];
    long owner;
    long flags;
    char attr_desc[DB_MSGLEN];        /* A_DESC - topic */
    char attr_pass[256];              /* A_PASS - password */
    char attr_oenter[DB_MSGLEN];      /* A_OENTER - join msg */
    char attr_oleave[DB_MSGLEN];      /* A_OLEAVE - leave msg */
    char attr_channel[DB_MSGLEN * 4]; /* A_CHANNEL - memberships */
    char attr_lock[DB_MSGLEN];        /* A_LOCK - join lock */
    char attr_lhide[DB_MSGLEN];       /* A_LHIDE - hide lock */
    char attr_slock[DB_MSGLEN];       /* A_SLOCK - speak lock */
    char attr_users[DB_MSGLEN];       /* A_USERS - operators */
    char attr_banned[DB_MSGLEN];      /* A_BANNED - ban list */
} parsed_object_t;

/*
 * db_read_attrs - Read attribute list, extracting specific attributes
 *
 * Reads the '>'...'<' attribute section from the file.
 * For db_version > 7: each attr is '>' atrnum atrobj value
 * For db_version <= 7: each attr is '>' atrnum value
 */
static void db_read_attrs(FILE *fp, int db_version, parsed_object_t *obj)
{
    int c;

    while (1) {
        c = fgetc(fp);
        if (c == '<') {
            fgetc(fp);  /* consume newline */
            return;
        }
        if (c != '>') {
            return;  /* unexpected character */
        }

        int atrnum = (int)db_read_ref(fp);
        if (db_version > 7) {
            db_read_ref(fp);  /* atrobj - skip */
        }

        /* Read the attribute value into the appropriate field */
        switch (atrnum) {
        case A_DESC:
            db_read_string(fp, obj->attr_desc, sizeof(obj->attr_desc));
            break;
        case A_PASS:
            db_read_string(fp, obj->attr_pass, sizeof(obj->attr_pass));
            break;
        case A_OENTER:
            db_read_string(fp, obj->attr_oenter, sizeof(obj->attr_oenter));
            break;
        case A_OLEAVE:
            db_read_string(fp, obj->attr_oleave, sizeof(obj->attr_oleave));
            break;
        case A_CHANNEL:
            db_read_string(fp, obj->attr_channel, sizeof(obj->attr_channel));
            break;
        case A_LOCK:
            db_read_string(fp, obj->attr_lock, sizeof(obj->attr_lock));
            break;
        case A_LHIDE:
            db_read_string(fp, obj->attr_lhide, sizeof(obj->attr_lhide));
            break;
        case A_SLOCK:
            db_read_string(fp, obj->attr_slock, sizeof(obj->attr_slock));
            break;
        case A_USERS:
            db_read_string(fp, obj->attr_users, sizeof(obj->attr_users));
            break;
        case A_BANNED:
            db_read_string(fp, obj->attr_banned, sizeof(obj->attr_banned));
            break;
        default:
            db_skip_string(fp);  /* skip unneeded attributes */
            break;
        }
    }
}

/*
 * chan_insert_channel - Insert a channel from a parsed TYPE_CHANNEL object
 *
 * Returns the new channel_id or -1 on failure
 */
static long chan_insert_channel(MYSQL *conn, parsed_object_t *obj)
{
    const char *plain_name = chan_strip_prefix(obj->name);
    int min_level = chan_name_to_level(obj->name);
    const char *cname = *obj->cname ? obj->cname : plain_name;
    long flags_clean = obj->flags & ~TYPE_MASK;  /* strip type bits */

    char esc_name[257], esc_cname[513];
    char esc_topic[DB_MSGLEN * 2 + 1], esc_slock[DB_MSGLEN * 2 + 1];
    char esc_jlock[DB_MSGLEN * 2 + 1], esc_hlock[DB_MSGLEN * 2 + 1];
    char esc_jmsg[DB_MSGLEN * 2 + 1], esc_lmsg[DB_MSGLEN * 2 + 1];
    char esc_pass[513];
    char query[8192];

    mysql_real_escape_string(conn, esc_name, plain_name,
                              (unsigned long)strlen(plain_name));
    mysql_real_escape_string(conn, esc_cname, cname,
                              (unsigned long)strlen(cname));
    mysql_real_escape_string(conn, esc_topic, obj->attr_desc,
                              (unsigned long)strlen(obj->attr_desc));
    mysql_real_escape_string(conn, esc_slock, obj->attr_slock,
                              (unsigned long)strlen(obj->attr_slock));
    mysql_real_escape_string(conn, esc_jlock, obj->attr_lock,
                              (unsigned long)strlen(obj->attr_lock));
    mysql_real_escape_string(conn, esc_hlock, obj->attr_lhide,
                              (unsigned long)strlen(obj->attr_lhide));
    mysql_real_escape_string(conn, esc_jmsg, obj->attr_oenter,
                              (unsigned long)strlen(obj->attr_oenter));
    mysql_real_escape_string(conn, esc_lmsg, obj->attr_oleave,
                              (unsigned long)strlen(obj->attr_oleave));
    mysql_real_escape_string(conn, esc_pass, obj->attr_pass,
                              (unsigned long)strlen(obj->attr_pass));

    snprintf(query, sizeof(query),
             "INSERT INTO channels (name, cname, owner, flags, "
             "is_system, min_level, password, topic, join_msg, "
             "leave_msg, speak_lock, join_lock, hide_lock) "
             "VALUES ('%s', '%s', %ld, %ld, 0, %d, %s, %s, %s, %s, %s, %s, %s) "
             "ON DUPLICATE KEY UPDATE channel_id = channel_id",
             esc_name, esc_cname, obj->owner, flags_clean, min_level,
             *esc_pass ? esc_pass : "NULL",
             *esc_topic ? esc_topic : "NULL",
             *esc_jmsg ? esc_jmsg : "NULL",
             *esc_lmsg ? esc_lmsg : "NULL",
             *esc_slock ? esc_slock : "NULL",
             *esc_jlock ? esc_jlock : "NULL",
             *esc_hlock ? esc_hlock : "NULL");

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Error inserting channel '%s': %s\n",
                plain_name, mysql_error(conn));
        return -1;
    }

    if (mysql_affected_rows(conn) == 0) {
        /* Already existed (ON DUPLICATE KEY did nothing) - look up ID */
        char lookup[512];
        snprintf(lookup, sizeof(lookup),
                 "SELECT channel_id FROM channels WHERE name = '%s'",
                 esc_name);
        if (mysql_query(conn, lookup)) {
            return -1;
        }

        MYSQL_RES *res = mysql_store_result(conn);
        if (!res) return -1;

        MYSQL_ROW row = mysql_fetch_row(res);
        long id = row ? atol(row[0]) : -1;
        mysql_free_result(res);
        return id;
    }

    return (long)mysql_insert_id(conn);
}

/*
 * chan_lookup_id - Look up a channel ID by name
 */
static long chan_lookup_id(MYSQL *conn, const char *name)
{
    char esc_name[257];
    char query[512];
    MYSQL_RES *res;
    MYSQL_ROW row;
    long id;

    mysql_real_escape_string(conn, esc_name, name,
                              (unsigned long)strlen(name));

    snprintf(query, sizeof(query),
             "SELECT channel_id FROM channels WHERE name = '%s'", esc_name);

    if (mysql_query(conn, query)) {
        return -1;
    }

    res = mysql_store_result(conn);
    if (!res) return -1;

    row = mysql_fetch_row(res);
    id = row ? atol(row[0]) : -1;
    mysql_free_result(res);

    return id;
}

/*
 * chan_lookup_cname - Look up a channel's cname by ID
 */
static int chan_lookup_cname(MYSQL *conn, long channel_id,
                              char *out, size_t out_size)
{
    char query[256];
    MYSQL_RES *res;
    MYSQL_ROW row;

    snprintf(query, sizeof(query),
             "SELECT cname FROM channels WHERE channel_id = %ld", channel_id);

    if (mysql_query(conn, query)) {
        out[0] = '\0';
        return 0;
    }

    res = mysql_store_result(conn);
    if (!res) {
        out[0] = '\0';
        return 0;
    }

    row = mysql_fetch_row(res);
    if (row && row[0]) {
        strncpy(out, row[0], out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        out[0] = '\0';
    }

    mysql_free_result(res);
    return 1;
}

/*
 * chan_convert_memberships - Parse A_CHANNEL attr and insert memberships
 *
 * Format: "channelname:alias:onoff channelname2:alias2:onoff2 ..."
 */
static void chan_convert_memberships(MYSQL *conn, long player,
                                      const char *attr_str)
{
    char buf[DB_MSGLEN * 4];
    char *tok, *saveptr;

    if (!attr_str || !*attr_str) return;

    strncpy(buf, attr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        char *colon1, *colon2;
        const char *alias = "";
        int onoff = 1;

        /* Parse "name:alias:onoff" or bare "name" */
        colon1 = strchr(tok, ':');
        if (colon1) {
            *colon1 = '\0';
            alias = colon1 + 1;
            colon2 = strchr(alias, ':');
            if (colon2) {
                *colon2 = '\0';
                onoff = atoi(colon2 + 1);
            }
        }

        const char *plain = chan_strip_prefix(tok);
        long channel_id = chan_lookup_id(conn, plain);

        if (channel_id > 0) {
            char esc_alias[257];
            char cname[512];
            char esc_cname[1025];
            char query[2048];

            mysql_real_escape_string(conn, esc_alias, alias,
                                      (unsigned long)strlen(alias));

            chan_lookup_cname(conn, channel_id, cname, sizeof(cname));
            mysql_real_escape_string(conn, esc_cname, cname,
                                      (unsigned long)strlen(cname));

            snprintf(query, sizeof(query),
                     "INSERT INTO channel_members "
                     "(channel_id, player, alias, color_name, muted) "
                     "VALUES (%ld, %ld, '%s', '%s', %d) "
                     "ON DUPLICATE KEY UPDATE "
                     "alias = VALUES(alias), muted = VALUES(muted)",
                     channel_id, player, esc_alias, esc_cname,
                     onoff ? 0 : 1);

            if (!mysql_query(conn, query)) {
                members_converted++;
            } else {
                errors++;
            }
        }

        tok = strtok_r(NULL, " ", &saveptr);
    }
}

/*
 * chan_convert_bans - Parse A_BANNED attr and mark members as banned
 *
 * Format: "channelname1 channelname2 ..."
 */
static void chan_convert_bans(MYSQL *conn, long player, const char *attr_str)
{
    char buf[DB_MSGLEN];
    char *tok, *saveptr;

    if (!attr_str || !*attr_str) return;

    strncpy(buf, attr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        const char *plain = chan_strip_prefix(tok);
        long channel_id = chan_lookup_id(conn, plain);

        if (channel_id > 0) {
            char cname[512];
            char esc_cname[1025];
            char query[1024];

            chan_lookup_cname(conn, channel_id, cname, sizeof(cname));
            mysql_real_escape_string(conn, esc_cname, cname,
                                      (unsigned long)strlen(cname));

            snprintf(query, sizeof(query),
                     "INSERT INTO channel_members "
                     "(channel_id, player, alias, color_name, is_banned) "
                     "VALUES (%ld, %ld, '', '%s', 1) "
                     "ON DUPLICATE KEY UPDATE is_banned = 1",
                     channel_id, player, esc_cname);

            if (!mysql_query(conn, query)) {
                bans_converted++;
            }
        }

        tok = strtok_r(NULL, " ", &saveptr);
    }
}

/*
 * chan_convert_operators - Parse A_USERS attr and mark operators
 *
 * Format: "#dbref1 #dbref2 ..."
 */
static void chan_convert_operators(MYSQL *conn, long channel_id,
                                    const char *attr_str)
{
    char buf[DB_MSGLEN];
    char *tok, *saveptr;

    if (!attr_str || !*attr_str) return;

    strncpy(buf, attr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        long op_player;
        char query[256];

        if (*tok == '#') tok++;
        op_player = atol(tok);

        if (op_player >= 0) {
            snprintf(query, sizeof(query),
                     "UPDATE channel_members SET is_operator = 1 "
                     "WHERE channel_id = %ld AND player = %ld",
                     channel_id, op_player);

            if (!mysql_query(conn, query)) {
                if (mysql_affected_rows(conn) > 0) {
                    operators_converted++;
                }
            }
        }

        tok = strtok_r(NULL, " ", &saveptr);
    }
}

/*
 * convert_channels - Parse flat-file database and convert channel data
 *
 * Reads the entire flat-file database format (version 14), extracting
 * TYPE_CHANNEL objects and player A_CHANNEL/A_BANNED attributes, and
 * inserts them into the MariaDB channels/channel_members tables.
 *
 * Two-pass approach:
 *   Pass 1: Read entire database, store parsed objects in arrays
 *   Pass 2: Insert channels first, then memberships/bans/operators
 */
static int convert_channels(MYSQL *conn, FILE *fp)
{
    int db_version = 1;
    int c;
    parsed_object_t obj;
    long obj_count = 0;

    /* Arrays to store parsed data (sized dynamically) */
    long chan_capacity = 64;
    long chan_count = 0;
    parsed_object_t *chan_objects = (parsed_object_t *)malloc(
        (size_t)chan_capacity * sizeof(parsed_object_t));

    long player_capacity = 256;
    long player_count = 0;
    parsed_object_t *player_objects = (parsed_object_t *)malloc(
        (size_t)player_capacity * sizeof(parsed_object_t));

    if (!chan_objects || !player_objects) {
        fprintf(stderr, "Error: Out of memory\n");
        free(chan_objects);
        free(player_objects);
        return 0;
    }

    printf("Reading flat-file database...\n");

    /* Parse the database */
    while ((c = fgetc(fp)) != EOF) {
        switch (c) {
        case '@':
            db_version = (int)db_read_ref(fp);
            printf("  Database format version: %d\n", db_version);
            break;

        case '~':
            db_read_ref(fp);  /* size hint, ignored */
            break;

        case '#':
            /* Old TinyMUSH format - skip */
            fprintf(stderr, "Warning: Old '#' format objects not supported "
                            "for channel conversion.\n");
            free(chan_objects);
            free(player_objects);
            return 0;

        case '!':   /* Non-zone database */
        case '&': { /* Zone-oriented database */
            int has_zone = (c == '&' && db_version >= 3);

            memset(&obj, 0, sizeof(obj));
            obj.objnum = db_read_ref(fp);

            /* Read name */
            db_read_string(fp, obj.name, sizeof(obj.name));

            /* cname (version >= 14) */
            if (db_version >= 14) {
                db_read_string(fp, obj.cname, sizeof(obj.cname));
            }

            /* location */
            db_read_ref(fp);

            /* zone */
            if (has_zone) {
                db_read_ref(fp);
            }

            /* contents, exits */
            db_read_ref(fp);
            db_read_ref(fp);

            /* fighting (version >= 12) */
            if (db_version >= 12) {
                db_read_ref(fp);
            }

            /* link (version >= 5) */
            if (db_version >= 5) {
                db_read_ref(fp);
            }

            /* next */
            db_read_ref(fp);

            /* boolexp (version <= 8) */
            if (db_version <= 8) {
                db_skip_string(fp);  /* simplified - may break on complex locks */
            }

            /* owner */
            obj.owner = db_read_ref(fp);

            /* pennies (version <= 3) */
            if (db_version <= 3) {
                db_read_ref(fp);
            }

            /* flags */
            obj.flags = db_read_ref(fp);

            /* mod_time, create_time (version >= 10) */
            if (db_version >= 10) {
                db_read_ref(fp);
                db_read_ref(fp);
            }

            /* powers (version >= 6, TYPE_PLAYER only) */
            if (db_version >= 6) {
                int obj_type = (int)(obj.flags & TYPE_MASK);
                if (obj_type == TYPE_PLAYER) {
                    db_skip_string(fp);
                } else if (db_version == 6) {
                    db_skip_string(fp);
                }
            }

            /* Read attribute list - extract specific attributes */
            db_read_attrs(fp, db_version, &obj);

            /* Skip parents, children, atrdefs (version > 7) */
            if (db_version > 7) {
                db_skip_list(fp);     /* parents */
                db_skip_list(fp);     /* children */
                db_skip_atrdefs(fp);  /* atrdefs */
            }

            /* Skip universe info (version > 12) */
            if (db_version > 12) {
                db_skip_univ_info(fp);
            }

            /* Store the object based on type */
            {
                int obj_type = (int)(obj.flags & TYPE_MASK);

                if (obj_type == TYPE_CHANNEL) {
                    if (chan_count >= chan_capacity) {
                        chan_capacity *= 2;
                        chan_objects = (parsed_object_t *)realloc(
                            chan_objects,
                            (size_t)chan_capacity * sizeof(parsed_object_t));
                        if (!chan_objects) {
                            fprintf(stderr, "Error: Out of memory\n");
                            free(player_objects);
                            return 0;
                        }
                    }
                    chan_objects[chan_count++] = obj;
                } else if (obj_type == TYPE_PLAYER) {
                    if (*obj.attr_channel || *obj.attr_banned) {
                        if (player_count >= player_capacity) {
                            player_capacity *= 2;
                            player_objects = (parsed_object_t *)realloc(
                                player_objects,
                                (size_t)player_capacity * sizeof(parsed_object_t));
                            if (!player_objects) {
                                fprintf(stderr, "Error: Out of memory\n");
                                free(chan_objects);
                                return 0;
                            }
                        }
                        player_objects[player_count++] = obj;
                    }
                }
            }

            obj_count++;
            if (obj_count % 1000 == 0) {
                printf("  Read %ld objects...\r", obj_count);
                fflush(stdout);
            }
            break;
        }

        case '*': {
            /* End of dump marker */
            char end_buf[64];
            if (fgets(end_buf, (int)sizeof(end_buf), fp)) {
                /* Check for ***END OF DUMP*** */
            }
            goto done_reading;
        }

        case '\n':
            break;  /* Skip empty lines */

        default:
            /* Unknown character - try to continue */
            break;
        }
    }

done_reading:
    printf("  Read %ld objects total (%ld channels, %ld players with "
           "memberships)\n", obj_count, chan_count, player_count);

    if (chan_count == 0) {
        printf("No TYPE_CHANNEL objects found in database.\n");
        free(chan_objects);
        free(player_objects);
        return 1;
    }

    /* Pass 1: Insert channels */
    printf("Converting %ld channels...\n", chan_count);
    {
        long j;
        for (j = 0; j < chan_count; j++) {
            long channel_id = chan_insert_channel(conn, &chan_objects[j]);
            if (channel_id > 0) {
                channels_converted++;

                /* Convert operators from A_USERS */
                if (*chan_objects[j].attr_users) {
                    chan_convert_operators(conn, channel_id,
                                           chan_objects[j].attr_users);
                }

                printf("  Channel: %s -> channel_id %ld\n",
                       chan_objects[j].name, channel_id);
            } else {
                errors++;
                fprintf(stderr, "  Failed: %s\n", chan_objects[j].name);
            }
        }
    }

    /* Pass 2: Insert memberships and bans */
    printf("Converting memberships for %ld players...\n", player_count);
    {
        long j;
        for (j = 0; j < player_count; j++) {
            if (*player_objects[j].attr_channel) {
                chan_convert_memberships(conn, player_objects[j].objnum,
                                          player_objects[j].attr_channel);
            }
            if (*player_objects[j].attr_banned) {
                chan_convert_bans(conn, player_objects[j].objnum,
                                   player_objects[j].attr_banned);
            }
        }
    }

    free(chan_objects);
    free(player_objects);
    return 1;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

static void usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [--mail | --board | --channels | --all] <database_file>\n",
            progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Convert legacy flat-file data to MariaDB.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --mail      Convert only private mail messages\n");
    fprintf(stderr, "  --board     Convert only board posts\n");
    fprintf(stderr, "  --channels  Convert channel objects and memberships\n");
    fprintf(stderr, "  --all       Convert mail and board (default)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Reads MariaDB credentials from db/mariadb.conf or "
                    "run/db/mariadb.conf\n");
}

int main(int argc, char *argv[])
{
    MYSQL *conn;
    FILE *fp;
    char buf[4096];
    const char *db_file = NULL;
    int mode = CONV_ALL;
    int found_end = 0;
    int i;

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mail") == 0) {
            mode = CONV_MAIL;
        } else if (strcmp(argv[i], "--board") == 0) {
            mode = CONV_BOARD;
        } else if (strcmp(argv[i], "--channels") == 0) {
            mode = CONV_CHANNELS;
        } else if (strcmp(argv[i], "--all") == 0) {
            mode = CONV_ALL;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else {
            db_file = argv[i];
        }
    }

    if (!db_file) {
        fprintf(stderr, "Error: No database file specified.\n\n");
        usage(argv[0]);
        return 1;
    }

    /* Parse MariaDB credentials */
    if (!parse_credentials()) {
        return 1;
    }

    /* Connect to MariaDB */
    conn = mysql_init(NULL);
    if (!conn) {
        fprintf(stderr, "Error: mysql_init() failed\n");
        return 1;
    }

    unsigned int timeout = 10;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (!mysql_real_connect(conn, db_host, db_user, db_pass,
                            db_name, db_port, NULL, 0)) {
        fprintf(stderr, "Error: MariaDB connection failed: %s\n",
                mysql_error(conn));
        mysql_close(conn);
        return 1;
    }

    mysql_set_character_set(conn, "utf8mb4");
    printf("Connected to MariaDB %s@%s:%u/%s\n",
           db_user, db_host, db_port, db_name);

    /* Ensure tables exist */
    if (mode & (CONV_MAIL | CONV_BOARD)) {
        if (!ensure_tables(conn)) {
            mysql_close(conn);
            return 1;
        }
    }
    if (mode & CONV_CHANNELS) {
        if (!ensure_channel_tables(conn)) {
            mysql_close(conn);
            return 1;
        }
    }

    /* Open database file */
    fp = fopen(db_file, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open database file: %s\n", db_file);
        mysql_close(conn);
        return 1;
    }

    if (mode & CONV_CHANNELS) {
        /* Channel conversion reads the database objects (before END OF DUMP) */
        printf("Converting channels...\n");
        convert_channels(conn, fp);

        printf("\nChannel conversion complete:\n");
        printf("  Channels converted:       %ld\n", channels_converted);
        printf("  Memberships converted:    %ld\n", members_converted);
        printf("  Bans converted:           %ld\n", bans_converted);
        printf("  Operators converted:      %ld\n", operators_converted);
        printf("  Errors:                   %ld\n", errors);
    }

    if (mode & (CONV_MAIL | CONV_BOARD)) {
        /* Message conversion reads after END OF DUMP */
        rewind(fp);

        while (fgets(buf, (int)sizeof(buf), fp) != NULL) {
            if (strncmp(buf, "***END OF DUMP***", 17) == 0) {
                found_end = 1;
                break;
            }
        }

        if (!found_end) {
            fprintf(stderr, "Warning: No ***END OF DUMP*** marker found.\n");
            fprintf(stderr,
                    "Rewinding to start of file and scanning for messages.\n");
            rewind(fp);
        }

        /* Convert messages */
        printf("Converting messages");
        if (mode == CONV_MAIL) printf(" (mail only)");
        else if (mode == CONV_BOARD) printf(" (board only)");
        printf("...\n");

        convert_messages(conn, fp, mode);

        printf("\nMessage conversion complete:\n");
        printf("  Mail messages converted:  %ld\n", mail_converted);
        printf("  Board posts converted:    %ld\n", board_converted);
        printf("  Errors:                   %ld\n", errors);
        printf("  Skipped:                  %ld\n", skipped);
        printf("  Total processed:          %ld\n",
               mail_converted + board_converted + errors + skipped);
    }

    fclose(fp);
    mysql_close(conn);

    return (errors > 0) ? 1 : 0;
}
