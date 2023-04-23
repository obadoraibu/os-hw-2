#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <signal.h>
#include <time.h>

#define MAX_CUSTOMERS 10

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

struct department_state {
    int semaphore; // semaphore for the department
    int num_customers; // number of customers in the queue
    pid_t customer_ids[MAX_CUSTOMERS]; // array of customer IDs
};

int sem_init(int sem_id, int val) {
    union semun arg;
    arg.val = val;
    return semctl(sem_id, 0, SETVAL, arg);
}

void sem_wait(int sem_id) {
    struct sembuf sem_ops = { 0, -1, SEM_UNDO };
    semop(sem_id, &sem_ops, 1);
}

void sem_post(int sem_id) {
    struct sembuf sem_ops = { 0, 1, SEM_UNDO };
    semop(sem_id, &sem_ops, 1);
}

void seller(int department, int fd, int sem_id) {
    struct department_state *state = (struct department_state*) mmap(NULL, sizeof(struct department_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    while (1) {
        // wait for the semaphore of the department
        sem_wait(sem_id);
        // serve the customer
        if (state->num_customers > 0) {
            pid_t customer_id = state->customer_ids[0];
            printf("Seller %d serving customer %d in department %d\n", getpid(), customer_id, department);
            for (int i = 1; i < state->num_customers; i++) {
                state->customer_ids[i-1] = state->customer_ids[i];
            }
            state->num_customers--;
        }
        // release the semaphore of the department
        sem_post(sem_id);
        sleep(rand() % 3); // rest
    }
}

void buyer(int *departments, int num_departments, int fd, int sem_id) {
    struct department_state *state = (struct department_state*) mmap(NULL, sizeof(struct department_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for (int i = 0; i < num_departments; i++) {
        while (1) {
            if (sem_trywait(sem_id) == 0) {
                // add the customer to the queue
                if (state->num_customers < MAX_CUSTOMERS) {
                    state->customer_ids[state->num_customers] = getpid();
                    state->num_customers++;
                }
                // release the semaphore of the department
                sem_post(sem_id);
                break; // go to the next department
            } else {
                // the seller is busy, sleep
                int sleep_time = rand() % 5 + 1; // sleep 1-5 seconds
                sleep(sleep_time);
            }
        }
    }
}

int main() {
    // create shared memory objects
    int fd1 = shm_open("/department1", O_CREAT | O_RDWR, 0666);
    int fd2 = shm_open("/department2", O_CREAT | O_RDWR, 0666);
    // set the size of shared memory
    ftruncate(fd1, sizeof(struct department_state));
    ftruncate(fd2, sizeof(struct department_state));

int sem_id1 = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);
int sem_id2 = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);
sem_init(sem_id1, 1);
sem_init(sem_id2, 1);


    // create the processes for sellers
pid_t seller1_pid = fork();
if (seller1_pid == 0) {
    // seller for department 1
    seller(1, fd1, sem_id1);
    exit(0);
}
pid_t seller2_pid = fork();
if (seller2_pid == 0) {
    // seller for department 2
    seller(2, fd2, sem_id2);
    exit(0);
}

// create the processes for buyers
int num_buyers = 20;
pid_t buyer_pids[num_buyers];
for (int i = 0; i < num_buyers; i++) {
    buyer_pids[i] = fork();
    if (buyer_pids[i] == 0) {
        // buyer for all departments
        int departments[] = {1, 2};
        buyer(departments, 2, fd1, sem_id1);
        buyer(departments, 2, fd2, sem_id2);
        exit(0);
    }
}

// wait for all buyers to finish
for (int i = 0; i < num_buyers; i++) {
    waitpid(buyer_pids[i], NULL, 0);
}

// terminate the sellers
kill(seller1_pid, SIGTERM);
kill(seller2_pid, SIGTERM);

// close the shared memory objects
close(fd1);
close(fd2);

// remove the shared memory objects
shm_unlink("/department1");
shm_unlink("/department2");

// remove the semaphores
semctl(sem_id1, 0, IPC_RMID);
semctl(sem_id2, 0, IPC_RMID);

return 0;
}