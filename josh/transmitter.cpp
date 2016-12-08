/*
* File : transmitter.cpp
* Author : Joshua K - 012, Albertus K - 100, Luthfi K -102
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "dcomm.h"
#include "crc16.h"

/** FRAME THING **/
// Variable length of data inside frame
#define VARLEN 16

/** WINDOW THING **/
//buffer list frame size
#define LISTSZ 256

//window size
#define WINSIZE 5

pthread_t ftimer[LISTSZ];

char listframe[LISTSZ][1 + 1 + 1 + VARLEN + 1 + 2];
bool listfbool[LISTSZ];
int headWin = 1;

/** GLOBAL VARIABLES **/
char lastByteReceived = XON;
int socket_desc;
int isMainUp = 1;
struct sockaddr_in server;
char str_to_send[2];

using namespace std;

// Function declarations
void *TIMER_HANDLER(void *threadid);
void *XON_XOFF_HANDLER(void *args);


/**
 * MAIN function
 * sends bytes to receiver
 */
int main(int argc, char *argv[])
{
	/** Initialization **/
	FILE* file;
	int msg_len = 2;
	int sock;
	char* hostname = argv[1];
	char* port = argv[2];
	char* fileName = argv[3];

	/** Load arguments **/
	hostname = argv[1];
	port = argv[2];
	fileName = argv[3];

	/** Socket **/
	// creates socket
    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_desc == -1)
    {
        printf("Error: could not create socket");
    }

    // initializes object server attributes
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(hostname);
    server.sin_port = htons(atoi(port));

	/** Initializes thread **/
	pthread_t xon_xoff_handler;
	// create thread xon_xoff_handler
	if(pthread_create(&xon_xoff_handler, NULL, XON_XOFF_HANDLER, NULL) < 0)
	{
		perror("Error: could not create thread");
		return 1;
	}

	printf("Membuat socket untuk koneksi ke %s:%s\n", hostname, port);

	/** Sends bytes to receiver **/
	// open file
	file = fopen(fileName, "r");

	if (file == NULL) {
		perror("Error: could not open file");
		exit(1);
	}

	// reads a line from the file and stores it into str_to_send
	// reads (msg_len - 1) characters
	// msg_len is 2 so that it only reads one character
	int counter = 0, fnum = 1;
	char frame[1 + 1 + 1 + VARLEN + 1 + 2] = "";
	char text[VARLEN+1] = "";
	char chks[3];
	char etx[2]; etx[0] = ETX; etx[1] = 0;
	
	// create buffer list frame
	while (fgets(str_to_send, msg_len, file) != NULL) {
		if ((counter+1) % VARLEN == 0) {
			frame[0] = SOH;
			frame[1] = (char) fnum;
			frame[2] = STX;
			if (str_to_send[0] != '\n') { // mencegah karakter newline untuk ditransmisikan
				strcat(text, str_to_send);
				counter++;
				
				printf("byte ke-%d: '%s'\n", counter, text);
				strcat(frame, text);
				
				unsigned short ichks = calc_crc16(frame, strlen(frame));
				chks[0] = ichks & 0xff;
				chks[1] = (ichks >> 8) & 0xff;
				chks[2] = 0;
				printf("checksum '%s' from '%d'\n", chks, ichks);
				strcat(frame, etx);
				strcat(frame, chks);
				printf("frame '%s'\n", frame);

				strncpy(listframe[fnum], frame, 1 + 1 + 1 + VARLEN + 1 + 2);
				printf("Copied frame '%s'\n", listframe[fnum]);

				// reset string
				memset(str_to_send, 0, sizeof(str_to_send));

			}
			// resetting every char array
			memset(text, 0, sizeof text);
			memset(frame, 0, sizeof frame);
			memset(chks, 0, sizeof chks);
			fnum++;
		}
		else {
			if (str_to_send[0] != '\n') {
				strcat(text, str_to_send);
				counter++;
			}
		}
	}
	// creating last frame
	frame[0] = SOH;
	frame[1] = (char) fnum;
	frame[2] = STX;
	
	printf("byte ke-%d: '%s'\n", counter, text);
	strcat(frame, text);							
	
	unsigned short ichks = calc_crc16(frame, strlen(frame));
	chks[0] = ichks & 0xff;
	chks[1] = (ichks >> 8) & 0xff;
	chks[2] = 0;
	printf("checksum '%s' from '%d'\n", chks, ichks);
	strcat(frame, etx);
	strcat(frame, chks);
	printf("frame '%s'\n", frame);

	strncpy(listframe[fnum], frame, 1 + 1 + 1 + VARLEN + 1 + 2);
	printf("Copied frame '%s'\n", listframe[fnum]);
	
	// sending frame with sliding window
	long cnt = 1;
	int timer = 1;
	while (headWin < fnum) {
		if ((cnt < headWin + WINSIZE)&&(cnt <= fnum)) {
			while (lastByteReceived == XOFF) {
				//wait until xon
			}
			
			printf("Send frame-%ld\n", cnt);
			
			pthread_create(&ftimer[cnt], NULL, TIMER_HANDLER, (void *)cnt);
			usleep(2);
			
			cnt++;
		}
		else {
			//printf("Waiting ACK\n");
			
			usleep(5);
						
		}
	}
	
	isMainUp = 0;

	printf("Reached end of file\n");
	
	for (int idx = 1; idx <= fnum; idx++) {
		if (pthread_join(ftimer[idx], NULL) == -1)	{
			perror("pthread_join");
			return -1;
		}
	}
	
	printf("Bye\n");
	return 0;
}

// handles sending frame and its timeout
void *TIMER_HANDLER(void *threadid) {
	long tid;
	tid = (long)threadid;
	
	int i = 0;
	while (!listfbool[tid]) {
		
		while (lastByteReceived == XOFF) {
			// wait for XON
		}
		
		if (i==0) {
			printf("Sending! Thread ID, %ld\n", tid);
		   
			sendto(socket_desc, listframe[tid], strlen(listframe[tid]), 0, (struct sockaddr *)&server, sizeof(server));
		}
		usleep(10);
		i++;
		i = i % 100000;
	}
	pthread_exit(NULL);
}

// handles ACK and NAK receive
void *XON_XOFF_HANDLER(void *args) {

	int rf;
	int server_len = sizeof(server);
	char frame[1 + 1 + 2] = "";

	while (true) {
		// menunggu signal XON/XOFF
		rf = recvfrom(socket_desc, frame, 4, 0, (struct sockaddr *)&server, (socklen_t*)&server_len);

		if (rf < 0) {
			perror ("Error: failed receiving ACK/NAK from socket");
			exit(1);
		}
		
		int fidx = (int)frame[1];
		if(frame[0] == NAK) //get NAK
		{
			printf("NAK %d received\n", fidx);
			printf("Resend frame '%s'\n", listframe[fidx]);
			sendto(socket_desc, listframe[fidx], strlen(listframe[fidx]), 0, (struct sockaddr *)&server, sizeof(server));
		}
		else if(frame[0] == ENQ) // get ACK
		{
			printf("ACK %d received\n", fidx);
			listfbool[fidx] = true;
			while(listfbool[headWin]) {
				headWin++;
			}
		}
		else if(frame[0] == XON) // get XON
		{
			printf("Get XON\n");
			lastByteReceived = XON;
		}
		else if(frame[0] == XOFF) // get XOFF
		{
			printf("Get XOFF\n");
			lastByteReceived = XOFF;
		}
		
		// reset
		memset(frame, 0, sizeof(frame));
	}

	printf("Exit - ACK/NAK handler");
	pthread_exit(0);
}
