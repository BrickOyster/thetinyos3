#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_dev.h"
#include <stdio.h>

socket_cb* PORT_MAP[MAX_PORT];

static file_ops socket_file_ops = {
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};


socket_cb* init_socket(port_t port)
{
	/* allocating the needed space to create a new socket control block*/
	socket_cb* socket = (socket_cb*) xmalloc(sizeof(socket_cb));

	/* initializing the fileds of the socket control block */
	socket->refcount = 0;
	socket->type = SOCKET_UNBOUND;
	socket->port = port;

	return socket;
}

Fid_t sys_Socket(port_t port)
{
	/* checking if the port is illegal, below NOPORT or above MAX_PORT*/
	if(port < NOPORT || port > MAX_PORT)
		return NOFILE;

	/* initializing socket_cb*/
	socket_cb* socket = init_socket(port);

	Fid_t fid_t;
	FCB* fcb;

	/* checking to see if we can reserve one FCB in the current process*/
	if(!FCB_reserve(1, &fid_t, &fcb))
		return NOFILE;

	/* Setting up the socket with its respective file_ops */
	fcb->streamfunc = &socket_file_ops;
	fcb->streamobj = socket;

	socket->fcb = fcb;

	return fid_t;
}

int socket_read(void* socketcb_t, char *buf, unsigned int n)
{
	socket_cb* socket_reader = (socket_cb*) socketcb_t;

	/* if the socket reader is null or if its type is not SOCKET_PEER then return -1*/
	if(socket_reader == NULL || socket_reader->type != SOCKET_PEER)
		return -1;

	/* returning the return_value of the pipe_read function*/
	return pipe_read(socket_reader->peer.read, buf, n);
}

int socket_write(void* socketcb_t, const char *buf, unsigned int n)
{
	socket_cb* socket_writer = (socket_cb*) socketcb_t;

	/* if the socket writer is null or if its type is not SOCKET_PEER then return -1*/
	if(socket_writer == NULL || socket_writer->type != SOCKET_PEER)
		return -1;

	/* returning the return_value of the pipe_write function*/
	return pipe_write(socket_writer->peer.write, buf, n);
}

int socket_close(void* socketcb_t)
{
	socket_cb* socket = (socket_cb*) socketcb_t;

	/* if it is already null*/
	if(socket == NULL)
		return -1;

	/* switch case accotding to socket's type*/
	switch(socket->type)
	{
		/* if socket type is UNBOUND nothing needs to be done. just break*/
		case(SOCKET_UNBOUND):
			break;

		/* if the type is LISTENER then we set NULL the accoring position in PORT_MAP (it is available from now on)
		 * and we singal the waiters*/
		case(SOCKET_LISTENER):
			PORT_MAP[socket->port] = NULL;

			kernel_broadcast(&socket->listener.req_available);
			break;
		
		/* if the type is PEER then we have to close both the writer and reader end to do that we utilize
		 * the close funtions from the pipe.c, at last we set the according fields in the socket_cb NULL*/
		case(SOCKET_PEER):
			pipe_writer_close(socket->peer.write);
			socket->peer.write = NULL;

			pipe_reader_close(socket->peer.read);
			socket->peer.read = NULL;
			break;

		default:
			return -1;
	}

	/* if the refcount to this specific socket it 0 then we can free it*/
	if(socket->refcount == 0){
		free(socket);
	}	

	return 0;
}

int sys_Listen(Fid_t sock)
{
	/* checking if the sock value is illegal (below 0 or above MAX_FILEID), get_fcb() also makes a similar test*/
	if(sock < 0 || sock > MAX_FILEID - 1 || get_fcb(sock) == NULL)
		return NOFILE;

	/* getting the socket_cb from the streamobj of the matched fcb*/
	socket_cb* socket = (socket_cb*) get_fcb(sock)->streamobj;

	/* if socket is NUll or its matched port in NOPORT then return -1*/
	if(socket == NULL || socket->port == NOPORT)
		return -1;

	/* if the PORT_MAP at socket->port is not NULL then it is not available and we return -1*/
	if(PORT_MAP[socket->port] != NULL)
		return -1;

	/* if the socket->type is not UNBOUND then return -1*/
	if(socket->type != SOCKET_UNBOUND)
		return -1;

	/* we now set socket as LISTENER socket*/
	PORT_MAP[socket->port] = socket;
	socket->type = SOCKET_LISTENER;

	/* initializing the fields of the listener_socket struct*/
	rlnode_init(&socket->listener.queue, NULL);
	socket->listener.req_available = COND_INIT;

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	/* checking if the sock value is illegal (below 0 or above MAX_FILEID), get_fcb() also makes a similar test*/
	if(lsock < 0 || lsock > MAX_FILEID - 1 || get_fcb(lsock) == NULL)
		return NOFILE;

	/* getting the socket_cb from the streamobj of the matched fcb*/
	socket_cb* listener = (socket_cb*) get_fcb(lsock)->streamobj;

	/* if the listener is NULL or its type is not type LISTENER return NOFILE*/
	if(listener == NULL || listener->type!=SOCKET_LISTENER)
		return NOFILE;

	/* increasing the refcount*/
	listener->refcount++;

	/* if listener's queue is empty (no one is waiting for connection) and 
	 * the PORT_MAP at listener->sock position is not NULL then wait*/
	while(is_rlist_empty(&listener->listener.queue) && PORT_MAP[listener->port] != NULL)
		kernel_wait(&listener->listener.req_available, SCHED_PIPE);

	/* if while we waited the PORT_MAP at listener->sock position became NULL, then we decrease the refcount and return NOFILE*/
	if(PORT_MAP[listener->port] == NULL){
		listener->refcount--;
		return NOFILE;
	}

	/* if listener is NULL or its type is not LISTNER or PORT_MAP at listener->sock position 
	 * was given to another socket, then we decrease the refcount and return NOFILE*/
	if(listener == NULL || listener->type != SOCKET_LISTENER || PORT_MAP[listener->port] != listener){
		listener->refcount--;
		return NOFILE;
	}

	/* getting a request from listener's waiting to connect list*/
	connection_req* request = (connection_req*) rlist_pop_front(&listener->listener.queue)->obj;

	Fid_t peer = sys_Socket(listener->port);

	/* if peer is matched to NOFILE or get_fcb detect an illegal fid then we decrease refcount and return NOFILE*/
	if(peer == NOFILE || get_fcb(peer) == NULL){
		listener->refcount--;
		return NOFILE;
	}

	/* mark the request as admitted*/
	request->admitted = 1;

	/* we get the socket_cbs from the streamobj of the fcb to fid_t peer, 
	 * and from the request's struct*/
	socket_cb* peer_socket = (socket_cb*) get_fcb(peer)->streamobj;
	socket_cb* req_socket = request->peer;

	/* we mark both of them as PEER sockets since a connection was established*/
	peer_socket->type = SOCKET_PEER;
	req_socket->type = SOCKET_PEER;

	/* initializing two pipe_cbs*/
	pipe_cb* pipe1 = init_Pipe();
	pipe_cb* pipe2 = init_Pipe();

	/* making the needed connections between the sockets' fcbs and 
	 * the pipes' readers and writers, furthermore we connect the 
	 * sockets' peer read and write to the according pipes*/
	pipe1->writer = req_socket->fcb;
	pipe2->reader = req_socket->fcb;
	pipe2->writer = peer_socket->fcb;
	pipe1->reader = peer_socket->fcb;

	req_socket->peer.read = pipe2;
	req_socket->peer.write = pipe1;
	peer_socket->peer.read = pipe1;
	peer_socket->peer.write = pipe2;

	/* signal all the waiters*/
	kernel_signal(&request->connected_cv);

	/* decrease the refcount since the connection was fulfilled*/
	listener->refcount--;

	return peer;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	/* checking if the sock value is illegal (below 0 or above MAX_FILEID), get_fcb() also makes a similar test*/
	if(sock < 0 || sock > MAX_FILEID - 1 || get_fcb(sock) == NULL)
		return NOFILE;

	/* checking if given port is illegal*/
	if(port <= NOPORT || port > MAX_PORT - 1)
		return -1;

	/* if the PORT_MAP at port position is NULL or if its type is not type LISTENER return -1*/
	if(PORT_MAP[port] == NULL || PORT_MAP[port]->type != SOCKET_LISTENER)
		return -1;

	/* getting fcb from fid_t sock and checking if it is NULL*/
	FCB* self_fcb = get_fcb(sock);
	if(self_fcb == NULL)
		return -1;

	/* getting the socket_cb from fcb's streamobj*/
	socket_cb* self_socket = self_fcb->streamobj;

	/* if it is not UNBOUND (which means it already has another role), return -1*/
	if(self_socket->type != SOCKET_UNBOUND)
		return -1;

	/* getting the listener socket from the PORT_MAP at the port position*/
	socket_cb* listener_socket = PORT_MAP[port];

	/* increasing self_socket's refcount*/
	self_socket->refcount++;

    /* allocating the needed space to create a new connection_req*/
	connection_req* request = (connection_req*) xmalloc(sizeof(connection_req));

	/* mark the new request as not admitted*/
	request->admitted = 0;
	/* set as its peer socket the self_socket, which is the one waiting for a connection*/
	request->peer = self_socket;

	/* initializing its condition variable*/
	request->connected_cv = COND_INIT;

	/* creating an rlnode with request as node key */
	rlnode_init(&request->queue_node, request);

	/* pushing it in listener's waiting list*/
	rlist_push_back(&listener_socket->listener.queue, &request->queue_node);

	/* signal that a new request is available*/
	kernel_signal(&listener_socket->listener.req_available);

	int timeOut;
	/* while the request is not admitted to timed wait for timeout and cause SCHED_PIPE*/
	while(request->admitted == 0){
		timeOut = kernel_timedwait(&request->connected_cv, SCHED_PIPE, timeout);
		/* if it is 0 it means it was not signalled*/
		if(timeOut == 0)
			break;
	}

	/* decrease refcount*/
	self_socket->refcount--;
	
	/* return 0 if the request was admitted and -1 if it was not*/
	return (request->admitted == 1) ? 0 : -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{	
	/* checking if the sock value is illegal (below 0 or above MAX_FILEID), get_fcb() also makes a similar test*/
	if(sock < 0 || sock > MAX_FILEID-1 || get_fcb(sock)==NULL){
		return -1;
	}

	socket_cb* socket = (socket_cb*) get_fcb(sock)->streamobj;

	if(socket == NULL)
		return -1;

	/* switch case according to the shutdown_mode*/
	switch(how){
		/* closing the peer's read end*/
		case(SHUTDOWN_READ):
			pipe_reader_close(socket->peer.read);
			socket->peer.read = NULL;
			break;

		/* closing the peer's write end*/
		case(SHUTDOWN_WRITE):
			pipe_writer_close(socket->peer.write);
			socket->peer.write = NULL;
			break; 

		/* closing both ends of peer*/
		case(SHUTDOWN_BOTH):
			pipe_reader_close(socket->peer.read);
			socket->peer.read = NULL;
			pipe_writer_close(socket->peer.write);
			socket->peer.write = NULL;
			break;

		default:
			return -1;
	}

	return 0;
}

