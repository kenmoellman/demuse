#include "externs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef MEMORY_DEBUG_LOG
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#endif

#define MAX_ALLOCATIONS 1000000
#ifdef MEMORY_DEBUG_LOG
#define DEFAULT_CONTENT_LOG_SIZE 64
#endif

typedef struct mem_stack_t {
      void *ptr;                /* information to be stored */
      size_t size;              /* size of stuff allocated. */
      int timer;                /* times through loops before killing. */
      int perm;                 /* permanant flag */
      struct mem_stack_t *prev; /* pointer to last peice of stack. */
      struct mem_stack_t *next; /* pointer to next peice of stack. */
} MSTACK;

typedef struct {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    int active;
#ifdef MEMORY_DEBUG_LOG
    unsigned long sequence;
#endif
} allocation_record_t;


void __free_hunk(MSTACK *);
void *stack_em_int(size_t, int, int);
char *stralloc_int(char *, int);


MSTACK *mem_stack;
MSTACK *mem_first = NULL;
MSTACK *mem_last = NULL;
size_t number_stack_blocks = 0;
size_t stack_size = 0;

static allocation_record_t allocations[MAX_ALLOCATIONS];
static int initialized = 0;

#ifdef MEMORY_DEBUG_LOG
static FILE *memdebug_file = NULL;
static unsigned long operation_sequence = 0;
static size_t content_log_size = DEFAULT_CONTENT_LOG_SIZE;

/* Internal helper for timestamp - NOT directly called anymore */
static void log_timestamp_internal(void) {
    if (!memdebug_file) return;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buffer[26];
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    if (fprintf(memdebug_file, "[%s] ", buffer) < 0) {
        fprintf(stderr, "ERROR: Failed to write timestamp to log file: %s\n", strerror(errno));
    }
}

/* Check if logging is active */
int memdebug_is_active(void) {
    return (memdebug_file != NULL);
}

/* Centralized logging function - like fprintf but with error checking */
void memdebug_log(const char *format, ...) {
    if (!memdebug_file) return;
    
    va_list args;
    va_start(args, format);
    
    int result = vfprintf(memdebug_file, format, args);
    va_end(args);
    
    if (result < 0) {
        fprintf(stderr, "ERROR: Failed to write to memory debug log: %s\n", strerror(errno));
        /* Don't close the file, just warn - let it continue */
    }
    
    /* Flush to ensure data is written */
    if (fflush(memdebug_file) != 0) {
        fprintf(stderr, "ERROR: Failed to flush memory debug log: %s\n", strerror(errno));
    }
}

/* Log with automatic timestamp */
void memdebug_log_ts(const char *format, ...) {
    if (!memdebug_file) return;
    
    /* Write timestamp first */
    log_timestamp_internal();
    
    /* Write the formatted message */
    va_list args;
    va_start(args, format);
    
    int result = vfprintf(memdebug_file, format, args);
    va_end(args);
    
    if (result < 0) {
        fprintf(stderr, "ERROR: Failed to write to memory debug log: %s\n", strerror(errno));
    }
    
    /* Flush */
    if (fflush(memdebug_file) != 0) {
        fprintf(stderr, "ERROR: Failed to flush memory debug log: %s\n", strerror(errno));
    }
}

/* Log hex dump - now uses centralized logging */
void memdebug_log_hex_dump(const void *data, size_t size) {
    if (!memdebug_file || !data || size == 0) return;
    
    const unsigned char *bytes = (const unsigned char *)data;
    size_t bytes_to_log = (size < content_log_size) ? size : content_log_size;
    
    memdebug_log("    Content (%zu bytes%s):\n    ",
            bytes_to_log, (size > content_log_size) ? " [truncated]" : "");
    
    for (size_t i = 0; i < bytes_to_log; i++) {
        memdebug_log("%02x ", bytes[i]);
        if ((i + 1) % 16 == 0 && i < bytes_to_log - 1) {
            memdebug_log("\n    ");
        }
    }
    memdebug_log("\n");
    
    /* ASCII representation */
    memdebug_log("    ASCII: ");
    for (size_t i = 0; i < bytes_to_log; i++) {
        char c = bytes[i];
        memdebug_log("%c", (c >= 32 && c <= 126) ? c : '.');
    }
    memdebug_log("\n");
}

void safe_memory_set_log_file(const char *filename) {
    if (memdebug_file && memdebug_file != stderr) {
        fclose(memdebug_file);
    }
    
    if (filename) {
        memdebug_file = fopen(filename, "w");
        if (!memdebug_file) {
            fprintf(stderr, "ERROR: Could not open log file '%s': %s\n", filename, strerror(errno));
            fprintf(stderr, "       Using stderr for debug output\n");
            memdebug_file = stderr;
        } else {
            memdebug_log("=== Memory Allocation Log Started ===\n");
            memdebug_log("Content log size: %zu bytes\n", content_log_size);
            memdebug_log("=====================================\n\n");
        }
    } else {
        memdebug_file = NULL;
    }
}

void safe_memory_set_content_log_size(size_t max_bytes) {
    content_log_size = max_bytes;
}
#endif /* MEMORY_DEBUG_LOG */

void safe_memory_init(void) {
    if (initialized) {
#ifdef MEMORY_DEBUG_LOG
        memdebug_log("WARNING: safe_memory_init() called multiple times\n");
#endif
        return;
    }
    memset(allocations, 0, sizeof(allocations));
    initialized = 1;
}

static void track_allocation(void *ptr, size_t size, const char *file, int line) {
    if (!initialized) {
#ifdef MEMORY_DEBUG_LOG
        memdebug_log("FATAL: safe_memory_init() not called before allocation at %s:%d\n", file, line);
#endif
        abort();
    }
    
    for (int i = 0; i < MAX_ALLOCATIONS; i++) {
        if (!allocations[i].active) {
            allocations[i].ptr = ptr;
            allocations[i].size = size;
            allocations[i].file = file;
            allocations[i].line = line;
            allocations[i].active = 1;
#ifdef MEMORY_DEBUG_LOG
            allocations[i].sequence = operation_sequence;
#endif
            return;
        }
    }
    
#ifdef MEMORY_DEBUG_LOG
    memdebug_log_ts("WARNING: Allocation tracking table full\n");
#endif
}

static int untrack_allocation(void *ptr, const char *file, int line
#ifdef MEMORY_DEBUG_LOG
    , unsigned long *alloc_seq, size_t *alloc_size,
    const char **alloc_file, int *alloc_line
#endif
) {
    if (!initialized) {
#ifdef MEMORY_DEBUG_LOG
        memdebug_log("FATAL: safe_memory_init() not called before free at %s:%d\n", file, line);
#endif
        abort();
    }
    
    if (ptr == NULL) {
        return 1; /* freeing NULL is allowed */
    }
    
    for (int i = 0; i < MAX_ALLOCATIONS; i++) {
        if (allocations[i].active && allocations[i].ptr == ptr) {
#ifdef MEMORY_DEBUG_LOG
            *alloc_seq = allocations[i].sequence;
            *alloc_size = allocations[i].size;
            *alloc_file = allocations[i].file;
            *alloc_line = allocations[i].line;
#endif
            allocations[i].active = 0;
            return 1;
        }
    }
    
#ifdef MEMORY_DEBUG_LOG
    /* Not found - potential double-free */
    memdebug_log_ts("!!! DOUBLE-FREE DETECTED !!!\n");
    memdebug_log("    Pointer: %p\n", ptr);
    memdebug_log("    Free attempt at: %s:%d\n", file, line);
    memdebug_log("    This pointer is NOT in active allocations table\n");
#endif
    log_error(tprintf("!!! DOUBLE-FREE DETECTED !!! Pointer: %p   Free attempt at: %s:%d", ptr, file, line));
//    return 0;
    return 1;
}

void* safe_malloc(size_t size, const char *file, int line) {
    void *ptr = malloc(size);
    
    if (!ptr) {
        fprintf(stderr, "PANIC: Out of memory at %s:%d (requested %zu bytes)\n",
                file, line, size);
	fflush(stderr);
#ifdef MEMORY_DEBUG_LOG
        memdebug_log_ts("PANIC: Out of memory at %s:%d (requested %zu bytes)\n",
                file, line, size);
#endif
        exit(1);
    }
    
#ifdef MEMORY_DEBUG_LOG
    operation_sequence++;
#endif
    
    track_allocation(ptr, size, file, line);
    
#ifdef MEMORY_DEBUG_LOG
    /* Log the allocation */
    memdebug_log_ts("MALLOC #%lu: %p (%zu bytes) at %s:%d\n",
            operation_sequence, ptr, size, file, line);
    /* Log initial content (usually garbage) */
    memdebug_log_hex_dump(ptr, size);
#endif
    
    return ptr;
}

void smart_free(void *ptr, const char *file, int line)
{
    if (ptr == NULL) {
        return;
    }

    // Check if it's in the MSTACK
    MSTACK *m;
    for (m = mem_first; m; m = m->next)
    {
        if (m->ptr == ptr)
        {
            // Found in stack - expire it
            m->timer = 1;
#ifdef MEMORY_DEBUG_LOG
            memdebug_log_ts("SMART_FREE: %p found in MSTACK=%p, expiring (timer=1) at %s:%d\n",
                           ptr, (void*)m, file, line);
#endif
            return;
        }
    }

    // Not in stack - use regular safe_free
#ifdef MEMORY_DEBUG_LOG
    memdebug_log_ts("SMART_FREE: %p not in stack, using safe_free() at %s:%d\n",
                   ptr, file, line);
#endif
    safe_free(ptr, file, line);
}

void safe_free(void *ptr, const char *file, int line) {
#ifdef MEMORY_DEBUG_LOG
    unsigned long alloc_seq = 0;
    size_t alloc_size = 0;
    const char *alloc_file = NULL;
    int alloc_line = 0;
    
    operation_sequence++;
    
    /* Log before freeing so we can see content */
    memdebug_log_ts("FREE #%lu: %p at %s:%d\n",
            operation_sequence, ptr, file, line);
    
    if (ptr == NULL) {
        memdebug_log("    (NULL pointer - no-op)\n");
    }
#endif
    
    if (!untrack_allocation(ptr, file, line
#ifdef MEMORY_DEBUG_LOG
        , &alloc_seq, &alloc_size, &alloc_file, &alloc_line
#endif
    )) {
#ifdef MEMORY_DEBUG_LOG
        memdebug_log("!!! ABORTING DUE TO DOUBLE-FREE !!!\n");
#endif
        abort();
    }
    
#ifdef MEMORY_DEBUG_LOG
    /* Log allocation details and content before freeing */
    if (ptr != NULL) {
        memdebug_log("    Originally allocated: MALLOC #%lu at %s:%d (%zu bytes)\n",
                alloc_seq, alloc_file, alloc_line, alloc_size);
        memdebug_log("    Lifetime: %lu operations\n",
                operation_sequence - alloc_seq);
        /* Log content before freeing */
        memdebug_log_hex_dump(ptr, alloc_size);
    }
#endif
    
    free(ptr);
}

void safe_memory_cleanup(void) {
#ifdef MEMORY_DEBUG_LOG
    if (!initialized) {
        memdebug_log_ts("WARNING: safe_memory_cleanup() called but system not initialized\n");
        return;
    }
    
    /* Check for memory leaks */
    int leaks = 0;
    memdebug_log("\n=== Memory Cleanup Report ===\n");
    
    for (int i = 0; i < MAX_ALLOCATIONS; i++) {
        if (allocations[i].active) {
            if (leaks == 0) {
                memdebug_log("MEMORY LEAKS DETECTED:\n");
            }
            memdebug_log("  LEAK: %p (%zu bytes) allocated at %s:%d (seq #%lu)\n",
                    allocations[i].ptr, allocations[i].size,
                    allocations[i].file, allocations[i].line,
                    allocations[i].sequence);
            leaks++;
        }
    }
    
    if (leaks > 0) {
        memdebug_log("Total: %d leaked allocation(s)\n", leaks);
    }
    else 
    {
        memdebug_log("No memory leaks detected.\n");
    }
    
    memdebug_log("Total operations: %lu\n", operation_sequence);
    memdebug_log("=== End of Log ===\n");
    
    if (memdebug_file && memdebug_file != stderr) {
        fclose(memdebug_file);
    }
    memdebug_file = NULL;
#endif
    return;
}

void safe_memory_report(void) {
#ifdef MEMORY_DEBUG_LOG
    if (!initialized) {
        memdebug_log("WARNING: safe_memory_report() called but system not initialized\n");
        return;
    }
    
    int active_count = 0;
    size_t total_size = 0;
    
    for (int i = 0; i < MAX_ALLOCATIONS; i++) {
        if (allocations[i].active) {
            active_count++;
            total_size += allocations[i].size;
        }
    }
    
    memdebug_log_ts("REPORT: Active allocations: %d (%zu bytes)\n",
            active_count, total_size);
#endif
    return;
}

void *stack_em_fun(size_t size)
{
    return(stack_em_int(size, 200, 0));
}

void *stack_em(size_t size)
{
    return(stack_em_int(size, 1, 0));
}

void *stack_em_int(size_t size, int timer, int perm)
{
      MSTACK *mnew;
      
      mnew = (MSTACK *)safe_malloc(sizeof(MSTACK) + 1, __FILE__, __LINE__);
      mnew->ptr = safe_malloc(size, __FILE__, __LINE__);
      
      // CHECK FOR DUPLICATE POINTERS AT ALLOCATION TIME
      MSTACK *check;
      for (check = mem_first; check; check = check->next) {
          if (check->ptr == mnew->ptr) {
#ifdef MEMORY_DEBUG_LOG
              memdebug_log_ts("DUPLICATE PTR AT ALLOCATION!\n");
              memdebug_log("  Old MSTACK=%p, timer=%d, perm=%d, ptr=%p\n",
                      (void*)check, check->timer, check->perm, check->ptr);
              memdebug_log("  New MSTACK=%p, timer=%d, perm=%d, ptr=%p\n",
                      (void*)mnew, timer, perm, mnew->ptr);
#endif
              abort();
          }
      }
      
      mnew->size = size;
      mnew->timer = timer + 50;  /* add some padding in case */
      mnew->perm = perm; /* perm flag */
      mnew->next = NULL;;
      mnew->prev = mem_last;
      
      if (mem_last)
      {
        mem_last->next = mnew;
        mem_last = mnew;
      }
      else
      {
        mem_first = mem_last = mnew;
      }
      
      stack_size = stack_size + sizeof(MSTACK) + size + 1;
      number_stack_blocks++;
      
#ifdef MEMORY_DEBUG_LOG
      memdebug_log_ts("DEBUG: ALLOC: number_stack_blocks: %zu / MSTACK=%p, ptr=%p, mnew->prev=%p, mnew->next=%p m->size=%zu, m->timer=%d, m->perm=%d\n",
          number_stack_blocks, (void*)mnew, mnew->ptr, (void*)mnew->prev, (void*)mnew->next, mnew->size, mnew->timer, mnew->perm);
#endif
      
      return(mnew->ptr);
}

void __free_hunk(MSTACK *m)
{
#ifdef MEMORY_DEBUG_LOG
    memdebug_log_ts("DEBUG: Entering __free_hunk:  number_stack_blocks: %zu / MSTACK=%p, ptr=%p, m->prev=%p, m->next=%p m->size=%zu, m->timer=%d, m->perm=%d\n",
          number_stack_blocks, (void*)m, m->ptr, (void*)m->prev, (void*)m->next, m->size, m->timer, m->perm);
#endif
    
    // Verify we actually have blocks to free
    if (number_stack_blocks == 0) {
#ifdef MEMORY_DEBUG_LOG
      memdebug_log("ERROR: Attempting to free when number_stack_blocks is 0!\n");
      memdebug_log("MSTACK pointer: %p, m->ptr: %p\n", (void*)m, m->ptr);
#endif
      return;
    }
    
    // Verify this block is actually in our list
#ifdef MEMORY_DEBUG_LOG
    memdebug_log_ts("DEBUG: Checking if MSTACK is in list...\n");
#endif
    MSTACK *check;
    int found = 0;
    for (check = mem_first; check; check = check->next) {
      if (check == m) {
          found = 1;
          break;
      }
    }
    
    if (!found) {
#ifdef MEMORY_DEBUG_LOG
      memdebug_log("ERROR: Attempting to free MSTACK %p not in our list!\n", (void*)m);
      memdebug_log("This is likely a double-free or corruption.\n");
#endif
      return;
    }
    
#ifdef MEMORY_DEBUG_LOG
    memdebug_log("DEBUG: Found in list: %d\n", found);
#endif
    
    if (m == mem_first)
    {
      mem_first = m->next;
      if(mem_first)
      {
        mem_first->prev = NULL;
      }
      else
      {
        mem_last = NULL;
      }
    }
    else if (m == mem_last)
    {
      mem_last = m->prev;
      if(mem_last)
      {
        mem_last->next = NULL;

      }
      else
      {
        mem_first = NULL;
      }
    }
    else
    {
      (m->prev)->next = m->next;
      (m->next)->prev = m->prev;
    }
    
//    fprintf(stderr, "DEBUG: About to modify linked list...\n");
//    fflush(stderr);
    
    if(m->ptr)
    {
            SAFE_FREE(m->ptr);
            // NULL out after freeing
            m->ptr=NULL;
    }
    
    // Verify stack_size doesn't underflow
    if (stack_size < m->size + sizeof(MSTACK)) {
#ifdef MEMORY_DEBUG_LOG
        memdebug_log("ERROR: stack_size underflow! Current: %zu, trying to subtract: %zu\n", stack_size, m->size + sizeof(MSTACK));
#endif
        abort();
    }
    
    // continue previous code
    stack_size = stack_size - m->size - sizeof(MSTACK);
    m->next = NULL;
    m->prev = NULL;
    SAFE_FREE(m);
    number_stack_blocks--;
}

void clear_stack(void)
{
    MSTACK *m, *mnext;
    if(!mem_first)
        return;
    
    m = mem_first;
    for(; m; m=mnext)
    {
#ifdef MEMORY_DEBUG_LOG
        memdebug_log("DEBUG: clear_stack:  number_stack_blocks: %zu / MSTACK=%p, ptr=%p, m->prev=%p, m->next=%p m->size=%zu, m->timer=%d, m->perm=%d\n",
              number_stack_blocks, (void*)m, m->ptr, (void*)m->prev, (void*)m->next, m->size, m->timer, m->perm);
#endif
        
        mnext = m->next;
        m->timer--;
        if ((m->timer < 1) && (m->perm == 0))
        {
            __free_hunk(m);
        }
    }
}

char *stralloc(const char *string)
{
   return stralloc_int(string, 0);
}

char *stralloc_p(char *string)
{
   return stralloc_int(string, 1);
}

char *stralloc_int(char *string, int perm)
{
  char *p;
  size_t slen = strlen(string);
  p = stack_em_int((slen + 1), 5, perm);
  strncpy(p, string, slen);
  p[slen] = '\0';
  return(p);
}

char *funalloc(char *string)
{
    size_t slen = strlen(string);
    char *p = stack_em_int((slen + 1), 5, 0);  /* permanantly allocate functions? */
    strncpy(p, string, slen);
    p[slen] = '\0';
    return(p);
}

void strfree_p(char *string)
{
  MSTACK *m;
  for (m = mem_first; m; m=m->next)
  {
    if (m->ptr == string) /* if it's the same pointer */
    {
      __free_hunk(m);
      break;
    }
  }
}

void shutdown_stack(void)
{
  int x;
  for (x=0;x<MAX_ALLOCATIONS;x++)
  {
    clear_stack();
  }
}
