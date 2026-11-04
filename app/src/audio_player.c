#include "audio_player.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PLAYER_PROGRAM       "mpg123"
#define DEFAULT_VOLUME       70
#define BANNER_TIMEOUT_SEC   3
#define SHUTDOWN_WAIT_MS     2000
#define ERROR_TEXT_CAPACITY  200
#define COMMAND_CAPACITY     (PATH_MAX + 32)

// ---- state shared between the caller's threads and the reader thread ----
static bool is_initialized = false;
static pid_t child_pid = -1;
static int child_stdin_fd = -1;
static FILE *child_output = NULL;
static pthread_t reader_thread_id;

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t state_changed = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;

static audio_status_t status;
static bool got_banner = false;
static bool backend_alive = false;
// True between sending LOAD and the player confirming it is playing. While
// set, the "stopped" message the player sends for the *previous* track is ignored.
static bool load_pending = false;
static char last_error[ERROR_TEXT_CAPACITY];

static void sleep_ms(int milliseconds)
{
    struct timespec delay = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };
    nanosleep(&delay, NULL);
}

// Copy `text` into last_error without the trailing newline. Call with state_mutex held.
static void set_last_error_locked(const char *text)
{
    snprintf(last_error, sizeof(last_error), "%s", text);
    size_t length = strlen(last_error);
    while (length > 0 && isspace((unsigned char)last_error[length - 1])) {
        last_error[--length] = '\0';
    }
}

static void set_last_error(const char *text)
{
    pthread_mutex_lock(&state_mutex);
    set_last_error_locked(text);
    pthread_mutex_unlock(&state_mutex);
}

// ---------------------------------------------------------------------------
// Reading the player's status messages. mpg123's remote-mode lines include:
//   @R <banner>              player is ready
//   @P 0|1|2                 stopped / paused / playing
//   @F frame left secs left  progress (seconds so far, seconds remaining)
//   @V 70.000000%            volume
//   @E <message>             error
// Anything not starting with '@' is diagnostic text (e.g. ALSA errors).
// ---------------------------------------------------------------------------
static void handle_line(const char *line)
{
    pthread_mutex_lock(&state_mutex);

    if (strncmp(line, "@R ", 3) == 0) {
        got_banner = true;
        pthread_cond_broadcast(&state_changed);
    } else if (strncmp(line, "@P ", 3) == 0) {
        int code = -1;
        if (sscanf(line + 3, "%d", &code) == 1) {
            if (code == 2) {
                status.state = AUDIO_PLAYING;
                load_pending = false;
            } else if (code == 1) {
                status.state = AUDIO_PAUSED;
            } else if (code == 0 && !load_pending) {
                status.state = AUDIO_STOPPED;
            }
        }
    } else if (strncmp(line, "@F ", 3) == 0) {
        double seconds = 0.0;
        double seconds_left = 0.0;
        if (sscanf(line + 3, "%*d %*d %lf %lf", &seconds, &seconds_left) == 2) {
            status.position_sec = seconds;
            status.duration_sec = seconds + seconds_left;
        }
    } else if (strncmp(line, "@V ", 3) == 0) {
        double volume = 0.0;
        if (sscanf(line + 3, "%lf", &volume) == 1) {
            status.volume_percent = (int)(volume + 0.5);
        }
    } else if (strncmp(line, "@E ", 3) == 0) {
        set_last_error_locked(line + 3);
        load_pending = false;
    } else if (line[0] != '@' && line[0] != '\n' && line[0] != '\0') {
        set_last_error_locked(line);
    }

    pthread_mutex_unlock(&state_mutex);
}

static void *reader_thread(void *arg)
{
    (void)arg;
    char *line = NULL;
    size_t capacity = 0;

    while (getline(&line, &capacity, child_output) != -1) {
        handle_line(line);
    }
    free(line);

    // The player has exited (normally or not).
    pthread_mutex_lock(&state_mutex);
    backend_alive = false;
    status.state = AUDIO_STOPPED;
    pthread_cond_broadcast(&state_changed);
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

// ---------------------------------------------------------------------------
// Sending commands
// ---------------------------------------------------------------------------
static bool send_command(const char *format, ...)
{
    char command[COMMAND_CAPACITY];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(command, sizeof(command) - 1, format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof(command) - 1) {
        return false;
    }
    command[length++] = '\n';

    pthread_mutex_lock(&write_mutex);
    bool ok = (child_stdin_fd >= 0);
    int written_total = 0;
    while (ok && written_total < length) {
        ssize_t written = write(child_stdin_fd, command + written_total, (size_t)(length - written_total));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;   // e.g. EPIPE: the player has exited
        } else {
            written_total += (int)written;
        }
    }
    pthread_mutex_unlock(&write_mutex);
    return ok;
}

static bool backend_is_alive(void)
{
    pthread_mutex_lock(&state_mutex);
    bool alive = backend_alive;
    pthread_mutex_unlock(&state_mutex);
    return alive;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
bool audio_player_init(const char *alsa_device)
{
    assert(!is_initialized);
    const char *device = (alsa_device != NULL && alsa_device[0] != '\0') ? alsa_device : "default";

    // Writing to a pipe whose reader has exited must return EPIPE, not kill us.
    struct sigaction ignore_pipe;
    memset(&ignore_pipe, 0, sizeof(ignore_pipe));
    ignore_pipe.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ignore_pipe, NULL);

    int to_child[2];     // we write -> player's stdin
    int from_child[2];   // player's stdout+stderr -> we read
    if (pipe(to_child) != 0) {
        perror("audio_player: pipe");
        return false;
    }
    if (pipe(from_child) != 0) {
        perror("audio_player: pipe");
        close(to_child[0]);
        close(to_child[1]);
        return false;
    }

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        perror("audio_player: fork");
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return false;
    }
    if (pid == 0) {
        // Child: become the player.
        setpgid(0, 0);                         // own process group: Ctrl-C in the terminal won't hit it
        // Never outlive the parent, even if it is killed outright (kill -9, crash,
        // dropped SSH session) - otherwise the orphan would keep the sound card busy.
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent_pid) {
            _exit(1);                          // the parent died before the prctl took effect
        }
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        char *const arguments[] = {
            (char *)PLAYER_PROGRAM, (char *)"-R", (char *)"-o", (char *)"alsa",
            (char *)"-a", (char *)device, NULL
        };
        execvp(PLAYER_PROGRAM, arguments);
        _exit(127);                            // exec failed (not installed?)
    }

    // Parent
    setpgid(pid, pid);                         // (also done in the child; avoids a race)
    close(to_child[0]);
    close(from_child[1]);
    child_pid = pid;
    child_stdin_fd = to_child[1];
    child_output = fdopen(from_child[0], "r");
    if (child_output == NULL) {
        perror("audio_player: fdopen");
        close(from_child[0]);
        close(child_stdin_fd);
        child_stdin_fd = -1;
        kill(-pid, SIGKILL);
        waitpid(pid, NULL, 0);
        child_pid = -1;
        return false;
    }

    memset(&status, 0, sizeof(status));
    status.state = AUDIO_STOPPED;
    status.volume_percent = 100;
    got_banner = false;
    backend_alive = true;
    load_pending = false;
    last_error[0] = '\0';

    if (pthread_create(&reader_thread_id, NULL, reader_thread, NULL) != 0) {
        fprintf(stderr, "audio_player: cannot start reader thread\n");
        close(child_stdin_fd);
        child_stdin_fd = -1;
        kill(-pid, SIGKILL);
        waitpid(pid, NULL, 0);
        fclose(child_output);
        child_output = NULL;
        child_pid = -1;
        return false;
    }

    // Wait for the player's "ready" banner (or for it to die).
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += BANNER_TIMEOUT_SEC;
    pthread_mutex_lock(&state_mutex);
    while (!got_banner && backend_alive) {
        if (pthread_cond_timedwait(&state_changed, &state_mutex, &deadline) != 0) {
            break;
        }
    }
    bool ready = got_banner && backend_alive;
    pthread_mutex_unlock(&state_mutex);

    is_initialized = true;   // so cleanup() below is allowed
    if (!ready) {
        fprintf(stderr, "audio_player: could not start '%s'.\n", PLAYER_PROGRAM);
        fprintf(stderr, "              Is it installed?  sudo apt install mpg123\n");
        audio_player_cleanup();
        return false;
    }

    audio_player_set_volume(DEFAULT_VOLUME);
    return true;
}

void audio_player_cleanup(void)
{
    assert(is_initialized);

    send_command("QUIT");
    pthread_mutex_lock(&write_mutex);
    close(child_stdin_fd);
    child_stdin_fd = -1;
    pthread_mutex_unlock(&write_mutex);

    // Give the player a moment to exit on its own, then force it.
    bool exited = false;
    for (int waited = 0; waited < SHUTDOWN_WAIT_MS && !exited; waited += 20) {
        if (waitpid(child_pid, NULL, WNOHANG) == child_pid) {
            exited = true;
        } else {
            sleep_ms(20);
        }
    }
    if (!exited) {
        kill(-child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);
    }

    pthread_join(reader_thread_id, NULL);   // ends when the pipe reaches EOF
    fclose(child_output);
    child_output = NULL;
    child_pid = -1;
    is_initialized = false;
}

bool audio_player_play(const char *mp3_path)
{
    assert(is_initialized);
    if (mp3_path == NULL || access(mp3_path, R_OK) != 0) {
        set_last_error("Cannot read that audio file.");
        return false;
    }
    if (!backend_is_alive()) {
        set_last_error("The audio player is not running.");
        return false;
    }

    pthread_mutex_lock(&state_mutex);
    load_pending = true;
    last_error[0] = '\0';
    status.position_sec = 0.0;
    status.duration_sec = 0.0;
    status.state = AUDIO_PLAYING;       // confirmed (or corrected) by the player's reply
    pthread_mutex_unlock(&state_mutex);

    if (!send_command("LOAD %s", mp3_path)) {
        pthread_mutex_lock(&state_mutex);
        load_pending = false;
        status.state = AUDIO_STOPPED;
        set_last_error_locked("Could not talk to the audio player.");
        pthread_mutex_unlock(&state_mutex);
        return false;
    }
    return true;
}

bool audio_player_toggle_pause(void)
{
    assert(is_initialized);
    pthread_mutex_lock(&state_mutex);
    audio_state_t state = status.state;
    pthread_mutex_unlock(&state_mutex);

    if (state == AUDIO_STOPPED || !send_command("PAUSE")) {
        return false;
    }
    pthread_mutex_lock(&state_mutex);
    status.state = (state == AUDIO_PLAYING) ? AUDIO_PAUSED : AUDIO_PLAYING;
    pthread_mutex_unlock(&state_mutex);
    return true;
}

void audio_player_stop(void)
{
    assert(is_initialized);
    pthread_mutex_lock(&state_mutex);
    load_pending = false;
    pthread_mutex_unlock(&state_mutex);
    if (send_command("STOP")) {
        pthread_mutex_lock(&state_mutex);
        status.state = AUDIO_STOPPED;
        pthread_mutex_unlock(&state_mutex);
    }
}

void audio_player_set_volume(int percent)
{
    assert(is_initialized);
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (send_command("VOLUME %d", percent)) {
        pthread_mutex_lock(&state_mutex);
        status.volume_percent = percent;
        pthread_mutex_unlock(&state_mutex);
    }
}

void audio_player_adjust_volume(int delta_percent)
{
    assert(is_initialized);
    pthread_mutex_lock(&state_mutex);
    int target = status.volume_percent + delta_percent;
    pthread_mutex_unlock(&state_mutex);
    audio_player_set_volume(target);
}

void audio_player_seek_relative(int seconds)
{
    assert(is_initialized);
    send_command("JUMP %+ds", seconds);
}

audio_status_t audio_player_get_status(void)
{
    assert(is_initialized);
    pthread_mutex_lock(&state_mutex);
    audio_status_t copy = status;
    pthread_mutex_unlock(&state_mutex);
    return copy;
}

void audio_player_get_last_error(char *out, size_t out_size)
{
    assert(is_initialized);
    pthread_mutex_lock(&state_mutex);
    snprintf(out, out_size, "%s", last_error);
    pthread_mutex_unlock(&state_mutex);
}
