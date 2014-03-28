#define _XOPEN_SOURCE 600

#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <strings.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "solitaire.h"
#include "cardstack.h"
#include "carddeck.h"

// MAXTHREADS is a global constant that detrmines
// the size of the game server.
//
#define MAXLINE 8192
#define MAXCONNECTIONS 10
#define MAXTHREADS 20

int threadnumber = 0;
pthread_mutex_t threadnumber_mutex = PTHREAD_MUTEX_INITIALIZER;

arena_t *newArena();
deal_t *newDeal(deck_t *deck);
void putArena(arena_t *arena);
void putDeal(deal_t *deal);
int arenaTake(card_t *card, arena_t *arena);
int makeArenaPlay(card_t *card, arena_t *arena);
cardstack_t *findEmptyLainStack(deal_t *deal);
int moveKingOntoFree(card_t *king, deal_t *deal);
int moveCardOntoAnother(card_t *card, card_t *onto);
int pullFromDrawPile(deal_t *deal);
void getNextPlay(int stream, play_t *play, deck_t *deck);		    /* EDITED */
void sendAck(int client, int ack);
void playSolitaire(int client, deck_t *deck, arena_t *arena);		/* EDITED */
int acceptClientOn(int socket);						                      /* EDITED */
int initConnection(int port);
char *sendArena(arena_t *arena);
char *sendDeal(deal_t *deal);
void sendBoard(int client, arena_t *arena, deal_t *deal);
void *connection_handler(void *arguments);

arena_t *newArena() {
	arena_t *arena = (arena_t *)malloc(sizeof(arena_t));
	for (int s = 0; s < 4; s++) {
		arena->highest[s] = 0;
	}
	return arena;
}

deal_t *newDeal(deck_t *deck) {

	deal_t *deal = (deal_t *)malloc(sizeof(deal_t));

	// Make all the stacks.
	deal->draw = new_cardstack(DRAW);
	deal->discard = new_cardstack_fed_by(DISCARD,deal->draw);
	for (int p = 0; p < 7; p++) {
		deal->hidden[p] = new_cardstack(HIDDEN);
		deal->lain[p] = new_cardstack_fed_by(LAIN,deal->hidden[p]);
	}

	// Build the draw pile.
	for (int i = 0; i < 52; i++) {
		push(&deck->cards[deck->order[i]],deal->draw);
	}

	// Fill each of the hidden piles.
	for (int i = 0; i < 7; i++) {
		// Place first cards, then second cards, then third...
		for (int j = i; j < 7; j++) {
			push(pop(deal->draw),deal->hidden[j]);
		}
	}
	
	// Flip the top card of each pile.
	for (int i = 0; i < 7; i++) {
		feed(deal->lain[i]);
	}

	// Flip up the first card.
	feed(deal->discard);

	return deal;
}

void putArena(arena_t *arena) {
	for (int s=0; s<4; s++) {
		printf("[");
		putCardOfCode(arena->highest[s],s);
		printf("] ");
	}
	printf("\n");
}

//
// sendArena
//
// Sends the arena to client. The code relies on a new function
// defined in carddeck.c, sendCardOfCode. It is basically a copy
// of putCardOfCode, but it returns a string rather than printing
// out a line.
//
char *sendArena(arena_t *arena) {
	char strArena[MAXLINE];
	strArena[0] = '\0';
	
	strcat(strArena, "\n");
	for (int s=0; s<4; s++) {
		strcat(strArena, "[");		
		strcat(strArena, sendCardOfCode(arena->highest[s],s));		
		strcat(strArena, "] ");
	}
	strcat(strArena, "\n\n");
	
	char *str = malloc(MAXLINE);
	strcpy(str, strArena);
	return str;
}

void putDeal(deal_t *deal) {
	for (int p = 6; p >= 0; p--) {
		printf("[");
		put_cardstack(deal->hidden[p],FALSE);
		printf("]");
		put_cardstack(deal->lain[p],TRUE);
		printf("\n");
	}
	put_cardstack(deal->discard,TRUE);
	printf("\n");
	printf("[");
	put_cardstack(deal->draw,FALSE);
	printf("]\n\n");
}

//
// sendDeal
//
// Sends the deal to client. Again, this function relies on a 
// new function defined in cardstack.c, send_cardstack. Again,
// this is basically a clone of put_cardstack, but it returns a
// string rather than void.
//
char *sendDeal(deal_t *deal) {
	char strDeal[MAXLINE];
	strDeal[0] = '\0';

	for (int p = 6; p >= 0; p--)
	{
		strcat(strDeal, "[");
		strcat(strDeal, send_cardstack(deal->hidden[p], FALSE));
		strcat(strDeal, "]");
		strcat(strDeal, send_cardstack(deal->lain[p], TRUE));
		strcat(strDeal, "\n");
	}
	strcat(strDeal, send_cardstack(deal->discard, TRUE));
	strcat(strDeal, "\n");
	strcat(strDeal, "[");
	strcat(strDeal, send_cardstack(deal->draw,FALSE));
	strcat(strDeal, "]\n");

	char *str = malloc(MAXLINE);
	strcpy(str, strDeal);
	return str;
}

//
// sendBoard
//
// Sends the board to client by producing a large string which
// is then written into the buffer, which the client proceeds
// to read.
//
void sendBoard(int client, arena_t *arena, deal_t *deal) {
	int bytes_sent = 0;
	int temp = 0;
	char buffer[MAXLINE];
	buffer[0] = '\0';
	char *a = sendArena(arena);
	char *d = sendDeal(deal);
	char *board = strcat(a, d);
	strcpy(buffer, board); 
	
	// 
	// This while loop ensures that the correct amount of data
	// has been written into the buffer. 'Write' sometimes cuts
	// out before finishing. 'Write' returns the number of bytes written,
	// so if temp < MAXLINE, then the loop continues.
	//
	while (bytes_sent < MAXLINE) 
	{
		temp = write(client, buffer, MAXLINE);	
		if (temp == 0) 		break;
		else if (temp < 0) 	perror("Problem writing to socket: ");
		
		bytes_sent += temp;
	}

}

int arenaTake(card_t *card, arena_t *arena) {
	if (arena->highest[card->suit] == card->face-1) {
		arena->highest[card->suit]++;
		return SUCCESS;
	} else {
		return FAILURE;
	}
}

int makeArenaPlay(card_t *card, arena_t *arena) {

	cardstack_t *stack = card->stack;

	if (isOnTop(card) && (arenaTake(card,arena) == SUCCESS)) {
		pop(stack);
		if (stack->type == LAIN && is_empty(stack) && !is_empty(stack->feed)) {
			feed(stack);
		}
		return SUCCESS;
	} else {
		return FAILURE;
	}
}

cardstack_t *findEmptyLainStack(deal_t *deal) {
	for (int p = 0; p < 7; p++) {
		if (is_empty(deal->lain[p])) {
			return deal->lain[p];
		}
	}
	return NULL;
}

int moveKingOntoFree(card_t *king, deal_t *deal) {
	cardstack_t *dest = findEmptyLainStack(deal);
	if (isUp(king) && dest != NULL) {
		move_onto(king,dest);
		return SUCCESS;
	} else {
		return FAILURE;
	}
}

int moveCardOntoAnother(card_t *card, card_t *onto) 
{
	cardstack_t *dest = onto->stack;
	if ((dest->type == LAIN) && isUp(card) && isOnTop(onto) && isOkOn(card,onto)) 
	{
		move_onto(card,dest);
		return SUCCESS;
	} 
	else {
		return FAILURE;
	}
}

int pullFromDrawPile(deal_t *deal) 
{
	if (is_empty(deal->draw)) 
	{
		return FAILURE;
	} 
	else 
	{
		feed(deal->discard);
		return SUCCESS;
	}
}

void getNextPlay(int stream, play_t *play, deck_t *deck) 
{
	static char buffer[MAXLINE];
	static char cmd[MAXLINE];
	static char from[MAXLINE];
	static char onto[MAXLINE];
	read(stream,buffer,MAXLINE);
	sscanf(buffer,"%s",cmd);
	if (cmd[0] == 'm') 
	{
		// it's a move onto a lain stack
		sscanf(buffer,"%s %s",cmd,from);
		play->from = cardOf(from,deck);
		if (isKing(play->from)) 
		{
			play->type = KING_PLAY;
			play->onto = NULL;
		} 
		else 
		{
			play->type = MOVE_PLAY;
			sscanf(buffer,"%s %s %s",cmd,from,onto);
			play->onto = cardOf(onto,deck);
		}
	}
	else if (cmd[0] == 'p') 
	{
		// it's a play onto the arena
		play->type = ARENA_PLAY;
		sscanf(buffer,"%s %s",cmd,from);
		play->from = cardOf(from,deck);
		play->onto = NULL;
	} 
	else if (cmd[0] == 'd') 
	{
		// it's a draw
		play->type = DRAW_PLAY;
		play->from = NULL;
		play->onto = NULL;
	}
	else if (cmd[0] == 'u') play->type = UPDATE;
	else if (cmd[0] == 'q') play->type = QUIT;
	
}
	
void sendAck(int client, int ack) {
	if (ack == SUCCESS) {
		write(client,"SUCCESS",8);
	} else {
		write(client,"FAILURE",8);
	}
}

void playSolitaire(int client, deck_t *deck, arena_t *arena) 
{
	// initialize
  //
	deal_t *deal = newDeal(deck);
	int i=0;
	play_t play;
	int status = SUCCESS;
	
	// Send the initial game-state to the client.
	//
	sendBoard(client, arena, deal);

	// This code, despite being cosmetically different, is largely 
	// the same as the original code. I split the switch into an
	// if-else switch, because I intended to fuss with arena plays.
	// When a single client makes an arena play, I wanted to somehow
	// broadcast that to every other client, automatically updating
	// the arena for every client. I have added to other play-types:
	// Update (since I couldn't get the auto update to work), and
	// quit, to cleanly break from the server.
	//
	while (1)
	{
		getNextPlay(client,&play,deck);

		if (play.type == ARENA_PLAY) {		
			status = makeArenaPlay(play.from,arena);
			sendAck(client,status);
		} else if (play.type == KING_PLAY) {
			status = moveKingOntoFree(play.from,deal);
			sendAck(client, status);
		} else if (play.type == MOVE_PLAY) {
			status = moveCardOntoAnother(play.from,play.onto);
			sendAck(client, status);
		} else if (play.type == DRAW_PLAY) {
			status = pullFromDrawPile(deal);
			sendAck(client, status);
		} else if (play.type == UPDATE) {
			status = SUCCESS;
			sendAck(client, status);
		} else if (play.type == QUIT) {
			threadnumber--;
			break;
		}
		
		sendBoard(client, arena, deal);

	}
	
}

int acceptClientOn(int socket) {

	socklen_t acceptlen = sizeof(struct sockaddr_in);
	struct sockaddr_in clientaddr;
	int connection;
	int ret = 0;
	int *client = malloc(1);
	arena_t *arena = newArena();
	pthread_t threads[MAXTHREADS];

	// Infinite loop that looks for clients trying to connect.
	// 
	while(1) 
	{	
		connection = accept(socket, (struct sockaddr *)&clientaddr, &acceptlen);
		
		// If the connection is established
		//
		if (connection > 0)
		{

			*client = connection;
			
			// Protect the shared threadnumber
			//
			pthread_mutex_lock(&threadnumber_mutex);

			// Assuming we have not exceeded the max number
			// of clients, create a tinfo struct and initiate another
			// player into the solitaire game.
			//
			if (threadnumber < MAXTHREADS)
			{
				tinfo_t *tinfo = (tinfo_t *)malloc(sizeof(tinfo_t));
				tinfo->client = client;
				tinfo->arena = arena;
				tinfo->deal = NULL;

				ret = pthread_create(&threads[threadnumber], NULL, 
								connection_handler, (void *)tinfo);
				
				// This if-else just ensures that the thread creation
				// is successful. If successful, increase the threadnumber,
				// detach the thread, and report the client.  Else, free 
				// the memory allocated for tinfo and close the connection.
				//
				if (ret == 0)
				{
					pthread_detach(threads[threadnumber]);
					threadnumber++;
					
					// Report the client that connected
					// 
					struct hostent *hostp;
					if ((hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
						 					sizeof(struct in_addr), AF_INET)) == NULL) 
					{
						fprintf(stderr, "GETHOSTBYADDR failed.");
					}
		
					printf("Accepted connection from client %s (%s)\n", 
					hostp->h_name, inet_ntoa(clientaddr.sin_addr));
	
				}
				else
				{
					free(tinfo);
					close(connection);
				}

			}

			// If the game is already full, close the connection.
			//
			else
			{
				fprintf(stderr, "Too many clients.\n");
				close(connection);
			}
			pthread_mutex_unlock(&threadnumber_mutex);

		}
		else
		{
			perror("Connection not established ");
			exit(0);
		}
		
	}
	
	return 0;
}


//
// connection_handler 
//
// This function is passed to each thread. It is just an intermediate
// function between acceptClientOn and playSolitaire.
//
void *connection_handler(void *arguments)
{
	// Get the socket descriptor and
	// shared arena.
	//
	tinfo_t *tinfo = (tinfo_t *)arguments;

	deck_t *deck = newDeck();
	shuffle(deck);
	playSolitaire(*tinfo->client, deck, tinfo->arena);

	free(tinfo);
}

int initConnection(int port) 
{
	// Open a socket to listen for client connections. Creates a 
	// socket that is bound to a specific transport service
	// provider.
	//
	int listenfd;
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
	{
		fprintf(stderr,"SOCKET creation failed.\n");
		exit(-1);
	}
	
	// Build the service's info into a (struct sockaddr_in).
	//
	struct sockaddr_in serveraddr;
	bzero((char *) &serveraddr, sizeof(struct sockaddr_in));
	serveraddr.sin_family = AF_INET; 
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	serveraddr.sin_port = htons((unsigned short)port); 

	// Bind that socket to a port. Associates a local address
	// with a socket.
	//
	if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr)) < 0) 
	{
		perror("Bind failed: ");
		exit(-1);
	}

	// Listen for client connections on that socket.
	//
	if (listen(listenfd, MAXCONNECTIONS) < 0) 
	{
		fprintf(stderr,"LISTEN failed.\n");
		exit(-1);
	}
	
	fprintf(stderr,"Solitaire server listening on port %d...\n",port);
	
	return listenfd;
}
	
void srand48(long);
double drand48(void);

int main(int argc, char **argv) 
{
	// Make sure we've been given a port to listen on.
	//
	if (argc != 2) 
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	unsigned long seed = 0;
	if (argc == 1) 
	{
		struct timeval tp; 
		gettimeofday(&tp,NULL); 
		seed = tp.tv_sec;
	} 
	else 
	{
		seed = atol(argv[1]);
	}
	
	printf("Shuffling with seed %ld.\n",seed);
	srand48(seed);
	
	int listener = initConnection(atoi(argv[1]));
	int client = acceptClientOn(listener);

	return 0;
}
