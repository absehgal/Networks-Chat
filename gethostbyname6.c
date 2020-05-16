/* Code written by Hugh Smith	April 2017	*/

/* replacement code for gethostbyname - works for IPv4 and IPV6  */
// Feel free to copy, just leave my name in it, use at your own risk.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
   
 #include "gethostbyname6.h"
 
uint8_t * gethostbyname6(const char * hostName)
{
	struct sockaddr_in6 aSockaddr6;
		
	return(getIPAddress6(hostName, &aSockaddr6));
}

char * getIPAddressString(uint8_t * ipAddress)
{
	// makes it easy to print the IP address (v4 or v6)
	static char ipString[INET6_ADDRSTRLEN];

	if (ipAddress != NULL)
	{
		inet_ntop(AF_INET6, ipAddress, ipString, sizeof(ipString));
	}
	else
	{
		strcpy(ipString, "(IP not found)");
	}
	
	return ipString;
}

uint8_t * getIPAddress6(const char * hostName, struct sockaddr_in6 * aSockaddr6) 
{
	// Puts host IPv6 (or mapped IPV) into the aSockaddr6 struct and return pointer to 16 byte address (NULL on error)
	// Only pulls the first IP address from the list of possible addresses
	
	static uint8_t ipAddress[16];
	
	uint8_t * returnValue = NULL;
	int addrError = 0;
	struct addrinfo hints;	
	struct addrinfo *hostInfo = NULL;

	memset(&hints,0,sizeof(hints));
	hints.ai_flags = AI_V4MAPPED | AI_ALL;
	hints.ai_family = AF_INET6;
	
	if ((addrError = getaddrinfo(hostName, NULL, &hints, &hostInfo)) != 0)
	{
		fprintf(stderr, "Error getaddrinfo (host: %s): %s\n", hostName, gai_strerror(addrError));
		returnValue = NULL;
	}
	else 
	{
		memcpy(((struct sockaddr_in6 *)aSockaddr6)->sin6_addr.s6_addr, &(((struct sockaddr_in6 *)hostInfo->ai_addr)->sin6_addr.s6_addr), 16);
		memcpy(ipAddress, &((struct sockaddr_in6 *)aSockaddr6)->sin6_addr.s6_addr, 16); 
	
		returnValue = ipAddress;
		freeaddrinfo(hostInfo);
	}

  return returnValue;    // Either Null or IP address
}
