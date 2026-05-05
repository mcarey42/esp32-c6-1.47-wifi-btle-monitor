#include "wlogger_bloom.h"
#include <string.h>
#include <math.h>

static uint32_t fnv1a(const uint8_t *p, size_t n, uint32_t seed) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x01000193u; }
    /* mix seed into the finalized hash so different seeds give well-separated hashes */
    h ^= seed;
    h *= 0x01000193u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

void bloom_init(bloom_t *b) {
    memset(b->bits, 0, BLOOM_BYTES);
    b->inserted = 0;
}

static void positions(const uint8_t mac[6], uint32_t out[BLOOM_K]) {
    uint32_t h1 = fnv1a(mac, 6, 0);
    uint32_t h2 = fnv1a(mac, 6, 0xDEADBEEF);
    for (int i = 0; i < BLOOM_K; ++i)
        out[i] = (h1 + (uint32_t)i * h2) % BLOOM_BITS;
}

void bloom_add(bloom_t *b, const uint8_t mac[6]) {
    uint32_t pos[BLOOM_K];
    positions(mac, pos);
    int already = 1;
    for (int i = 0; i < BLOOM_K; ++i) {
        uint32_t p = pos[i];
        if (!(b->bits[p >> 3] & (1u << (p & 7)))) {
            already = 0;
            b->bits[p >> 3] |= (1u << (p & 7));
        }
    }
    if (!already) b->inserted++;
}

int bloom_contains(const bloom_t *b, const uint8_t mac[6]) {
    uint32_t pos[BLOOM_K];
    positions(mac, pos);
    for (int i = 0; i < BLOOM_K; ++i) {
        uint32_t p = pos[i];
        if (!(b->bits[p >> 3] & (1u << (p & 7)))) return 0;
    }
    return 1;
}

uint32_t bloom_count_estimate(const bloom_t *b) {
    uint32_t set = 0;
    for (size_t i = 0; i < BLOOM_BYTES; ++i)
        for (int j = 0; j < 8; ++j) if (b->bits[i] & (1u << j)) set++;
    if (set == 0) return 0;
    double m = (double)BLOOM_BITS, k = (double)BLOOM_K, X = (double)set;
    if (X >= m) return b->inserted;
    double n = -(m / k) * log(1.0 - X / m);
    return (uint32_t)(n + 0.5);
}
