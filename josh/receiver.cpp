/*
* File : receiver.cpp
* Author : Joshua K - 012, Albertus K - 100, Luthfi K -102
*/
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "dcomm.h"
#include "crc16.h"

/* Define receive buffer size */
#define RXQSIZE 6 + VARLEN
#define UPPERLIMIT 4
#define LOWERLIMIT 2

/** FRAME THING **/
// Variable length of data inside frame
#define VARLEN 16
char frame[1 + 1 + 1 + VARLEN + 1 + 2] = "";

// Get position of ETX char inside frame
int getEtx (char* frame) {
	int i = 3;
	while ((frame[i] != ETX)&&(i < RXQSIZE)) {
		i++;
	}
	return i;
}

// Get whether the frame is corrupted or not using checksum
bool checkSum (char* frame) {
	char text[4 + VARLEN];
	memset(text, 0, sizeof text);
	char chks[2];
	for (int i = 0;i < getEtx(frame);i++) {
		text[i] = frame[i];
	}
	printf("Getting checksum for %s\n", text);
	unsigned short ichks = calc_crc16(text, strlen(text));
	chks[0] = ichks & 0xff;
	chks[1] = (ichks >> 8) & 0xff;
	printf("Checksum: should be %d-%d ; get %d-%d\n", (int) chks[0], (int) chks[1], frame[getEtx(frame)+1], frame[getEtx(frame)+2]);
	if ((frame[getEtx(frame)+1] != chks[0]) || (frame[getEtx(frame)+2] != chks[1])) {
		return false;
	} else {
		return true;
	}
}

/** WINDOW THING **/
//buffer list frame size
#define LISTSZ 256

//window size
#define WINSIZE 5

char listframe[LISTSZ][1 + 1 + 1 + VARLEN + 1 + 2];
bool listfbool[LISTSZ];
int headWin = 1;
int ucbuffersize = 0;

/* Delay to adjust speed of consuming buffer, in milliseconds */
#define DELAY 600000

/* Message */
Byte x_msg[1];
char r_msg[1];

/* Buffer */
Byte rxbuf[RXQSIZE];
QTYPE rcvq = {0, 0, 0, RXQSIZE, rxbuf };
QTYPE *rxq = &rcvq;
Byte sent_xonxoff = XON;
bool send_xon = false, send_xoff = false, end = false, childead = false;
FILE* pipein_fp;
char readbuf[80];

/* Server-Client socket address */
struct sockaddr_in sserver, sclient;
socklen_t cli_len = sizeof(sclient);


/* Socket */
int sockfd; // listen on sock_fd

/* Functions declaration */
static Byte *rcvchar(int sockfd, QTYPE *queue);
static void sendXON();
static void *consume(void *param);

// Send NAK to transmitter
void sendNAK(int frameNum, int udpSocket) {
	char frame[1 + 1 + 2] = "";
	char chks[3];
	frame[0] = NAK;
	frame[1] = frameNum;
	unsigned short ichks = calc_crc16(frame, strlen(frame));
	chks[0] = ichks & 0xff;
	chks[1] = (ichks >> 8) & 0xff;
	chks[2] = 0;
	strcat(frame, chks);
	printf("Send NAK %d\n", frameNum);
	sendto(udpSocket, frame, strlen(frame), 0, (struct sockaddr *)&sclient,sizeof(sclient));
}

// Send ACK to transmitter
void sendENQ(int frameNum, int udpSocket) {
	char frame[1 + 1 + 2] = "";
	char chks[3];
	frame[0] = ENQ;
	frame[1] = frameNum;
	unsigned short ichks = calc_crc16(frame, strlen(frame));
	chks[0] = ichks & 0xff;
	chks[1] = (ichks >> 8) & 0xff;
	chks[2] = 0;
	strcat(frame, chks);
	printf("Send ACK %d\n", frameNum);
	sendto(udpSocket, frame, strlen(frame), 0, (struct sockaddr *)&sclient,sizeof(sclient));
}


int main(int argc, char *argv[])
{
	Byte c;
	/* bind socket to the port number given in argv[1].
	*/
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
    {
        puts("Could not create socket");
        return 1;
    }

	sserver.sin_family = AF_INET;
	sserver.sin_addr.s_addr = INADDR_ANY;
	sserver.sin_port = htons(atoi(argv[1]));

	if (bind(sockfd,(struct sockaddr *)&sserver, sizeof(sserver)) < 0)
    {
        perror("Bind failed. Error");
        return 1;
    }
	// Get ip address
	pipein_fp = popen("hostname -I","r");
	char* ip;
	fgets(ip, 13, pipein_fp);
	pclose(pipein_fp);
	printf("Binding pada %s %d...\n", ip, atoi(argv[1]));

	/* Initialize XON/XOFF flags */
	sent_xonxoff = XON;
	send_xon = true, send_xoff = false;

	/* Create child process */
	int j=0;
	pthread_t consume_thread;

	if(pthread_create(&consume_thread, NULL, consume, &rcvq)){
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	/*** IF PARENT PROCESS ***/
	while (true) {
		c = *(rcvchar(sockfd, rxq));
		/* Quit on end of file */
		if (end) {
			while (true) {
				if(childead)
				{
					exit(0);
				}
			}
		}

	}

	if(pthread_join(consume_thread,NULL)){
		fprintf(stderr,"Error joining thread\n");
		return 2;
	}
}

static Byte *rcvchar(int sockfd, QTYPE *queue)
{
	/**
	Read a character from socket and put it to the receive buffer. If the number of characters in the
	* receive buffer is above certain level, then send XOFF and set a flag
	*/
	if ((ucbuffersize >= UPPERLIMIT)&&(!send_xoff)) { //buffer full
		sent_xonxoff = XOFF;
		send_xon = false;
		send_xoff = true;

		x_msg[0] = sent_xonxoff;

		// send 'sent_xonxoff' via socket
		if (sendto(sockfd, x_msg, 1, 0, (struct sockaddr *)&sclient,sizeof(sclient)) > 0){
			puts("Buffer > minimum upperlimit.\nMengirim XOFF.");
		}
		else {
			puts("XOFF gagal dikirim");
		}
	}

	// read char from socket (receive)
	if (recvfrom(sockfd, frame, RXQSIZE, 0, (struct sockaddr *)&sclient, &cli_len) < 0) {
		puts("Receive byte failed");
	} else {
		printf("From transmitter: %s, framenum %d, checksum %c%c\n", frame, (int)frame[1], frame[getEtx(frame)+1], frame[getEtx(frame)+2]);

		if (checkSum(frame) == true) {
			printf("Checksum pass, file OK!\n");
			sendENQ(frame[1],sockfd);
			int fnum = (int)frame[1];
			strncpy(listframe[fnum], frame, 1 + 1 + 1 + VARLEN + 1 + 2);
			listfbool[fnum] = true;
			ucbuffersize++;
			printf("ucbuffersize : %d\n", ucbuffersize);
		}
		else {
			printf("Checksum failed, file corrupted!\n");
			sendNAK(frame[1],sockfd);
		}
	}
	// check end of file
	if(frame[3] == Endfile) {
		puts("Received end of file");
		end = true;
		return queue->data;
	}
	else {
	// transfer from received msg to buffer

	queue->data[queue->rear] = r_msg[0];

	// increment rear index, check for wraparound, increased buffer content size
	queue->rear += 1;
	if (queue->rear >= RXQSIZE) queue->rear -= RXQSIZE;
	queue->count += 1;

	return queue->data;
	}
}

static void sendXON(){
	/** send XON **/
	if ((ucbuffersize <= LOWERLIMIT) && (!send_xon)){
		sent_xonxoff = XON;
		send_xon = true;
		send_xoff = false;
		x_msg[0] = sent_xonxoff;

		if (sendto(sockfd, x_msg, 1, 0, (struct sockaddr *)&sclient,sizeof(sclient)) > 0)
			puts("Buffer < maximum lowerlimit.\nMengirim XON.");
		else
			puts("XON gagal dikirim");
	}
}


static void* consume(void *queue){
	int offset = 0;

	while (true) {
		if (listfbool[headWin]) {
			char text[VARLEN];
			memset(text, 0, sizeof text);
			for (int i = 3;i < getEtx(listframe[headWin]);i++) {
				text[i-3] = listframe[headWin][i];
			}
			printf("Mengkonsumsi frame ke-%d : '%s'\n", headWin + offset, text);

			listfbool[headWin] = false; // marking the frame is consumed and reset
			ucbuffersize--;
			printf("ucbuffersize : %d\n", ucbuffersize);
			sendXON();
			headWin++;
			if (headWin >= LISTSZ) {
				headWin = 1;
				offset += LISTSZ-1;
			}
		}
	}
	pthread_exit(0);
}
