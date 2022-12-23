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

	if(port < 0 || port > MAX_PORT - 1)
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

	return pipe_write(socket_reader->peer.read, buf, n);
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
			break;
		
		case(SOCKET_PEER):
			if(socket->peer.write != NULL){
				pipe_writer_close(socket->peer.write);
				socket->peer.write = NULL;
			}

			if(socket->peer.read != NULL){
				pipe_reader_close(socket->peer.read);
				socket->peer.read = NULL;
			}	
		default:
			return -1;
	}

	return 0;
}

int sys_Listen(Fid_t sock)
{
	if(sock < 0 || sock > MAX_FILEID - 1 || get_fcb(sock) == NULL)
		return NOFILE;

	socket_cb* socket = get_fcb(sock)->streamobj;

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
	return NOFILE;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
}

