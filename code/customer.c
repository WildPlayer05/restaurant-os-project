#include "restaurant.h"
#include "error_codes.h"
#include "rng.h"
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#define MIN_DISHES 1
#define MAX_DISHES 4
#define MIN_BONUS_PATIENCE 10
#define MAX_BONUS_PATIENCE 50

extern MenuDish menu[];
extern int menu_items_count;
extern int game_speed;
extern int random_seed;
extern int score;
extern int current_customers_room;
extern int global_customers_completed;
extern int unserved_customers;

extern pthread_mutex_t score_mutex;
extern pthread_mutex_t room_mutex;
extern sem_t restaurant_remaining_capacity;

void init(Customer* self) {
    self->total_price = 0;
    self->total_cooking_time = 0;
    self->dishes_served = 0;
    self->spawn_time = get_current_time();
    //generate the number of dishes
    self->total_dishes = rng_range(&self->rng, MIN_DISHES, MAX_DISHES);
	self->order_list = (OrderedDish*) malloc(self->total_dishes * sizeof(OrderedDish));
    if (!self->order_list) {
        perror("Allocation error for customer order_list");
        exit(ERR_ALLOC_FAILED);
    }
    int i;
    for (i = 0; i < self->total_dishes; i++) {
        int index_dish = rng_range(&self->rng, 0, menu_items_count - 1);
		self->order_list[i].dish_info = &menu[index_dish];
        self->total_price += menu[index_dish].price;
        self->total_cooking_time += menu[index_dish].cooking_time;
        self->order_list[i].assigned = false;
        self->order_list[i].ready = false;
        self->order_list[i].served = false;
        self->order_list[i].assigned_cook_id = -1;
        self->order_list[i].parent_customer = self;
    }
    //set initial patience
    int random_bonus = rng_range(&self->rng, MIN_BONUS_PATIENCE, MAX_BONUS_PATIENCE);
	self->initial_patience = self->total_cooking_time + random_bonus;
    self->current_patience = self->initial_patience;
    pthread_mutex_lock(&self->patience_mutex);

    self->active = true;

    pthread_mutex_unlock(&self->patience_mutex);
}

void wait(Customer* self) {
    while (1) {
        usleep((useconds_t)(60 * 1000000.0 / game_speed));
        pthread_mutex_lock(&self->patience_mutex);
        
		self->current_patience--;
        //exit
        if (self->current_patience <= 0 || self->dishes_served >= self->total_dishes) {
            self->active = false;
            pthread_mutex_unlock(&self->patience_mutex);
            break;
        }
        
        pthread_mutex_unlock(&self->patience_mutex);
    }
}

double calculate_time_to_serve(double spawn_time_seconds) {
    double now = get_current_time();
    double elapsed_virtual_minutes = ((now - spawn_time_seconds) * game_speed) / 60.0;
    return elapsed_virtual_minutes;
}

void update_score(Customer* self) {
    double time_to_serve = calculate_time_to_serve(self->spawn_time);
    pthread_mutex_lock(&score_mutex);
    
    if (self->dishes_served == self->total_dishes) {
    	//to contribute POSITIVELY
		if (time_to_serve > (double)self->initial_patience) {
            time_to_serve = (double)self->initial_patience;
        }
        //customer satisfied
       double val = (double)self->total_price * (1.0 - (time_to_serve / (double)self->initial_patience));
       score += (int)ceil(val);
    } else {
        //customer unsatisfied
        double val = (double)self->total_price * log2(1.0 + ((double)self->initial_patience / (1.0 + (double)self->dishes_served)));
        score -= (int)ceil(val);
        unserved_customers++;
    }
    
    pthread_mutex_unlock(&score_mutex);
	pthread_mutex_lock(&room_mutex);
    
	current_customers_room--;
    global_customers_completed++;
    
	pthread_mutex_unlock(&room_mutex);
    //free up a seat
    sem_post(&restaurant_remaining_capacity);
}

void* customer_thread(void* arg) {
    Customer* self = (Customer*) arg;
    
    //customer initialization
    init(self);

    //wait for the dishes
    wait(self);

    //update the score and exit
    update_score(self);

    pthread_exit(NULL);
}
