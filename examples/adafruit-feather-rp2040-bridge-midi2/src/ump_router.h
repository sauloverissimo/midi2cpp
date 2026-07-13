/*
 * ump_router.h: single-threaded ring buffer for forwarding UMP between
 * the bridge's two USB stacks. Modeled after a FreeRTOS-backed router
 * used in earlier dual-stack production firmware, simplified for
 * RP2040 bare metal: no locks, drain 1 message per main-loop
 * iteration to avoid saturating the destination TX FIFO.
 *
 * One queue per source. The drain function pulls one message at a time
 * (1..4 UMP words) and hands it to the platform-specific writer.
 */
#ifndef UMP_ROUTER_H_
#define UMP_ROUTER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of UMP messages each queue can hold. 64 covers a noteOn flurry
 * from a controller without dropping while the bridge drains. */
#ifndef UMP_ROUTER_QUEUE_LEN
#define UMP_ROUTER_QUEUE_LEN 64
#endif

typedef enum {
    UMP_SOURCE_HOST   = 0,   /* upstream device → forward to USB-C        */
    UMP_SOURCE_DEVICE = 1,   /* DAW/PC          → forward to USB-A device */
    UMP_SOURCE_MAX
} ump_source_t;

void     ump_router_init(void);

/* Push a UMP message (1..4 words). count==0 or >4 is a no-op. Returns
 * true if accepted, false on queue full (drop_count is incremented). */
bool     ump_router_push(ump_source_t src, const uint32_t* words, uint8_t count);

/* Drain ONE message from the given source if available. Writes count of
 * words produced into *out_count and the words into out_words[0..3].
 * Returns true if a message was popped. */
bool     ump_router_pop(ump_source_t src, uint32_t* out_words, uint8_t* out_count);

uint32_t ump_router_drop_count(ump_source_t src);

/* Count a drop that happened past the queue (e.g. a bounded TX retry that
 * exhausted); folds into the same per-source counter. */
void     ump_router_count_drop(ump_source_t src);

#ifdef __cplusplus
}
#endif

#endif
