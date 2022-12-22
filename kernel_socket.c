#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_dev.h"
#include <stdio.h>

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
	int chars_read = 0;

	return chars_read;
}

int socket_write(void* socketcb_t, const char *buf, unsigned int n)
{
	int chars_writen = 0;

	return chars_writen;	
}

int socket_close(void* socketcb_t)
{
	return 0;
}

int sys_Listen(Fid_t sock)
{
	return -1;
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

