#include "rng.h"

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;

    return z ^ (z >> 31);
}

void rng_init(RNG *rng, uint64_t *master_seed) {
    rng->s[0] = splitmix64_next(master_seed);
    rng->s[1] = splitmix64_next(master_seed);
    rng->s[2] = splitmix64_next(master_seed);
    rng->s[3] = splitmix64_next(master_seed);

    if (
        rng->s[0] == 0 &&
        rng->s[1] == 0 &&
        rng->s[2] == 0 &&
        rng->s[3] == 0
    ) {
        rng->s[0] = 1;
    }
}

uint64_t rng_next(RNG *rng) {
    const uint64_t result = rotl(rng->s[0] + rng->s[3], 23) + rng->s[0];
    const uint64_t t = rng->s[1] << 17;

    rng->s[2] ^= rng->s[0];
    rng->s[3] ^= rng->s[1];
    rng->s[1] ^= rng->s[2];
    rng->s[0] ^= rng->s[3];

    rng->s[2] ^= t;
    rng->s[3] = rotl(rng->s[3], 45);

    return result;
}

int rng_range(RNG *rng, int min, int max) {
    if (max < min) return min;
    return min + (int)(rng_next(rng) % (uint64_t)(max - min + 1));
}

double rng_double(RNG *rng) {
    return (rng_next(rng) >> 11) * (1.0 / 9007199254740992.0);
}
