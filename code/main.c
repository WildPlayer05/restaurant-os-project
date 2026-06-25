#include "restaurant.h"
#include "error_codes.h"
#include "rng.h"
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

//define of shared structures
int score = 0;
int spawned_customers = 0;
int total_customers;
int max_customers;
int num_cooks;
int num_waiters;
int game_speed;
int random_seed;
int unserved_customers = 0;
int menu_items_count = 0;
int resource_types_count = 0;
Customer *global_customers;
CookQueue *global_cook_queues;
int global_customers_completed = 0;
int current_customers_room = 0;
MenuDish menu[20];
Resource ResourcesPool[20];
pthread_mutex_t score_mutex;
pthread_mutex_t room_mutex;
pthread_mutex_t sink;
sem_t restaurant_remaining_capacity;

void* customer_thread(void* arg);
void* waiter_thread(void* arg);
void* cook_thread(void* arg);

//print the base summary of the simulation status
void print_report(){

    //thread-safe reading of score
    pthread_mutex_lock(&score_mutex);
    int current_score = score;
    int uc = unserved_customers;
    pthread_mutex_unlock(&score_mutex);

    int current_customers;
    sem_getvalue(&restaurant_remaining_capacity, &current_customers);

    current_customers = max_customers - current_customers;

    float percentage = ((float)spawned_customers / total_customers) * 100.0;
    printf("The score is: %d\n", current_score);
    printf("In the restaurant there are actually: %d\n", current_customers);
    printf("Unserved customers: %d\n", uc);
    printf("Progress spawn: %.2f%%\n", percentage);
}

//signal handler of SIGUSR1
void handler(int signum){
	(void)signum;
    printf("\n-------SPECIAL STATUS REPORT-------\n");
    print_report();
    printf("\n--- COOK QUEUES STATUS ---\n");
    for (int i = 0; i < num_cooks; i++) {
        pthread_mutex_lock(&global_cook_queues[i].queue_mutex);
        
		printf("Cook %d: %d dishes in queue\n", i, global_cook_queues[i].count);
        
		pthread_mutex_unlock(&global_cook_queues[i].queue_mutex);
    }

    printf("\n--- KITCHEN RESOURCES AVAILABILITY ---\n");
    for (int i = 0; i < resource_types_count; i++) { 
        pthread_mutex_lock(&ResourcesPool[i].mutex);
        
        int disponibili = 0;
        for (int j = 0; j < ResourcesPool[i].quantity; j++) {
            if (!ResourcesPool[i].acquired[j]) {
                disponibili++;
            }
        }
        printf("Resource [%s]: %d/%d avaiable\n", ResourcesPool[i].name, disponibili, ResourcesPool[i].quantity);
        
		pthread_mutex_unlock(&ResourcesPool[i].mutex);
    }
    printf("-------------------------------------\n\n");
}

//parse a row from menu CSV and set menu struct
void parse_row_menu(char *row, MenuDish *menu){
    char* token = strtok(row, ",");
    if (!token){
    	perror("Parsing error in menu");
    	exit(ERR_INVALID_PARSING);
	}
    strcpy(menu->name, token);

    token = strtok(NULL, ",");
    if (!token){
    	perror("Parsing error in menu");
    	exit(ERR_INVALID_PARSING);
	}
    menu->price = atoi(token);

    token = strtok(NULL, ",");
    if (!token){
    	perror("Parsing error in menu");
    	exit(ERR_INVALID_PARSING);
	}
    menu->cooking_time = atoi(token);

    token = strtok(NULL, ",\n");
    if (!token){
    	perror("Parsing error in menu");
    	exit(ERR_INVALID_PARSING);
	}
    strcpy(menu->requirements, token);
}

//parse a row from resources CSV and set resource struct
void parse_row_resource(char *row, Resource *resource){
	char* token = strtok(row, ",");
    if (!token){
    	perror("Parsing error in resource");
    	exit(ERR_INVALID_PARSING);
	}
	strcpy(resource->name, token);
    
    token = strtok(NULL, ",");
    if (!token){
    	perror("Parsing error in resource");
    	exit(ERR_INVALID_PARSING);
	}
    resource->quantity = atoi(token);
    
    token = strtok(NULL, ",\n");
    if (!token){
    	perror("Parsing error in resource");
    	exit(ERR_INVALID_PARSING);
	}
    resource->clean_time = atoi(token);
}

void sortResources(Resource resource[], int n) {
    int i, j;
    Resource temp;
    bool swapped;

    for (i = 0; i < n - 1; i++) {
        swapped = false;
        
        for (j = 0; j < n - i - 1; j++) {
            if (resource[j].quantity > resource[j + 1].quantity) {
                temp = resource[j];
                resource[j] = resource[j + 1];
                resource[j + 1] = temp;
                
                swapped = true;
            }
        }

        if (!swapped) break;
    }
}

char* safe_getenv(const char* name) {
    char* val = getenv(name);
    if (!val) {
        perror("Missing an environment variable");
        exit(ERR_ENV_MISSING);
    }
    return val;
}

int main(){
    //initialize the mutex
    if (pthread_mutex_init(&room_mutex, NULL) != 0 || pthread_mutex_init(&score_mutex, NULL) != 0 || pthread_mutex_init(&sink, NULL) != 0) {
        perror("Failed to initialize mutexes");
        exit(ERR_SYS_FAILURE);
    }
    int i = 0;

    //write PID for status.sh communication
    int fd = open("/tmp/restaurant.pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);//rw-r--r--

    if(fd == -1) {
        perror("Opening file error(/tmp/restaurant.pid)");
        exit(ERR_SYS_FAILURE);
    }

    char pid[10];
    snprintf(pid, sizeof(pid), "%d", getpid());

    if(write(fd, pid, strlen(pid)) == -1){
        perror("Writing PID file error");
        close(fd);
        exit(ERR_SYS_FAILURE);
    } 

    close(fd);
    
    //inizialize the signal handler
    struct sigaction act;
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGUSR1, &act, NULL) != 0) {
        perror("Failed to set signal handler");
        exit(ERR_SYS_FAILURE);
    }

    //load simulation parameters from environment variables
    num_cooks = atoi(safe_getenv("NUM_COOKS"));
    num_waiters = atoi(safe_getenv("NUM_WAITERS"));
    max_customers = atoi(safe_getenv("MAX_CUSTOMERS"));
    total_customers = atoi(safe_getenv("TOTAL_CUSTOMERS"));
    char* menu_file_path = safe_getenv("MENU_FILE");
    char* resources_file_path = safe_getenv("RESOURCES_FILE");
    game_speed = atoi(safe_getenv("GAME_SPEED"));
    random_seed = atoi(safe_getenv("RANDOM_SEED"));
    
    uint64_t master_seed = (uint64_t) random_seed;
    //inizialize semaphore used to ensure restaurant capacity
    if (sem_init(&restaurant_remaining_capacity, 0, max_customers) != 0) {
        perror("Failed to initialize restaurant capacity semaphore");
        exit(ERR_SYS_FAILURE);
    }

    //read of menu file
    FILE *fd_menu = fopen(menu_file_path, "r");

    if(fd_menu == NULL) {
        perror("Failed to open menu file");
        exit(ERR_FILE_NOT_FOUND);
    }

    char row[256];

    fgets(row, sizeof(row), fd_menu);//skip CSV header

    while(fgets(row, sizeof(row), fd_menu) != NULL){
        parse_row_menu(row, &menu[i]);
        i++;
    }
    menu_items_count = i;
    
    fclose(fd_menu);

    //read of resource file
    FILE *fd_resources = fopen(resources_file_path, "r");
    
    if(fd_resources == NULL) {
        perror("Failed to open resource file");
        exit(ERR_FILE_NOT_FOUND);
    }

    i=0;

    fgets(row, sizeof(row), fd_resources);//skip CSV header

    while(fgets(row, sizeof(row), fd_resources) != NULL){
        parse_row_resource(row, &ResourcesPool[i]);
        if (sem_init(&ResourcesPool[i].available, 0, ResourcesPool[i].quantity) != 0 || pthread_mutex_init(&ResourcesPool[i].mutex, NULL) != 0) {
            perror("Failed to initialize resource synchronization tools");
            fclose(fd_resources);
            exit(ERR_SYS_FAILURE);
        }
		for (int j = 0; j < 100; j++) {
        	ResourcesPool[i].acquired[j] = false;
        	ResourcesPool[i].consecutive_dirty_uses[j] = 0;
    	}
        i++;
    }
	resource_types_count = i;
    fclose(fd_resources);
    
    //Order them by quantity to prevent deadlock
    sortResources(ResourcesPool, resource_types_count);
    
    //create cooks queues
    global_cook_queues = malloc(num_cooks * sizeof(CookQueue));
    if (!global_cook_queues) {
        perror("Allocation error for global_cook_queues");
        exit(ERR_ALLOC_FAILED);
    }
    
    for (i = 0; i < num_cooks; i++) {
        global_cook_queues[i].head = 0;
        global_cook_queues[i].tail = 0;
        global_cook_queues[i].count = 0;
        if (pthread_mutex_init(&global_cook_queues[i].queue_mutex, NULL) != 0 || pthread_cond_init(&global_cook_queues[i].queue_cond, NULL) != 0) {
            perror("Failed to initialize cook queue sync tools");
            exit(ERR_SYS_FAILURE);
        }
    }

    //create cooks and waiters thread
    pthread_t t_cooks[num_cooks];
    pthread_t t_waiters[num_waiters];

	global_customers = malloc(total_customers * sizeof(Customer));
    if (!global_customers) { 
		perror("Allocation error for global_customers"); 
		exit(ERR_ALLOC_FAILED); 
	}
	for (i = 0; i < total_customers; i++) {
    	global_customers[i].active = false;
        if (pthread_mutex_init(&global_customers[i].patience_mutex, NULL) != 0) {
            perror("Failed to initialize customer patience mutex");
            exit(ERR_SYS_FAILURE);
        }
	}
	
	ThreadArgs* cook_args = malloc(num_cooks * sizeof(ThreadArgs));
	if (!cook_args) { 
		perror("Allocation error for cook_args"); 
		exit(ERR_ALLOC_FAILED); 
	}
    for(i=0; i < num_cooks; i++){
        cook_args[i].id = i;
        uint64_t derived_seed = splitmix64_next(&master_seed);
        rng_init(&cook_args[i].rng, &derived_seed);
		if (pthread_create(&t_cooks[i], NULL, cook_thread, &cook_args[i]) != 0) {
            perror("Failed to create cook thread");
            exit(ERR_SYS_FAILURE);
        }    
	}

    ThreadArgs* waiter_args = malloc(num_waiters * sizeof(ThreadArgs));
	if (!waiter_args) { 
		perror("Allocation error for waiter_args"); 
		exit(ERR_ALLOC_FAILED); 
	}
    for (i = 0; i < num_waiters; i++) {
        waiter_args[i].id = i;
        uint64_t waiter_seed = splitmix64_next(&master_seed);
		rng_init(&waiter_args[i].rng, &waiter_seed);
		if (pthread_create(&t_waiters[i], NULL, waiter_thread, &waiter_args[i]) != 0) {
            perror("Failed to create waiter thread");
            exit(ERR_SYS_FAILURE);
        }    
	}
    
    //definition of time variables
    double last_customer_time = get_current_time();
    double last_print = 0.0;
    double print_interval = 3.0;
	double spawn_random_interval = 0.0;

	//create customers
	RNG main_rng;
	rng_init(&main_rng, &master_seed);
	
	pthread_t t_customers[total_customers];
	
	for (int i = 0; i < total_customers; i++) {
    	uint64_t customer_seed = splitmix64_next(&master_seed);
    	rng_init(&global_customers[i].rng, &customer_seed);
	}
	while(spawned_customers < total_customers){
        
        //verify if is time to spawn a customer
        if(last_customer_time + spawn_random_interval <= get_current_time()){

            //use non-blocking trywait to ensure maximum capacity
            if(sem_trywait(&restaurant_remaining_capacity) == 0){
            	int idx = spawned_customers;
				global_customers[idx].customer_id = idx + 1;
				if (pthread_create(&t_customers[idx], NULL, customer_thread, &global_customers[idx]) != 0) {
                    perror("Failed to create customer thread");
                    exit(ERR_SYS_FAILURE);
                }
                if (pthread_detach(t_customers[idx]) != 0) {
                    perror("Failed to detach customer thread");
                    exit(ERR_SYS_FAILURE);
                }
				spawned_customers++;
                pthread_mutex_lock(&room_mutex);
				current_customers_room++;
				pthread_mutex_unlock(&room_mutex);
                last_customer_time = get_current_time();

                //calulate interval for random spawning
                spawn_random_interval = (double) rng_range(&main_rng, 1, 4) / game_speed;
            }
        }

        //periodic status report print
        if(last_print + print_interval <= get_current_time()){
            printf("\n-------PERIODIC STATUS REPORT-------\n");
            print_report();
            last_print = get_current_time();
        }
        usleep(100000.0 / game_speed);
    }

    //wait customer in restaurant finish
    int customer_inside;
    do{
        sem_getvalue(&restaurant_remaining_capacity, &customer_inside);
        if(last_print + print_interval <= get_current_time()){
        printf("\n-------PERIODIC STATUS REPORT-------\n");
        print_report();
        last_print = get_current_time();
    }
        usleep(100000 / game_speed); 
    }while(customer_inside < max_customers);

    //terminate simulation
    for(i=0; i < num_cooks; i++){
        pthread_cancel(t_cooks[i]);
    }

    for(i=0; i < num_waiters; i++){
        pthread_join(t_waiters[i], NULL);
    }
    
    for (i = 0; i < total_customers; i++) {
    	if (global_customers[i].order_list) {
        	free(global_customers[i].order_list);
    	    global_customers[i].order_list = NULL;
    	}
	}

    //destroy syncronization tools
    sem_destroy(&restaurant_remaining_capacity);
    pthread_mutex_destroy(&score_mutex);
    pthread_mutex_destroy(&room_mutex);
    pthread_mutex_destroy(&sink);
    
    printf("\n=== SUCCESS ===\n");
    printf
	("Final score of the restaurant: %d\n", score);
    print_report();
	//deallocate the structures
    free(global_customers);
    free(global_cook_queues);
    free(cook_args);
    free(waiter_args);
    unlink("/tmp/restaurant.pid");

    return 0;
}
