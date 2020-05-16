
// Hugh Smith April 2017
// Network code to support TCP client/server connections
// Feel free to copy, just leave my name in it, use at your own risk.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>

#include "networks.h"
#include "gethostbyname6.h"


// This function creates the server socket.  The function 
// returns the server socket number and prints the port 
// number to the screen.

int tcpServerSetup(int portNumber)
{
	int server_socket= 0;
	struct sockaddr_in6 server;      /* socket address for local side  */
	socklen_t len= sizeof(server);  /* length of local address        */

	/* create the tcp socket  */
	server_socket = socket(AF_INET6, SOCK_STREAM, 0);
	if(server_socket < 0)
	{
		perror("socket call");
		exit(1);
	}

	// setup the information to name the socket
	server.sin6_family= AF_INET6;         		
	server.sin6_addr = in6addr_any;   //wild card machine address
	server.sin6_port= htons(portNumber);         

	// bind the name to the socket  (name the socket)
	if (bind(server_socket, (struct sockaddr *) &server, sizeof(server)) < 0)
	{
		perror("bind call");
		exit(-1);
	}
	
	//get the port number and print it out
	if (getsockname(server_socket, (struct sockaddr*)&server, &len) < 0)
	{
		perror("getsockname call");
		exit(-1);
	}

	if (listen(server_socket, BACKLOG) < 0)
	{
		perror("listen call");
		exit(-1);
	}
	
	printf("Server Port Number %d \n", ntohs(server.sin6_port));
	
	return server_socket;
}

// This function waits for a client to ask for services.  It returns
// the client socket number.   

int tcpAccept(int server_socket, int debugFlag)
{
	struct sockaddr_in6 clientInfo;   
	int clientInfoSize = sizeof(clientInfo);
	int client_socket= 0;

	if ((client_socket = accept(server_socket, (struct sockaddr*) &clientInfo, (socklen_t *) &clientInfoSize)) < 0)
	{
		perror("accept call error");
		exit(-1);
	}
	
	if (debugFlag)
	{
		printf("Client accepted.  Client IP: %s Client Port Number: %d\n",  
				getIPAddressString(clientInfo.sin6_addr.s6_addr), ntohs(clientInfo.sin6_port));
	}
	

	return(client_socket);
}

int tcpClientSetup(char * serverName, char * port, int debugFlag)
{
	// This is used by the client to connect to a server using TCP
	
	int socket_num;
	uint8_t * ipAddress = NULL;
	struct sockaddr_in6 server;      
	
	// create the socket
	if ((socket_num = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
	{
		perror("socket call");
		exit(-1);
	}
	
	if (debugFlag)
	{
		printf("Connecting to server on port number %s\n", port);
	}
	
	// setup the server structure
	server.sin6_family = AF_INET6;
	server.sin6_port = htons(atoi(port));
	
	// get the IP address of the server (DNS lockup)
	if ((ipAddress = getIPAddress6(serverName, &server)) == NULL)
	{
		exit(-1);
	}

	printf("server ip address: %s\n", getIPAddressString(ipAddress));
	if(connect(socket_num, (struct sockaddr*)&server, sizeof(server)) < 0)
	{
		perror("connect call");
		exit(-1);
	}

	if (debugFlag)
	{
		printf("Connected to %s IP: %s Port Number: %d\n", serverName, getIPAddressString(ipAddress), atoi(port));
	}
	
	return socket_num;
}

int selectCall(int socketNumber, int seconds, int microseconds, int timeIsNotNull)
{
	// Returns 1 if socket is ready, 0 if socket is not ready  
	// Only works for one socket (would need to change for multiple sockets)
	// set timeIsNotNull = TIME_IS_NOT_NULL when providing a time value
	int numReady = 0;
	fd_set fileDescriptorSet;  // the file descriptor set 
	struct timeval timeout;
	struct timeval * timeoutPtr;   // needed for the time = NULL case
	
	
	// setup fileDescriptorSet (socket to select on)
	  FD_ZERO(&fileDescriptorSet);
	  FD_SET(socketNumber, &fileDescriptorSet);
	
	// Time can be NULL, 0 or a seconds/microseconds 
	if (timeIsNotNull == TIME_IS_NOT_NULL)
	{
		timeout.tv_sec = seconds;  
		timeout.tv_usec = microseconds; 
		timeoutPtr = &timeout;
    } else
    {
		timeoutPtr = NULL;  // time is null so block forever - until input
    }

	if ((numReady = select(socketNumber + 1, &fileDescriptorSet, NULL, NULL, timeoutPtr)) < 0)
	{
		perror("select");
		exit(-1);
    }
  
	// Will be either 0 (socket not ready) or 1 (socket is ready for read)
    return numReady;
}

//Send messages to a socket
void safesend(int socketNum, char *sendBuf, int sendLen){
	int sent =  send(socketNum, sendBuf, sendLen, 0);
	if (sent < 0)
	{
		perror("send call");
		exit(-1);
	}
}
