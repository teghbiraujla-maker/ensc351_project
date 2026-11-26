// Host-side test of the thread-safe input queue: order, capacity, the wake-up
// file descriptor, and pushes from another thread (like the encoder thread).
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include "input_queue.h"

static int failures = 0;

#define CHECK(condition, description) do { \
    int ok_ = (condition); \
    printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", description); \
    if (!ok_) failures++; } while (0)

static bool fd_readable(void)
{
    struct pollfd watch = { input_queue_fd(), POLLIN, 0 };
    return poll(&watch, 1, 0) == 1;
}

#define THREAD_PUSHES 1000

static void *pusher(void *arg)
{
    (void)arg;
    for (int i = 0; i < THREAD_PUSHES; i++) {
        control_event_t e = { CONTROL_TURN, 1 };
        while (!input_queue_push(e)) {
            // queue full: wait for the main thread to catch up
        }
    }
    return NULL;
}

int main(void)
{
    printf("Input queue\n");
    CHECK(input_queue_init(), "init");
    control_event_t e;
    CHECK(!input_queue_pop(&e), "empty queue: nothing to pop");
    CHECK(!fd_readable(), "empty queue: fd not readable");

    control_event_t a = { CONTROL_TURN, -1 }, b = { CONTROL_PRESS, 0 };
    input_queue_push(a);
    input_queue_push(b);
    CHECK(fd_readable(), "events waiting: fd readable");
    CHECK(input_queue_pop(&e) && e.type == CONTROL_TURN && e.steps == -1, "first in, first out (1)");
    CHECK(input_queue_pop(&e) && e.type == CONTROL_PRESS, "first in, first out (2)");
    CHECK(!fd_readable(), "drained: fd not readable again");

    int pushed = 0;
    while (input_queue_push(a)) {
        pushed++;
    }
    CHECK(pushed == INPUT_QUEUE_CAPACITY, "holds exactly INPUT_QUEUE_CAPACITY events, then refuses");
    while (input_queue_pop(&e)) {
    }

    pthread_t thread;
    pthread_create(&thread, NULL, pusher, NULL);
    int total = 0;
    while (total < THREAD_PUSHES) {
        struct pollfd watch = { input_queue_fd(), POLLIN, 0 };
        poll(&watch, 1, 100);
        while (input_queue_pop(&e)) {
            total += e.steps;
        }
    }
    pthread_join(thread, NULL);
    CHECK(total == THREAD_PUSHES, "1000 events pushed from another thread all arrive");

    input_queue_cleanup();
    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
