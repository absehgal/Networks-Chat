//
// Written Hugh Smith, Updated: April 2020
// Use at your own risk.  Feel free to copy, just leave my name in it.
//
// Modified by Abhinav Sehgal


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

#include "networks.h"
#include "pollLib.h"

#define DEBUG_FLAG 1

//Handle table struct - linked list
struct user {
	char handle[100];
	int socket;
	struct user *next;
};

void processSockets(int mainServerSocket);
struct user *recvFromClient(int clientSocket, struct user *head);
void addNewClient(int mainServerSocket);
void removeClient(int clientSocket);
int checkArgs(int argc, char *argv[]);
struct user *handleTable(int newClientSocket, char *buf, struct user *head);
struct user *type(char *buf, int clientSocket, struct user *head);
void printList(struct user *start); 
struct user *removeHandleTable(int clientSocket, struct user *head);
void message(char *buf, struct user *head);
void broad(char *buf, struct user *head);
int checkhandle(char *desthandle, struct user *head);
void messagehandleDNE(int sourcesocket, char *dest);
void exitack(int exitsocket);
void goodhandle(int socket);
void badhandle(int socket);
void sendnames(int clientSocket, struct user *head);
void list(int clientSocket, struct user *head);
void count(int clientSocket, struct user *head);
void endlist(int clientSocket);
int buildarrays(char *buf, char *names, int *namelens, int offset);

int main(int argc, char *argv[])
{
	int mainServerSocket = 0;   //socket descriptor for the server socket
	int portNumber = 0;
	
	setupPollSet();
	portNumber = checkArgs(argc, argv);
	
	mainServerSocket = tcpServerSetup(portNumber); //create server socket

	// Main control process (clients and accept())
	processSockets(mainServerSocket);
	
	close(mainServerSocket); //close the socket
	
	return 0;
}

//Wait for data from sockets
void processSockets(int mainServerSocket)
{
	int socketToProcess = 0;
	
	addToPollSet(mainServerSocket);
		
	struct user *head = NULL;

	while (1)
	{
		if ((socketToProcess = pollCall(POLL_WAIT_FOREVER)) != -1)
		{
			if (socketToProcess == mainServerSocket)
			{
				addNewClient(mainServerSocket);
			}
			else
			{
				head = recvFromClient(socketToProcess, head);
			}
		}
		else
		{
			printf("Poll timed out waiting for client to send data\n");
		}
		
	}
}

//Receive data from clients
struct user *recvFromClient(int clientSocket, struct user *head)
{
	char buf[MAXBUF], inbuf[MAXBUF];
	int messageLen1 = 0, messageLen2 = 0;
	memset(buf, 0, MAXBUF);
	memset(inbuf, 0, MAXBUF);
	uint16_t length;
		
	//now get the data from the clientSocket (message includes null)
	if ((messageLen1 = recv(clientSocket, &length, 2, MSG_WAITALL)) < 0)
	{
		perror("recv call");
		exit(-1);
	}
	memcpy(buf, &length, 2);
	length = ntohs(length);

	if ((messageLen2 = recv(clientSocket, inbuf, length-2, 0)) < 0)
	{
		perror("recv call");
		exit(-1);
	}
	memcpy(buf+2, inbuf, messageLen2); 
	
	if (messageLen1 == 0)
	{
		// recv() 0 bytes so client is gone
		head = removeHandleTable(clientSocket, head);
		removeClient(clientSocket);
	}
	else
	{
		
		return type(buf, clientSocket, head);
	}
	return head;
}

//Remove client from handle table
struct user *removeHandleTable(int clientSocket, struct user *head){
	struct user *remove = NULL;
	struct user *prev = NULL;
	remove = head;
	if (head -> socket == clientSocket){
		struct user *temp;
		temp = head -> next;
		free(head);
		return temp;
	}
	while (remove -> socket != clientSocket && remove != NULL){
		prev = remove;
		remove = remove -> next;
	}
	prev -> next = remove -> next;
	free(remove);
	return head;
}

//Determine type of incoming packet
struct user *type(char *buf, int clientSocket, struct user *head){
	int flag = buf[2];
	if (flag == 5){
		message(buf, head);
	} else if (flag == 1){
		if (checkhandle(&buf[4], head) != -1){
			badhandle(clientSocket);
		} else {
			goodhandle(clientSocket);
		}
		return handleTable(clientSocket, buf, head);
	} else if (flag == 4){
		broad(buf, head);
	} else if (flag == 10){
		list(clientSocket, head);
	} else if (flag == 8){
		exitack(clientSocket);
	}
	return head;
}

//Receive and process broadcast packet
void broad(char *buf, struct user *head){
	struct user *test = NULL;
	test = head;
	char src[100];
	uint16_t PDUlen = buf[1]*256 + buf[0];
	PDUlen = ntohs(PDUlen);
	memset(src, 0, 100);
	memcpy(src, &buf[4], buf[3]);
	while (test != NULL){
		//Send packet if handle is not the same as receiver
		if (strcmp(test -> handle, src) != 0){
			safesend(test -> socket, buf, PDUlen);
		}
		test = test -> next;
	}
}

//Steps of operation for listing
void list(int clientSocket, struct user *head){
	count(clientSocket, head);
	sendnames(clientSocket, head);
	endlist(clientSocket);
}

//Send number of clients on server
void count(int clientSocket, struct user *head){
	struct user *test = NULL;
	uint32_t count = 0;
	test = head;
	while (test != NULL){
		count++;
		test = test -> next;
	}
	int totallen = 7;
	int flag = 11;
	uint16_t nettotallen = htons(totallen);
	uint32_t netcount = htonl(count);
	char buf[MAXBUF];
	memset(buf, 0, MAXBUF);
	memcpy(buf, &nettotallen, 2);
	memcpy(buf+2, &flag, 1);
	memcpy(buf+3, &netcount, 4);
	safesend(clientSocket, buf, totallen);
}

//Send client handles one at a time
void sendnames(int clientSocket, struct user *head){
	char buf[MAXBUF];
	char handle[100];
	struct user *test = NULL;
	int flag = 12;
	uint16_t totallen = 4;
	uint16_t nettotallen;
	int handlelen = 0;
	test = head;
	memset(buf, 0, MAXBUF);
	memset(handle, 0, 100);
	while (test != NULL){
		handlelen = strlen(test -> handle);
		memcpy(buf+2, &flag, 1);
		memcpy(buf+3, &handlelen, 1);
		memcpy(buf+4, test -> handle, handlelen);
		totallen += handlelen;
		nettotallen = htons(totallen);
		memcpy(buf, &nettotallen, 2);
		safesend(clientSocket, buf, totallen);
		test = test -> next;
		memset(buf, 0, MAXBUF);
		memset(handle, 0, 100);
		totallen = 4;
	}
}	

//Send end of list packet to client
void endlist(int clientSocket){
	char buf[MAXBUF];
	memset(buf, 0, MAXBUF);
	int flag = 13;
	uint16_t totallen = 3;
	uint16_t nettotallen = htons(totallen);
	memcpy(buf, &nettotallen, 2); 
	memcpy(buf+2, &flag, 1);
	safesend(clientSocket, buf, totallen);
}

//Handle is not taken, client can connect
void goodhandle(int socket){
	int totallen = 3;
	int flag = 2;
	uint16_t nettotallen = htons(totallen);
	char buf[MAXBUF];
	memset(buf, 0, MAXBUF);
	memcpy(buf, &nettotallen, 2);
	memcpy(buf+2, &flag, 1);
	safesend(socket, buf, totallen);
}

//Handle is taken, client cannot connect
void badhandle(int socket){
	int totallen = 3;
	int flag = 3;
	uint16_t nettotallen = htons(totallen);
	char buf[MAXBUF];
	memset(buf, 0, MAXBUF);
	memcpy(buf, &nettotallen, 2);
	memcpy(buf+2, &flag, 1);
	safesend(socket, buf, totallen);
}

//Acknowledge exit packet from client
void exitack(int exitsocket){
	int totallen = 3;
	int flag = 9;
	uint16_t nettotallen = htons(totallen);
	char buf[MAXBUF];
	memset(buf, 0, MAXBUF);
	memcpy(buf, &nettotallen, 2);
	memcpy(buf+2, &flag, 1);
	safesend(exitsocket, buf, totallen);
}

//Create arrays of multiple destination handle names and name lengths
int buildarrays(char *buf, char *names, int *namelens, int offset){
	int i;
	int numdest = buf[offset];
	int namesoffset = 0;
	offset += 1;
	for (i = 0; i < numdest; i++){
		namelens[i] = buf[offset];
		offset += 1;
		memcpy(names+namesoffset, &buf[offset], namelens[i]);
		offset += namelens[i];
		namesoffset += namelens[i];
	}
	return offset;
}

//Read incoming message packet
void message(char *buf, struct user *head){
	uint16_t PDUlen = buf[1]*256 + buf[0];
	PDUlen = ntohs(PDUlen);
	int srchandlelen = buf[3];
	char srchandle[100];
	char desthandle[100];
	char names[900];
	char message[MAXBUF];
	int namelens[9];
	memset(message, 0, MAXBUF);
	memset(names, 0, 900);
	memset(srchandle, 0, 100);
	memset(desthandle, 0, 100);
	memcpy(srchandle, &buf[4], srchandlelen);
	int offset = 4 + srchandlelen;
	int numdest = buf[offset];
	int i;
	offset = buildarrays(buf, names, namelens, offset);
	memcpy(message, &buf[offset], PDUlen - offset);

	int x = 0;
	int socket = 0, srcsocket = 0;

	//Send to each destination handle
	for (i = 0; i < numdest; i++){
		memcpy(desthandle, &names[x], namelens[i]);

		socket = checkhandle(desthandle, head);
		if (socket == -1){
			//Reply to sender handle - handle does not exist
			srcsocket = checkhandle(srchandle, head);
			messagehandleDNE(srcsocket, desthandle);
		} else {
			safesend(socket, buf, PDUlen);
		}
		x += namelens[i];
		memset(desthandle, 0, 100);
	}
}

//Return message if handle DNE
void messagehandleDNE(int sourcesocket, char *dest){
	char buf[MAXBUF];
	int flag = 7;
	int destlen = strlen(dest);
	int totallen = 4 + destlen;
	uint16_t nettotallen = htons(totallen);
	memset(buf, 0, MAXBUF);
	memcpy(buf, &nettotallen, 2); //PDUlen
	memcpy(buf+2, &flag, 1); //flag = 7
	memcpy(buf+3, &destlen, 1); //dest handle len
	memcpy(buf+4, dest, destlen); //dest handle name
	safesend(sourcesocket, buf, totallen);
}

//Check if handle exists
int checkhandle(char *desthandle, struct user *head){
	struct user *test = NULL;
	test = head;
	while (test != NULL){
		if (strcmp(test -> handle, desthandle) == 0){
			return test -> socket; //return socketnum
		}
		test = test -> next;
	}
	return -1; //handle DNE
}

//Print handles and sockets in handle table - for testing
void printList(struct user *start){
	int count = 0;
	while (start != NULL){
		count++;
		printf("|%s, %d|\n", start -> handle, start -> socket);
		start = start -> next;
		if (count > 5){
			break;
		}
	}
	printf("num of people: %d\n", count);
}

//Add new client to handle table
struct user *handleTable(int newClientSocket, char *buf, struct user *head){
	if (head == NULL){
		head = (struct user *)malloc(sizeof(struct user));
		if (head == NULL){
			perror("malloc call");
			exit(-1);
		}
		head -> socket = newClientSocket;
		memcpy(head -> handle, &(buf[4]), buf[3]);
		head -> next = NULL;
	} else {
		struct user *headcpy = NULL;
		struct user *insert = (struct user *)malloc(sizeof(struct user));
		if (insert == NULL){
			perror("malloc call");
			exit(-1);
		}

		insert -> socket = newClientSocket;
		strncpy(insert -> handle, &buf[4], buf[3]);
		insert -> next = NULL;

		headcpy = head;
		while (headcpy -> next != NULL){
			headcpy = headcpy -> next;
		}
		headcpy -> next = insert;
	}
	memset(buf, 0, MAXBUF);
	return head;
}

//Provided add new client function
void addNewClient(int mainServerSocket)
{
	int newClientSocket = tcpAccept(mainServerSocket, DEBUG_FLAG);
	
	addToPollSet(newClientSocket);
	
}

//Provided remove client function
void removeClient(int clientSocket)
{
	printf("Client on socket %d terminated\n", clientSocket);
	removeFromPollSet(clientSocket);
	close(clientSocket);
}

//Check arg count
int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;

	if (argc > 2)
	{
		fprintf(stderr, "Usage %s [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 2)
	{
		portNumber = atoi(argv[1]);
	}
	
	return portNumber;
}

