/* info.c - System information display commands
 * Located in comm/ directory
 * 
 * This file contains commands for displaying various system information
 * including configuration, database stats, function lists, memory usage, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"

/* For mallinfo on systems that support it */
#ifdef __GLIBC__
#include <malloc.h>
#endif

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static void info_cpu(dbref player);
static void info_mem(dbref player);

/* ===================================================================
 * Main Info Command
 * =================================================================== */

/**
 * INFO command - display various system information
 * @param player Player requesting info
 * @param arg1 Type of info to display
 */
void do_info(dbref player, char *arg1)
{
    if (!arg1 || !*arg1) {
        notify(player, "Usage: @info <type>");
        notify(player, "Available types: config, db, funcs, memory, mail"
#ifdef USE_PROC
               ", pid, cpu"
#endif
        );
        return;
    }

    if (!string_compare(arg1, "config")) {
        info_config(player);
    }
    else if (!string_compare(arg1, "db")) {
        info_db(player);
    }
    else if (!string_compare(arg1, "funcs")) {
        info_funcs(player);
    }
    else if (!string_compare(arg1, "memory")) {
        info_mem(player);
    }
    else if (!string_compare(arg1, "mail")) {
        info_mail(player);
    }
#ifdef USE_PROC
    else if (!string_compare(arg1, "pid")) {
        info_pid(player);
    }
    else if (!string_compare(arg1, "cpu")) {
        info_cpu(player);
    }
#endif
    else {
        notify(player, tprintf("Unknown info type: %s", arg1));
        notify(player, "Try: @info (with no arguments) for a list of types.");
    }
}

/* ===================================================================
 * Memory Information
 * =================================================================== */

/**
 * Display memory usage statistics
 * @param player Player to send info to
 */
static void info_mem(dbref player)
{
    notify(player, "=== Memory Statistics ===");
    
    /* Stack information */
    notify(player, tprintf("Stack Size/Blocks: %zu/%zu", 
                          stack_size, number_stack_blocks));
    
    /* Text block information */
    notify(player, tprintf("Text Block Size/Count: %zu/%zu",
                          text_block_size, text_block_num));
    
#ifdef __GLIBC__
    /* Use mallinfo2 on newer glibc, mallinfo on older */
    #if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)
    struct mallinfo2 m_info = mallinfo2();
    notify(player, tprintf("Total Allocated Memory: %zu bytes", m_info.arena));
    notify(player, tprintf("Free Allocated Memory: %zu bytes", m_info.fordblks));
    notify(player, tprintf("Free Chunks: %zu", m_info.ordblks));
    notify(player, tprintf("Used Memory: %zu bytes", m_info.uordblks));
    #else
    struct mallinfo m_info = mallinfo();
    notify(player, tprintf("Total Allocated Memory: %d bytes", m_info.arena));
    notify(player, tprintf("Free Allocated Memory: %d bytes", m_info.fordblks));
    notify(player, tprintf("Free Chunks: %d", m_info.ordblks));
    notify(player, tprintf("Used Memory: %d bytes", m_info.uordblks));
    #endif
#else
    notify(player, "Detailed memory statistics not available on this platform.");
#endif
}


/* ===================================================================
 * Process Information (Linux /proc filesystem)
 * =================================================================== */

#ifdef USE_PROC

/**
 * Display process ID and memory information from /proc
 * @param player Player to send info to
 */
void info_pid(dbref player)
{
    char filename[256];
    char buf[1024];
    FILE *fp;
    char *p;
    
    /* Build /proc path */
    snprintf(filename, sizeof(filename), "/proc/%d/status", getpid());
    
    /* Try to open /proc file */
    fp = fopen(filename, "r");
    if (!fp) {
        notify(player, tprintf("Couldn't open \"%s\" for reading!", filename));
        notify(player, "Process information not available on this system.");
        return;
    }
    
    /* Search for VmSize line */
    while (fgets(buf, sizeof(buf), fp)) {
        if (!strncmp(buf, "VmSize", 6)) {
            break;
        }
    }
    
    if (feof(fp)) {
        fclose(fp);
        notify(player, tprintf("Error reading \"%s\"!", filename));
        return;
    }
    
    fclose(fp);
    
    /* Parse the VmSize value */
    /* Remove trailing newline */
    buf[strcspn(buf, "\n")] = '\0';
    
    /* Skip to the value */
    p = buf;
    while (*p && !isspace(*p)) {
        p++;
    }
    while (isspace(*p)) {
        p++;
    }
    
    /* Display results */
    notify(player, tprintf("=== %s Process Information ===", muse_name));
    notify(player, tprintf("PID: %d", getpid()));
    notify(player, tprintf("Virtual Memory Size: %s", p));
}

/**
 * Display CPU information from /proc/cpuinfo
 * @param player Player to send info to
 */
static void info_cpu(dbref player)
{
    FILE *fp;
    char buf[256];
    
    fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        notify(player, "CPU information not available on this system.");
        return;
    }
    
    notify(player, "=== CPU Information ===");
    
    while (fgets(buf, sizeof(buf), fp)) {
        /* Remove trailing newline */
        buf[strcspn(buf, "\n")] = '\0';
        notify(player, buf);
    }
    
    fclose(fp);
}

#endif /* USE_PROC */
