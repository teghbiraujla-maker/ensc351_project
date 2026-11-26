#include "app_log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static char lines[APP_LOG_CAPACITY][APP_LOG_LINE_CHARS];
static int first = 0;          // index of the oldest line
static int count = 0;
static unsigned long version = 0;
static bool echo = true;
static FILE *log_file = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

bool app_log_init(const char *log_file_path, bool echo_to_stdout)
{
    pthread_mutex_lock(&mutex);
    first = 0;
    count = 0;
    echo = echo_to_stdout;
    bool ok = true;
    if (log_file_path != NULL && log_file_path[0] != '\0') {
        log_file = fopen(log_file_path, "a");
        ok = (log_file != NULL);
    }
    pthread_mutex_unlock(&mutex);
    if (!ok) {
        fprintf(stderr, "app_log: cannot open log file '%s'\n", log_file_path);
    }
    return ok;
}

void app_log_cleanup(void)
{
    pthread_mutex_lock(&mutex);
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&mutex);
}

// Caller holds the mutex.
static void add_line_locked(const char *text, size_t length, bool echo_this)
{
    if (length >= APP_LOG_LINE_CHARS) {
        length = APP_LOG_LINE_CHARS - 1;
    }
    int slot;
    if (count < APP_LOG_CAPACITY) {
        slot = (first + count) % APP_LOG_CAPACITY;
        count++;
    } else {
        slot = first;                         // overwrite the oldest
        first = (first + 1) % APP_LOG_CAPACITY;
    }
    memcpy(lines[slot], text, length);
    lines[slot][length] = '\0';
    version++;

    if (echo_this) {
        printf("%s\n", lines[slot]);
        fflush(stdout);
    }
    if (log_file != NULL) {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        char stamp[16];
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
        fprintf(log_file, "%s %s\n", stamp, lines[slot]);
        fflush(log_file);
    }
}

static void add_text(const char *text, bool echo_this)
{
    pthread_mutex_lock(&mutex);
    const char *start = text;
    for (;;) {
        const char *newline = strchr(start, '\n');
        if (newline == NULL) {
            if (*start != '\0') {
                add_line_locked(start, strlen(start), echo_this);
            }
            break;
        }
        add_line_locked(start, (size_t)(newline - start), echo_this);
        start = newline + 1;
    }
    pthread_mutex_unlock(&mutex);
}

void app_log(const char *format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    add_text(text, echo);
}

void app_log_input(const char *typed_text)
{
    char text[APP_LOG_LINE_CHARS];
    snprintf(text, sizeof(text), "> %s", typed_text);
    add_text(text, false);
}

int app_log_line_count(void)
{
    pthread_mutex_lock(&mutex);
    int result = count;
    pthread_mutex_unlock(&mutex);
    return result;
}

bool app_log_get_line(int index, char *out, size_t out_size)
{
    pthread_mutex_lock(&mutex);
    bool ok = (index >= 0 && index < count);
    if (ok) {
        snprintf(out, out_size, "%s", lines[(first + index) % APP_LOG_CAPACITY]);
    }
    pthread_mutex_unlock(&mutex);
    return ok;
}

unsigned long app_log_version(void)
{
    pthread_mutex_lock(&mutex);
    unsigned long result = version;
    pthread_mutex_unlock(&mutex);
    return result;
}
