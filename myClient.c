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

void sendToServer(int socketNum, char *handle);
int getFromStdin(char * sendBuf, int socketNum);
void checkArgs(int argc, char * argv[]);
void parse(char *sendBuf, char *handle, int socket);
void initPacket(int socketNum, char *handle);
void usage();
void list(char *sendBuf);
void end(char *sendBuf);
void broadcast(char *sendBuf, char *handle, int socket);
void message(char *sendBuf, char *handle, int socket);
int adddest(char *sendBuf, int offset, char *ptr);
void checknumdest(int numdest);
int splitmessage(char *buf, int offset, char *message, int socket);
void messagebase(char *sendBuf, char *handle, int clientLen, int numdest);
void finishmessage(char *sendBuf, int offset, char *ptr);
void recvFromServer(int socketNum, char *buf);
void recvparse(int socketNum, char *buf);
void decodemessage(char *buf);
void decodebroadcast(char *buf);
void messagehandleDNE(char *buf);
void listcount(char *buf, int socketNum);
void listname(char *buf);

int main(int argc, char * argv[])
{
	int socketNum = 0;         //socket descriptor
	setupPollSet();
	checkArgs(argc, argv);

	/* set up the TCP Client socket  */
	socketNum = tcpClientSetup(argv[2], argv[3], DEBUG_FLAG);

	addToPollSet(0);
	addToPollSet(socketNum);
	initPacket(socketNum, argv[1]);
	sendToServer(socketNum, argv[1]);
	close(socketNum);
	return 0;
}

//Polling - print prompt and receive from stdin or server
void sendToServer(int socketNum, char *handle)
{
	char sendBuf[MAXBUF];   //data buffer
	char recvBuf[MAXBUF];
	char newBuf[MAXBUF];
	uint16_t PDUlen = 0;
	int socketToProcess = 0;
			
	memset(sendBuf, 0, MAXBUF);
	memset(recvBuf, 0, MAXBUF);
	memset(newBuf, 0, MAXBUF);
	printf("$: ");
	fflush(stdout);
	while (1)
	{
		if ((socketToProcess = pollCall(POLL_WAIT_FOREVER)) != -1)
		{
			if (socketToProcess == socketNum)
			{
				recvFromServer(socketNum, recvBuf);
				printf("\n");
			}
			else
			{
				getFromStdin(sendBuf, socketNum);
				parse(sendBuf, handle, socketNum);
				PDUlen = sendBuf[1]*256 + sendBuf[0];
				PDUlen = ntohs(PDUlen);
				safesend(socketNum, sendBuf, PDUlen);
			}
		}
		
		if (recvBuf[2] != 11 || recvBuf[2] != 9){
			printf("$: ");
			fflush(stdout);
		}
	}
}

//Receive from server - two receive calls
void recvFromServer(int socketNum, char *buf){
	int messageLen1, messageLen2 = 0;
	char newBuf[MAXBUF];
	memset(newBuf, 0, MAXBUF);
	uint16_t length = 0;
	memset(buf, 0, MAXBUF);

	//Length of packet
	if ((messageLen1 = recv(socketNum, &length, 2, MSG_WAITALL)) < 0)
	{
		perror("recv call (length) - client");
		exit(-1);
	}
	
	if (messageLen1 == 0){
		printf("Server Terminated\n");
		close(socketNum);
		exit(0);
	}

	memcpy(buf, &length, 2);
	length = ntohs(length);

	//Rest of packet
	if ((messageLen2 = recv(socketNum, newBuf, length-2, 0)) < 0)
	{
		perror("recv call (packet) - client");
		exit(-1);
	}

	//Reconstruct complete packet
	memcpy(buf+2, newBuf, messageLen2);
	
	recvparse(socketNum, buf);
}

//Get user input from stdin
int getFromStdin(char * sendBuf, int socketNum)
{
	// Gets input up to MAXBUF-1 (and then appends \0)
	// Returns length of string including null
	char aChar = 0;
	int inputLen = 0;       
	
	// Important you don't input more characters than you have space 
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			sendBuf[inputLen] = aChar;
			inputLen++;
		}
	}

	sendBuf[inputLen] = '\0';
	inputLen++;  //we are going to send the null
	
	return inputLen;
}

//Check command line arguments
void checkArgs(int argc, char * argv[])
{
	if (argc != 4)
	{
		printf("usage: cclient handle server-name server-port\n");
		exit(1);
	}
	if (strlen(argv[1]) > 100){
		printf("Handle cannot be greater than 100 characters\n");
		exit(1);
	}
	if (isdigit(argv[1][0])){
		printf("Handle cannot start with a number\n");
		exit(1);
	}
}

//Parse user input
void parse(char *sendBuf, char *handle, int socket){
	if (sendBuf[0] != '%'){
		usage();
	}
	if (sendBuf[1] == 'M' || sendBuf[1] == 'm'){
		message(sendBuf, handle, socket);
	} else if (sendBuf[1] == 'B' || sendBuf[1] == 'b'){
		broadcast(sendBuf, handle, socket);
	} else if (sendBuf[1] == 'L' || sendBuf[1] == 'l'){
		list(sendBuf);
	} else if (sendBuf[1] == 'E' || sendBuf[1] == 'e'){
		end(sendBuf);
	} else {
		usage();
	}
}

//Parse received buffer
void recvparse(int socketNum, char *buf){
	int flag = buf[2];
	if (flag == 4){
		decodebroadcast(buf);
	} else if (flag == 5){
		decodemessage(buf);
	} else if (flag == 7){
		messagehandleDNE(buf);
	} else if (flag == 9){
		close(socketNum);
		exit(0);
	} else if (flag == 11){
		listcount(buf, socketNum);
	}
}

//Decode and print a broadcast message
void decodebroadcast(char *buf){
	uint16_t PDUlen = buf[1]*256 + buf[0];
	PDUlen = ntohs(PDUlen);
	int srchandlelen = 0;
	srchandlelen = buf[3];
	char srchandle[100];
	memset(srchandle, 0, 100);
	memcpy(srchandle, &buf[4], srchandlelen);
	printf("\n%s: ", srchandle);
	printf("%s", &buf[4+srchandlelen]);
}

//Write broadcast packet
void broadcast(char *sendBuf, char *handle, int socket){
	char msgBuf[MAXBUF];
	memset(msgBuf, 0, MAXBUF);
	memcpy(msgBuf, sendBuf, MAXBUF);
	char *ptr = strtok(msgBuf, " "); //%b
	ptr = strtok(NULL, ""); //message
	int flag = 4, splits = 0, offset = 0;
	int handlelen = strlen(handle);
	offset = 4+handlelen;
	memset(sendBuf, 0, MAXBUF);
	memcpy(sendBuf+2, &flag, 1);
	memcpy(sendBuf+3, &handlelen, 1);
	memcpy(sendBuf+4, handle, handlelen);

	//If blank message
	if (ptr == NULL){
		uint16_t totalnet = htons(offset);
		memcpy(sendBuf, &totalnet, 2);
	} else if (strlen(ptr) > 200){
		splits = splitmessage(sendBuf, offset, ptr, socket);
		ptr += 200*splits;
		finishmessage(sendBuf, offset, ptr);
	} else {
		//Between 0 and 200 message length
		finishmessage(sendBuf, offset, ptr);
	}
}

//Print incorrect handle for message
void messagehandleDNE(char *buf){
	printf("Client with handle %s does not exist", &buf[4]);
}

//Print number of clients for list
void listcount(char *buf, int socketNum){
	uint32_t count = buf[6]*16777216 +  buf[5]*65536 + buf[4]*256 + buf[3];
	count = ntohl(count);
	printf("\nNumber of clients: %lu", (long unsigned int) count);
	int listing = 1;
	int recvflag = 0;
	
	//Wait for all client names to be received
	while (listing){
		recvFromServer(socketNum, buf);
		recvflag = buf[2];
		if (recvflag == 12){
			listname(buf);
		} else if (recvflag == 13){
			listing = 0;
		} else {
			continue;
		}
	}
}

//Print each client for list
void listname(char *buf){
	printf("\n\t%s", &buf[4]);
}

//Decode and print a message
void decodemessage(char *buf){
	uint16_t PDUlen = buf[1]*256 + buf[0];
	PDUlen = ntohs(PDUlen);
	int srchandlelen = 0;
	srchandlelen = buf[3];
	char srchandle[100];
	memset(srchandle, 0, 100);
	memcpy(srchandle, &buf[4], srchandlelen);
	printf("\n%s: ", srchandle);
	int offset = 4+srchandlelen;
	int dests = buf[offset];
	int desthandlelen = 0;
	int i;
	offset += 1;
	for (i = 0; i < dests; i++){
		desthandlelen = buf[offset];
		offset += 1;
		offset += desthandlelen;
	}
	printf("%s", &buf[offset]);
}

//Error message for incorrect input format
void usage(){
	printf("Incorrect input format\n");
	exit(1);
}

//Write init packet for logging in
void initPacket(int socketNum, char *handle){
	char initBuf[MAXBUF];
	int handlelen = strlen(handle);
	int totallen = handlelen + 4;
	uint16_t nettotallen = htons(totallen);
	int flag = 1, listing = 1, recvflag = 0;
	memset(initBuf, 0, MAXBUF);
	memcpy(initBuf, &nettotallen, 2);
	memcpy(initBuf+2, &flag, 1);
	memcpy(initBuf+3, &handlelen, 1);
	memcpy(initBuf+4, handle, handlelen);
	
	safesend(socketNum, initBuf, totallen);

	//Wait for acknowledgement from server
	while (listing){
		recvFromServer(socketNum, initBuf);
		recvflag = initBuf[2];
		if (recvflag == 3){
			printf("Handle already in use: %s\n", handle);
			exit(-1);
		} else if (recvflag == 2){
			listing = 0;
		} else {
			continue;
		}
	}
}

//Write exit packet
void end(char *sendBuf){
	int totallen = 3;
	int flag = 8;
	uint16_t nettotallen = htons(totallen);
	totallen = htons(totallen);
	memset(sendBuf, 0, MAXBUF);
	memcpy(sendBuf, &nettotallen, 2);
	memcpy(sendBuf+2, &flag, 1);
}

//Write list packet
void list(char *sendBuf){
	int totallen = 3;
	int flag = 10;
	uint16_t nettotallen = htons(totallen);
	memset(sendBuf, 0, MAXBUF);
	memcpy(sendBuf, &nettotallen, 2);
	memcpy(sendBuf+2, &flag, 1);
}

//Write message packet
void message(char *sendBuf, char *handle, int socket){
	char msgBuf[MAXBUF];
	memset(msgBuf, 0, MAXBUF);
	memcpy(msgBuf, sendBuf, MAXBUF);
	memset(sendBuf, 0, MAXBUF);
	int numdest, i, splits = 0;
	int clientlen = strlen(handle);
	uint16_t offset, totalnet;

	char *ptr = strtok(msgBuf, " "); //%m
	ptr = strtok(NULL, " "); //numdest
	numdest = atoi(ptr);
	checknumdest(numdest);

	messagebase(sendBuf, handle, clientlen, numdest);
	offset = 5+clientlen;

	for (i = 0; i < numdest; i++){
		ptr = strtok(NULL, " "); //desthandle
		if (strlen(ptr) > 100){
			perror("destination handle too long\n");
			exit(-1);
		}
		offset = adddest(sendBuf, offset, ptr);
	}
	
	ptr = strtok(NULL, ""); //message
	if (ptr == NULL){
		totalnet = htons(offset);
		memcpy(sendBuf, &totalnet, 2);
	} else if (strlen(ptr) > 200){
		splits = splitmessage(sendBuf, offset, ptr, socket);
		ptr += 200*splits;
		finishmessage(sendBuf, offset, ptr);
	} else {
		finishmessage(sendBuf, offset, ptr);
	}

}

//Write common parameters for message packet - flag, clienthandle, numdest
void messagebase(char *sendBuf, char *handle, int clientlen, int numdest){
	int flag = 5;
	memcpy(sendBuf+2, &flag, 1);
	memcpy(sendBuf+3, &clientlen, 1);
	memcpy(sendBuf+4, handle, clientlen);
	memcpy(sendBuf+4+clientlen, &numdest, 1);
}

//Add last few parameters to message packet - PDU length
void finishmessage(char *sendBuf, int offset, char *ptr){
	memcpy(sendBuf+offset, ptr, strlen(ptr));
	offset += strlen(ptr);
	uint16_t totalnet = htons(offset);
	memcpy(sendBuf, &totalnet, 2);
}

//Add destination handles to message packet
int adddest(char *sendBuf, int offset, char *ptr){
	char desthandle[100];
	memset(desthandle, 0, 100);
	int destlen = strlen(ptr);
	memcpy(desthandle, ptr, destlen);
	memcpy(sendBuf+offset, &destlen, 1);
	offset += 1;
	memcpy(sendBuf+offset, desthandle, destlen);
	offset += destlen;
	return offset;
}

//Check user input for number of message destinations
void checknumdest(int numdest){
	if (numdest < 1 || numdest > 9){
		printf("Only 1-9 destinations possible\n");
		exit(-1);
	}
}

//If message length > 200, split and send packets 
int splitmessage(char *buf, int offset, char *message, int socket){
	char outbuf[MAXBUF];
	memset(outbuf, 0, MAXBUF);
	memcpy(outbuf, buf, offset);
	int msglen = strlen(message);
	int count = 0;
	uint16_t totallen = 0, nettotallen = 0;
	while (msglen > 200){
		memset(outbuf+offset, 0, MAXBUF-offset);
		memcpy(outbuf+offset, message, 200);
		outbuf[offset+200] = '\0';
		message += 200;
		msglen -= 200;
		count++;
		totallen = offset + 200;
		nettotallen = htons(totallen);
		memcpy(outbuf, &nettotallen, 2);
		safesend(socket, outbuf, totallen);
	}
	return count;
}
