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
int  append(char*s, size_t size, char c) {
     if(strlen(s) + 1 >= size) {
          return 1;
     }
     int len = strlen(s);
     s[len] = c;
     s[len+1] = '\0';
     return 0;
}
#define SIZECHAR 5
#define VARLEN 6

/** WINDOW THING **/
#define LISTSZ 100
#define WINSIZE 5

char listframe[100][1 + 1 + 1 + VARLEN + 1 + 2];
int head = 0;

/** GLOBAL VARIABLES **/
char lastByteReceived = XON;
int socket_desc;
int isMainUp = 1;
struct sockaddr_in server;
char str_to_send[2];

using namespace std;

// Function declarations
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
	char text[VARLEN] = "";
	char chks[3];
	char etx[2]; etx[0] = ETX; etx[1] = 0;
	while (fgets(str_to_send, msg_len, file) != NULL) {
		if ((counter+1) % VARLEN == 0) {
			frame[0] = SOH;
			frame[1] = (char) fnum;
			frame[2] = STX;
			if (str_to_send[0] != '\n') { // mencegah karakter newline untuk ditransmisikan
				strcat(text, str_to_send);


				counter++;

				unsigned short ichks = calc_crc16(text, strlen(text));
				printf("Mengirim byte ke-%d: '%s'\n", counter, text);
				strcat(frame, text);
				chks[0] = ichks & 0xff;
				chks[1] = (ichks >> 8) & 0xff;
				chks[2] = 0;
				printf("Mengirim checksum '%s' from '%d'\n", chks, ichks);
				strcat(frame, etx);
				strcat(frame, chks);
				printf("Mengirim frame '%s'\n", frame);

				strncpy(listframe[fnum], frame, 1 + 1 + 1 + VARLEN + 1 + 2);
				printf("Copied frame '%s'\n", listframe[fnum]);

				sendto(socket_desc, frame, strlen(frame), 0, (struct sockaddr *)&server, sizeof(server));

				// reset string
				memset(str_to_send, 0, sizeof(str_to_send));

			}
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
	// mengirim endfile
	frame[0] = SOH;
	frame[1] = (char) fnum;
	frame[2] = STX;
	unsigned short ichks = calc_crc16(text, strlen(text));
	printf("Mengirim byte ke-%d: '%s'\n", counter, text);
	strcat(frame, text);
	chks[0] = ichks & 0xff;
	chks[1] = (ichks >> 8) & 0xff;
	chks[2] = 0;
	printf("Mengirim checksum '%s' from '%d'\n", chks, ichks);
	strcat(frame, etx);
	strcat(frame, chks);
	printf("Mengirim frame '%s'\n", frame);

	strncpy(listframe[fnum], frame, 1 + 1 + 1 + VARLEN + 1 + 2);
	printf("Copied frame '%s'\n", listframe[fnum]);
	sendto(socket_desc, frame, strlen(frame), 0, (struct sockaddr *)&server, sizeof(server));

	str_to_send[0] = Endfile;
	sendto(socket_desc, str_to_send, strlen(str_to_send), 0, (struct sockaddr *)&server, sizeof(server));

	isMainUp = 0;

	printf("Reached end of file\n");
	printf("Bye\n");
	return 0;
}


/** THREAD
 * 	receives XON/XOFF
 */

void *XON_XOFF_HANDLER(void *args) {

	int rf;
	int server_len = sizeof(server);
	char frame[1 + 1 + 2] = "";

	while (true) {
		// menunggu signal XON/XOFF
		rf = recvfrom(socket_desc, frame, 4, 0, (struct sockaddr *)&server, (socklen_t*)&server_len);

		if (rf < 0) {
			perror ("Error: failed receiving XON/XOFF from socket");
			exit(1);
		}
		// XON or XOFF
		if(frame[0] == NAK)
		{
			printf("NAK\n");
		}
		else
		{
			printf("ENQ\n");
		}
		// reset
		memset(frame, 0, sizeof(frame));
	}

	printf("Exit - XON/XOFF handler");
	pthread_exit(0);
}
