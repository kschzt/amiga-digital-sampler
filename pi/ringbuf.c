#include "ringbuf.h"
#include <stdlib.h>

int ringbuf_init(ringbuf_t *r, uint32_t size)
{
    // size must be power of 2
    if ((size & (size - 1)) != 0) return -1;

    r->buf = malloc(size);
    if (!r->buf) return -1;

    r->size = size;
    atomic_store(&r->write_idx, 0);
    atomic_store(&r->read_idx, 0);

    return 0;
}

void ringbuf_free(ringbuf_t *r)
{
    free(r->buf);
}
