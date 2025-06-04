#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

//single threaded version
//explore (sleep) -> get ticket (mutex protected) -> wait for car -> board car -> ride(sleep) -> unboard -> repeat or exit
//pthread_create, pthread_join, pthread_exit
//mutexes/locks protect shared data

//ticket (mutex) and time
pthread_mutex_t ticket_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec start_time;

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

//entering park
void explore_park (int id){
    int explore_time = rand() % 10 + 1; //random time exploring
    print_time();
    printf("Passenger %d is exploring the park for %d seconds!\n", id, explore_time);
    sleep(explore_time); //sleeping for amount in the park
}

//getting ticket (mutex)!
void get_ticket(int id){
    pthread_mutex_lock(&ticket_mutex); //locking for critical section (ticket)
    print_time();
    printf("Passenger %d is getting ticket!\n", id);
    sleep(1); //waiting to get ticket
    print_time();
    printf("Passenger %d has gotten ticket!\n", id);
    pthread_mutex_unlock(&ticket_mutex); //have to unlock mutex when you lock it
}

//boarding the car
void board_car(int id){
    print_time();
    printf("Passenger %d is waiting to board car!", id);
    sleep(2); //waiting to board
    print_time();
    printf("Passenger %d has boarded the car!\n", id);
}

//riding the car
void ride_car(int id){
    int ride_time = rand() % 3 + 3; //random ride time from 3 - 5 seconds
    print_time();
    printf("Passenger %d is on the ride for %d seconds!", id, ride_time);
    sleep(ride_time); //to replicate being on the ride
    print_time();
    printf("Passenger %d's ride is over!", id);
}

//unboarding the car
void unboard_car(int id){
    print_time();
    printf("Passenger %d has unboarded the ride!\n", id);
}

//passenger thread logic
void* passenger_thread(void *arg){
    int id = *((int*)arg);
    print_time();
    printf("Passenger %d has entered the park!\n", id);

    explore_park(id);
    get_ticket(id);
    board_car(id);
    ride_car(id);
    unboard_car(id);

    return NULL;
}

int main(){
    srand(time(NULL));
    clock_gettime(CLOCK_REALTIME, &start_time);

    pthread_t passenger;
    int id = 1;

    pthread_create(&passenger, NULL, passenger_thread, &id);
    pthread_join(passenger, NULL);

    print_time();
    printf("Simulation complete\n");

    pthread_mutex_destroy(&ticket_mutex);
    return 0;
}