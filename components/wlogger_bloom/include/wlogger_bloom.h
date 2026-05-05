#pragma once
#include <stdint.h>
#include <stddef.h>

#define BLOOM_BITS    (16 * 8 * 1024)   // 16 KB = 131072 bits
#define BLOOM_BYTES   (BLOOM_BITS / 8)
#define BLOOM_K       7

typedef struct {
    uint8_t  bits[BLOOM_BYTES];
    uint32_t inserted;
} bloom_t;

void     bloom_init(bloom_t *b);
void     bloom_add(bloom_t *b, const uint8_t mac[6]);
int      bloom_contains(const bloom_t *b, const uint8_t mac[6]);
uint32_t bloom_count_estimate(const bloom_t *b);
