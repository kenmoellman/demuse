/* convert_db.c - External database conversion tool for deMUSE
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * General-purpose tool for converting legacy flat-file data to MariaDB.
 * Currently supports converting +mail and +board messages.
 *
 * Usage: convert_db [--mail | --board | --all] <database_file>
 *
 * The tool:
 *   1. Reads MariaDB credentials from run/db/mariadb.conf (or db/mariadb.conf)
 *   2. Connects to MariaDB and ensures tables exist
 *   3. Reads the flat-file database, skips to after ***END OF DUMP***
 *   4. Parses +from:to:date:flags:message lines
 *   5. Routes to mail or board table based on MF_BOARD flag (0x08)
 *   6. Reports conversion results
 *
 * Can be run standalone for manual re-import, or is called automatically
 * by the server when legacy messages are detected during database load.
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
static long errors = 0;
static long skipped = 0;

/* Conversion mode flags */
#define CONV_MAIL  0x01
#define CONV_BOARD 0x02
#define CONV_ALL   (CONV_MAIL | CONV_BOARD)

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
 * MAIN
 * ============================================================================ */

static void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [--mail | --board | --all] <database_file>\n",
            progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Convert legacy flat-file messages to MariaDB.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --mail   Convert only private mail messages\n");
    fprintf(stderr, "  --board  Convert only board posts\n");
    fprintf(stderr, "  --all    Convert both mail and board (default)\n");
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
    if (!ensure_tables(conn)) {
        mysql_close(conn);
        return 1;
    }

    /* Open database file */
    fp = fopen(db_file, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open database file: %s\n", db_file);
        mysql_close(conn);
        return 1;
    }

    /* Skip to after ***END OF DUMP*** */
    while (fgets(buf, (int)sizeof(buf), fp) != NULL) {
        if (strncmp(buf, "***END OF DUMP***", 17) == 0) {
            found_end = 1;
            break;
        }
    }

    if (!found_end) {
        fprintf(stderr, "Warning: No ***END OF DUMP*** marker found.\n");
        fprintf(stderr, "Rewinding to start of file and scanning for messages.\n");
        rewind(fp);
    }

    /* Convert messages */
    printf("Converting messages");
    if (mode == CONV_MAIL) printf(" (mail only)");
    else if (mode == CONV_BOARD) printf(" (board only)");
    printf("...\n");

    convert_messages(conn, fp, mode);

    /* Report results */
    printf("\n");
    printf("Conversion complete:\n");
    printf("  Mail messages converted:  %ld\n", mail_converted);
    printf("  Board posts converted:    %ld\n", board_converted);
    printf("  Errors:                   %ld\n", errors);
    printf("  Skipped:                  %ld\n", skipped);
    printf("  Total processed:          %ld\n",
           mail_converted + board_converted + errors + skipped);

    fclose(fp);
    mysql_close(conn);

    return (errors > 0) ? 1 : 0;
}
