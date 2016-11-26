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
	int counter = 0;
	while (fgets(str_to_send, msg_len, file) != NULL) {
		if (str_to_send[0] != '\n') { // mencegah karakter newline untuk ditransmisikan
			
			/** XOFF, transmisi dipause **/
			if (lastByteReceived == XOFF) {
				printf("XOFF diterima.\n");

				// waiting for XON
				while (lastByteReceived == XOFF) {
					printf("Menunggu XON...\n");
					usleep(100000);
				}
				counter++;
				printf("Mengirim byte ke-%d: '%s'\n", counter, str_to_send);
				sendto(socket_desc, str_to_send, strlen(str_to_send), 0, (struct sockaddr *)&server, sizeof(server));
				printf("XON diterima.\n");
			
			/** XON, transmisi berjalan **/
			} else {
				
				usleep(10000);
				// sending bytes (one character)
				counter++;
				printf("Mengirim byte ke-%d: '%s'\n", counter, str_to_send);
				sendto(socket_desc, str_to_send, strlen(str_to_send), 0, (struct sockaddr *)&server, sizeof(server));

				// reset string
				memset(str_to_send, 0, sizeof(str_to_send));
			}
		}
	}
	// mengirim endfile
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
	Byte recv_str[1];

	while (true) {
		// menunggu signal XON/XOFF
		rf = recvfrom(socket_desc, recv_str, 1, 0, (struct sockaddr *)&server, (socklen_t*)&server_len);

		if (rf < 0) {
			perror ("Error: failed receiving XON/XOFF from socket");
			exit(1);
		}
		// XON or XOFF
		lastByteReceived = recv_str[0];
		// reset
		memset(recv_str, 0, sizeof(recv_str));
	}

	printf("Exit - XON/XOFF handler");
	pthread_exit(0);
}
