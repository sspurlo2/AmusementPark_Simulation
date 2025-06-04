#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>

//same as part 2, but with the new addition of monitor statistics. They have to be in interval, and then a final statistic print at the end of the simulation.

#define MAX_CAPACITY 100
#define MONITOR_INTERVAL 5 // Print stats every 5 seconds

int NUM_PASSENGERS = 10;
int NUM_CARS = 1;
int CAR_CAPACITY = 5;
int WAIT_SECONDS = 8;
int RIDE_SECONDS = 6;

pthread_mutex_t ticket_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec start_time;

// monitor statistics
int total_passengers_served = 0;
int total_rides_completed = 0;
double total_ticket_wait = 0;
double total_ride_wait = 0;
int total_ticket_requests = 0;
int total_ride_requests = 0;

typedef struct {
    int passengers_boarded;
    int passengers_unboarded;
    int car_capacity;
    int car_id;
    int loading;
    int unloading;
    int ready;
    int running;
    pthread_mutex_t mutex;
    pthread_cond_t car_unloading;
    pthread_cond_t car_loading;
    pthread_cond_t car_ready_to_run;
} ParkState;

ParkState state;

void print_time() {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int total_time = now.tv_sec - start_time.tv_sec;
    int hh = total_time / 3600;
    int mm = (total_time % 3600) / 60;
    int ss = total_time % 60;
    printf("[Time: %02d:%02d:%02d] ", hh, mm, ss);
}

//monitor thread function 
void* monitor_thread(void* arg) {
    (void)arg; 
    while (1) {
        sleep(MONITOR_INTERVAL);
        
        pthread_mutex_lock(&state.mutex);
        int running = state.running;
        pthread_mutex_unlock(&state.mutex);
        if (!running) break;

        pthread_mutex_lock(&ticket_mutex);
        int rides = total_rides_completed;
        int passengers = total_passengers_served;
        int tkt_req = total_ticket_requests;
        int ride_req = total_ride_requests;
        double tkt_wait = total_ticket_wait;
        double ride_wait = total_ride_wait;
        pthread_mutex_unlock(&ticket_mutex);

        double avg_tkt = tkt_req ? tkt_wait / tkt_req : 0;
        double avg_ride = ride_req ? ride_wait / ride_req : 0;
        double util = rides ? (100.0 * passengers) / (rides * CAR_CAPACITY) : 0;
        double avg_passengers = rides ? (1.0 * passengers) / rides : 0;

        print_time();
        printf("[Monitor] Current Statistics:\n");
        print_time();
        printf("  Total passengers served: %d\n", passengers);
        print_time();
        printf("  Total rides completed: %d\n", rides);
        print_time();
        printf("  Avg ticket wait: %.1f ms\n", avg_tkt);
        print_time();
        printf("  Avg ride wait: %.1f ms\n", avg_ride);
        print_time();
        printf("  Car utilization: %.0f%% (%.1f/%d passengers)\n", 
               util, avg_passengers, CAR_CAPACITY);
    }
    return NULL;
}

void* passenger_thread(void* arg) {
    int id = *((int*)arg);
    free(arg);

    while (1) {
        int explore_time = rand() % 5 + 1;
        print_time();
        printf("Passenger %d exploring for %d seconds!\n", id, explore_time);
        sleep(explore_time);

        struct timespec ticket_start, ticket_end;
        clock_gettime(CLOCK_MONOTONIC, &ticket_start);

        pthread_mutex_lock(&ticket_mutex);
        print_time();
        printf("Passenger %d getting ticket!\n", id);
        sleep(1);
        pthread_mutex_unlock(&ticket_mutex);

        clock_gettime(CLOCK_MONOTONIC, &ticket_end);
        double ticket_wait = (ticket_end.tv_sec - ticket_start.tv_sec) * 1000 +
                            (ticket_end.tv_nsec - ticket_start.tv_nsec) / 1e6;
        pthread_mutex_lock(&ticket_mutex);
        total_ticket_requests++;
        total_ticket_wait += ticket_wait;
        pthread_mutex_unlock(&ticket_mutex);

        print_time();
        printf("Passenger %d got ticket!\n", id);

        struct timespec ride_start, ride_end;
        clock_gettime(CLOCK_MONOTONIC, &ride_start);

        pthread_mutex_lock(&state.mutex);
        while (!state.loading && state.running) {
            pthread_cond_wait(&state.car_loading, &state.mutex);
        }
        if (!state.running) {
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &ride_end);
        double ride_wait = (ride_end.tv_sec - ride_start.tv_sec) * 1000 +
                           (ride_end.tv_nsec - ride_start.tv_nsec) / 1e6;
        pthread_mutex_lock(&ticket_mutex);
        total_ride_requests++;
        total_ride_wait += ride_wait;
        pthread_mutex_unlock(&ticket_mutex);

        state.passengers_boarded++;
        print_time();
        printf("Passenger %d boarded car %d (%d/%d)\n", 
               id, state.car_id, state.passengers_boarded, CAR_CAPACITY);
        if (state.passengers_boarded >= CAR_CAPACITY) {
            pthread_cond_signal(&state.car_ready_to_run);
        }
        pthread_mutex_unlock(&state.mutex);

        pthread_mutex_lock(&state.mutex);
        while (state.running && !state.unloading) {
            pthread_cond_wait(&state.car_unloading, &state.mutex);
        }
        if (!state.running) {
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        state.passengers_unboarded++;
        print_time();
        printf("Passenger %d unboarded car!\n", id);
        if (state.passengers_unboarded >= state.passengers_boarded) {
            pthread_cond_signal(&state.car_unloading);
        }
        pthread_mutex_unlock(&state.mutex);
    }
    return NULL;
}

void* car_thread(void* arg) {
    int id = *((int*)arg);
    free(arg);

    while (1) {
        pthread_mutex_lock(&state.mutex);
        if (!state.running) {
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        state.loading = 1;
        state.car_id = id;
        state.passengers_boarded = 0;
        state.passengers_unboarded = 0;
        state.car_capacity = CAR_CAPACITY;

        print_time();
        printf("Car %d loading passengers!\n", id);
        pthread_cond_broadcast(&state.car_loading);

        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += WAIT_SECONDS;

        while (state.running && state.passengers_boarded < CAR_CAPACITY) {
            int res = pthread_cond_timedwait(&state.car_ready_to_run, &state.mutex, &timeout);
            if (res == ETIMEDOUT) {
                print_time();
                printf("[Car %d] Timed out with %d/%d passengers\n", 
                       id, state.passengers_boarded, CAR_CAPACITY);
                break;
            }
        }

        state.loading = 0;
        pthread_mutex_unlock(&state.mutex);

        pthread_mutex_lock(&state.mutex);
        int running = state.running;
        pthread_mutex_unlock(&state.mutex);
        if (!running){
            break;
        }

        print_time();
        printf("[Car %d] Running ride...\n", id);
        pthread_mutex_lock(&ticket_mutex);
        total_rides_completed++;
        total_passengers_served += state.passengers_boarded;
        pthread_mutex_unlock(&ticket_mutex);
        sleep(RIDE_SECONDS);

        pthread_mutex_lock(&state.mutex);
        state.unloading = 1;
        pthread_cond_broadcast(&state.car_unloading);
        while (state.running && state.passengers_unboarded < state.passengers_boarded) {
            pthread_cond_wait(&state.car_unloading, &state.mutex);
        }
        state.unloading = 0;
        if (!state.running) {
            pthread_mutex_unlock(&state.mutex);
            break;
        }
        print_time();
        printf("[Car %d] Unloading complete\n", id);
        pthread_mutex_unlock(&state.mutex);
    }
    print_time();
    printf("Car %d exiting\n", id);
    return NULL;
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    clock_gettime(CLOCK_REALTIME, &start_time);

    int opt;
    while ((opt = getopt(argc, argv, "n:c:p:w:r:")) != -1) {
        switch (opt) {
            case 'n': NUM_PASSENGERS = atoi(optarg); break;
            case 'c': NUM_CARS = atoi(optarg); break;
            case 'p': CAR_CAPACITY = atoi(optarg); break;
            case 'w': WAIT_SECONDS = atoi(optarg); break;
            case 'r': RIDE_SECONDS = atoi(optarg); break;
            default:
                fprintf(stderr, "Usage: %s -n num_passengers -c num_cars -p capacity -w wait -r ride\n", argv[0]);
                exit(1);
        }
    }

    state.running = 1;
    state.car_capacity = CAR_CAPACITY;
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.car_loading, NULL);
    pthread_cond_init(&state.car_unloading, NULL);
    pthread_cond_init(&state.car_ready_to_run, NULL);

    pthread_t passenger_threads[NUM_PASSENGERS];
    pthread_t car_threads[NUM_CARS];
    pthread_t monitor_tid;

    // monitor thread
    pthread_create(&monitor_tid, NULL, monitor_thread, NULL);

    for (int i = 0; i < NUM_CARS; ++i) {
        int* id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&car_threads[i], NULL, car_thread, id);
    }

    for (int i = 0; i < NUM_PASSENGERS; ++i) {
        int* id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&passenger_threads[i], NULL, passenger_thread, id);
    }

    sleep(10);

    pthread_mutex_lock(&state.mutex);
    state.running = 0;
    pthread_cond_broadcast(&state.car_loading);
    pthread_cond_broadcast(&state.car_unloading);
    pthread_cond_broadcast(&state.car_ready_to_run);
    pthread_mutex_unlock(&state.mutex);

    for (int i = 0; i < NUM_PASSENGERS; ++i) {
        pthread_join(passenger_threads[i], NULL);
    }
    for (int i = 0; i < NUM_CARS; ++i) {
        pthread_join(car_threads[i], NULL);
    }
    pthread_join(monitor_tid, NULL);

    pthread_mutex_destroy(&state.mutex);
    pthread_cond_destroy(&state.car_loading);
    pthread_cond_destroy(&state.car_unloading);
    pthread_cond_destroy(&state.car_ready_to_run);

    print_time();
    printf("Simulation ended\n");
    struct timespec sim_end;
    clock_gettime(CLOCK_REALTIME, &sim_end);
    int duration = sim_end.tv_sec - start_time.tv_sec;
    int hh = duration / 3600, mm = (duration % 3600) / 60, ss = duration % 60;

    //final monitor statistics
    printf("\n[Monitor] FINAL STATISTICS:\n");
    printf("Total simulation time: %02d:%02d:%02d\n", hh, mm, ss);
    printf("Total passengers: %d\n", total_passengers_served);
    printf("Total rides completed: %d\n", total_rides_completed);
    printf("Average wait time in ticket queue: %.1f ms\n",
        total_ticket_requests ? total_ticket_wait / total_ticket_requests : 0);
    printf("Average wait time in ride queue: %.1f ms\n",
        total_ride_requests ? total_ride_wait / total_ride_requests : 0);
    printf("Average car utilization: %.0f%% (%.1f/%d passengers per ride)\n",
        total_rides_completed ? (100.0 * total_passengers_served) / (total_rides_completed * CAR_CAPACITY) : 0,
        total_rides_completed ? (1.0 * total_passengers_served) / total_rides_completed : 0,
        CAR_CAPACITY);

    return 0;
}