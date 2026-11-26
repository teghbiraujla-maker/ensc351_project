#include "input_queue.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

// Ring buffer protected by a mutex. A self-pipe signals "events waiting":
// one byte is written per push and one byte is read per pop, so the pipe is
// readable exactly while the queue is non-empty.

static control_event_t buffer[INPUT_QUEUE_CAPACITY];
static int head = 0;    // next slot to read
static int count = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int wake_pipe[2] = {-1, -1};

bool input_queue_init(void)
{
    assert(wake_pipe[0] < 0);
    if (pipe(wake_pipe) != 0) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(wake_pipe[i], F_GETFL);
        fcntl(wake_pipe[i], F_SETFL, flags | O_NONBLOCK);
    }
    head = 0;
    count = 0;
    return true;
}

void input_queue_cleanup(void)
{
    for (int i = 0; i < 2; i++) {
        if (wake_pipe[i] >= 0) {
            close(wake_pipe[i]);
            wake_pipe[i] = -1;
        }
    }
}

bool input_queue_push(control_event_t event)
{
    pthread_mutex_lock(&mutex);
    bool ok = (count < INPUT_QUEUE_CAPACITY && wake_pipe[1] >= 0);
    if (ok) {
        buffer[(head + count) % INPUT_QUEUE_CAPACITY] = event;
        count++;
        char byte = 1;
        ssize_t ignored = write(wake_pipe[1], &byte, 1);
        (void)ignored;   // capacity 64 bytes << pipe size, so this cannot fill up
    }
    pthread_mutex_unlock(&mutex);
    return ok;
}

bool input_queue_pop(control_event_t *event)
{
    pthread_mutex_lock(&mutex);
    bool ok = (count > 0);
    if (ok) {
        *event = buffer[head];
        head = (head + 1) % INPUT_QUEUE_CAPACITY;
        count--;
        char byte;
        ssize_t ignored = read(wake_pipe[0], &byte, 1);
        (void)ignored;
    }
    pthread_mutex_unlock(&mutex);
    return ok;
}

int input_queue_fd(void)
{
    return wake_pipe[0];
}
