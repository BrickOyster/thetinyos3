#ifndef __KERNEL_STREAMS_H
#define __KERNEL_STREAMS_H

#include "tinyos.h"
#include "kernel_dev.h"

/**
	@file kernel_streams.h
	@brief Support for I/O streams.


	@defgroup streams Streams.
	@ingroup kernel
	@brief Support for I/O streams.

	The stream model of tinyos3 is similar to the Unix model.
	Streams are objects that are shared between processes.
	Streams are accessed by file IDs (similar to file descriptors
	in Unix).

	The streams of each process are held in the file table of the
	PCB of the process. The system calls generally use the API
	of this file to access FCBs: @ref get_fcb, @ref FCB_reserve
	and @ref FCB_unreserve.

	Streams are connected to devices by virtue of a @c file_operations
	object, which provides pointers to device-specific implementations
	for read, write and close.

	@{
*/



/** @brief The file control block.

	A file control block provides a uniform object to the
	system calls, and contains pointers to device-specific
	functions.
 */
typedef struct file_control_block
{
  unsigned int refcount;  			/**< @brief Reference counter. */
  void* streamobj;			/**< @brief The stream object (e.g., a device) */
  file_ops* streamfunc;		/**< @brief The stream implementation methods */
  rlnode freelist_node;		/**< @brief Intrusive list node */
} FCB;

/** 
  @brief Initialization for files and streams.

  This function is called at kernel startup.
 */
void initialize_files();


/**
	@brief Increase the reference count of an fcb 

	@param fcb the fcb whose reference count will be increased
*/
void FCB_incref(FCB* fcb);


/**
	@brief Decrease the reference count of the fcb.

	If the reference count drops to 0, release the FCB, calling the 
	Close method and returning its return value.
	If the reference count is still >0, return 0. 

	@param fcb  the fcb whose reference count is decreased
	@returns if the reference count is still >0, return 0, else return the value returned by the
	     `Close()` operation
*/
int FCB_decref(FCB* fcb);


/** @brief Acquire a number of FCBs and corresponding fids.

   Given an array of fids and an array of pointers to FCBs  of
   size @ num, this function will check is available resources
   in the current process PCB and FCB are available, and if so
   it will fill the two arrays with the appropriate values.
   If not, the state is unchanged (but the array contents
   may have been overwritten).

   If these resources are not needed, the operation can be
   reversed by calling @ref FCB_unreserve.

   @param num the number of resources to reserve.
   @param fid array of size at least `num` of `Fid_t`.
   @param fcb array of size at least `num` of `FCB*`.
   @returns 1 for success and 0 for failure.
*/
int FCB_reserve(size_t num, Fid_t *fid, FCB** fcb);


/** @brief Release a number of FCBs and corresponding fids.

   Given an array of fids of size @ num, this function will 
   return the fids to the free pool of the current process and
   release the corresponding FCBs.

   This is the opposite of operation @ref FCB_reserve. 
   Note that this is very different from closing open fids.
   No I/O operation is performed by this function.

   This function does not check its arguments for correctness.
   Use only with arrays filled by a call to @ref FCB_reserve.

   @param num the number of resources to unreserve.
   @param fid array of size at least `num` of `Fid_t`.
   @param fcb array of size at least `num` of `FCB*`.
*/
void FCB_unreserve(size_t num, Fid_t *fid, FCB** fcb);


/** @brief Translate an fid to an FCB.

	This routine will return NULL if the fid is not legal.

	@param fid the file ID to translate to a pointer to FCB
	@returns a pointer to the corresponding FCB, or NULL.
 */
FCB* get_fcb(Fid_t fid);

/*******************************************
 *
 * Pipes 
 *
 *******************************************/

#define PIPE_BUFFER_SIZE 8192

/** @brief The pipe control block.

	A stucture containing all attributes of a pipe
 */
typedef struct pipe_control_block
{
	FCB *reader, *writer;
	
	CondVar has_space;						/**< @brief condition variable for pipe has space*/
	CondVar has_data;						/**< @brief condition variable for pipe has data*/

	int w_position, r_position;				/**< @brief  Write/Read positioning buffer*/

	char BUFFER[PIPE_BUFFER_SIZE];			/**< @brief  bounded (cyclic) byte buffer*/
} pipe_cb;

/**
  @brief Creates a Pipe.

  This function will return the pipe_cb of the created pipe 
  inisializing some values

  @returns the pipe.
*/
pipe_cb* init_Pipe();

/**
  @brief Sets up a pipe.

  This function will create the pipe 
  It is given 2 fids and will return 0 if successful or -1 if it fails

  @param pipe the pair of file ids. 
  @returns 0 or -1
*/
int sys_Pipe(pipe_t* pipe);

/**
  @brief Writes something with a given pipe.

  This function tryies to writes a specified amount of characters @c n 
  from a given buffer @c buf using @c pipecb_t to it's @c BUFFER
   
  @param pipecb_t pair of file ids 
  @param buf the buffer containing the characters
  @param n size of string to write
  @returns the amount of characters writen by the pipe or -1 on error.
  Possible reasons for error are:
		- pipe doesn't exist.
        - writer already closed.
*/
int pipe_write(void* pipecb_t, const char *buf, unsigned int n);

/**
  @brief Read something with a given pipe.

  This function tryies to read a specified amount of characters @c n 
  to a given buffer @c buf using @c pipecb_t from it's @c BUFFER

  @param pipecb_t pair of file ids 
  @param buf the buffer to put the read characters in
  @param n size of requested string
  @returns the amount of characters read by the pipe or -1 on error.
  Possible reasons for error are:
		- pipe doesn't exist.
        - writer already closed.
*/
int pipe_read(void* pipecb_t, char *buf, unsigned int n);

/**
  @brief Close writer of a pipe.

  This function closes the writer of the pipe given to it
  and will return 0 if successful

  @param _pipecb the pair of file ids
  @returns 1 on success or -1 on error
  Possible reasons for error are:
		- pipe doesn't exist.
        - writer already closed.
*/
int pipe_writer_close(void* _pipecb);

/**
  @brief Close reader of a pipe.

  This function closes the reader of the pipe given to it
  and will return 0 if successful

  @param _pipecb the pair of file ids
  @returns 1 on success or -1 on error
  Possible reasons for error are:
		- pipe doesn't exist.
        - reader already closed.
*/
int pipe_reader_close(void* _pipecb);

/**
  @brief For blocking read

  This function is used when a file_ops isn't allowed to write
 
  @returns value -1
*/
int no_op_write();

/**
  @brief For blocking read

  This function is used when a file_ops isn't allowed to read
 
  @returns value -1
*/
int no_op_read();

/*******************************************
 *
 * Sockets
 *
 *******************************************/

/** @brief The socket types.

	LISTENER = 0 @see listener_socket, UNBOUND = 1 @see unbound_socket, PEER = 2 @see peer_socket.
 */
typedef enum {
	SOCKET_LISTENER, /**< @brief a socket that waits for a connection request */
	SOCKET_UNBOUND, /**< @brief a unitialised socket */
	SOCKET_PEER /**< @brief a connected socket */
} Socket_type;

/** @brief Listener Socket
    
	Extra structure of listener socket
 */
typedef struct listener_socket
{
	rlnode queue; /**< @brief queue of requests for the listener */
	CondVar req_available; 
}listener_socket;

/** @brief Unbound Socket
    
	Extra structure of unbound socket
 */
typedef struct unbound_socket
{
	rlnode unbound_socket; /**< @brief Dummy variable used for symetry */
}unbound_socket;

/** @brief Peer Socket
    
	Extra structure of peer socket
 */
typedef struct peer_socket
{
	struct peer_socket* peer; 			/**< @brief the peer's socket pair */
	pipe_cb* write; 					/**< @brief the peer's socket writer pipe */
	pipe_cb* read; 						/**< @brief the peer's socket reader pipe */
}peer_socket;

/** @brief the Socket Control Block
    
	Structure containing all information of a socket
 */
typedef struct socket_control_block
{
	uint refcount; 						/**< @brief amount of sockets waiting this socket for something */

	FCB* fcb; 							/**< @brief a file control block used for reading/writing */

	Socket_type type;					/**< @brief the type of the socket */
	port_t port; 						/**< @brief port the socket is connected to */

	union{
		listener_socket listener;
		unbound_socket unbound;
		peer_socket peer;
	};									/**< the extra structures that a socket can have depending of type */
}socket_cb;

/** @brief A connection request
    
	Structure containing all information of a connection request
 */
typedef struct connection_request
{
	int admitted; 						/**< @brief 0 if pending 1 if completed */

	socket_cb* peer;					/**< @brief the socket that made the request */

	CondVar connected_cv;				/**< @brief condition variable for connected socket */
	rlnode queue_node;					/**< @brief node for listener queue */
}connection_req;

/** 
  @brief Creates a socket assigned to the given port 
  
  This function returns the id of the socket created

  @param port the port the socket will be assigned to
  @returns the id of the socket
 */
Fid_t sys_Socket(port_t port);

/** 
  @brief Make a listener socket

  creates a listener socket and ist queue

  @param sock id of the socket @c fid_t
  @returns 0 if successful or -1 if error
  Possible reasons for error are:
		- invalid socket id.
        - given socket doesn't exist.
		- socket port invalid
		- port already occupied
		- socket already initilized
 */
int sys_Listen(Fid_t sock);

/** 
  @brief Accept a connection

  process request and accept/denies connection
  if it accepts it makes all neccesery connections

  @param lsock id of listener socket @c fid_t
  @returns the new socket that establishes the connection
  Possible reasons for error are:
		- invalid listener socket id @c fid_t.
        - given socket doesn't exist.
		- socket isn't of listener type
		- socket disconnected from port
		- socket changed while running
		- socket to connect couldn't be created
 */
Fid_t sys_Accept(Fid_t lsock);

/** 
  @brief Accept a connection

  requests for a connection to be established
  within a given time

  @param sock id of socket @c fid_t
  @param port port of listener socket to handle the request
  @param timeout the limit of time for the request to be established
  @returns 0 if successful or -1 on error
  Possible reasons for error are:
		- invalid socket id @c fid_t.
		- invalid port
		- port isn't assigned
		- socket on given port not of listener type
		- socket @c sock already initialized
		- timeout time passed and request not handles yet
 */
int sys_Connect(Fid_t sock, port_t port, timeout_t timeout);

/** 
  @brief ShutDown a socket

  Closes a socket partially or entirelly

  @param sock id of socket @c fid_t
  @param how mode of shutdown (SHUTDOWN_[WRITE READ BOTH])
  @returns 0 if successful or -1 on error
  Possible reasons for error are:
		- invalid socket id @c fid_t.
		- socket doesn't exist
		- Invalid shutdown mode
 */
int sys_ShutDown(Fid_t sock, shutdown_mode how);

/** 
  @brief Read with a socket

  

  @param 
  @returns number of characters read
  Possible reasons for error are:
		- 
 */
int socket_read(void* socketcb_t, char *buf, unsigned int n);

/** 
  @brief Write with a socket

  Closes a socket partially or entirelly

  @param 
  @returns number of characters writen
  Possible reasons for error are:
		- 
 */
int socket_write(void* socketcb_t, const char *buf, unsigned int n);

/** 
  @brief 
  
  s


 */
int socket_close(void* socketcb_t);

/** @} */

#endif
