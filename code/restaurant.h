#ifndef __RESTAURANT_H__
#define __RESTAURANT_H__

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <semaphore.h>
#include <time.h>
#include "rng.h"

typedef struct ThreadArgs {
    int id;
    RNG rng;
} ThreadArgs;

typedef struct Resource {
	char name[64];
	int quantity;
	int clean_time;

	sem_t available;
    pthread_mutex_t mutex;

	bool acquired[100];
	int consecutive_dirty_uses[100];
} Resource;


typedef struct MenuDish {
	char name[64];
	int price;
	int cooking_time;
	char requirements[128];
} MenuDish;

typedef struct OrderedDish {
	MenuDish *dish_info;
	bool assigned;
	bool ready;
	bool served;
	int assigned_cook_id;
	void* parent_customer;
} OrderedDish;

typedef struct CookQueue {
    OrderedDish* dishes[100];
    int head;
    int tail;
    int count;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
} CookQueue;

typedef struct Customer {
	int customer_id;
	int initial_patience;
	int current_patience;
	pthread_mutex_t patience_mutex;
	OrderedDish *order_list;
	int total_dishes;
	int dishes_served;
	int total_price;
	int total_cooking_time;
	bool active;
	RNG rng;
	double spawn_time;
} Customer;

static inline double get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

#endif

