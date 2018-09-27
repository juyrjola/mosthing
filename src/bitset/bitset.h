#ifndef _BITSET_H
#define _BITSET_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct bitset {
    uint16_t max_bits;
    uint16_t n_bits;
    uint8_t data[];
};

static inline void bitset_set(struct bitset *bitset, int idx)
{
    assert(idx <= bitset->max_bits);
    bitset->data[idx >> 3] |= 1 << (idx & 0x07);
    if (bitset->n_bits < idx + 1)
        bitset->n_bits = idx + 1;
}

static inline void bitset_unset(struct bitset *bitset, int idx)
{
    assert(idx <= bitset->max_bits);
    bitset->data[idx >> 3] &= ~(1 << (idx & 0x07));
    if (bitset->n_bits < idx + 1)
        bitset->n_bits = idx + 1;
}

static inline void bitset_append(struct bitset *bitset, bool value)
{
    if (value)
        bitset_set(bitset, bitset->n_bits);
    else
        bitset_unset(bitset, bitset->n_bits);
}

static inline struct bitset * bitset_init(void *buf, size_t buf_len)
{
    struct bitset *bitset = (struct bitset *) buf;

    buf_len -= sizeof(struct bitset);

    bitset->n_bits = 0;
    bitset->max_bits = buf_len << 3;
    memset(bitset->data, 0, buf_len);

    return bitset;     
}

static inline struct bitset * bitset_alloc(int max_bits)
{
    int bytes = (max_bits + 7) >> 3;
    void *buf;

    buf = malloc(sizeof(struct bitset) + bytes);
    assert(buf != NULL);
    return bitset_init(buf, bytes);
}

static inline bool bitset_get(const struct bitset *bitset, int idx)
{
    assert(idx < bitset->n_bits);
    return bitset->data[idx >> 3] & (1 << (idx & 0x07)) ? true : false;
}

static inline void bitset_copy(struct bitset *dest, const struct bitset *src)
{
    assert(dest->max_bits >= src->n_bits);
    dest->n_bits = src->n_bits;
    memcpy(dest->data, src->data, (src->n_bits + 7) / 8);
}

static inline struct bitset * bitset_dup(const struct bitset *src)
{
    struct bitset *dest = bitset_alloc(src->n_bits);

    bitset_copy(dest, src);
    return dest;
}

static inline void bitset_print(char *out, const struct bitset *bitset)
{
    for (int idx = 0; idx < bitset->n_bits; idx++) {
        if ((idx % 8) == 0 && idx)
            *out++ = ' ';
        *out++ = bitset_get(bitset, idx) ? '1' : '0';
    }
    *out = '\0';
}

#endif
