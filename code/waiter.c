#include "restaurant.h"
#include "error_codes.h"
#include "rng.h"
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#define MAX_BONUS 2
#define MIN_MALUS -1

extern int num_cooks;
extern int game_speed;
extern int total_customers;
extern int global_customers_completed;
extern Customer* global_customers;
extern CookQueue* global_cook_queues;

extern pthread_mutex_t room_mutex;

int get_cook_id(){
	int min_dishes = 99999;
    int selected_cook_id = 0;
    
	int i;
    for (i = 0; i < num_cooks; i++) {
        pthread_mutex_lock(&global_cook_queues[i].queue_mutex);
        //select the cook with the least work
        if (global_cook_queues[i].count < min_dishes) {
            min_dishes = global_cook_queues[i].count;
            selected_cook_id = i;
        }
        pthread_mutex_unlock(&global_cook_queues[i].queue_mutex);
    }
    return selected_cook_id;
}

bool dishes_split(Customer* cust){
	bool worked = false;
    int i;
	for (i = 0; i < cust->total_dishes; i++) {
        if (!cust->order_list[i].assigned) {
            int target_cook = get_cook_id();
            CookQueue* q = &global_cook_queues[target_cook];
            pthread_mutex_lock(&q->queue_mutex);
            
            if (q->count < 100) {
                q->dishes[q->tail] = &cust->order_list[i];
                q->tail = (q->tail + 1) % 100;
                q->count++;
                cust->order_list[i].assigned = true; 
                cust->order_list[i].assigned_cook_id = target_cook;
                
                pthread_cond_signal(&q->queue_cond);
                worked = true;
            }
            
            pthread_mutex_unlock(&q->queue_mutex);
        }
    }
    return worked;
}

bool dishes_single(Customer* cust){
	bool worked = false;
    int target_cook = get_cook_id();
    CookQueue* q = &global_cook_queues[target_cook];
    pthread_mutex_lock(&q->queue_mutex);
    
    int i;
	for (i = 0; i < cust->total_dishes; i++) {
        if (!cust->order_list[i].assigned) {
            if (q->count < 100) {
                q->dishes[q->tail] = &cust->order_list[i];
                q->tail = (q->tail + 1) % 100;
                q->count++;
                cust->order_list[i].assigned = true; 
                cust->order_list[i].assigned_cook_id = target_cook;
                worked = true;
            } else {
                break;
            }                
        }
    }
    if (worked) {
        pthread_cond_signal(&q->queue_cond);
    }
    
    pthread_mutex_unlock(&q->queue_mutex);
    return worked;
}

bool assign_dishes(){
	bool worked = false;
	int i;
	for(i = 0; i < total_customers; i++){
		Customer* cust = &global_customers[i];
		pthread_mutex_lock(&cust->patience_mutex);
		
		if(cust->active){
			bool dishes_to_assign = false;
			int j;
			for(j = 0; j < cust->total_dishes; j++){
				if(!cust->order_list[j].assigned){
					dishes_to_assign = true;
					break;
				}
			}
			if(dishes_to_assign){
				bool split_dishes = false;
				if (cust->current_patience < (cust->total_cooking_time * 1.5) || cust->total_price > 40) {
					split_dishes = true;
				}
				if (split_dishes){
					if(dishes_split(cust)) worked = true;
				} else {
					if(dishes_single(cust)) worked = true;
				}
			}
		}
		
		pthread_mutex_unlock(&cust->patience_mutex);
	}
	return worked;
}

bool delivery(){
	bool worked = false;
    int i;
	for (i = 0; i < total_customers; i++) {
        Customer* cust = &global_customers[i];
        pthread_mutex_lock(&cust->patience_mutex);
        
        if (cust->active) {
            int j;
			for (j = 0; j < cust->total_dishes; j++) {
                if (cust->order_list[j].ready && !cust->order_list[j].served) {
                    cust->order_list[j].served = true;
                    cust->dishes_served++;
                    worked = true;
                }
            }
        }
        
        pthread_mutex_unlock(&cust->patience_mutex);
    }
    return worked;
}

void entertain(ThreadArgs* args){
	if (rng_range(&args->rng, 0, 99) < 20) {
        int random_idx = rng_range(&(args->rng), 0, total_customers - 1);
        Customer* cust = &global_customers[random_idx];
        pthread_mutex_lock(&cust->patience_mutex);
        
        if (cust->active && cust->dishes_served < cust->total_dishes) {
            int bonus_malus = rng_range(&(args->rng), MIN_MALUS, MAX_BONUS);
            cust->current_patience += bonus_malus;
            if (cust->current_patience < 0) {
                cust->current_patience = 0;
            }
        }
        
        pthread_mutex_unlock(&cust->patience_mutex);
    }
}

void* waiter_thread(void* arg){
	if (!arg) {
        perror("Waiter thread received NULL argument");
        exit(ERR_SYS_FAILURE);
    }
    ThreadArgs* args = (ThreadArgs*) arg;
	bool worked;
	while(1){
		pthread_mutex_lock(&room_mutex);
		
		if (global_customers_completed >= total_customers){
			pthread_mutex_unlock(&room_mutex);
			break;
		}
		
		pthread_mutex_unlock(&room_mutex);
		worked = false;
		//assigning dishes to the cook
		if(assign_dishes()) worked = true;
		//delivery
		if(delivery()) worked = true;
		//entertain
		if(!worked){
			entertain(args);
		}
		usleep((useconds_t)(1000000.0 / game_speed));
	}
	pthread_exit(NULL);
}
