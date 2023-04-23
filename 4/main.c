#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

#define MAX_CUSTOMERS 10

struct department_state {
    sem_t semaphore; // семафор отдела
    int num_customers; // число посетителей в очереди
    pid_t customer_ids[MAX_CUSTOMERS]; // массив посетителей
};

void seller(int department, int fd) {
    struct department_state *state = mmap(NULL, sizeof(struct department_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    while (1) {
        // ожидание семафора отдела
        sem_wait(&state->semaphore);
        // обслуживание
        if (state->num_customers > 0) {
            pid_t customer_id = state->customer_ids[0];
            printf("Seller %d serving customer %d in department %d\n", getpid(), customer_id, department);
            for (int i = 1; i < state->num_customers; i++) {
                state->customer_ids[i-1] = state->customer_ids[i];
            }
            state->num_customers--;
        }
        // освобождение семафора отдела
        sem_post(&state->semaphore);
        sleep(rand() % 3); // отдых
    }
}

void buyer(int *departments, int num_departments, int fd) {
    struct department_state *state = mmap(NULL, sizeof(struct department_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for (int i = 0; i < num_departments; i++) {
        while (1) {
            if (sem_trywait(&state->semaphore) == 0) {
                // добавление посетителя в очередь
                if (state->num_customers < MAX_CUSTOMERS) {
                    state->customer_ids[state->num_customers] = getpid();
                    state->num_customers++;
                }
                // осовбождение семафора отделов
                sem_post(&state->semaphore);
                break; // к след отделу
            } else {
                // продавец занят, спим
                int sleep_time = rand() % 5 + 1; // сон 1-5 секунд
                sleep(sleep_time);
            }
        }
    }
}

int main() {
    // создание объектов общей памяти 
    int fd1 = shm_open("/department1", O_CREAT | O_RDWR, 0666);
    int fd2 = shm_open("/department2", O_CREAT | O_RDWR, 0666);
    int fd3 = shm_open("/department3", O_CREAT | O_RDWR, 0666);
    if (fd1 == -1 || fd2 == -1 || fd3 == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }


    struct department_state *state1 = mmap(NULL, sizeof(struct department_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
    struct department_state *state2 = mmap(NULL, sizeof(struct department_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    struct department_state *state3 = mmap(NULL, sizeof(struct department_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 0);


    sem_init(&state1->semaphore, 1, 1);
    sem_init(&state2->semaphore, 1, 1);
    sem_init(&state3->semaphore, 1, 1);

    state1->num_customers = 0;
    state2->num_customers = 0;
    state3->num_customers = 0;

    // процессы продавцы
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        seller(1, fd1);
        exit(EXIT_SUCCESS);
    } else {
        pid_t pid1 = fork();
        if (pid1 < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid1 == 0) {
            seller(2, fd2);
            exit(EXIT_SUCCESS);
        } else {
            pid_t pid2 = fork();
            if (pid2 < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
            } else if (pid2 == 0) {
                seller(3, fd3);
                exit(EXIT_SUCCESS);
            }
        }
    }


    // процессы покупатели
    srand(time(NULL));
    for (int i = 0; i < 10; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // buyer process
            int num_departments = rand() % 3 + 1;
            int departments[num_departments];
            for (int j = 0; j < num_departments; j++) {
                departments[j] = rand() % 3 + 1;
            }
            buyer(departments, num_departments, fd1);
            buyer(departments, num_departments, fd2);
            buyer(departments, num_departments, fd3);
            exit(EXIT_SUCCESS);
        }
    }

    while (wait(NULL) > 0);


    // destroy semaphores
    sem_destroy(&state1->semaphore);
    sem_destroy(&state2->semaphore);
    sem_destroy(&state3->semaphore);

    exit(EXIT_SUCCESS);
}
