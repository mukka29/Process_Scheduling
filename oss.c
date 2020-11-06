#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "queue.h"

struct option{
  int val;  //current value
  int max;  //maximum value
};

struct option started, running, runtime;  //-n, -s, -t options
struct option exited; //exited processes and word index

static int mid=-1, qid = -1;
static struct data *data = NULL;

static int loop_flag = 1;
static struct timeval timestep;

#define QUANTUM 10000000
//Size of the multi-level feedback queue
#define FB_SIZE 4

static queue_t blocked, fb_ready[FB_SIZE]; //blocked and feedback queu
static int fb_quant[FB_SIZE];              //quantum for each ready queue

enum sim_times {TS_IDLE, TS_TURN, TS_WAIT, TS_SLEEP, TS_COUNT};
static struct timeval T[TS_COUNT];

static int idle_flag = 0; //CPU flag showing if CPU is idle
static struct timeval timer_idle = {0,0}; //to save duration of each idle period
static struct timeval timer_fork = {0,0}; //tells when to fork next

static unsigned int bitvector = 0; //bit vector for user[]

/* Return bit value */
static int check_bit(const int b){
  return ((bitvector & (1 << b)) >> b);
}

/* Switch bit value */
static void switch_bit(const int b){
  bitvector ^= (1 << b);
}

/* Find unset bit */
static int find_bit(){
  int i;
  for(i=0; i < RUNNING_MAX; i++){
    if(check_bit(i) == 0){
      return i;
    }
  }
  return -1;
}

/* Increment a timer */
static void timerinc(struct timeval *a, struct timeval * inc){
  struct timeval res;
  timeradd(a, inc, &res);
  *a = res;
}

//Exit master cleanly
static void clean_exit(const int code){
  int i;
  struct msgbuf mbuf;

  if(data){
    //empty slice
    mbuf.exec_time.tv_sec = 0;
    mbuf.exec_time.tv_usec = 0;

    //send empty slice to all running processes to terminate
    for(i=0; i < RUNNING_MAX; i++){
      if(data->users[i].pid > 0){
        mbuf.mtype = data->users[i].pid;
        if(msgsnd(qid, &mbuf, MSGBUF_SIZE, 0) == -1){
          perror("msgsnd");
        }
      }
    }
  }

  printf("Master:  Done (exit %d)at system time %lu:%li\n", code, data->timer.tv_sec, data->timer.tv_usec);

  q_deinit(&blocked);
  for(i=0; i < FB_SIZE; ++i){
    q_deinit(&fb_ready[i]);
  }

  //clean shared memory and semaphores
  shmctl(mid, IPC_RMID, NULL);
  msgctl(qid, 0, IPC_RMID);
  shmdt(data);

	exit(code);
}

//Spawn a user program
static int spawn_user(){

  const int id = find_bit();
  if(id == -1){
    //fprintf(stderr, "No bits. Running=%d\n", running.val);
    return -1;
  }
  struct user_pcb * usr = &data->users[id];


	const pid_t pid = fork();
  if(pid == -1){
    perror("fork");

  }else if(pid == 0){

    execl("user", "user", NULL);
    perror("execl");
    exit(EXIT_FAILURE);

  }else{
    ++running.val;
    switch_bit(id);
    //fprintf(stderr, "Running=%d\n", running.val);

    usr->pid = pid;
    usr->id	= started.val++;
    usr->state = ST_READY;
    usr->t[T_READY] = data->timer;
    usr->t[T_FORKED] =  data->timer;

    if(q_enq(&fb_ready[0], id) < 0){
      printf("OSS: Enqueue of process with PID %d failed at system time %lu:%li\n",  usr->pid, data->timer.tv_sec, data->timer.tv_usec);
    }else{
      printf("OSS: Generating process with PID %u and putting it in queue 0 at system time %lu:%li\n", usr->id, data->timer.tv_sec, data->timer.tv_usec);
    }
  }

	return pid;
}

//Show usage menu
static void usage(){
  printf("Usage: master [-c 5] [-l log.txt] [-t 20]\n");
  printf("\t-h\t Show this message\n");
  printf("\t-c 5\tMaximum processes to be started\n");
  printf("\t-l log.txt\t Log file \n");
  printf("\t-t 20\t Maximum runtime\n");
}

//Set options specified as program arguments
static int set_options(const int argc, char * const argv[]){

  //default options
  started.val = 0; started.max = 100; //max users started
  exited.val  = 0; exited.max = 100;
  runtime.val = 0; runtime.max = 3;  //maximum runtime in real seconds
  running.val = 0; running.max = RUNNING_MAX;


  int c, redir=0;
	while((c = getopt(argc, argv, "hc:l:t:")) != -1){
		switch(c){
			case 'h':
        usage();
				return -1;
      case 'c':  started.val	= atoi(optarg); break;
			case 'l':
        stdout = freopen(optarg, "w", stdout);
        redir = 1;
        break;
      case 't':  runtime.max	= atoi(optarg); break;
			default:
				printf("Error: Option '%c' is invalid\n", c);
				return -1;
		}
	}

  if(redir == 0){
    stdout = freopen("log.txt", "w", stdout);
  }
  return 0;
}

//Attach the data in shared memory
static int attach_data(){

	key_t k1 = ftok(PATH_KEY, MEM_KEY);
	key_t k2 = ftok(PATH_KEY, MSG_KEY);
	if((k1 == -1) || (k2 == -1)){
		perror("ftok");
		return -1;
	}

  //create the shared memory area
  const int flags = IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR;
	mid = shmget(k1, sizeof(struct data), flags);
  if(mid == -1){
  	perror("shmget");
  	return -1;
  }

  //create the message queue
	qid = msgget(k2, flags);
  if(qid == -1){
  	perror("msgget");
  	return -1;
  }

  //attach to the shared memory area
	data = (struct data *) shmat(mid, NULL, 0);
	if(data == NULL){
		perror("shmat");
		return -1;
	}

  //clear the memory
  bzero(data, sizeof(struct data));

	return 0;
}

//Wait all finished user programs
static void reap_zombies(){
  pid_t pid;
  int status;

  //while we have a process, that exited
  while((pid = waitpid(-1, &status, WNOHANG)) > 0){

    if (WIFEXITED(status)) {
      printf("Master: Child %i exited code %d at systm time %lu:%li\n", pid, WEXITSTATUS(status), data->timer.tv_sec, data->timer.tv_usec);
    }else if(WIFSIGNALED(status)){
      printf("Master: Child %i signalled with %d at systm time %lu:%li\n", pid, WTERMSIG(status), data->timer.tv_sec, data->timer.tv_usec);
    }

    --running.val;
    //fprintf(stderr, "Reaped %d\n", pid);
    if(++exited.val >= started.max){
      printf("OSS: All children exited\n");
      loop_flag = 0;
    }
  }
}

//Process signals sent to master
static void sig_handler(const int signal){

  switch(signal){
    case SIGINT:  //interrupt signal
      printf("Master: Signal TERM receivedat system time %lu:%li\n", data->timer.tv_sec, data->timer.tv_usec);
      loop_flag = 0;  //stop master loop
      break;

    case SIGALRM: //alarm - end of runtime
      printf("Master:  Signal ALRM receivedat system time %lu:%li\n", data->timer.tv_sec, data->timer.tv_usec);
      loop_flag = 0;
      break;

    case SIGCHLD: //user program exited
      reap_zombies();
      break;

    default:
      break;
  }
}

static int schedule_blocked(){

  int i;
  for(i=0; i < blocked.len; i++){
    const int id = blocked.items[i];
    if(timercmp(&data->timer, &data->users[id].t[T_BLOCKED], >=) != 0){
      break;
    }
  }

  if(i == blocked.len){
    return 0; //nobody to unblock
  }

  const int id = q_deq(&blocked, i);
  struct user_pcb * usr = &data->users[id];

  //update global timer for slepp time
  timerinc(&T[TS_SLEEP], &usr->t[T_BURST]);

  //process becomes ready
  usr->state = ST_READY;
  //clear timer for IO and burst
  timerclear(&usr->t[T_BLOCKED]);
  timerclear(&usr->t[T_BURST]);
  //save the time process became ready
  usr->t[T_READY] = data->timer;

  //and goes to first ready queue
  if(q_enq(&fb_ready[0], id) < 0){
    fprintf(stderr, "OSS: Failed to enqueue process with PID %d at system time %lu:%li\n", usr->id, data->timer.tv_sec, data->timer.tv_usec);
    return -1;
  }else{
    printf("OSS: Unblocked process with PID %d to queue 0 at system time %lu:%li\n", usr->id, data->timer.tv_sec, data->timer.tv_usec);
    return 0;
  }
}

//Put CPU in idle mode
static void idle_mode_on(){
  if(idle_flag) //if we are already in idle mode
    return;

  printf("OSS: IDLE ON at system time %lu:%li\n", data->timer.tv_sec, data->timer.tv_usec);
  timer_idle = data->timer; //save idle time stamp
  idle_flag = 1;
}

//Remove idle CPU mode
static void idle_mode_off(){
  struct timeval tv;

  if(idle_flag == 0)
    return;

  timersub(&data->timer, &timer_idle, &tv);

  printf("OSS: IDLE OFF (lasted %lu:%li) at system time %lu:%li\n", tv.tv_sec, tv.tv_usec, data->timer.tv_sec, data->timer.tv_usec);
  timerinc(&T[TS_IDLE], &tv);
  timerclear(&timer_idle);
  idle_flag = 0;
}

static void time_jump(){

  if(q_len(&blocked) == 0){ //no blocked processes
    //fork time jump
    printf("OSS: Jump to fork timer %lu:%li at sysmte time %lu:%li\n", timer_fork.tv_sec, timer_fork.tv_usec, data->timer.tv_sec, data->timer.tv_usec);
    data->timer = timer_fork;

  }else{
    //blocked time jump
    const int id = q_top(&blocked);
    struct user_pcb * usr = &data->users[id];
    printf("OSS: No process ready. Setting time to first unblock %lu:%li at system time %lu:%li\n",
      usr->t[T_BLOCKED].tv_sec, usr->t[T_BLOCKED].tv_usec, data->timer.tv_sec, data->timer.tv_usec);

    data->timer = usr->t[T_BLOCKED];
  }
}

static int schedule_dispatch(const int q){
  struct msgbuf mbuf;
  struct timeval tv;
  const int id = q_deq(&fb_ready[q], 0);
  int nextq = q;

  struct user_pcb * usr = &data->users[id];

  printf("OSS: Dispatching process with PID %u from queue %i at system time %lu:%li\n", usr->id, q, data->timer.tv_sec, data->timer.tv_usec);

  mbuf.mtype = usr->pid;
  mbuf.exec_time.tv_sec = 0;
  mbuf.exec_time.tv_usec = fb_quant[q];

  if( (msgsnd(qid, &mbuf, MSGBUF_SIZE, 0) == -1) ||
      (msgrcv(qid, (void*)&mbuf, MSGBUF_SIZE, getpid(), 0) == -1)){
		perror("schedule_dispatch");
		return -1;
	}

  usr->t[T_BURST]  = mbuf.exec_time;
  usr->state       = mbuf.exec_decision;

  switch(usr->state){
    case ST_TERM:
      timerinc(&usr->t[T_CPU], &usr->t[T_BURST]);
      timersub(&data->timer, &usr->t[T_FORKED], &usr->t[T_SYS]);
      printf("OSS: Process with PID %u terminated at system time %lu:%li\n", usr->id, data->timer.tv_sec, data->timer.tv_usec);

      timerinc(&T[TS_TURN],    &usr->t[T_SYS]);
      timersub(&usr->t[T_SYS], &usr->t[T_CPU], &tv);
      timerinc(&T[TS_WAIT], &tv);

      printf("OSS: Process with PID %u terminated, removed from queue %d at system time %lu:%li\n", usr->id, q, data->timer.tv_sec, data->timer.tv_usec);

      switch_bit(id);
      //reap_zombies();
      bzero(usr, sizeof(struct user_pcb));

      break;

    case ST_BLOCKED:
      printf("OSS: Process with PID %u has blocked on IO to %lu:%li at system time %lu:%li\n", usr->id, usr->t[T_BURST].tv_sec, usr->t[T_BURST].tv_usec, data->timer.tv_sec, data->timer.tv_usec);

      timerinc(&usr->t[T_BLOCKED], &usr->t[T_BURST]);
      timerinc(&usr->t[T_BLOCKED], &data->timer);

      printf("OSS: Putting process with PID %u into blocked queue at system time %lu:%li\n", usr->id, data->timer.tv_sec, data->timer.tv_usec);
      q_enq(&blocked, id);
      break;

    case ST_READY:

      if(mbuf.exec_time.tv_usec == fb_quant[q]){  //if not preepted
        //change to next queue
        if(q < (FB_SIZE - 1)){
          nextq = q + 1;
        }else{
          nextq = q;
        }
      }else{
        nextq = q;
        printf("OSS: not using its entire time quantum at system time %lu:%li\n", data->timer.tv_sec, data->timer.tv_usec);
      }
      usr->t[T_READY] = data->timer;

      printf("OSS: Receiving that process with PID %u ran for %li nanoseconds at system time %lu:%li\n", usr->id, usr->t[T_BURST].tv_usec, data->timer.tv_sec, data->timer.tv_usec);
      timerinc(&usr->t[T_CPU], &usr->t[T_BURST]);
      timerinc(&data->timer,   &usr->t[T_BURST]);

      printf("OSS: Process with PID %u moved to queue %d at system time %lu:%li\n", usr->id, nextq, data->timer.tv_sec, data->timer.tv_usec);
      q_enq(&fb_ready[nextq], id);
      break;
    default:
      return -1;
      break;
  }

  //calculate dispatch time
  tv.tv_sec = 0; tv.tv_usec = rand() % 100;
  timerinc(&data->timer, &tv);
  printf("OSS: total time this dispatching was %li nanoseconds at system time %lu:%li\n", tv.tv_usec, data->timer.tv_sec, data->timer.tv_usec);

  return 0;
}

static int schedule_ready(){

  int i;
  //find next ready queue, that has a process
  for(i=0; i < FB_SIZE; ++i){

    const int id = q_top(&fb_ready[i]);
    if(id == -1){
      continue;
    }

    if(data->users[id].state == ST_READY){
      break;
    }
  }

  if(i == FB_SIZE){ //all queues checked
    idle_mode_on();
    time_jump();
    return -1;  //nobody ready
  }else{
    //if we are in idle mode
    idle_mode_off();  //turn it off
    //and dispatch one process from queue i
    if(schedule_dispatch(i) < 0){

      return -1;
    }
  }
  return 0;
}

int main(const int argc, char * const argv[]){

  int i;
  const int maxTimeBetweenNewProcsSecs = 1;
  const int maxTimeBetweenNewProcsNS = 500000;

  if( (attach_data() < 0)||
      (set_options(argc, argv) < 0)){
      clean_exit(EXIT_FAILURE);
  }

  //our clock step of 100 ns
  timestep.tv_sec = 0;
  timestep.tv_usec = 100;

  //ignore signals to avoid interrupts in msgrcv
  signal(SIGINT,  sig_handler);
  signal(SIGTERM, sig_handler);
  signal(SIGCHLD, sig_handler);
  signal(SIGALRM, sig_handler);

  alarm(runtime.max);

  //initialize the queues
  int quant = QUANTUM;
  q_init(&blocked, started.max);
  for(i=0; i < FB_SIZE; i++, quant *= 2){
    q_init(&fb_ready[i], started.max);
    fb_quant[i] = quant;
  }

	while(loop_flag){

    //clock moves forward
    timerinc(&data->timer, &timestep);
    if(timercmp(&data->timer, &timer_fork, >=) != 0){

      if(started.val < started.max){
        spawn_user();
      }else{  //we have generated all of the children
        break;
      }

      //set next fork time
      timer_fork.tv_sec  = data->timer.tv_sec + (rand() % maxTimeBetweenNewProcsSecs);
      timer_fork.tv_usec = data->timer.tv_usec + (rand() % maxTimeBetweenNewProcsNS);
    }

    schedule_blocked();
    schedule_ready();
	}

  //average the turnaround, wait and sleep times
  T[TS_TURN].tv_sec /= started.val; T[TS_TURN].tv_usec /= started.val;
  T[TS_WAIT].tv_sec /= started.val; T[TS_WAIT].tv_usec /= started.val;
  T[TS_SLEEP].tv_sec /= started.val; T[TS_SLEEP].tv_usec /= started.val;

  printf("Time taken: %lu:%li\n", data->timer.tv_sec, data->timer.tv_usec);
  printf("Turnaround Timer (average): %lu:%li\n",  T[TS_TURN].tv_sec,   T[TS_TURN].tv_usec);
  printf("Wait Timer (average): %lu:%li\n",        T[TS_WAIT].tv_sec,   T[TS_WAIT].tv_usec);
  printf("IO Timer (average): %lu:%li\n",          T[TS_SLEEP].tv_sec,  T[TS_SLEEP].tv_usec);
  printf("CPU Idle Timer (total): %lu:%li\n",      T[TS_IDLE].tv_sec,   T[TS_IDLE].tv_usec);

	clean_exit(EXIT_SUCCESS);
  return 0;
}
