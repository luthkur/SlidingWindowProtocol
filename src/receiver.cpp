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

/* Delay to adjust speed of consuming buffer, in milliseconds */
#define DELAY 600000

/* Define receive buffer size */
#define RXQSIZE 8
#define UPPERLIMIT 6
#define LOWERLIMIT 3

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
static Byte *q_get(QTYPE *, Byte *);
static void *consume(void *param);

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
		else {
				if (!c) {
				j = 1;
				} else {
				j++;
			}
			printf("Menerima byte ke-%d.\n", j);
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
	if ((queue->count >= UPPERLIMIT)&&(!send_xoff)) { //buffer full
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
	if (recvfrom(sockfd, r_msg, RXQSIZE, 0, (struct sockaddr *)&sclient, &cli_len) < 0)
		puts("Receive byte failed");

	// check end of file
	if(r_msg[0] == Endfile) {
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

static Byte *q_get(QTYPE *queue, Byte *data)
{
	Byte *current;
	/* Nothing in the queue */
	if (!queue->count) return (NULL);

	/** send XON **/
	if ((queue->count <= LOWERLIMIT) && (!send_xon)){
		sent_xonxoff = XON;
		send_xon = true;
		send_xoff = false;
		x_msg[0] = sent_xonxoff;

		if (sendto(sockfd, x_msg, 1, 0, (struct sockaddr *)&sclient,sizeof(sclient)) > 0)
			puts("Buffer < maximum lowerlimit.\nMengirim XON.");
		else
			puts("XON gagal dikirim");
	}
	//select current data from buffer
	current = &queue->data[queue->front];

	//increment front index, check for wraparound, reduced buffer content size
	queue->front += 1;
	if (queue->front >= RXQSIZE) queue->front -= RXQSIZE;
	queue->count -= 1;

	return current;
}

static void* consume(void *queue){

	QTYPE *rcvq_ptr = (QTYPE *)queue;

	int i=1; //consume counter
	while (true) {
		Byte *res, *dummy;
		res = q_get(rcvq_ptr, dummy);
		if(res){
			printf("Mengkonsumsi byte ke-%d : '%c'\n",i,*res);
			i++;
		}
		if(rcvq_ptr->count == 0 && end)
		{
			childead = true;
			pthread_exit(0);
		}
		usleep(DELAY); //delay
	}
	pthread_exit(0);
}
