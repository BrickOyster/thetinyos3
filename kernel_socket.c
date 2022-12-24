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

	socket_cb* socket = (socket_cb*) xmalloc(sizeof(socket_cb));

	socket->refcount = 0;
	socket->type = SOCKET_UNBOUND;
	socket->port = port;

	return socket;
}

Fid_t sys_Socket(port_t port)
{
	// Ask about NOPORT MAX_PORT Cases
	if(port < NOPORT || port > MAX_PORT)
		return NOFILE;

	socket_cb* socket = init_socket(port);

	Fid_t fid_t;
	FCB* fcb;

	if(!FCB_reserve(1, &fid_t, &fcb))
		return NOFILE;

	fcb->streamfunc = &socket_file_ops;
	fcb->streamobj = socket;

	socket->fcb = fcb;

	return fid_t;
}

int socket_read(void* socketcb_t, char *buf, unsigned int n)
{
	socket_cb* socket_reader = (socket_cb*) socketcb_t;

	if(socket_reader == NULL || socket_reader->type != SOCKET_PEER)
		return -1;

	return pipe_read(socket_reader->peer.read, buf, n);
}

int socket_write(void* socketcb_t, const char *buf, unsigned int n)
{
	socket_cb* socket_writer = (socket_cb*) socketcb_t;

	if(socket_writer == NULL || socket_writer->type != SOCKET_PEER)
		return -1;

	return pipe_write(socket_writer->peer.write, buf, n);
}

int socket_close(void* socketcb_t)
{
	socket_cb* socket = (socket_cb*) socketcb_t;

	if(socket == NULL)
		return -1;

	switch(socket->type)
	{
		case(SOCKET_UNBOUND):
			break;

		case(SOCKET_LISTENER):
			PORT_MAP[socket->port] = NULL;

			kernel_broadcast(&socket->listener.req_available);
			break;
		
		case(SOCKET_PEER):
			pipe_writer_close(socket->peer.write);
			socket->peer.write = NULL;

			pipe_reader_close(socket->peer.read);
			socket->peer.read = NULL;
			break;

		default:
			return -1;
	}

	if(socket->refcount == 0){
		free(socket);
	}	

	return 0;
}

int sys_Listen(Fid_t sock)
{
	if(sock < 0 || sock > MAX_FILEID - 1 || get_fcb(sock) == NULL)
		return NOFILE;

	socket_cb* socket = (socket_cb*) get_fcb(sock)->streamobj;

	if(socket == NULL || socket->port == NOPORT)
		return -1;

	if(PORT_MAP[socket->port] != NULL)
		return -1;

	if(socket->type != SOCKET_UNBOUND)
		return -1;

	PORT_MAP[socket->port] = socket;
	socket->type = SOCKET_LISTENER;

	rlnode_init(&socket->listener.queue, NULL);
	socket->listener.req_available = COND_INIT;

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	if(lsock < 0 || lsock > MAX_FILEID - 1 || get_fcb(lsock) == NULL)
		return NOFILE;

	socket_cb* listener = (socket_cb*) get_fcb(lsock)->streamobj;

	if(listener == NULL || listener->type!=SOCKET_LISTENER)
		return NOFILE;

	listener->refcount++;

	while(is_rlist_empty(&listener->listener.queue) && PORT_MAP[listener->port] != NULL)
		kernel_wait(&listener->listener.req_available, SCHED_PIPE);

	if(PORT_MAP[listener->port] == NULL){
		listener->refcount--;
		return NOFILE;
	}

	if(listener == NULL || listener->type != SOCKET_LISTENER || PORT_MAP[listener->port] != listener){
		listener->refcount--;
		return NOFILE;
	}

	connection_req* request = (connection_req*) rlist_pop_front(&listener->listener.queue)->obj;

	Fid_t peer = sys_Socket(listener->port);

	if(peer == NOFILE || get_fcb(peer) == NULL){
		listener->refcount--;
		return NOFILE;
	}

	request->admitted = 1;

	socket_cb* peer_socket = (socket_cb*) get_fcb(peer)->streamobj;
	socket_cb* req_socket = request->peer;

	peer_socket->type = SOCKET_PEER;
	req_socket->type = SOCKET_PEER;

	pipe_cb* pipe1 = init_Pipe();
	pipe_cb* pipe2 = init_Pipe();

	pipe1->writer = req_socket->fcb;
	pipe2->reader = req_socket->fcb;
	pipe2->writer = peer_socket->fcb;
	pipe1->reader = peer_socket->fcb;

	req_socket->peer.read = pipe2;
	req_socket->peer.write = pipe1;
	peer_socket->peer.read = pipe1;
	peer_socket->peer.write = pipe2;

	kernel_signal(&request->connected_cv);

	listener->refcount--;

	return peer;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	if(sock < 0 || sock > MAX_FILEID - 1 || get_fcb(sock) == NULL)
		return NOFILE;

	if(port <= NOPORT || port > MAX_PORT - 1)// </<= >/>= ???
		return -1;

	if(PORT_MAP[port] == NULL || PORT_MAP[port]->type != SOCKET_LISTENER)
		return -1;

	FCB* self_fcb = get_fcb(sock);
	if(self_fcb == NULL)
		return -1;

	socket_cb* self_socket = self_fcb->streamobj;

	if(self_socket->type != SOCKET_UNBOUND)
		return -1;

	socket_cb* listener_socket = PORT_MAP[port];

	self_socket->refcount++;

	connection_req* request = (connection_req*) xmalloc(sizeof(connection_req));

	request->admitted = 0;
	request->peer = self_socket;

	request->connected_cv = COND_INIT;

	rlnode_init(&request->queue_node, request);

	rlist_push_back(&listener_socket->listener.queue, &request->queue_node);
	kernel_signal(&listener_socket->listener.req_available);

	int timeOut;
	while(request->admitted == 0){
		timeOut = kernel_timedwait(&request->connected_cv, SCHED_PIPE, timeout); // What does in return...?
		if(timeOut == 0)
			break;
	}


	self_socket->refcount--;
	
	return (request->admitted == 1) ? 0 : -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{	

	if(sock < 0 || sock > MAX_FILEID-1 || get_fcb(sock)==NULL){
		return -1;
	}

	socket_cb* socket = (socket_cb*) get_fcb(sock)->streamobj;

	if(socket == NULL)
		return -1;

	switch(how){
		case(SHUTDOWN_READ):
			pipe_reader_close(socket->peer.read);
			socket->peer.read = NULL;
			break;

		case(SHUTDOWN_WRITE):
			pipe_writer_close(socket->peer.write);
			socket->peer.write = NULL;
			break; 

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

