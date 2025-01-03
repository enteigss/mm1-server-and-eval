/*******************************************************************************
* Dual-Threaded FIFO Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch. It launches a secondary thread to
*     process incoming requests and allows to specify a maximum queue size.
*
* Usage:
*     <build directory>/server -q <queue_size> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 29, 2023
*
* Last Update:
*     September 25, 2024
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. If the queue is full at the time a
*     new request is received, the request is rejected with a negative ack.
*
*******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s -q <queue size> <port_number>\n"


/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

struct meta_request {
	struct request req;
	struct timespec receipt_timestamp;
};

struct queue {
    struct meta_request * items;
	int front;
	int rear;
	int queue_size;
};

struct worker_params {
    struct queue * the_queue;
	volatile int * stop_flag;
	int conn_socket;
};

/* Helper function to perform queue initialization */
void initialize(struct queue * the_queue, size_t queue_size)
{
	the_queue->front = -1;
	the_queue->rear = -1;
	the_queue->queue_size = queue_size;
	the_queue->items = malloc(queue_size * sizeof(struct meta_request));
}

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct meta_request to_add, struct queue * the_queue)
{
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Make sure that the queue is not full */
	if ((the_queue->rear + 1) %  the_queue->queue_size == the_queue->front) {
		/* What to do in case of a full queue */
		retval = -1;
	} else {
		/* If all good, add the item in the queue */
		if (the_queue->front == -1) {
			the_queue->front = 0;
		} 
		the_queue->rear = (the_queue->rear + 1) % the_queue->queue_size;
		the_queue->items[the_queue->rear] = to_add;

		/* QUEUE SIGNALING FOR CONSUMER --- DO NOT TOUCH */
		sem_post(queue_notify);
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct meta_request * get_from_queue(struct queue * the_queue)
{
	struct meta_request * retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	if (the_queue->front == -1) {
		retval = NULL;
	} 
	retval = (struct meta_request *)malloc(sizeof(struct meta_request));
	*retval = the_queue->items[the_queue->front];
	if (the_queue->front == the_queue->rear) {
		the_queue->front = -1;
		the_queue->rear = -1;
	} else {
		the_queue->front = (the_queue->front + 1) % the_queue->queue_size;
	}
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Implement this method to correctly dump the status of the queue
 * following the format Q:[R<request ID>,R<request ID>,...] */
int dump_queue_status(struct queue * the_queue)
{
	int i;
	int c = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	printf("Q:[");
	if (the_queue->front != -1) {
		for (i = the_queue->front; i <= the_queue->rear; i++) {
			printf("R%ld", the_queue->items[i].req.req_id);
			c++;
			if (i != the_queue->rear) {
				printf(",");
			}
		}
	}
	printf("]\n");
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return c;
}


/* Main logic of the worker thread */
/* IMPLEMENT HERE THE MAIN FUNCTION OF THE WORKER */
void *worker_main(void * void_worker_params) {
	// Initialize variables
	struct worker_params worker_params = *(struct worker_params *)void_worker_params;
	struct queue * the_queue = worker_params.the_queue;
	volatile int * stop_flag = worker_params.stop_flag;
	int conn_socket = worker_params.conn_socket;
	struct timespec start_timestamp, completion;
	int queue_length;
	FILE * file = fopen("output.txt", "w");

	// Manage client requests
	while (!(*stop_flag)) {

		struct meta_request meta_req;
		struct response resp;
		meta_req = *get_from_queue(the_queue);

		if (*stop_flag) {
			break;
		}

		clock_gettime(CLOCK_MONOTONIC, &start_timestamp);
		busywait_timespec(meta_req.req.req_length); 
		clock_gettime(CLOCK_MONOTONIC, &completion);

		resp.req_id = meta_req.req.req_id;
		resp.ack = 0;
		send(conn_socket, &resp, sizeof(struct response), 0);

		printf("R%ld:%lf,%lf,%lf,%lf,%lf\n", meta_req.req.req_id,
				TSPEC_TO_DOUBLE(meta_req.req.req_timestamp),
				TSPEC_TO_DOUBLE(meta_req.req.req_length),
				TSPEC_TO_DOUBLE(meta_req.receipt_timestamp),
				TSPEC_TO_DOUBLE(start_timestamp),
				TSPEC_TO_DOUBLE(completion)
				);
		queue_length = dump_queue_status(the_queue);
		fprintf(file, "%ld,%lf,%lf,%lf,%lf,%lf,%i\n", meta_req.req.req_id, 
				TSPEC_TO_DOUBLE(meta_req.req.req_timestamp),
				TSPEC_TO_DOUBLE(meta_req.req.req_length),
				TSPEC_TO_DOUBLE(meta_req.receipt_timestamp),
				TSPEC_TO_DOUBLE(start_timestamp),
				TSPEC_TO_DOUBLE(completion),
				queue_length);
		
	}

	return NULL;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket, int queue_size)
{

	struct meta_request * req;
	struct queue * the_queue;
	size_t in_bytes;

	/* The connection with the client is alive here. Let's
	 * initialize the shared queue. */
	 struct worker_params * worker_params = (struct worker_params *)malloc(sizeof(struct worker_params));;

	/* IMPLEMENT HERE ANY QUEUE INITIALIZATION LOGIC */
	the_queue = (struct queue *)malloc(sizeof(struct queue));
	initialize(the_queue, queue_size);

	/* Queue ready to go here. Let's start the worker thread. */

	/* IMPLEMENT HERE THE LOGIC TO START THE WORKER THREAD. */
	worker_params->conn_socket = conn_socket;
	volatile int stop_flag = 0;
	worker_params->stop_flag = &stop_flag;
	worker_params->the_queue = the_queue;

	pthread_t p;
	pthread_create(&p, NULL, worker_main, (void *)worker_params);

	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	/* REUSE LOGIC FROM HW1 TO HANDLE THE PACKETS */

	req = (struct meta_request *)malloc(sizeof(struct meta_request));
	
	FILE * reject_file = fopen("reject_file.txt", "w");
	do {
		in_bytes = recv(conn_socket, &req->req, sizeof(struct request), 0);
		clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

		/* Don't just return if in_bytes is 0 or -1. Instead
		 * skip the response and break out of the loop in an
		 * orderly fashion so that we can de-allocate the req
		 * and resp varaibles, and shutdown the socket. */
		if (in_bytes > 0) {
			if (add_to_queue(*req, the_queue) == -1) {

				struct timespec reject_time;
				clock_gettime(CLOCK_MONOTONIC, &reject_time);

				struct response resp;
				resp.req_id = req->req.req_id;
				resp.ack = 1;
				send(conn_socket, &resp, sizeof(struct response), 0);
				printf("X%ld:%lf,%lf,%lf\n", req->req.req_id,
						TSPEC_TO_DOUBLE(req->req.req_timestamp),
						TSPEC_TO_DOUBLE(req->req.req_length),
						TSPEC_TO_DOUBLE(reject_time));
				
				dump_queue_status(the_queue);

				fprintf(reject_file, "%ld,%lf\n", req->req.req_id, TSPEC_TO_DOUBLE(reject_time));
			};
			
			/* HANDLE REJECTION IF NEEDED */
		}
	} while (in_bytes > 0);

	/* PERFORM ORDERLY DEALLOCATION AND OUTRO HERE */
	fclose(reject_file);;
	
	/* Ask the worker thead to terminate */
	/* ASSERT TERMINATION FLAG FOR THE WORKER THREAD */
	stop_flag = 1;
	
	/* Make sure to wake-up any thread left stuck waiting for items in the queue. DO NOT TOUCH */
	sem_post(queue_notify);

	/* Wait for orderly termination of the worker thread */	
	pthread_join(p, NULL);
	free(the_queue);
	free(worker_params);

	free(req);
	shutdown(conn_socket, SHUT_RDWR);
	close(conn_socket);
	printf("INFO:: Client disconnected.\n");
	
	/* FREE UP DATA STRUCTURES AND SHUTDOWN CONNECTION WITH CLIENT */
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;
	int queue_size;

	/* Parse all the command line arguments */
	if (argc > 1) {
		int option;
		while ((option = getopt(argc, argv, "q:")) != -1) {
			switch (option) {
				case 'q':
				if (optarg == NULL) {
					fprintf(stderr, "Queue size not provided with -q option.\n");
					exit(EXIT_FAILURE);
				}
					queue_size = atoi(optarg);
					break;
				case '?':
					fprintf(stderr, "Must provide valid argument.\n");
			}
		}
		socket_port = strtol(argv[3], NULL, 10);
		printf("INFO: setting server port as: %d\n", socket_port);
	} else {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}
	/* PARSE THE COMMANDS LINE: */
	/* 1. Detect the -q parameter and set aside the queue size  */
	
	/* 2. Detect the port number to bind the server socket to (see HW1 and HW2) */

	/* Now onward to create the right type of socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		ERROR_INFO();
		perror("Unable to create socket");
		return EXIT_FAILURE;
	}

	/* Before moving forward, set socket to reuse address */
	optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));

	/* Convert INADDR_ANY into network byte order */
	any_address.s_addr = htonl(INADDR_ANY);

	/* Time to bind the socket to the right port  */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(socket_port);
	addr.sin_addr = any_address;

	/* Attempt to bind the socket with the given parameters */
	retval = bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to bind socket");
		return EXIT_FAILURE;
	}

	/* Let us now proceed to set the server to listen on the selected port */
	retval = listen(sockfd, BACKLOG_COUNT);

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to listen on socket");
		return EXIT_FAILURE;
	}

	/* Ready to accept connections! */
	printf("INFO: Waiting for incoming connection...\n");
	client_len = sizeof(struct sockaddr_in);
	accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);

	if (accepted == -1) {
		ERROR_INFO();
		perror("Unable to accept connections");
		return EXIT_FAILURE;
	}

	/* Initialize queue protection variables. DO NOT TOUCH. */
	queue_mutex = (sem_t *)malloc(sizeof(sem_t));
	queue_notify = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(queue_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue mutex");
		return EXIT_FAILURE;
	}
	retval = sem_init(queue_notify, 0, 0);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue notify");
		return EXIT_FAILURE;
	}
	/* DONE - Initialize queue protection variables. DO NOT TOUCH */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted, queue_size);

	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;

}