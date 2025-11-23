#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdatomic.h>

typedef struct {
    uint8_t *buf;
    uint32_t size;     // must be power of 2
    atomic_uint write_idx;
    atomic_uint read_idx;
} ringbuf_t;

int ringbuf_init(ringbuf_t *r, uint32_t size);
void ringbuf_free(ringbuf_t *r);

static inline uint32_t ringbuf_mask(const ringbuf_t *r) {
    return r->size - 1;
}

// SPSC, lock-free
static inline int ringbuf_push(ringbuf_t *r, uint8_t v)
{
    uint32_t w = atomic_load_explicit(&r->write_idx, memory_order_relaxed);
    uint32_t r_i = atomic_load_explicit(&r->read_idx, memory_order_acquire);

    if (((w + 1) & ringbuf_mask(r)) == r_i)
        return 0; // full

    r->buf[w] = v;
    atomic_store_explicit(&r->write_idx, (w + 1) & ringbuf_mask(r), memory_order_release);
    return 1;
}

static inline int ringbuf_pop(ringbuf_t *r, uint8_t *v)
{
    uint32_t r_i = atomic_load_explicit(&r->read_idx, memory_order_relaxed);
    uint32_t w_i = atomic_load_explicit(&r->write_idx, memory_order_acquire);

    if (r_i == w_i)
        return 0; // empty

    *v = r->buf[r_i];
    atomic_store_explicit(&r->read_idx, (r_i + 1) & ringbuf_mask(r), memory_order_release);
    return 1;
}

#endif
