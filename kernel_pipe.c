#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_dev.h"
#include <stdio.h>

static file_ops reader_file_ops = {
	.Read = pipe_read,
	.Write = no_op_write,
	.Close = pipe_reader_close
};

static file_ops writer_file_ops = {
	.Read = no_op_read,
	.Write = pipe_write,
	.Close = pipe_writer_close
};

/* Cyclic queue implementation*/

// Is the queue full?
int checkFull(int *w_position, int *r_position) 
{
	if ((*r_position == *w_position + 1) || (*r_position == 0 && (*w_position == (PIPE_BUFFER_SIZE - 1)))) return 1;
	return 0;
}

// Is the Queue Empty?
int checkEmpty(int *r_position)
{
	if (*r_position == -1) return 1;
	return 0;
}

int checkRemaining(int *w_position, int *r_position)
{
	if(*r_position > *w_position)
		return (*r_position - *w_position -1);
	if(*w_position > *r_position)
		return (PIPE_BUFFER_SIZE - (*w_position - *r_position +1));
	return PIPE_BUFFER_SIZE;

}

// Element Adding
void enQueue(char ele, char BUFFER[], int *w_position, int *r_position) {
	if (!checkFull(w_position, r_position)) {
		if (*r_position == -1) *r_position = 0;
		*w_position = (*w_position + 1) % PIPE_BUFFER_SIZE;
		BUFFER[*w_position] = ele;
	}
}

// Element removing
char deQueue(char BUFFER[], int *w_position, int *r_position) {
	char ele;
	if (!checkEmpty(r_position)) {
		ele = BUFFER[*r_position];
		if (*r_position == *w_position) {
			*r_position = -1;
			*w_position = -1;
		}
		// Reset Queue after all elements are removed
		else {
			*r_position = (*r_position + 1) % PIPE_BUFFER_SIZE;
		}
		return (ele);
	}
	return '\0';
}

/* Pipes Implementation*/

pipe_cb* init_Pipe(){
	pipe_cb* pipe = (pipe_cb*) xmalloc(sizeof(pipe_cb));

	pipe->has_space = COND_INIT;
	pipe->has_data = COND_INIT;
	
	pipe->w_position = -1;
	pipe->r_position = -1;

	return pipe;
}

int sys_Pipe(pipe_t* pipe)
{
	Fid_t fid_ts[2];
	FCB *fcbs[2];
	
	if(!FCB_reserve(2, fid_ts, fcbs))
		return -1;

	pipe_cb* pipecb = init_Pipe();

	pipe->read = fid_ts[0];

	/* Index of reader FCB*/
	pipecb->reader = fcbs[0];

	/* Setting up the reader side with its respective file_ops */
	fcbs[0]->streamfunc = &reader_file_ops;
	fcbs[0]->streamobj = pipecb;

    pipe->write = fid_ts[1];

	/* Index of writer FCB*/
	pipecb->writer = fcbs[1];

	/* Setting up the writer side with its respective file_ops */
	fcbs[1]->streamfunc = &writer_file_ops;
	fcbs[1]->streamobj = pipecb;

	return 0;
}

int pipe_write(void* pipecb_t, const char *buf, unsigned int n)
{	
	pipe_cb* pipecb = (pipe_cb*) pipecb_t;

	if(pipecb == NULL || n < 1 || pipecb->writer == NULL || pipecb->reader == NULL)
		return -1;

	while(checkFull(&pipecb->w_position, &pipecb->r_position) && pipecb->reader != NULL)
    	kernel_wait(&pipecb->has_space, SCHED_PIPE);

	if(pipecb->reader == NULL)
		return -1;

	/* We are ready to write*/

	/* Check how many chars we can write*/
	int remainingSpace = checkRemaining(&pipecb->w_position, &pipecb->r_position);
	int elementsToWrite = (remainingSpace < n) ? remainingSpace : n;

	/* Writing elements*/
	for(int i = 0; i < elementsToWrite; i++)
		enQueue(buf[i], pipecb->BUFFER, &pipecb->w_position, &pipecb->r_position);
	
	kernel_broadcast(&pipecb->has_data);

	return elementsToWrite;
}

int pipe_read(void* pipecb_t, char *buf, unsigned int n)
{	
	pipe_cb* pipecb = (pipe_cb*) pipecb_t;

	if(pipecb == NULL || n < 1 || pipecb->reader == NULL)
		return -1;

	if(pipecb->writer == NULL && (checkRemaining(&pipecb->w_position, &pipecb->r_position) == PIPE_BUFFER_SIZE))
		return 0;

	while(checkEmpty(&pipecb->r_position) && pipecb->writer!=NULL)
    	kernel_wait(&pipecb->has_data, SCHED_PIPE);

	/* We are ready to read*/

	if(pipecb->writer == NULL && (checkEmpty(&pipecb->r_position)))
		return 0;

	/* Check how many chars we can write*/
	int remainingData = PIPE_BUFFER_SIZE - checkRemaining(&pipecb->w_position, &pipecb->r_position);
	int elementsToRead = (remainingData < n) ? remainingData : n;

	/* Writing elements*/
	for(int i = 0; i < elementsToRead; i++)
		buf[i] = deQueue(pipecb->BUFFER, &pipecb->w_position, &pipecb->r_position);

	kernel_broadcast(&pipecb->has_space);

	return elementsToRead;
}

int pipe_writer_close(void* pipecb_t)
{
	pipe_cb* pipecb = (pipe_cb*) pipecb_t;

	if(pipecb == NULL || pipecb->writer == NULL)
		return -1;

	pipecb->writer = NULL;

	kernel_broadcast(&pipecb->has_data);

	return 0;
}

int pipe_reader_close(void* pipecb_t)
{
	pipe_cb* pipecb = (pipe_cb*) pipecb_t;

	if(pipecb == NULL || pipecb->reader == NULL)
		return -1;

	pipecb->reader = NULL;

	kernel_broadcast(&pipecb->has_space);

	return 0;
}

int no_op_write()
{
	return -1;
}

int no_op_read()
{
	return -1;	
}