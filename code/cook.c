#include "restaurant.h"
#include "error_codes.h"
#include "rng.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_RESOURCE_TYPES 20
#define MAX_ITEMS_PER_RESOURCE 20

extern Resource ResourcesPool[];
extern int game_speed;
extern int score;
extern int resource_types_count;
extern pthread_mutex_t sink;
extern pthread_mutex_t score_mutex;
extern CookQueue* global_cook_queues;

void updateScore(int value) {
    pthread_mutex_lock(&score_mutex);
    score += value;
    pthread_mutex_unlock(&score_mutex);
}

void useDirty(int type, int number) {
    int penalty = -1* ceil(pow(2, ResourcesPool[type].consecutive_dirty_uses[number]) * log2(1 + ResourcesPool[type].clean_time));
    updateScore(penalty);
}

int cleanBeforeCooking(int i, int j) {
    if (pthread_mutex_trylock(&sink) == 0) {
        usleep((useconds_t)(60 * 1000000 * ResourcesPool[i].clean_time / game_speed));
        ResourcesPool[i].consecutive_dirty_uses[j] = 0;

        pthread_mutex_unlock(&sink);
        return 0;
    }

    useDirty(i, j);

    return 1;
}

int cleanWhenIdle() {
    int k = -1;
    int l = -1;
    
    //Search for available dirty dishes
    int i;
	for (i = 0; i < resource_types_count; i++) {
        if (pthread_mutex_trylock(&ResourcesPool[i].mutex) == 0) {
            int j;
			for (j = 0; j < ResourcesPool[i].quantity; j++) {
                if (!ResourcesPool[i].acquired[j] && (ResourcesPool[i].consecutive_dirty_uses[j] > 0)) {
                
                    if (sem_trywait(&ResourcesPool[i].available) == 0) {
                        ResourcesPool[i].acquired[j] = true;
                        k = i;
                        l = j;
                        break;
                    }
                }
            }

            pthread_mutex_unlock(&ResourcesPool[i].mutex);
        }

        if (k != -1) break;
    }

    //Return code 1 if no dirty dishes are found
    if (k == -1) {
        return 1;
    }

    //Try to wash the dish, skip if sink is being used in order to allow it to do other works
    if (pthread_mutex_trylock(&sink) == 0) {
        usleep((useconds_t)(60 * 1000000 * ResourcesPool[k].clean_time / game_speed));
        ResourcesPool[k].consecutive_dirty_uses[l] = 0;

        pthread_mutex_unlock(&sink);

    }

    pthread_mutex_lock(&ResourcesPool[k].mutex);
    
    ResourcesPool[k].acquired[l] = false;
    sem_post(&ResourcesPool[k].available);
    
    pthread_mutex_unlock(&ResourcesPool[k].mutex);

    return 0;
}

int cleanAfterCooking(int i, int j) {
    if (pthread_mutex_trylock(&sink) == 0) {
        usleep((useconds_t)(60 * 1000000 * ResourcesPool[i].clean_time / game_speed));
        ResourcesPool[i].consecutive_dirty_uses[j] = 0;

        pthread_mutex_unlock(&sink);

        return 0;
    }

    ResourcesPool[i].consecutive_dirty_uses[j]++;
    
    return 1;
}

void releaseNotAvailable(int k, int l, int resources_count[]) {
    int i;
	for (i = 0; i <= k; i++) {
        if (resources_count[i] == 0) continue;
        int count = 0;
		int j;
        for (j = 0; j < MAX_ITEMS_PER_RESOURCE; j++) {
            if ((i == k && j == l) || count == resources_count[i]) break;

            sem_post(&ResourcesPool[i].available);
            count++;
        }
    }
}

int acquire(int resources_count[], int acquiredOnes[MAX_RESOURCE_TYPES][MAX_ITEMS_PER_RESOURCE]) {

    //Deadlock prevention by acquiring the resource in increasing order
    int i;
	for (i = 0; i < resource_types_count; i++) {

        if (resources_count[i] == 0) continue;
        int count = 0;
		int j;
        for (j = 0; j < MAX_ITEMS_PER_RESOURCE; j++) {
            if (count == resources_count[i]) break;
            //If resource not available and decide to not stay idle, 
            //release the already acquired and return to do somenthing else

            if (sem_trywait(&ResourcesPool[i].available) != 0) {
                releaseNotAvailable(i, j, resources_count);

                return 1;
            }

            count++;
        }
    }
	for (i = 0; i < resource_types_count; i++) {
        if (resources_count[i] == 0) continue;

        //Mutex to change variable in resourcesPool withuot race condition
        pthread_mutex_lock(&ResourcesPool[i].mutex);

        int found = 0;
		int j;
        for (j = 0; j < ResourcesPool[i].quantity; j++) {
            if (!ResourcesPool[i].acquired[j]) {
                ResourcesPool[i].acquired[j] = true;

                acquiredOnes[i][j] = 1;
                found++;

                if (found == resources_count[i]) break;
            }
        }

        pthread_mutex_unlock(&ResourcesPool[i].mutex);
    }

    //Clean them after requiring them all, so other cooks can grab resources while one is cleaning its own
    for (i = 0; i < resource_types_count; i++) {
        if (resources_count[i] == 0) continue;
		int j;
        for (j = 0; j < MAX_ITEMS_PER_RESOURCE; j++) {
            if (acquiredOnes[i][j] == 0) continue;
            if (ResourcesPool[i].consecutive_dirty_uses[j] > 0) cleanBeforeCooking(i, j);
        }
    }

    return 0;
}

void releaseAfterCooking(int resources_count[], int acquiredOnes[MAX_RESOURCE_TYPES][MAX_ITEMS_PER_RESOURCE]) {

    //I try to clean them, otherwise I'll pile them in the sink
    int i;
	for (i = 0; i < resource_types_count; i++) {
        if (resources_count[i] == 0) continue;
		int j;
        for (j = 0; j < MAX_ITEMS_PER_RESOURCE; j++) {
            if (acquiredOnes[i][j] == 0) continue;
            cleanAfterCooking(i, j);
        }
    }

    //Make the resource available
    for (i = 0; i < resource_types_count; i++) {

        if (resources_count[i] == 0) continue;

        int found = 0;

        pthread_mutex_lock(&ResourcesPool[i].mutex);
        int j;
		for (j = 0; j < ResourcesPool[i].quantity; j++) {
            if (acquiredOnes[i][j] == 1) {
                ResourcesPool[i].acquired[j] = false;
                sem_post(&ResourcesPool[i].available);

                acquiredOnes[i][j] = 0;
                found++;

                if (found == resources_count[i]) break;
            }
        }

        pthread_mutex_unlock(&ResourcesPool[i].mutex);       
    }
}

int resourceIndex(Resource resource[], int n, char *name) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(name, resource[i].name) == 0) return i;
    }

    return -1;
}

void requirements(char *req_str, int *resources_count) {
    int i;
	for (i = 0; i < resource_types_count; i++) resources_count[i] = 0;
    
    char temp[128];
    strncpy(temp, req_str, sizeof(temp));
    
    char *saveptr1;
    char *token = strtok_r(temp, ";", &saveptr1);
    
    while (token != NULL) {
        char name[64];
        int count = 1;
        
        if (strchr(token, ':')) {
            sscanf(token, "%[^:]:%d", name, &count);
        } else {
            strncpy(name, token, sizeof(name));
        }
        
        int idx = resourceIndex(ResourcesPool, resource_types_count, name);
        if (idx != -1) {
            resources_count[idx] = count;
        }
        token = strtok_r(NULL, ";", &saveptr1);
    }
}

void* cook_thread(void* arg) {
	if (!arg) {
        perror("Cooker thread received NULL argument");
        exit(ERR_SYS_FAILURE);
    }
    ThreadArgs* args = (ThreadArgs*)arg;
	int cook_id = args->id;
    CookQueue* my_queue = &global_cook_queues[cook_id];
	OrderedDish* dish;
    
    while (true) {
            pthread_mutex_lock(&my_queue->queue_mutex);
            while (my_queue->count == 0) {
                pthread_cond_wait(&my_queue->queue_cond, &my_queue->queue_mutex);
            }
        
        dish = my_queue->dishes[my_queue->head];
        my_queue->head = (my_queue->head + 1) % 100;
        my_queue->count--;
        pthread_mutex_unlock(&my_queue->queue_mutex);

        //Check which resources the cook need
        int resources_count[MAX_RESOURCE_TYPES] = {0};
        requirements(dish->dish_info->requirements, resources_count);
        int acquiredOnes[MAX_RESOURCE_TYPES][MAX_ITEMS_PER_RESOURCE] = {0};
        
        //Decision making of the cook. 33% idle, 33% cook another dish, 33% clean resources

        //Try to acquire the resources and cook
        bool idle = true;

        while (idle) {
        	//if the customer is not active, skip
        	Customer* cust = (Customer*)dish->parent_customer;
			pthread_mutex_lock(&cust->patience_mutex);
			if (!cust->active) {
			    pthread_mutex_unlock(&cust->patience_mutex);
			    break;
			}
			pthread_mutex_unlock(&cust->patience_mutex);
            if (acquire(resources_count, acquiredOnes) == 0) {
                usleep((useconds_t)(60 * 1000000 * dish->dish_info->cooking_time / game_speed));
                
				Customer* cust = (Customer*)dish->parent_customer;
                pthread_mutex_lock(&cust->patience_mutex);
                dish->ready = true;
                pthread_mutex_unlock(&cust->patience_mutex);

                releaseAfterCooking(resources_count, acquiredOnes);
                break;
            }

            idle = rng_range(&args->rng, 0, 2) == 0;

            if (idle) {
                usleep(rng_range(&args->rng, 10000, 59999) / game_speed);
            }
        }

        if (!idle) {
            if (rng_range(&(args->rng), 0, 1)) {
                cleanWhenIdle();
            }

            pthread_mutex_lock(&my_queue->queue_mutex);
            my_queue->dishes[my_queue->tail] = dish;
            my_queue->tail = (my_queue->tail + 1) % 100;
            my_queue->count++;
            pthread_cond_signal(&my_queue->queue_cond);
            pthread_mutex_unlock(&my_queue->queue_mutex);

            usleep(50000.0 / game_speed); 
        }
    }
    
    return NULL;
}
