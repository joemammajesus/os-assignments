
#include "shared.h"


int process_count();
void cleanup();
void signal_handler() ;
void alarm_handler() ;
void signal_init() ;
void init_pcb(int);
pid_t r_wait(int *);
void sem_wait(int);
int bit_get(const unsigned char *, int) ;
void bit_set(unsigned char *, int , int) ;
void fork_processes(int);
void print_stats();
void time_totals(double total_cpu, double total_burst_time) ;


int shmid_shared;
shared_datat_t *shared;
int semid_oss;
char msg[50];
int process_pid[MAX_PROCESSES] = { 0 }; //  process pids
unsigned char bitvector[8];             // pcb array/bit vector
double avg_cpu_time;
double avg_burst_time;


int main(int argc, char **argv) {
	int process_number;                         // process index
	int process_to_fork;                        // process to fork
	key_t SHMKEY;                               // shared memory key
	key_t SEMKEY;                               // semaphore key
	float max_wait_time;                        // scheduling result
	float current_wait_time = 0;                // result of the current scheduling
	int pcb_index = 0;
	int i, x;
	float process_wait_time = 0;
	float current_time = 0;
	int child_to_sched = 0;
	int c;
	const char     *short_opt = "hn:";
	struct option   long_opt[] = {
		{"help",          no_argument,       NULL, 'h'},
		{NULL,            0,                 NULL, 0  }
	};

	while ((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1)   {
		switch (c) {
			case -1:        //  no more arguments
			case  0:        //  long options toggles
				break;

			// help
			case 'h':
				fprintf(stderr, "Usage: %s no options \n", argv[0]);
				fprintf(stderr, "  -h, --help                print this help and exit\n\n");

			case ':':
			case '?':
				if (isprint(optopt)) {
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
					exit(EXIT_FAILURE);
				} else {
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
					exit(EXIT_FAILURE);
				}

			default:
				abort();
		}
	}

	//  make sure no more arguments. if so exit
	for (i = optind; i < argc; i++) {
		fprintf(stderr, "Non-option argument %s\n", argv[i]);
		exit(EXIT_FAILURE);
	}

	// seed the random number generator
	srand(time(NULL));
	// initialize signals and alarm
	signal_init();
	alarm(TIMEOUT);

	// init bitvector for pcb
	for (i = 0; i < 8; i++) {
		bitvector[i] = 0x00;
	}

	for (i = 0; i <= MAX_PROCESSES; i++) {
		bit_set(bitvector, i, 0);
	}

	/* SHARED MEMORY */
	// shared memory key
	if ((SHMKEY =  ftok("./oss.c", 'A')) == -1) {
		perror("oss: ftok");
		exit(EXIT_FAILURE);
	}

	// semaphore key
	if ((SEMKEY = ftok("./oss.c", 'B')) == -1) {
		perror("oss: ftok (SEMKEY)");
		exit(EXIT_FAILURE);
	}

	// get memory for the shared data
	if ((shmid_shared = shmget(SHMKEY, (sizeof(shared_datat_t) + (18 * sizeof(pcb_t))), 0600 | IPC_CREAT)) ==  - 1) {
		perror("shmget");
		exit(EXIT_FAILURE);
	}

	// shared data pointer
	if ((shared = (shared_datat_t *)(shmat(shmid_shared, 0, 0))) == (void *) - 1) {
		perror("shmat");
		exit(EXIT_FAILURE);
	}

	// init logical clock and time quantum
	shared->sec = 0;
	shared->n_sec = 0;
	shared->quantum = QUANTUM;

	// get semaphore for user oss sharing
	if ((semid_oss = semget(SEMKEY, 1, 0600 | IPC_CREAT)) == -1) {
		perror("semget semid oss");
		exit(EXIT_FAILURE);
	}

	if (semctl(semid_oss, 0, SETVAL, 1) == -1) {
		perror("semctl");
		exit(EXIT_FAILURE);
	}

	while (1) {
		/* FORK */
		// if running < MAX_PROCESSES processes
		if (process_count() < MAX_PROCESSES) {
			// Determine which PCB to use by fork'd process
			for (process_number = 0; process_number <= MAX_PROCESSES; process_number++) {
				if ((process_number + pcb_index) >= MAX_PROCESSES) {
					x = process_number;
				} else {
					x = process_number + pcb_index;
				}

				if (bit_get(bitvector, x) != 1) {
					pcb_index = x;
					process_to_fork = x;
					break;
				}
			}

			sprintf(msg, "forking process number %d \n", process_to_fork);
			writelog(msg);
			write(STDOUT_FILENO, msg, strlen(msg));   // terminal
			fork_processes(process_to_fork);
		}

		/* SCHEDULING  */
		// wait for last scheduled process to run
		sem_wait(semid_oss);
		max_wait_time = 0;

		// see wich process has been waiting the longest o schedule.
		for (i = 0; i < MAX_PROCESSES; i++) {
			current_time = ((float) shared->sec * + (float) shared->n_sec) / 1000;
			process_wait_time = (current_time - shared->child_pcb[i].last_run_time);

			// check for divide by 0
			if (shared->child_pcb[i].total_cpu_time_ms > 0) {
				// get process value for its wait time/ total cpu time
				current_wait_time = process_wait_time / shared->child_pcb[i].total_cpu_time_ms;
			} else {
				current_wait_time = process_wait_time;
			}

			// who has been waiting the longest
			if (current_wait_time > max_wait_time) {
				child_to_sched = i;
				max_wait_time = current_wait_time;
			}

			time_totals(shared->child_pcb[i].total_cpu_time_ms,  shared->child_pcb[i].last_run_time);
		}

		// increment the clock
		shared->n_sec = 1 + (rand() % 1000); //  Random 1 to 1000
		shared->sec++;
		sprintf(msg, "Logical clock: %d.%d seconds \n", shared->sec, shared->n_sec);
		writelog(msg);                            // logfile
		// Now run the process
		sprintf(msg, "process ID %d running \n", process_pid[child_to_sched]);
		writelog(msg);
		shared->selected_process_pid_to_sched = process_pid[child_to_sched];
		shared->child_pcb[child_to_sched].run_status = 0;
	}

	while (r_wait(NULL) > 0) ;  // wait for children

	print_stats();
	sprintf(msg, "\noss: exiting normally \n");
	writelog(msg);                            // logfile
	write(STDOUT_FILENO, msg, strlen(msg));   // terminal
	cleanup();
	return 0;
}

void signal_init() {
	struct sigaction sigint;
	sigint.sa_handler = signal_handler;
	sigint.sa_flags = 0;

	if (sigaction(SIGINT, &sigint, NULL) == -1) {
		perror("signal_init: SIGINT");
	}

	// alarm
	if (signal(SIGALRM, &alarm_handler) == SIG_ERR) {
		perror("signal_init: unable to set SIGALARM");
		exit(EXIT_FAILURE);
	}
}

void signal_handler() {
	sprintf(msg, "\nCaught CONTROL-C\n");
	writelog(msg);                            // logfile
	write(STDOUT_FILENO, msg, strlen(msg));   // terminal
	cleanup();
}

void alarm_handler() {
	sprintf(msg, "\nTimer Elapsed-C\n");
	writelog(msg);                            // logfile
	write(STDOUT_FILENO, msg, strlen(msg));   // terminal
	cleanup();
	raise(SIGALRM);
}



pid_t r_wait(int *stat_loc) {
	int retval;

	while (((retval = wait(stat_loc)) == -1) && (errno == EINTR));

	return retval;
}



void cleanup() {
	writelog("cleaning up\n");
	// remove shared memory
	int returnval = shmctl(shmid_shared, IPC_RMID, (struct shmid_ds *) NULL);

	if (returnval != 0) {
		sprintf(msg, "error releasing shared memory\n");
		writelog(msg);                            // logfile
		write(STDOUT_FILENO, msg, strlen(msg));   // terminal
	}

	// remove semaphore
	if ((semctl(semid_oss, 0, IPC_RMID, 1) == -1)) {
		sprintf(msg, "semctl error semid_oss");
		writelog(msg);                            // logfile
		perror(msg);
	} else {
		sprintf(msg, "removed semaphore\n");
		writelog(msg);                            // logfile
		write(STDOUT_FILENO, msg, strlen(msg));   // terminal
	}

	print_stats();
	exit(EXIT_FAILURE);
}
// pcb initialization function
void init_pcb(int process) {
	shared->child_pcb[process].total_cpu_time_ms = 0;
	shared->child_pcb[process].total_system_time_ms = 0;
	shared->child_pcb[process].previous_burst_time = 0;
	shared->child_pcb[process].last_run_time = (float) shared->sec + ((float) shared->n_sec / 1000);
	shared->child_pcb[process].done_status = 0;
	shared->child_pcb[process].run_status = 0;
}

// Semaphore wait function
void sem_wait(int semid) {
	struct sembuf sbuf;                         // Semaphore operation struct
	sbuf.sem_num = 0;                           // First (and only) semaphore in set
	sbuf.sem_op = -1;                           // Decrement the semaphore
	sbuf.sem_flg = 0;                           // Operation flag

	if (semop(semid, &sbuf, 1) == -1)  {
		sprintf(msg,  "semid: %d\n", semid);
		perror(msg);
		exit(EXIT_FAILURE);
	}
}

int process_count() {
	int i, count = 0;
	int status;

	// check status of processes
	for (i = 0; i < MAX_PROCESSES; i++) {
		if (shared->child_pcb[i].done_status == 1) {
			kill(process_pid[i], SIGTSTP);
			process_pid[i] = wait(&status);
			sprintf(msg, "process %02d returned %d\n", i, WEXITSTATUS(status));
			writelog(msg);
			sprintf(msg, "process %02d has exited\n", i);
			writelog(msg);
			init_pcb(i);
			bit_set(bitvector, i, 0);
		}

		// increment the total process count if pcb used
		if (bit_get(bitvector, i) == 1) {
			count++;
		}
	}

	sprintf(msg, "process count is %d\n", count);
	writelog(msg);
	return count;
}

// bit vector operations
int bit_get(const unsigned char *bits, int pos) {
	unsigned char      mask;
	int                i;
	mask = 0x80;

	for (i = 0; i < (pos % 8); i++) {
		mask = mask >> 1;
	}

	return (((mask & bits[(int)(pos / 8)]) == mask) ? 1 : 0);
}



void bit_set(unsigned char *bits, int pos, int state) {
	unsigned char      mask;
	int                i;
	mask = 0x80;

	for (i = 0; i < (pos % 8); i++) {
		mask = mask >> 1;
	}

	if (state) {
		bits[pos / 8] = bits[pos / 8] | mask;
	} else {
		bits[pos / 8] = bits[pos / 8] & (~mask);
	}

	return;
}

void fork_processes(int process) {
	char arg[3] ;         // string to hold  arg

	if ((process_pid[process] = fork()) < 0) {
		sprintf(msg, "fork process %02d\n", process);
		perror(msg);
		exit(EXIT_FAILURE);
	} else {
		// exec process if pid == 0
		if (process_pid[process] == 0) {
			sprintf(arg, "%02d\n", process);
			execl("./user", "user", arg, (char *) NULL);
			perror("fork");     // error, if one occurs
		} else {
			// im the parent; writelog
			sprintf(msg, "forked process ID %d for process %02d\n", process_pid[process], process);
			writelog(msg);
			bit_set(bitvector, process, 1);         // set the bit flag for to used
		}
	}
}


void time_totals(double total_cpu,  double  total_burst_time) {
	avg_cpu_time += total_cpu;
	avg_burst_time += total_burst_time;
	avg_cpu_time /= 2.0;
	avg_burst_time /= 2.0;
}


void print_stats() {
	fprintf(stderr, "\nSTATISTICS\n");
	fprintf(stderr, "Avg CPU Time    = %8.2f ms \n"\
			"Avg Burst Time  = %8.2f ms\n"\
			, avg_cpu_time, avg_burst_time);
}
