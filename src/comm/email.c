/* email.c - Modern SMTP email functionality using libcurl
 * Simplified and secure email sending for TinyMUSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <curl/curl.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"

/* ===================================================================
 * Configuration and Constants
 * =================================================================== */

/* Buffer sizes */
#define EMAIL_BUFFER_SIZE 8192
#define ADDRESS_BUFFER_SIZE 256
#define SUBJECT_BUFFER_SIZE 256

/* Rate limiting */
#ifndef MAX_EMAILS_PER_DAY
#define MAX_EMAILS_PER_DAY 10
#endif

#ifndef EMAIL_COOLDOWN
#define EMAIL_COOLDOWN 60  /* seconds between emails */
#endif

#ifndef MAX_EMAIL_LENGTH
#define MAX_EMAIL_LENGTH 4096
#endif

/* Email validation */
#define MAX_EMAIL_ADDRESS_LENGTH 254  /* RFC 5321 */
#define MIN_EMAIL_ADDRESS_LENGTH 6    /* a@b.co minimum */

/* ===================================================================
 * Data Structures
 * =================================================================== */

/* Email payload structure for libcurl */
struct email_data {
    const char *payload;
    size_t bytes_read;
    size_t total_size;
};

/* Rate limiting tracking */
struct email_rate_limit {
    time_t last_email_time;
    int emails_sent_today;
    time_t day_start;
};

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static size_t payload_reader(void *ptr, size_t size, size_t nmemb, void *userp);
static char *build_email_payload(const char *from, const char *to, 
                                 const char *subject, const char *body);
static int send_email_curl(const char *to, const char *subject, const char *body,
                          const char *from, char *error_buf, size_t error_buf_size);
static int validate_email_address(const char *email);
static int check_rate_limit(dbref player, struct email_rate_limit *limits);
static void update_rate_limit(dbref player, struct email_rate_limit *limits);
static char *sanitize_email_header(const char *input);
static char *encode_email_body(const char *body);

/* ===================================================================
 * Email Address Validation
 * =================================================================== */

/**
 * Validate email address format (RFC 5322 simplified)
 * @param email Email address to validate
 * @return 1 if valid, 0 if invalid
 */
static int validate_email_address(const char *email)
{
    const char *at_sign;
    const char *p;
    size_t len;
    int dot_count = 0;
    
    if (!email || !*email) {
        return 0;
    }
    
    len = strlen(email);
    if (len < MIN_EMAIL_ADDRESS_LENGTH || len > MAX_EMAIL_ADDRESS_LENGTH) {
        return 0;
    }
    
    /* Find @ symbol */
    at_sign = strchr(email, '@');
    if (!at_sign || at_sign == email) {
        return 0;  /* No @ or starts with @ */
    }
    
    /* Check for second @ */
    if (strchr(at_sign + 1, '@')) {
        return 0;  /* Multiple @ symbols */
    }
    
    /* Validate local part (before @) */
    for (p = email; p < at_sign; p++) {
        if (!isalnum((unsigned char)*p) && 
            *p != '.' && *p != '_' && *p != '-' && *p != '+') {
            return 0;  /* Invalid character in local part */
        }
        if (*p == '.' && (p == email || *(p+1) == '@')) {
            return 0;  /* Can't start with dot or end with dot */
        }
        if (*p == '.' && *(p-1) == '.') {
            return 0;  /* No consecutive dots */
        }
    }
    
    /* Validate domain part (after @) */
    p = at_sign + 1;
    if (!*p || *p == '.') {
        return 0;  /* Domain can't be empty or start with dot */
    }
    
    for (; *p; p++) {
        if (*p == '.') {
            dot_count++;
            if (*(p+1) == '.' || !*(p+1)) {
                return 0;  /* No consecutive dots or ending with dot */
            }
        } else if (!isalnum((unsigned char)*p) && *p != '-') {
            return 0;  /* Invalid character in domain */
        }
    }
    
    /* Domain must have at least one dot */
    if (dot_count < 1) {
        return 0;
    }
    
    return 1;
}

/**
 * Sanitize email header to prevent injection attacks
 * @param input Raw header value
 * @return Sanitized string (allocated, caller must free)
 */
static char *sanitize_email_header(const char *input)
{
    char *output;
    char *p;
    const char *q;
    size_t len;
    
    if (!input) {
        return stralloc("");
    }
    
    len = strlen(input);
    if (len > 998) {  /* RFC 5322 line length limit */
        len = 998;
    }
    
    output = malloc(len + 1);
    if (!output) {
        return NULL;
    }
    
    p = output;
    for (q = input; *q && (size_t)(p - output) < len; q++) {
        /* Remove control characters except tab */
        if (iscntrl((unsigned char)*q) && *q != '\t') {
            continue;
        }
        /* Prevent header injection */
        if (*q == '\r' || *q == '\n') {
            continue;
        }
        *p++ = *q;
    }
    *p = '\0';
    
    return output;
}

/**
 * Encode email body for safe transmission
 * @param body Raw body text
 * @return Encoded string (allocated, caller must free)
 */
static char *encode_email_body(const char *body)
{
    char *output;
    size_t len;
    const char *p;
    char *q;
    
    if (!body) {
        return stralloc("");
    }
    
    len = strlen(body);
    if (len > MAX_EMAIL_LENGTH) {
        len = MAX_EMAIL_LENGTH;
    }
    
    /* Allocate extra space for encoding */
    output = malloc(len * 2 + 1);
    if (!output) {
        return NULL;
    }
    
    q = output;
    for (p = body; *p && (size_t)(q - output) < len * 2 - 1; p++) {
        /* Handle line breaks */
        if (*p == '\n') {
            if (p > body && *(p-1) == '\r') {
                /* Already have CRLF, just copy */
                *q++ = *p;
            } else {
                /* Convert LF to CRLF */
                *q++ = '\r';
                *q++ = '\n';
            }
        } else if (*p == '\r') {
            /* Skip bare CR, will be handled with LF */
            if (*(p+1) != '\n') {
                *q++ = '\r';
                *q++ = '\n';
            }
        } else if (iscntrl((unsigned char)*p) && *p != '\t') {
            /* Remove control characters except tab */
            continue;
        } else {
            *q++ = *p;
        }
    }
    *q = '\0';
    
    return output;
}

/* ===================================================================
 * Rate Limiting
 * =================================================================== */

/**
 * Check if player is within rate limits
 * @param player Player to check
 * @param limits Rate limit structure to populate
 * @return 1 if allowed, 0 if rate limited
 */
static int check_rate_limit(dbref player, struct email_rate_limit *limits)
{
    const char *attr_value;
    time_t current_time;
    struct tm *tm_info;
    time_t today_start;
    
    current_time = time(NULL);
    tm_info = localtime(&current_time);
    if (!tm_info) {
        return 0;  /* Safety: deny on error */
    }
    
    /* Calculate start of today */
    tm_info->tm_hour = 0;
    tm_info->tm_min = 0;
    tm_info->tm_sec = 0;
    today_start = mktime(tm_info);
    
    /* Get rate limit data from attribute */
    attr_value = atr_get(player, A_EMAIL);
    if (attr_value && *attr_value) {
        /* Parse: last_time:count:day_start */
        if (sscanf(attr_value, "%ld:%d:%ld", 
                  &limits->last_email_time,
                  &limits->emails_sent_today,
                  &limits->day_start) != 3) {
            /* Invalid format, reset */
            limits->last_email_time = 0;
            limits->emails_sent_today = 0;
            limits->day_start = today_start;
        }
    } else {
        /* No data, initialize */
        limits->last_email_time = 0;
        limits->emails_sent_today = 0;
        limits->day_start = today_start;
    }
    
    /* Check if it's a new day */
    if (limits->day_start < today_start) {
        limits->emails_sent_today = 0;
        limits->day_start = today_start;
    }
    
    /* Check cooldown */
    if (current_time - limits->last_email_time < EMAIL_COOLDOWN) {
        return 0;  /* Too soon */
    }
    
    /* Check daily limit */
    if (limits->emails_sent_today >= MAX_EMAILS_PER_DAY) {
        return 0;  /* Daily limit reached */
    }
    
    return 1;
}

/**
 * Update rate limit tracking after sending email
 * @param player Player who sent email
 * @param limits Current rate limit data
 */
static void update_rate_limit(dbref player, struct email_rate_limit *limits)
{
    char buf[128];
    
    limits->last_email_time = time(NULL);
    limits->emails_sent_today++;
    
    snprintf(buf, sizeof(buf), "%ld:%d:%ld",
            limits->last_email_time,
            limits->emails_sent_today,
            limits->day_start);
    
    atr_add(player, A_EMAIL, buf);
}

/* ===================================================================
 * SMTP Functions
 * =================================================================== */

/**
 * Callback function for libcurl to read email data
 */
static size_t payload_reader(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct email_data *upload = (struct email_data *)userp;
    size_t room = size * nmemb;
    size_t len;
    
    if (!upload->payload || upload->bytes_read >= upload->total_size) {
        return 0;  /* No more data */
    }
    
    len = upload->total_size - upload->bytes_read;
    if (len > room) {
        len = room;
    }
    
    memcpy(ptr, upload->payload + upload->bytes_read, len);
    upload->bytes_read += len;
    
    return len;
}

/**
 * Build RFC-compliant email payload
 */
static char *build_email_payload(const char *from, const char *to, 
                                 const char *subject, const char *body)
{
    char *payload;
    char *clean_from = NULL;
    char *clean_to = NULL;
    char *clean_subject = NULL;
    char *clean_body = NULL;
    char date_buffer[128];
    time_t now;
    struct tm *tm_info;
    size_t needed_size;
    int result;
    
    /* Get current time for Date header */
    time(&now);
    tm_info = gmtime(&now);
    if (!tm_info) {
        return NULL;
    }
    
    strftime(date_buffer, sizeof(date_buffer), 
             "%a, %d %b %Y %H:%M:%S +0000", tm_info);
    
    /* Sanitize all inputs */
    clean_from = sanitize_email_header(from);
    clean_to = sanitize_email_header(to);
    clean_subject = sanitize_email_header(subject);
    clean_body = encode_email_body(body);
    
    if (!clean_from || !clean_to || !clean_subject || !clean_body) {
        free(clean_from);
        free(clean_to);
        free(clean_subject);
        free(clean_body);
        return NULL;
    }
    
    /* Calculate needed size */
    needed_size = strlen(clean_from) + strlen(clean_to) + 
                  strlen(clean_subject) + strlen(clean_body) + 512;
    
    payload = malloc(needed_size);
    if (!payload) {
        free(clean_from);
        free(clean_to);
        free(clean_subject);
        free(clean_body);
        return NULL;
    }
    
    /* Build email with proper headers */
    result = snprintf(payload, needed_size,
        "Date: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Subject: %s\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "Content-Transfer-Encoding: 8bit\r\n"
        "MIME-Version: 1.0\r\n"
        "X-Mailer: TinyMUSE-Email/1.0\r\n"
        "\r\n"
        "%s\r\n",
        date_buffer, clean_from, clean_to, clean_subject, clean_body);
    
    free(clean_from);
    free(clean_to);
    free(clean_subject);
    free(clean_body);
    
    if (result < 0 || (size_t)result >= needed_size) {
        free(payload);
        return NULL;
    }
    
    return payload;
}

/**
 * Send email using libcurl
 */
static int send_email_curl(const char *to, const char *subject, const char *body,
                          const char *from, char *error_buf, size_t error_buf_size)
{
    CURL *curl;
    CURLcode res = CURLE_OK;
    struct curl_slist *recipients = NULL;
    struct email_data upload;
    char smtp_url[256];
    int success = 0;
    
    /* Build email payload */
    upload.payload = build_email_payload(from, to, subject, body);
    if (!upload.payload) {
        snprintf(error_buf, error_buf_size, "Failed to build email");
        return 0;
    }
    upload.bytes_read = 0;
    upload.total_size = strlen(upload.payload);
    
    /* Initialize curl */
    curl = curl_easy_init();
    if (!curl) {
        free((void *)upload.payload);
        snprintf(error_buf, error_buf_size, "Failed to initialize CURL");
        return 0;
    }
    
    /* Set SMTP server URL */
    snprintf(smtp_url, sizeof(smtp_url), "smtp://%s:%d", SMTP_SERVER, SMTP_PORT);
    curl_easy_setopt(curl, CURLOPT_URL, smtp_url);
    
    /* Authentication */
#ifdef SMTP_USERNAME
    curl_easy_setopt(curl, CURLOPT_USERNAME, SMTP_USERNAME);
#endif
#ifdef SMTP_PASSWORD
    curl_easy_setopt(curl, CURLOPT_PASSWORD, SMTP_PASSWORD);
#endif
    
    /* Use TLS/SSL if configured */
#if SMTP_USE_SSL
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#endif
    
    /* Set sender */
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from);
    
    /* Set recipient */
    recipients = curl_slist_append(recipients, to);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    
    /* Set upload callback */
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_reader);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    
    /* Timeout settings */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    /* Error buffer */
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);
    
    /* Perform the send */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (!*error_buf) {
            snprintf(error_buf, error_buf_size, "%s", curl_easy_strerror(res));
        }
        log_error(tprintf("Email send failed: %s", error_buf));
    } else {
        success = 1;
    }
    
    /* Cleanup */
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    free((void *)upload.payload);
    
    return success;
}

/* ===================================================================
 * Public Commands
 * =================================================================== */

/**
 * EMAIL command - send email to player or address
 */
void do_email(dbref player, const char *arg1, const char *msg)
{
    dbref victim = NOTHING;
    char to_address[ADDRESS_BUFFER_SIZE];
    char from_address[ADDRESS_BUFFER_SIZE];
    char subject[SUBJECT_BUFFER_SIZE];
    char body[EMAIL_BUFFER_SIZE];
    const char *player_email;
    const char *victim_email;
    struct email_rate_limit limits;
    char error_buf[CURL_ERROR_SIZE];
    
    /* Check permissions */
    if (Guest(player)) {
        notify(player, "Guests cannot send email.");
        return;
    }
    
    /* Check message */
    if (!msg || !*msg) {
        notify(player, "You must specify a message.");
        return;
    }
    
    if (strlen(msg) > MAX_EMAIL_LENGTH) {
        notify(player, tprintf("Message too long. Maximum %d characters.", 
                              MAX_EMAIL_LENGTH));
        return;
    }
    
    /* Check rate limits */
    if (!check_rate_limit(player, &limits)) {
        if (time(NULL) - limits.last_email_time < EMAIL_COOLDOWN) {
            notify(player, tprintf("Please wait %ld seconds before sending another email.",
                                  EMAIL_COOLDOWN - (time(NULL) - limits.last_email_time)));
        } else {
            notify(player, tprintf("Daily email limit of %d reached. Try again tomorrow.",
                                  MAX_EMAILS_PER_DAY));
        }
        return;
    }
    
    /* Get sender's email from their EMAIL attribute */
    player_email = atr_get(player, A_LASTPAGE);  /* Use different attr for sender email */
    if (!player_email || !*player_email) {
        notify(player, "You must set your email address first. Use: &LASTPAGE me=your@email.com");
        return;
    }
    
    if (!validate_email_address(player_email)) {
        notify(player, "Your email address is invalid. Please set a valid address.");
        return;
    }
    
    strncpy(from_address, player_email, sizeof(from_address) - 1);
    from_address[sizeof(from_address) - 1] = '\0';
    
    /* Determine recipient */
    if (strchr(arg1, '@')) {
        /* Direct email address */
        if (!validate_email_address(arg1)) {
            notify(player, "Invalid email address format.");
            return;
        }
        strncpy(to_address, arg1, sizeof(to_address) - 1);
        to_address[sizeof(to_address) - 1] = '\0';
    } else {
        /* Player name - look up their email */
        victim = lookup_player((char *)arg1);
        if (victim == NOTHING) {
            notify(player, tprintf("No such player: %s", arg1));
            return;
        }
        
        victim_email = atr_get(victim, A_LASTPAGE);
        if (!victim_email || !*victim_email) {
            notify(player, tprintf("%s has no email address set.", db[victim].name));
            return;
        }
        
        if (!validate_email_address(victim_email)) {
            notify(player, tprintf("%s has an invalid email address.", db[victim].name));
            return;
        }
        
        strncpy(to_address, victim_email, sizeof(to_address) - 1);
        to_address[sizeof(to_address) - 1] = '\0';
    }
    
    /* Build subject and body */
    snprintf(subject, sizeof(subject), 
            "Message from %s on %s", db[player].name, muse_name);
    
    snprintf(body, sizeof(body),
        "You have received a message from %s (%s)\n"
        "====================================\n\n"
        "%s\n\n"
        "====================================\n"
        "This message was sent from the %s game server.\n"
        "To reply, use the email command in-game or reply to: %s\n",
        db[player].name, from_address, msg, muse_name, from_address);
    
    /* Send the email */
    notify(player, "Sending email...");
    
    error_buf[0] = '\0';
    if (send_email_curl(to_address, subject, body, from_address, 
                       error_buf, sizeof(error_buf))) {
        notify(player, "Email successfully sent!");
        
        /* Update rate limiting */
        update_rate_limit(player, &limits);
        
        /* Log the email */
        log_io(tprintf("EMAIL: %s (#%ld) to %s", db[player].name, player, to_address));
    } else {
        notify(player, "Failed to send email.");
        if (*error_buf) {
            notify(player, tprintf("Error: %s", error_buf));
        }
        log_error(tprintf("Email failed: %s to %s: %s",
                         db[player].name, to_address, 
                         *error_buf ? error_buf : "unknown error"));
    }
}

/**
 * Initialize email system
 * Call this once at startup
 */
void init_email_system(void)
{
    /* Initialize libcurl globally */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    log_important("Email system initialized");
}

/**
 * Cleanup email system
 * Call this at shutdown
 */
void cleanup_email_system(void)
{
    /* Cleanup libcurl */
    curl_global_cleanup();
    
    log_important("Email system shut down");
}
