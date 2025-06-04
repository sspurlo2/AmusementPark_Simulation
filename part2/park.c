#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>

#define MAX_CAPACITY 100
//multi threaded version
//multiple passengers and cars running in parallel
//each passenger: explore (sleep) -> get ticket (mutex protected) -> wait for car -> board car -> ride(sleep) -> unboard -> repeat or exit
//each car: wait until enough passengers are waiting, or time out is reached -> wait for passengers -> call run() -> ride (sleep)-> call unload() -> wait for all passengers to unboard -> repeat 
//only one car loading at a time
//FIFO order
//multiple cars can be running at the same time
//pthread_create, pthread_join, pthread_exit
//mutexes/locks protect shared data

//settings
int NUM_PASSENGERS = 10;
int NUM_CARS = 1;
int CAR_CAPACITY = 5;
int WAIT_SECONDS = 8;
int RIDE_SECONDS = 6;

//ticket (mutex) and time
pthread_mutex_t ticket_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec start_time;

//park state
typedef struct{
int passengers_boarded;
int passengers_unboarded;
//car
int car_capacity;
int car_id;
int loading;
int unloading;
int ready;
int running;
//pthread
pthread_mutex_t mutex;
pthread_cond_t car_unloading;
pthread_cond_t car_loading;
pthread_cond_t car_ready_to_run;
} ParkState;

ParkState state; //will be used for the state of objects

//timestamp printer
void print_time(){
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int total_time = now.tv_sec - start_time.tv_sec;
    int hh = total_time / 3600;
    int mm = (total_time % 3600) / 60;
    int ss = total_time % 60;
    printf("[Time: %02d:%02d:%02d] ", hh, mm, ss);
}

//passenger logic
void* passenger_thread(void* arg) {
    int id = *((int*)arg);
    free (arg);

    while (1){
        //explore
        int explore_time = rand() % 5 + 1;
        print_time();
        printf("Paassenger %d exploring for %d seconds! \n", id, explore_time);
        sleep(explore_time); //similar logic to part 1

        //get ticket ... use mutex lock and unlock
        pthread_mutex_lock(&ticket_mutex);
        print_time();
        printf("Passenger %d getting ticket! \n", id);
        sleep(1);
        pthread_mutex_unlock(&ticket_mutex);
        print_time();
        printf("Passenger %d got ticket! \n", id);

        //loading car
        pthread_mutex_lock(&state.mutex);
        while (!state.loading && state.running) { // Wait only if car is not loading and park is running
            pthread_cond_wait(&state.car_loading, &state.mutex);
        }
        if (!state.running) { // After waking, if park is not running, exit
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        state.passengers_boarded++;
        print_time();
        printf("Passenger %d boarded car %d (%d/%d) \n", id, state.car_id, state.passengers_boarded, CAR_CAPACITY);
        if (state.passengers_boarded >= CAR_CAPACITY){
            pthread_cond_signal(&state.car_ready_to_run);
        }
        pthread_mutex_unlock(&state.mutex);

        //unloading
        pthread_mutex_lock(&state.mutex);
        while(state.running && !state.unloading){
            pthread_cond_wait(&state.car_unloading, &state.mutex);
        }
        if (!state.running) { // ADD: Exit if park closed
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        state.passengers_unboarded++;
        print_time();
        printf("Passenger %d unboarded car! \n", id);
        if (state.passengers_unboarded >= state.passengers_boarded) {
            pthread_cond_signal(&state.car_unloading);
        }
        pthread_mutex_unlock(&state.mutex);
    }
    return NULL;
}

//car logic
void* car_thread(void* arg) {
    int id = *((int*)arg);
    free(arg);

    while (1) {
        pthread_mutex_lock(&state.mutex);
        if (!state.running){
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        state.loading = 1;
        state.car_id = id;
        state.passengers_boarded = 0;
        state.passengers_unboarded = 0;
        state.car_capacity = CAR_CAPACITY;

        print_time();
        printf("Car %d loading passengers! \n", id);
        pthread_cond_broadcast(&state.car_loading); //tell all threads waiting on car_loading

        // wait for passengers or timeout
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += WAIT_SECONDS;

        while (state.running && state.passengers_boarded < CAR_CAPACITY) {
            int res = pthread_cond_timedwait(&state.car_ready_to_run, &state.mutex, &timeout);
            if (res == ETIMEDOUT) {
                print_time();
                printf("[Car %d] Timed out with %d/%d passengers.\n", id, state.passengers_boarded, CAR_CAPACITY);
                break;
            }
        }

        state.loading = 0;
        pthread_mutex_unlock(&state.mutex); //unlock after use

        pthread_mutex_lock(&state.mutex);
        int running = state.running;
        pthread_mutex_unlock(&state.mutex);
        if (!running){
            break;
        }

        // Ride
        print_time();
        printf("[Car %d] Running ride...\n", id);
        sleep(RIDE_SECONDS); //sleep for amount on ride

        // Unload
        pthread_mutex_lock(&state.mutex);
        state.unloading = 1;
        pthread_cond_broadcast(&state.car_unloading);
        while (state.running &&state.passengers_unboarded < state.passengers_boarded) {
            pthread_cond_wait(&state.car_unloading, &state.mutex);
        }
        state.unloading = 0;
        if (!state.running) {
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        print_time();
        printf("[Car %d] Unloading complete.\n", id);
        pthread_mutex_unlock(&state.mutex); //always have to unlock mutex after use
    }
    print_time();
    printf("Car %d exiting.\n", id);
    return NULL;
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    clock_gettime(CLOCK_REALTIME, &start_time); // Initialize start time
    
    // parse flags
    int opt;
    while ((opt = getopt(argc, argv, "n:c:p:w:r:")) != -1) {
        switch (opt) {
            case 'n': NUM_PASSENGERS = atoi(optarg); break;
            case 'c': NUM_CARS = atoi(optarg); break;
            case 'p': CAR_CAPACITY = atoi(optarg); break;
            case 'w': WAIT_SECONDS = atoi(optarg); break;
            case 'r': RIDE_SECONDS = atoi(optarg); break;
            default: fprintf(stderr, "Usage: %s -n num_passengers -c num_cars -p capacity -w wait -r ride\n", argv[0]); 
                     exit(1);
        }
    }

    // Initialize state
    state.running = 1;
    state.car_capacity = CAR_CAPACITY;
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.car_loading, NULL);
    pthread_cond_init(&state.car_unloading, NULL);
    pthread_cond_init(&state.car_ready_to_run, NULL);

    pthread_t passenger_threads[NUM_PASSENGERS];
    pthread_t car_threads[NUM_CARS];

    // launch cars
    for (int i = 0; i < NUM_CARS; ++i) {
        int* id = malloc(sizeof(int)); 
        *id = i + 1;
        pthread_create(&car_threads[i], NULL, car_thread, id);
    }

    // launch passengers
    for (int i = 0; i < NUM_PASSENGERS; ++i) {
        int* id = malloc(sizeof(int)); 
        *id = i + 1;
        pthread_create(&passenger_threads[i], NULL, passenger_thread, id);
    }

    sleep(10); // run for 10 seconds
    
    pthread_mutex_lock(&state.mutex);
    state.running = 0; // signal threads to exit
    pthread_cond_broadcast(&state.car_loading);
    pthread_cond_broadcast(&state.car_unloading);
    pthread_cond_broadcast(&state.car_ready_to_run);
    pthread_mutex_unlock(&state.mutex);

    // wait for threads to finish
    for (int i = 0; i < NUM_PASSENGERS; ++i) {
        pthread_join(passenger_threads[i], NULL);
    }
    for (int i = 0; i < NUM_CARS; ++i) {
        pthread_join(car_threads[i], NULL);
    }

    // cleanup
    pthread_mutex_destroy(&state.mutex);
    pthread_cond_destroy(&state.car_loading);
    pthread_cond_destroy(&state.car_unloading);
    pthread_cond_destroy(&state.car_ready_to_run);

    print_time();
    printf("Simulation ended!\n");
    return 0;
}

