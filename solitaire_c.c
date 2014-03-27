/*
 *
 * SOLITAIRE CLIENT 
 *
 */

// This is here so I can use the function 'usleep'
//
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <strings.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "solitaire.h"
#include "cardstack.h"
#include "carddeck.h"

#define MAXLINE 8192

//
// Jim's functions (Edited functions are marked)
//
int connectToServer(char *host, int port);
int receiveAck(int socket); 				/* Edited -- mostly cosmetic */
int main(int argc, char **argv);			/* Edited -- mostly cosmetic */

// 
// Our functions
//
void receiveBoard(int server);
void updateBoard(int server, int status);

int connectToServer(char *host, int port) {

	//
	// Look up the host's name to get its IP address.
	//
	struct hostent *hp;
	if ((hp = gethostbyname(host)) == NULL) {
		fprintf(stderr,"GETHOSTBYNAME failed.\n");
		exit(-1);
	}

	//
	// Request a socket and get its file descriptor.
	//
	int clientfd;
	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr,"SOCKET creation failed.\n");
		exit(-1);
	}

	//
	// Fill in the host/port info into a (struct sockaddr_in).
	//
	struct sockaddr_in serveraddr;
	bzero((char *) &serveraddr, sizeof(struct sockaddr_in));
	serveraddr.sin_family = AF_INET;
	bcopy((char *) hp->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
	serveraddr.sin_port = htons(port);

	//
	// Connect to the given host at the given port number.
	//
	if (connect(clientfd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr_in)) < 0) {
		fprintf(stderr,"CONNECT failed.\n");
		exit(-1);
	}

	unsigned char *ip;
	ip = (unsigned char *)&serveraddr.sin_addr.s_addr;
	printf("Connected to solitaire service at %d.%d.%d.%d.\n",ip[0],ip[1],ip[2],ip[3]);

	return clientfd;
}

int receiveAck(int socket) 
{
	char buffer[MAXLINE];
	read(socket, buffer, MAXLINE);
	
	if 	(strcmp(buffer,"SUCCESS") == 0) return SUCCESS;
	else if (strcmp(buffer,"FAILURE") == 0) return FAILURE;

	
	fprintf(stderr,"Received unexpected message from the server.\n");
	fprintf(stderr,"Message was '%s'.\n", buffer);
	exit(-1);
}	


// 
// receiveBoard
//
// receiveBoard is a helper function for updateBoard. It 
// requests the updated solitaire board from the server
//
void receiveBoard(int server)
{	
	usleep(500);
	char buffer[MAXLINE];
	int bytes_read = 0;
	int temp = 0;

	// The read function does not necessarily read 
	// 100% of a message in one go. This loop accounts for 
	// that by ensuring #MAXLINE bytes have been read into
	// the buffer before exiting. The internal part of the
	// loop is just error handling.
	//
	while (bytes_read < MAXLINE) 
	{
		temp = read(server, buffer, MAXLINE);
		if (temp == 0)		break;
		else if ( temp < 0 ) 	perror("Problem reading from socket ");
		bytes_read += temp;
	}
	
	// 
	// If the length of the buffer is 0, there is most likely
	// no connection between the server and client. Our program
	// disconnects clients if the server is 'full'.
	//
	if ( strlen(buffer) == 0)
	{
		fprintf(stderr, "Sorry, the server is full.\n");
		exit(0);
	}
	
	printf("%s", buffer);
}

// 
// updateBoard
//
// updateBoard runs after each play. It clears the terminal, reprints
// the rules, and then prints the updated solitaire board through
// a call to receiveBoard. Depending on whether the play was accepted
// or not, one of the two messages below receiveBoard(server) displays.
//
void updateBoard(int server, int status) {
	
	system("clear");

	printf("Welcome to the remote SOLITAIRE game for MATH 442.\n\n");
	printf("Here is a key of possible card plays you can make:\n");
	printf("\tplay <face><suit> - throw a card onto an arena pile in the center.\n");
	printf("\tmove k<suit> - move a king onto an empty pile.\n");
	printf("\tmove <face><suit> <face><suit>- move a card, putting it on top of another card.\n");
	printf("\tdraw - draw and overturn the next card, placing it on the discard pile.\n");
	printf("\tup - update the board to see if anyone has played to the arena.\n");
	printf("\tquit - safely exit the server.\n");
	printf("where <face> is one of a,2,3,...,9,t,j,q,k and <suit> is one of d,c,s,h.\n");

	receiveBoard(server);
	if (status) 	printf("\nPlease enter a play: ");
	else 		printf("\nPlay not made. Enter different play: ");
}

int main(int argc, char **argv) {

	char buffer[MAXLINE];

	// 
	// Check for host name and port number.
	// 
	if (argc != 3) 
	{
		fprintf(stderr,"usage: %s <host> <port>\n", argv[0]);
		exit(0);
	}

	//
	// Connect to the server.
	//
	int server = connectToServer(argv[1],atoi(argv[2]));

	// 
	// Get the initial board state from the server
	//
	updateBoard(server, 1);
	
	// 
	// This infinite while loop runs the game. The basic structure
	// is as follows:
	// 	
	// 	(1) Get user input (a play)
	// 	(2) Verify that the play is valid with the server
	// 	(3) Update the board to reflect the most
	// 	recent play
	// 	(4) Repeat
	//
	while (1) 
	{	
		
		fgets(buffer,MAXLINE,stdin);
		write(server,buffer,strlen(buffer)+1);
		

		if (receiveAck(server)) updateBoard(server, 1);
		else 			updateBoard(server, 0);		
	}

}


