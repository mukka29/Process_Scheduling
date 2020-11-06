#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include "queue.h"

//Initialize an empty queue
int q_init(queue_t * q, const int size){
  //allocate space for the queue items
  q->items = (int*) malloc(sizeof(int)*size);
  if(q->items == NULL){ //if malloc failed
    perror("malloc");
    return -1;
  }
  //queue is empty
  q->size = size;
  q->len = 0;
  return 0;
}

//Deinitialize an empty queue
void q_deinit(queue_t * q){
  free(q->items); //release the memory
  q->items = NULL;
  q->len = 0;
  q->size = 0;
}

//Add an item to queue
int q_enq(queue_t * q, const int item){
  if(q->len < q->size){ //if queue is not full
    q->items[q->len++] = item;  //add the item
    return 0; //return success
  }else{
    return -1;
  }
}

//Remove an item from position at
int q_deq(queue_t * q, const int at){
  int i;

  //if position is after queue end
  if(at >= q->len){
    return -1;
  }

  const int item = q->items[at];  //get the item
  q->len = q->len - 1;  //reduce queue length

  //shift forward the items after dequeued item
  for(i=at; i < q->len; ++i){
    q->items[i] = q->items[i + 1];
  }
  q->items[i] = -1; //clear the position at which item was

  return item;
}

//Return first item in queue
int q_top(queue_t * q){
  if(q->len > 0){ //if we have items
    return q->items[0]; //return first
  }else{
    return -1;  //return error
  }
}

//Return length of queue
int q_len(queue_t * q){
  return q->len;
}
