#include <string.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "data.h"

static int mid=-1, qid = -1;	//memory and msg queue identifiers
static struct data *data = NULL;	//shared memory pointer

//Attach the shared memory pointer
static int attach_data(){

	key_t k1 = ftok(PATH_KEY, MEM_KEY);
	key_t k2 = ftok(PATH_KEY, MSG_KEY);
	if((k1 == -1) || (k2 == -1)){
		perror("ftok");
		return -1;
	}

	//get shared memory id
	mid = shmget(k1, 0, 0);
  if(mid == -1){
  	perror("shmget");
  	return -1;
  }

	//get message queue ud
	qid = msgget(k2, 0);
  if(qid == -1){
  	perror("msgget");
  	return -1;
  }

	//attach to the shared memory
	data = (struct data *) shmat(mid, NULL, 0);
	if(data == NULL){
		perror("shmat");
		return -1;
	}
	return 0;
}

static void execute(const struct timeval slice, struct msgbuf *mbuf){

	int decision = 0;
	if((rand() % 100) >= TERMINATE_PROBABILITY){
		//choose one of - use whole slice, use part of slice, or block
		decision = rand() % 3;
	}else{
		//only on option - terminate
		decision = 3;
	}

	switch(decision){
		case 0:		//use whole slice
			mbuf->exec_decision = ST_READY;
			mbuf->exec_time = slice;
			break;

		case 1:	//use part of slice
			mbuf->exec_decision = ST_READY;
			mbuf->exec_time.tv_sec  = 0;
			mbuf->exec_time.tv_usec = (int)((float) slice.tv_usec / (100.0f / (SLICE_MIN + (rand() % SLICE_MAX))));
			break;

		case 2:	//block for io
			mbuf->exec_decision = ST_BLOCKED;
			mbuf->exec_time.tv_sec = rand() % R_VAL;
			mbuf->exec_time.tv_usec = rand() % S_VAL;
			break;

		case 3:	//terminate
			mbuf->exec_decision = ST_TERM;
			mbuf->exec_time.tv_sec = 0;
			mbuf->exec_time.tv_usec = 0;
			break;
	}
}

int main(const int argc, char * const argv[]){

	struct msgbuf mbuf;

	if(	attach_data() < 0)
		return EXIT_FAILURE;

	srand(getpid());

	const pid_t master_pid = getppid();
	int stop = 0;
	while(!stop){

		//wait for slice from master
		if(msgrcv(qid, (void*)&mbuf, MSGBUF_SIZE, getpid(), 0) == -1){
			perror("msgrcv");
			break;
		}

		//printf("CHILD: slice=%d\n", mbuf.exec_time.tv_usec);

		if(mbuf.exec_time.tv_usec == 0){
			break;
		}

		//scheduling logic
		execute(mbuf.exec_time, &mbuf);

		//send our
		mbuf.mtype = master_pid;
		mbuf.pid = getpid();
		if(msgsnd(qid, &mbuf, MSGBUF_SIZE, 0) == -1){
			perror("msgsnd");
			return -1;
		}

		if(mbuf.exec_decision == ST_TERM){
			break;
		}
	}
	shmdt(data);

	return EXIT_SUCCESS;
}
