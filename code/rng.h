#ifndef RNG_H
#define RNG_H

#include <stdint.h>

typedef struct RNG {
    uint64_t s[4];
} RNG;

//init SplitMix64
uint64_t splitmix64_next(uint64_t *state);

//init xoshiro256++
void rng_init(RNG *rng, uint64_t *master_seed);

uint64_t rng_next(RNG *rng);

int rng_range(RNG *rng, int min, int max);

double rng_double(RNG *rng);

#endif

