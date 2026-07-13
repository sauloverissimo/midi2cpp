/*
 * ump_router.c: single-threaded ring buffer per source.
 *
 * The Pico SDK runs all TinyUSB host + device callbacks in the same
 * cooperative task context as the main loop (CFG_TUSB_OS=OPT_OS_NONE),
 * so push/pop never race. No locks, no FromISR variants.
 */
#include "ump_router.h"
#include <string.h>

typedef struct {
    uint32_t words[4];
    uint8_t  count;
} ump_entry_t;

typedef struct {
    ump_entry_t buf[UMP_ROUTER_QUEUE_LEN];
    uint16_t    head;   /* read index  */
    uint16_t    tail;   /* write index */
    uint16_t    fill;
    uint32_t    drops;
} ump_queue_t;

static ump_queue_t _queues[UMP_SOURCE_MAX];

void ump_router_init(void) {
    memset(_queues, 0, sizeof(_queues));
}

bool ump_router_push(ump_source_t src, const uint32_t* words, uint8_t count) {
    if (src >= UMP_SOURCE_MAX || count == 0 || count > 4) return false;
    ump_queue_t* q = &_queues[src];
    if (q->fill == UMP_ROUTER_QUEUE_LEN) {
        q->drops++;
        return false;
    }
    ump_entry_t* e = &q->buf[q->tail];
    memcpy(e->words, words, count * sizeof(uint32_t));
    e->count = count;
    q->tail = (uint16_t)((q->tail + 1) % UMP_ROUTER_QUEUE_LEN);
    q->fill++;
    return true;
}

bool ump_router_pop(ump_source_t src, uint32_t* out_words, uint8_t* out_count) {
    if (src >= UMP_SOURCE_MAX || !out_words || !out_count) return false;
    ump_queue_t* q = &_queues[src];
    if (q->fill == 0) return false;
    ump_entry_t* e = &q->buf[q->head];
    memcpy(out_words, e->words, e->count * sizeof(uint32_t));
    *out_count = e->count;
    q->head = (uint16_t)((q->head + 1) % UMP_ROUTER_QUEUE_LEN);
    q->fill--;
    return true;
}

void ump_router_count_drop(ump_source_t src) {
    if (src >= UMP_SOURCE_MAX) return;
    _queues[src].drops++;
}

uint32_t ump_router_drop_count(ump_source_t src) {
    if (src >= UMP_SOURCE_MAX) return 0;
    return _queues[src].drops;
}
