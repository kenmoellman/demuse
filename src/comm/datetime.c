/* test_datetime.c - Unit tests for datetime functions
 * 
 * Compile with: gcc -o test_datetime test_datetime.c datetime.c -I../hdrs
 * Run with: ./test_datetime
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

/* Mock implementation of tprintf and stralloc for testing */
char *stralloc(const char *str) {
    char *result = malloc(strlen(str) + 1);
    if (result) strcpy(result, str);
    return result;
}

char *tprintf(const char *format, ...) {
    static char buffer[8192];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return stralloc(buffer);
}

/* Include the datetime functions */
#include "datetime.h"

/* Test helper */
void test_function(const char *test_name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) == 0) {
        printf("✓ %s: PASS\n", test_name);
    } else {
        printf("✗ %s: FAIL\n", test_name);
        printf("  Expected: '%s'\n", expected);
        printf("  Got:      '%s'\n", actual);
    }
    free((void*)actual);  /* Clean up allocated string */
}

int main() {
    printf("========================================\n");
    printf("Testing datetime.c functions\n");
    printf("========================================\n\n");
    
    /* Test time_format_1 */
    printf("Testing time_format_1 (weeks/days HH:MM):\n");
    test_function("0 seconds", "00:00", time_format_1(0));
    test_function("1 hour", "01:00", time_format_1(3600));
    test_function("1 day", "1d 00:00", time_format_1(86400));
    test_function("1 week", "1w 00:00", time_format_1(604800));
    test_function("10 days", "1w 03:00", time_format_1(864000));
    
    printf("\nTesting time_format_2 (single unit):\n");
    test_function("0 seconds", "0s", time_format_2(0));
    test_function("30 seconds", "30s", time_format_2(30));
    test_function("5 minutes", "5m", time_format_2(300));
    test_function("2 hours", "2h", time_format_2(7200));
    test_function("3 days", "3d", time_format_2(259200));
    test_function("2 weeks", "2w", time_format_2(1209600));
    
    printf("\nTesting time_format_3 (human readable):\n");
    test_function("1 second", "a second", time_format_3(1));
    test_function("2 seconds", "2 seconds", time_format_3(2));
    test_function("1 minute", "a minute", time_format_3(60));
    test_function("5 minutes", "5 minutes", time_format_3(300));
    test_function("1 hour", "an hour", time_format_3(3600));
    test_function("3 hours", "3 hours", time_format_3(10800));
    test_function("1 day", "a day", time_format_3(86400));
    test_function("3 days", "3 days", time_format_3(259200));
    test_function("1 week", "a week", time_format_3(604800));
    test_function("2 weeks", "2 weeks", time_format_3(1209600));
    
    printf("\nTesting time_format_4 (detailed):\n");
    test_function("Mixed time 1", "an hour, and 30 minutes", 
                  time_format_4(3600 + 1800));
    test_function("Mixed time 2", "a day, 2 hours, and 30 minutes", 
                  time_format_4(86400 + 7200 + 1800));
    /* Note: Can't easily test exact output without mocking gmtime */
    
    printf("\nTesting edge cases:\n");
    test_function("Negative time (-100)", "00:00", time_format_1(-100));
    test_function("Very large time", "52w 01:00", time_format_1(31536000 + 3600));
    
    printf("\nTesting get_day:\n");
    /* Test with a known date - Unix epoch was a Thursday */
    time_t thursday = 0;  /* Jan 1, 1970 00:00:00 UTC */
    char *day = get_day(thursday);
    printf("  Unix epoch day (may vary by timezone): %s\n", day);
    free(day);
    
    printf("\nTesting mil_to_stndrd:\n");
    time_t morning = 32400;  /* 9:00 AM UTC */
    time_t evening = 75600;  /* 9:00 PM UTC */
    char *am = mil_to_stndrd(morning);
    char *pm = mil_to_stndrd(evening);
    printf("  Morning (9 AM): %s\n", am);
    printf("  Evening (9 PM): %s\n", pm);
    free(am);
    free(pm);
    
    printf("\nTesting time_stamp:\n");
    time_t test_time = time(NULL);
    char *timestamp = time_stamp(test_time);
    printf("  Current time: %s\n", timestamp);
    free(timestamp);
    
    printf("\n========================================\n");
    printf("Testing complete!\n");
    printf("========================================\n");
    
    return 0;
}
