#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#define DDA 10000

struct timer
{
	unsigned int seconds;
	unsigned int ns;
};

struct resource
{
	unsigned int maxAmt;
	unsigned int available;
	unsigned int request;
	unsigned int allocation;
	unsigned int release;
	unsigned int reqArray[18];
	unsigned int allArray[18];
	unsigned int relArray[18];
	int shared;
};

int errno;
char errmsg[200];
int shmidTime;
int shmidChild;
int shmidTerm;
int shmidRes;
struct timer *shmTime;
int *shmChild;
int *shmTerm;
struct resource *shmRes;
sem_t * semDead;
sem_t * semTerm;
sem_t * semChild;
int lockProc[18] = {0};
/* Insert other shmid values here */


void sigIntHandler(int signum)
{
	/* Send a message to stderr */
	snprintf(errmsg, sizeof(errmsg), "OSS: Caught SIGINT! Killing all child processes.");
	perror(errmsg);	
	
	/* Deallocate shared memory */
	errno = shmdt(shmTime);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmTime)");
		perror(errmsg);	
	}
	
	errno = shmctl(shmidTime, IPC_RMID, NULL);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidTime)");
		perror(errmsg);	
	}
	
	errno = shmdt(shmChild);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmChild)");
		perror(errmsg);	
	}
	
	errno = shmctl(shmidChild, IPC_RMID, NULL);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidChild)");
		perror(errmsg);	
	}
	
	errno = shmdt(shmTerm);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmTerm)");
		perror(errmsg);	
	}
	
	errno = shmctl(shmidTerm, IPC_RMID, NULL);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidTerm)");
		perror(errmsg);	
	}
	
	errno = shmdt(shmRes);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmRes)");
		perror(errmsg);	
	}
	
	errno = shmctl(shmidRes, IPC_RMID, NULL);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidRes)");
		perror(errmsg);	
	}
	
	/* Close Semaphore */
	sem_unlink("semDead");   
    sem_close(semDead);
	sem_unlink("semTerm");
	sem_close(semTerm);
	sem_unlink("semChild");
	sem_close(semChild);
	/* Exit program */
	exit(signum);
}

int deadlock(unsigned int maxRes, unsigned int maxSlaves, unsigned int numShared)
{
	/* resources */
	unsigned int work[maxRes]; 
	/* processes */
	unsigned int finish[maxSlaves]; 
	int i;
	int p;
	int deadlocked = 0;
	int numLocked = 0;
	for(i = 0; i < maxRes; i++)
	{
		work[i] = shmRes[i].available;
	}
	for(i = 0; i < maxSlaves; i++)
	{
		finish[i] = 0;
	}
	for(p = 0; p < maxSlaves; p++)
	{
		if( finish[p] )
		{
			continue;
		}
		if(req_lt_avail (work, p, maxRes, numShared))
		{
			finish[p] = 1;
			for(i = 0; i < maxRes; i++)
			work[i] += shmRes[i].allArray[p];
			p = -1;
		}
	}
	for(p = 0; p < maxSlaves; p++)
	{
		if(!finish[p])
		{			
			numLocked++;
			lockProc[p] = 1;
			if(numLocked >= 2)
			{
				deadlocked = 1;
			}
		}
	}
	return(deadlocked);
}

int req_lt_avail(unsigned int *work, unsigned int procNum, unsigned int maxRes, unsigned int numShared)
{
	int i;
	for(i = numShared; i < maxRes; i++)
	{
		if(shmRes[i].reqArray[procNum] > work[i])
		{
			break;
		}
	}
	return(i == maxRes);
}

void deadClear(int maxRes, int maxSlaves, int numShared)
{
	int i;
	int killed = 0;
	for(i = 0; i < maxSlaves; i++)
	{
		if(lockProc[i] == 1 && killed == 0)
		{
			printf("Killing P%d...\n", i);
			kill(shmChild[i], SIGINT);
			sem_wait(semDead);
			wait(shmChild[i]);
			killed = 1;
		}
		if(killed == 1)
		{
			lockProc[i] = 0;
		}
	}
	if(killed == 1)
	{
		if(deadlock(maxRes, maxSlaves, numShared))
		{
			printf("Deadlock Still Detected!\n");
			/* Display Deadlocks */
			printf("Deadlocked Processes:");
			for(i = 0; i < maxSlaves; i++)
			{
				if(lockProc[i] == 1)
				{
					printf("P%2d, ", i);
				}
			}
			printf("\nRepeating deadClear()\n");
			deadClear(maxRes, maxSlaves, numShared);
		}
	}
}

int main (int argc, char *argv[]) {
int o;
int i;
int j;
int children[18] = {0}; 
int maxSlaves = 1;
int numSlaves = 0;
int numProc = 0;
int maxTime = 20;
char *sParam = NULL;
char *lParam = NULL;
char *tParam = NULL;
char timeArg[33];
char childArg[33];
char indexArg[33];
char termArg[33];
char resArg[33];
pid_t pid = getpid();
key_t keyTime = 8675;
key_t keyChild = 5309;
key_t keyTerm = 1138;
key_t keyRes = 8311;
FILE *fp;
char *fileName = "./msglog.out";
signal(SIGINT, sigIntHandler);
time_t start;
time_t stop;
struct timer nextProc = {0};
int numShared = 0;
int cycles = 0;
int pKill = -1; 
int deadlocked = 0;
int numLocked = 0;
int deadCount = 0;
int deadKills = 0;

/* Seed RNG */
srand(pid * time(NULL));

/* Options */
while ((o = getopt (argc, argv, "hs:l:t:")) != -1)
{
	switch (o)
	{
		case 'h':
			snprintf(errmsg, sizeof(errmsg), "oss.c simulates a primitive operating system that spawns processes and logs completion times to a file using a simulated clock.\n\n");
			printf(errmsg);
			snprintf(errmsg, sizeof(errmsg), "oss.c options:\n\n-h\tHelp option: displays options and their usage for oss.c.\nUsage:\t ./oss -h\n\n");
			printf(errmsg);
			snprintf(errmsg, sizeof(errmsg), "-s\tSlave option: this option sets the number of slave processes from 1-19 (default 5).\nUsage:\t ./oss -s 10\n\n");
			printf(errmsg);
			snprintf(errmsg, sizeof(errmsg), "-l\tLogfile option: this option changes the name of the logfile to the chosen parameter (default msglog.out).\nUsage:\t ./oss -l output.txt\n\n");
			printf(errmsg);
			snprintf(errmsg, sizeof(errmsg), "-t\tTimeout option: this option sets the maximum run time allowed by the program in seconds before terminating (default 20).\nUsage:\t ./oss -t 5\n");
			printf(errmsg);
			exit(1);
			break;
		case 's':
			sParam = optarg;
			break;
		case 'l':
			lParam = optarg;
			break;
		case 't':
			tParam = optarg;
			break;
		case '?':
			if (optopt == 's' || optopt == 'l' || optopt == 't')
			{
				snprintf(errmsg, sizeof(errmsg), "OSS: Option -%c requires an argument.", optopt);
				perror(errmsg);
			}
			return 1;
		default:
			break;
	}	
}

/* Set maximum number of slave processes */
if(sParam != NULL)
{
	maxSlaves = atoi(sParam);
}
if(maxSlaves < 0)
{
	maxSlaves = 5;
}
if(maxSlaves > 18)
{
	maxSlaves = 18;
}

/* Set name of log file */
if(lParam != NULL)
{
	fp = fopen(lParam, "a");
	if(fp == NULL)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: fopen(lParam).");
		perror(errmsg);
	}
}
else
{
	fp = fopen(fileName, "a");
	if(fp == NULL)
	{
		snprintf(errmsg, sizeof(errmsg), "OSS: fopen(fileName).");
		perror(errmsg);
	}
}

/* Set maximum alloted run time in real time seconds */
if(tParam != NULL)
{
	maxTime = atoi(tParam);
}

/********************MEMORY ALLOCATION********************/
/* Create shared memory segment for a struct timer */
shmidTime = shmget(keyTime, sizeof(struct timer), IPC_CREAT | 0666);
if (shmidTime < 0)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmget(keyTime...)");
	perror(errmsg);
	exit(1);
}

/* Point shmTime to shared memory */
shmTime = shmat(shmidTime, NULL, 0);
if ((void *)shmTime == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmat(shmidTime)");
	perror(errmsg);
    exit(1);
}

/* Create shared memory segment for a child array */
shmidChild = shmget(keyChild, sizeof(int)*18, IPC_CREAT | 0666);
if (shmidChild < 0)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmget(keyChild...)");
	perror(errmsg);
	exit(1);
}

/* Point shmChild to shared memory */
shmChild = shmat(shmidChild, NULL, 0);
if ((void *)shmChild == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmat(shmidChild)");
	perror(errmsg);
    exit(1);
}

/* Create shared memory segment for a child termination status */
shmidTerm = shmget(keyTerm, sizeof(int)*19, IPC_CREAT | 0666);
if (shmidTerm < 0)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmget(keyTerm...)");
	perror(errmsg);
	exit(1);
}

/* Point shmTerm to shared memory */
shmTerm = shmat(shmidTerm, NULL, 0);
if ((void *)shmTerm == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmat(shmidTerm)");
	perror(errmsg);
    exit(1);
}

/* Create shared memory segment for a resource vector */
shmidRes = shmget(keyRes, sizeof(struct resource)*20, IPC_CREAT | 0666);
if (shmidRes < 0)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmget(keyRes...)");
	perror(errmsg);
	exit(1);
}

/* Point shmRes to shared memory */
shmRes = shmat(shmidRes, NULL, 0);
if ((void *)shmRes == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmat(shmidRes)");
	perror(errmsg);
    exit(1);
}
/********************END ALLOCATION********************/

/********************INITIALIZATION********************/
/* Convert shmTime and shmChild keys into strings for EXEC parameters */
sprintf(timeArg, "%d", shmidTime);
sprintf(childArg, "%d", shmidChild);
sprintf(termArg, "%d", shmidTerm);
sprintf(resArg, "%d", shmidRes);

/* Set the time to 00.00 */
shmTime->seconds = 0;
shmTime->ns = 0;

/* Set shmChild array indices to 0 */
for(i =0; i<maxSlaves; i++)
{
	shmChild[i] = 0;
	shmTerm[i] = 0;
}

/* Random allocation for resources in shmRes */
for(i = 0; i < 20; i++)
{
	shmRes[i].maxAmt = rand()%10+1;
	shmRes[i].available = shmRes[i].maxAmt;
}

/* Random allocation of shareable resources */
numShared = rand()%3+3;
for(i = 0; i < numShared; i++)
{
	shmRes[i].shared = 1;
}
for(i = numShared; i < 20; i++)
{
	shmRes[i].shared = 0;
}

/* Allocation for resource arrays in shmRes */
for(i = 0; i < 20; i++)
{
	for(j = 0; j < maxSlaves; j++)
	{
		shmRes[i].reqArray[j] = 0;
		shmRes[i].allArray[j] = 0;
		shmRes[i].relArray[j] = 0;
	}
}

/********************END INITIALIZATION********************/

/********************SEMAPHORE CREATION********************/
/* Open Semaphore */
semDead=sem_open("semDead", O_CREAT | O_EXCL, 0644, 0);
if(semDead == SEM_FAILED) {
	snprintf(errmsg, sizeof(errmsg), "OSS: sem_open(semDead)...");
	perror(errmsg);
    exit(1);
}

semTerm=sem_open("semTerm", O_CREAT | O_EXCL, 0644, 1);
if(semTerm == SEM_FAILED) {
	snprintf(errmsg, sizeof(errmsg), "OSS: sem_open(semTerm)...");
	perror(errmsg);
	exit(1);
}    
semChild=sem_open("semChild", O_CREAT | O_EXCL, 0644, 1);
if(semChild == SEM_FAILED) {
	snprintf(errmsg, sizeof(errmsg), "OSS: sem_open(semChild)...");
	perror(errmsg);
	exit(1);
}    
/********************END SEMAPHORE CREATION********************/

/* Schedule next process */
nextProc.seconds = 0;
nextProc.ns = rand()%500000001;

/* Start the timer */
start = time(NULL);


/**********************Main Program Loop**********************/
do
{
	/* Has enough time passed to spawn a child? */
	if(shmTime->seconds >= nextProc.seconds && shmTime->ns >= nextProc.ns)
	{
		/* Check shmChild for first unused process */
		sem_wait(semChild);
		for(i = 0; i < maxSlaves; i++)
		{
			
			if(shmChild[i] == 0)
			{
				sprintf(indexArg, "%d", i);
				break;
			}
			
		}
		sem_post(semChild);
		
		/* Check that number of currently running processes is under the limit (maxSlaves) */
		if(numSlaves < maxSlaves)
		{
			nextProc.ns += rand()%500000001;
			if(nextProc.ns >= 1000000000)
			{
				nextProc.seconds += 1;
				nextProc.ns -= 1000000000;
			}
			numSlaves += 1;
			numProc += 1;
			pid = fork();
			if(pid == 0)
			{
				pid = getpid();
				shmChild[i] = pid;
				execl("./user", "user", timeArg, childArg, indexArg, termArg, resArg, (char*)0);
			}
		}
	}
	
	/* Check for Resource Requests/Releases */
	for(i = 0; i < 20; i++)
	{
		for(j = 0; j < maxSlaves; j++)
		{
			if(shmRes[i].reqArray[j] == 1 && shmRes[i].allocation < shmRes[i].maxAmt)
			{
				shmRes[i].allocation++;
				shmRes[i].available--;
				shmRes[i].reqArray[j] = 0;
				shmRes[i].allArray[j]++;
			}
			if(shmRes[i].relArray[j] >= 1)
			{
				shmRes[i].allocation -= shmRes[i].relArray[j];
				shmRes[i].available += shmRes[i].relArray[j];
				shmRes[i].allArray[j] -= shmRes[i].relArray[j];
				shmRes[i].relArray[j] = 0;
			}
		}
	}
	
	/* Deadlock Detection Algorithm */
	if(cycles%DDA == 0 && numSlaves == maxSlaves)
	{
			if(deadlock(20, maxSlaves, numShared))
			{
				deadlocked = 1;
				deadCount++;
				printf("Deadlock Detected!\n");
				/* Display Deadlocks */
				printf("Deadlocked Processes:");
				for(i = 0; i < maxSlaves; i++)
				{
					if(lockProc[i] == 1)
					{
						printf("P%2d, ", i);
					}
				}
				printf("\n");
				deadClear(20, maxSlaves, numShared);
				sleep(1);
			}
			else
			{
				deadlocked = 0;
			}
	}
	
	/* Check for terminating children */
	sem_wait(semTerm);
	for(i = 0; i < maxSlaves; i++)
	{
		
		if(shmTerm[i] == 1)
		{
			/* sem_wait(semTerm); */
			snprintf(errmsg, sizeof(errmsg), "OSS: shmTerm %d is terminating!", i);
			perror(errmsg);
			/* wait(shmChild[i]); */
			shmChild[i] = 0;
			numSlaves--;
			shmTerm[i] = 0;
		}
		
	}
	sem_post(semTerm);
		
	/* Update the clock */
	shmTime->ns += (rand()%10000) + 1;
	if(shmTime->ns >= 1000000000)
	{
		shmTime->ns -= 1000000000;
		shmTime->seconds += 1;
	}
	deadKills = shmTerm[19];
	cycles++;
	stop = time(NULL);
}while(stop-start < maxTime && numProc < 100);
/********************End Main Program Loop********************/

deadKills = shmTerm[19];
sleep(1);
/* Kill all slave processes */
for(i = 0; i < maxSlaves; i++)
{
	if(shmChild[i] != 0)
	{
		printf("Killing process #%d, PID = %d\n", i, shmChild[i]);
		kill(shmChild[i], SIGINT);
		sem_wait(semDead);
		wait(shmChild[i]);
	}
	else
	{
		printf("Not killing process #%d, PID = %d\n", i, shmChild[i]);
	}
}

sleep(1);
/* Display Resource Vector (final result) */
printf("Resource Vector:\nResource:   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19\nAmount:    ");
for(i = 0; i < 20; i++)
{
	printf("%2d ", shmRes[i].maxAmt);
}
/* Display Resource Sharing */
printf("\nShareable: ");
for(i = 0; i < 20; i++)
{
	printf("%2d ", shmRes[i].shared);
}
/* Display Resource Allocation Table */
printf("\n\nAllocation Table:\nResource:   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19\n");
for(i = 0; i < maxSlaves; i++)
{
	printf("P%2d:       ", i);
	for(j = 0; j < 20; j++)
	{
		printf("%2d ", shmRes[j].allArray[i]);
	}
	printf("\n");
}
/* Display Resource Request Table */
printf("\n\nRequest Table:\nResource:   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19\n");
for(i = 0; i < maxSlaves; i++)
{
	printf("P%2d:       ", i);
	for(j = 0; j < 20; j++)
	{
		printf("%2d ", shmRes[j].reqArray[i]);
	}
	printf("\n");
}
/* Display Resource Release Table */
printf("\n\nRelease Table:\nResource:   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19\n");
for(i = 0; i < maxSlaves; i++)
{
	printf("P%2d:       ", i);
	for(j = 0; j < 20; j++)
	{
		printf("%2d ", shmRes[j].relArray[i]);
	}
	printf("\n");
}
/* Display Available Resources */
printf("\n\nAvailable Resources:\nResource:   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19\nAmount:    ");
for(i = 0; i < 20; i++)
{
	printf("%2d ", shmRes[i].available);
}
printf("\n");
/* Display Deadlock Count */
printf("\n\nNumber of Deadlocks: %d\n", deadCount);
/* Display Deadlock Kills */
printf("\n\nNumber of Killed Deadlocked Processes: %d\n", deadKills);
/********************DEALLOCATE MEMORY********************/
errno = shmdt(shmTime);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmTime)");
	perror(errmsg);	
}

errno = shmctl(shmidTime, IPC_RMID, NULL);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidTime)");
	perror(errmsg);	
}

errno = shmdt(shmChild);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmChild)");
	perror(errmsg);	
}

errno = shmctl(shmidChild, IPC_RMID, NULL);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidChild)");
	perror(errmsg);	
}

errno = shmdt(shmTerm);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmTerm)");
	perror(errmsg);	
}

errno = shmctl(shmidTerm, IPC_RMID, NULL);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidTerm)");
	perror(errmsg);	
}

errno = shmdt(shmRes);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmdt(shmRes)");
	perror(errmsg);	
}

errno = shmctl(shmidRes, IPC_RMID, NULL);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "OSS: shmctl(shmidRes)");
	perror(errmsg);	
}
/********************END DEALLOCATION********************/

/* Close Semaphore */
sem_unlink("semDead");   
sem_close(semDead);
sem_unlink("semTerm");
sem_close(semTerm);
sem_unlink("semChild");
sem_close(semChild);
return 0;
}
