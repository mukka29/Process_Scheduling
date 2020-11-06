#include <sys/time.h>

#define RUNNING_MAX 18

//Constants for our program
#define PATH_KEY "/"
#define MSG_KEY 1111
#define MEM_KEY 2222

enum states { ST_READY=1, ST_BLOCKED, ST_TERM, ST_COUNT};
enum times { T_CPU=0, T_SYS, T_BURST, T_FORKED, T_BLOCKED, T_READY, T_COUNT};

struct user_pcb {
	int	pid;
	int id;
	enum states state;

	struct timeval t[T_COUNT];
};

struct data {
	struct timeval timer;	//time
	struct user_pcb users[RUNNING_MAX];
};

//Probability in percent, for a user process to terminate
#define TERMINATE_PROBABILITY 15

#define R_VAL 3
#define S_VAL 1000

#define SLICE_MIN 1
#define SLICE_MAX 99

//Message queue buffer
struct msgbuf {
	long mtype;					//the type of message
	int pid;						//who is sending the message. Needed by master, to send reply

	enum states exec_decision;				//user decision
	struct timeval exec_time;	//decision execution time
};

//how big is our message ( without the mtype)
#define MSGBUF_SIZE (sizeof(int) + sizeof(enum states) + sizeof(struct timeval))
