#include "data.h"

struct queue {
	int * items;
	int len, size;
};
typedef struct queue queue_t;

int q_init(queue_t * q, const int size);
void q_deinit(queue_t * q);

int q_enq(queue_t * q, const int item);
int q_deq(queue_t * q, const int at);
int q_top(queue_t * q);
int q_len(queue_t * q);
