/* server_main.c - Main server loop and initialization
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include "sock.h"
#include "net.h"

#include <stddef.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <netdb.h>
#include <ctype.h>

/* External error number */
extern int errno;

/* Null device for file descriptor reservation */
const char *NullFile = "logs/null";

/* Flag for need more processing */
int need_more_proc = 0;

/* File I/O mode for line buffering */
#define mklinebuf(fp) setvbuf(fp, NULL, _IOLBF, 0)
#define DEF_MODE 0644

/* Forward declarations for static functions */
static void init_args(int argc, char **argv);
static void init_io(void);
static void shovechars(int port);

struct descriptor_data *descriptor_list = 0;


/* Main entry point */
int main(int argc, char *argv[])
{
    /* Initialize the safe memory system FIRST */
    safe_memory_init();
#ifdef MEMORY_DEBUG_LOG
    /* Set up logging if compiled with -DMEMORY_DEBUG_LOG */
    safe_memory_set_log_file(MEMORY_DEBUG_FILE);
    safe_memory_set_content_log_size(MEMORY_DEBUG_SIZE);
#endif
    atexit(safe_memory_cleanup);

    /* Initialize global state */
    init_io_globals();
    init_args(argc, argv);
    init_io();


    
    printf("--------------------------------\n");
    printf("MUSE online (pid=%d)\n", getpid());

    /* Initialize subsystems */
    init_attributes();
    init_mail();

    /* Set startup time (may be overridden by open_sockets for reboots) */
    time(&muse_up_time);
    time(&muse_reboot_time);

    /* Restore socket state if rebooting */
    open_sockets();

    /* Load database */
    if (init_game(def_db_in, def_db_out) < 0) {
        log_error(tprintf("Couldn't load %s!", def_db_in));
        exit_nicely(136);
    }

    /* Setup signal handlers */
    set_signals();

    /* Enter main server loop */
    shovechars(inet_port);

    /* Shutdown sequence */
    log_important("Shutting down normally.");
    close_sockets();
    do_haltall(1);
    dump_database();
    free_database();
    free_mail();
    free_hash();

    /* Handle socket preservation for reboot */
    if (exit_status == 1) {
        fcntl(sock, F_SETFD, 1);
    } else {
        close(sock);
    }

    /* Log reason for shutdown */
    if (sig_caught > 0) {
        log_important(tprintf("Shutting down due to signal %d", sig_caught));
    }

    /* Execute reboot if requested */
    if (exit_status == 1) {
        close_logs();
        remove_temp_dbs();
#ifdef MALLOCDEBUG
        mnem_writestats();
#endif
        if (fork() == 0) {
            exit(0);  /* Child exits cleanly */
        }

        alarm(0);  /* Cancel any pending alarms */
        wait(0);   /* Wait for child to exit */

        /* Try to exec the new server */
        execv(argv[0], argv);
        execv("../bin/netmuse", argv);
        execvp("netmuse", argv);
        
        /* If exec fails, cleanup and exit */
        unlink("logs/socket_table");
        _exit(exit_status);
    }



    /* Normal shutdown */
    shutdown_stack();
    exit_nicely(exit_status);
    exit(exit_status);
}

/* Process command line arguments */
static void init_args(int argc, char *argv[])
{
    /* Change default input database? */
    if (argc > 1) {
        --argc;
        def_db_in = *++argv;
    }

    /* Change default dump database? */
    if (argc > 1) {
        --argc;
        def_db_out = *++argv;
    }

    /* Change default log file? */
    if (argc > 1) {
        --argc;
        stdout_logfile = *++argv;
    }

    /* Change port number? */
    if (argc > 1) {
        --argc;
        inet_port = atoi(*++argv);
    }
}

/* Initialize I/O redirection */
static void init_io(void)
{
    int fd;

    /* Close standard input */
    fclose(stdin);

    /* Open log file */
    fd = open(stdout_logfile, O_WRONLY | O_CREAT | O_APPEND, DEF_MODE);
    if (fd < 0) {
        perror("open()");
        log_error(tprintf("Error opening %s for writing.", stdout_logfile));
        exit_nicely(136);
    }

    /* Redirect stdout to log file */
    close(fileno(stdout));
    if (dup2(fd, fileno(stdout)) == -1) {
        perror("dup2()");
        log_error("Error converting standard output to logfile.");
    }
    mklinebuf(stdout);

    /* Redirect stderr to log file */
    close(fileno(stderr));
    if (dup2(fd, fileno(stderr)) == -1) {
        perror("dup2()");
        printf("Error converting standard error to logfile.\n");
    }
    mklinebuf(stderr);

    /* Close the original fd */
    close(fd);

    /* Reserve a file descriptor for later use */
    reserved = open(NullFile, O_RDWR, 0);
}

/* Subtract two timevals */
struct timeval timeval_sub(struct timeval now, struct timeval then)
{
    now.tv_sec -= then.tv_sec;
    now.tv_usec -= then.tv_usec;
    
    while (now.tv_usec < 0) {
        now.tv_usec += 1000000;
        now.tv_sec--;
    }
    
    if (now.tv_sec < 0) {
        now.tv_sec = 0;  /* Clamp to zero */
    }
    
    return now;
}

/* Get millisecond difference between two timevals */
int msec_diff(struct timeval now, struct timeval then)
{
    return ((now.tv_sec - then.tv_sec) * 1000 +
            (now.tv_usec - then.tv_usec) / 1000);
}

/* Add milliseconds to a timeval */
struct timeval msec_add(struct timeval t, int x)
{
    t.tv_sec += x / 1000;
    t.tv_usec += (x % 1000) * 1000;
    
    if (t.tv_usec >= 1000000) {
        t.tv_sec += t.tv_usec / 1000000;
        t.tv_usec = t.tv_usec % 1000000;
    }
    
    return t;
}

/* Update command quotas for all descriptors */
struct timeval update_quotas(struct timeval last, struct timeval current)
{
    int nslices;
    struct descriptor_data *d;

    nslices = msec_diff(current, last) / command_time_msec;

    if (nslices > 0) {
        for (d = descriptor_list; d; d = d->next) {
            d->quota += commands_per_time * nslices;
            if (d->quota > command_burst_size) {
                d->quota = command_burst_size;
            }
        }
    }
    
    return msec_add(last, nslices * command_time_msec);
}

/* Main server loop
 * 
 * This function implements a two-phase server loop:
 * 
 * Phase 1 (!shutdown_flag && !loading_db):
 *   Handles connections while the database is being loaded.
 *   This keeps existing connections alive during database reload.
 * 
 * Phase 2 (shutdown_flag == 0):
 *   Normal game operation after database is fully loaded.
 *   Processes commands, handles new connections, manages idle players.
 */
static void shovechars(int port)
{
    fd_set input_set, output_set;
    struct timeval last_slice, current_time;
    struct timeval next_slice;
    struct timeval timeout, slice_timeout;
    struct timezone tz;
    int found;
    struct descriptor_data *d, *dnext;
    struct descriptor_data *newd;
    int avail_descriptors;

    time(&now);
    log_io(tprintf("Starting up on port %d", port));

    sock = make_socket(port);
    if (maxd <= sock) {
        maxd = sock + 1;
    }

    gettimeofday(&last_slice, &tz);
    avail_descriptors = getdtablesize() - 5;

    /* ================================================================
     * PHASE 1: Database Loading Loop
     * 
     * Purpose: Keep connections alive while database loads
     * Duration: Until !loading_db becomes true
     * ================================================================ */
    while (!shutdown_flag && !loading_db) {
        time(&now);
        load_more_db();

        FD_ZERO(&input_set);
        FD_ZERO(&output_set);
        
        if (ndescriptors < avail_descriptors && sock >= 0) {
            FD_SET(sock, &input_set);
        }

#ifdef USE_CID_PLAY
        /* Process remote descriptors */
        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;
            if (d->cstatus & C_REMOTE && d->output.head) {
                if (!process_output(d)) {
                    shutdownsock(d);
                }
                need_more_proc = 1;
            }
        }
#endif

        /* Setup descriptor sets */
        for (d = descriptor_list; d; d = d->next) {
            if (!(d->cstatus & C_REMOTE)) {
                if (d->input.head) {
                    timeout = slice_timeout;
                } else {
                    FD_SET(d->descriptor, &input_set);
                }
                if (d->output.head && 
                    (d->state != CONNECTED || d->player > 0)) {
                    FD_SET(d->descriptor, &output_set);
                }
            }
        }

#ifdef USE_CID_PLAY
        /* Process remote output again */
        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;
            if (d->cstatus & C_REMOTE) {
                process_output(d);
            }
        }
#endif

        /* Process pending output */
        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;
            if (!(d->cstatus & C_REMOTE) &&
                FD_ISSET(d->descriptor, &output_set)) {
                if (!process_output(d)) {
                    shutdownsock(d);
                }
            }
        }
    }

    /* ================================================================
     * PHASE 2: Main Game Loop
     * 
     * Purpose: Normal server operation after database is loaded
     * Duration: Until shutdown_flag is set
     * ================================================================ */
    while (shutdown_flag == 0) {
        gettimeofday(&current_time, (struct timezone *)0);
        time(&now);
        last_slice = update_quotas(last_slice, current_time);

        sprintf(welcome_msg_file, "msgs/welcome%03d.txt",
                rand() % NUM_WELCOME_MESSAGES);

        clear_stack();
        process_commands();
        check_for_idlers();
        
#ifdef USE_RLPAGE
        rlpage_tick();
#endif

        if (shutdown_flag) {
            break;
        }

        /* Test for events */
        dispatch();

        /* Setup timeout for select */
        timeout.tv_sec = (need_more_proc || test_top()) ? 0 : 100;
        need_more_proc = 0;
        timeout.tv_usec = 5;
        next_slice = msec_add(last_slice, command_time_msec);
        slice_timeout = timeval_sub(next_slice, current_time);

        /* Setup file descriptor sets */
        FD_ZERO(&input_set);
        FD_ZERO(&output_set);
        
        if (ndescriptors < avail_descriptors && sock >= 0) {
            FD_SET(sock, &input_set);
        }

#ifdef USE_CID_PLAY
        /* Process remote descriptors */
        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;
            if (d->cstatus & C_REMOTE && d->output.head) {
                if (!process_output(d)) {
                    shutdownsock(d);
                }
                need_more_proc = 1;
            }
        }
#endif

        /* Setup descriptor sets */
        for (d = descriptor_list; d; d = d->next) {
            if (!(d->cstatus & C_REMOTE)) {
                if (d->input.head) {
                    timeout = slice_timeout;
                } else {
                    FD_SET(d->descriptor, &input_set);
                }
                if (d->output.head && 
                    (d->state != CONNECTED || d->player > 0)) {
                    FD_SET(d->descriptor, &output_set);
                }
            }
        }

        /* Wait for I/O or timeout */
        found = select(maxd, &input_set, &output_set,
                      (fd_set *)0, &timeout);
        
        if (found < 0) {
            if (errno != EINTR) {
                perror("select");
            }
        } else {
            time(&now);
            
            /* Handle database loading timeout */
            if (loading_db && !found) {
                if (do_top() && do_top()) {
                    do_top();
                }
                continue;
            }

            time(&now);

            /* Accept new connections */
            if (sock >= 0 && FD_ISSET(sock, &input_set)) {
                newd = new_connection(sock);
                if (!newd) {
                    if (errno && errno != EINTR && 
                        errno != EMFILE && errno != ENFILE) {
                        perror("new_connection");
                    }
                } else {
                    if (newd->descriptor >= maxd) {
                        maxd = newd->descriptor + 1;
                    }
                }
            }

            /* Process input from descriptors */
            for (d = descriptor_list; d; d = dnext) {
                dnext = d->next;
                if (FD_ISSET(d->descriptor, &input_set) && 
                    !(d->cstatus & C_REMOTE)) {
                    if (!process_input(d)) {
                        shutdownsock(d);
                    }
                }
            }

#ifdef USE_CID_PLAY
            /* Process remote output */
            for (d = descriptor_list; d; d = dnext) {
                dnext = d->next;
                if (d->cstatus & C_REMOTE) {
                    process_output(d);
                }
            }
#endif

            /* Process output to descriptors */
            for (d = descriptor_list; d; d = dnext) {
                dnext = d->next;
                if (!(d->cstatus & C_REMOTE) &&
                    FD_ISSET(d->descriptor, &output_set)) {
                    if (!process_output(d)) {
                        shutdownsock(d);
                    }
                }
            }

#ifdef USE_CID_PLAY
            /* Cleanup orphaned remote descriptors */
            for (d = descriptor_list; d; d = dnext) {
                dnext = d->next;
                if (d->cstatus & C_REMOTE && !d->parent) {
                    shutdownsock(d);
                }
            }
#endif
        }
    }
}

/* Setup outgoing connection descriptor */
void outgoing_setupfd(dbref player, int fd)
{
    struct descriptor_data *d;

    if (player < 0 || fd < 0) {
        return;
    }

    ndescriptors++;
//    d = malloc(sizeof(struct descriptor_data));
    SAFE_MALLOC(d, struct descriptor_data, 1);
    if (!d) {
        log_error("Failed to allocate descriptor for outgoing connection");
        return;
    }

    d->descriptor = fd;
    d->concid = make_concid();
    d->cstatus = 0;
    d->parent = NULL;
    d->state = CONNECTED;
    make_nonblocking(fd);
    d->player = -player;
    d->output_prefix = NULL;
    d->output_suffix = NULL;
    d->output_size = 0;
    d->output.head = NULL;
    d->output.tail = &d->output.head;
    d->input.head = NULL;
    d->input.tail = &d->input.head;
    d->raw_input = NULL;
    d->raw_input_at = NULL;
    d->quota = command_burst_size;
    d->last_time = 0;
    strcpy(d->addr, "RWHO");

    if (descriptor_list) {
        descriptor_list->prev = &d->next;
    }
    d->next = descriptor_list;
    d->prev = &descriptor_list;
    descriptor_list = d;

    if (fd >= maxd) {
        maxd = fd + 1;
    }
}

/* Emergency shutdown of server */
void emergency_shutdown(void)
{
    log_error("Emergency shutdown.");
    shutdown_flag = 1;
    exit_status = 136;
    close_sockets();
}

/* Boot a player off the server */
int boot_off(dbref player)
{
    struct descriptor_data *d;

    if (player < 0) {
        return 0;
    }

    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && d->player == player) {
            process_output(d);
            shutdownsock(d);
            return 1;
        }
    }
    
    return 0;
}

/* Platform-specific compatibility functions */
#if defined(HPUX) || defined(SYSV)
#include <unistd.h>

int getdtablesize(void)
{
    return (int)sysconf(_SC_OPEN_MAX);
}
#endif

#ifdef SYSV
void setlinebuf(FILE *x)
{
    setbuf(x, NULL);
}

int vfork(void)
{
    return fork();
}
#endif
