/*******************************************************************************
* Single-Threaded FIFO Image Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client
*     requests for image processing in First In, First Out (FIFO)
*     order. The server binds to the specified port number provided as
*     a parameter upon launch. It launches a secondary thread to
*     process incoming requests and allows to specify a maximum queue
*     size.
*
* Usage:
*     <build directory>/server -q <queue_size> -w <workers> -p <policy> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests.
*     workers     - The number of parallel threads to process requests.
*     policy      - The queue policy to use for request dispatching.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     October 31, 2023
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
#include <assert.h>
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
	"Usage: %s -q <queue size> "		\
	"-w <workers: 1> "			\
	"-p <policy: FIFO> "			\
	"<port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* Mutex needed to protect the threaded printf. DO NOT TOUCH */
sem_t * printf_mutex;

/* Synchronized printf for multi-threaded operation */
#define sync_printf(...)			\
	do {					\
		sem_wait(printf_mutex);		\
		printf(__VA_ARGS__);		\
		sem_post(printf_mutex);		\
	} while (0)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

/* Global array of registered images and its length -- reallocated as we go! */

/*
void segfault_handler(int sig) {
	fprintf(stderr, "Thread ID: %lu", pthread_self());
	_exit(1);
}
*/


struct image_meta {
	struct image *image;
	sem_t * image_notify;
	uint64_t completed_count;
	uint64_t operation_count;
};

struct image_meta * images = NULL;
uint64_t image_count = 0;

sem_t * image_array_mutex;
// sem_t * image_array_notify;
sem_t * conn_socket_mutex;

struct request_meta {
	struct request request;
	struct timespec receipt_timestamp;
	struct timespec start_timestamp;
	struct timespec completion_timestamp;
	// sem_t * request_notify;
};

enum queue_policy {
	QUEUE_FIFO,
	QUEUE_SJN
};

struct queue {
	size_t wr_pos;
	size_t rd_pos;
	size_t max_size;
	size_t available;
	enum queue_policy policy;
	struct request_meta * requests;
};

struct connection_params {
	size_t queue_size;
	size_t workers;
	enum queue_policy queue_policy;
};

struct worker_params {
	int conn_socket;
	int worker_done;
	struct queue * the_queue;
	int worker_id;
};

enum worker_command {
	WORKERS_START,
	WORKERS_STOP
};


void queue_init(struct queue * the_queue, size_t queue_size, enum queue_policy policy)
{
	the_queue->rd_pos = 0;
	the_queue->wr_pos = 0;
	the_queue->max_size = queue_size;
	the_queue->requests = (struct request_meta *)malloc(sizeof(struct request_meta)
						     * the_queue->max_size);
	the_queue->available = queue_size;
	the_queue->policy = policy;
}

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request_meta to_add, struct queue * the_queue)
{
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Make sure that the queue is not full */
	if (the_queue->available == 0) {
		retval = 1;
	} else {
		/* If all good, add the item in the queue */
		the_queue->requests[the_queue->wr_pos] = to_add;
		the_queue->wr_pos = (the_queue->wr_pos + 1) % the_queue->max_size;
		the_queue->available--;
		/* QUEUE SIGNALING FOR CONSUMER --- DO NOT TOUCH */
		sem_post(queue_notify);
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct request_meta get_from_queue(struct queue * the_queue)
{
	struct request_meta retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	/*
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	*/
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	retval = the_queue->requests[the_queue->rd_pos];
	the_queue->rd_pos = (the_queue->rd_pos + 1) % the_queue->max_size;
	the_queue->available++;

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	// sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

void dump_queue_status(struct queue * the_queue)
{
	size_t i, j;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	sem_wait(printf_mutex);
	printf("Q:[");

	for (i = the_queue->rd_pos, j = 0; j < the_queue->max_size - the_queue->available;
	     i = (i + 1) % the_queue->max_size, ++j)
	{
		printf("R%ld%s", the_queue->requests[i].request.req_id,
		       ((j+1 != the_queue->max_size - the_queue->available)?",":""));
	}

	printf("]\n");
	sem_post(printf_mutex);
	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

void register_new_image(int conn_socket, struct request * req)
{
	int retval;
	/* Increase the count of registered images */
	image_count++;

	/* Reallocate array of image pointers */
	images = realloc(images, image_count * sizeof(struct image_meta));

	/* Read in the new image from socket */
	struct image * new_img = recvImage(conn_socket);

	/* Store its pointer at the end of the global array */
	images[image_count - 1].image = new_img;

	/* Create semaphores */
	sem_t * image_notify = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(image_notify, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue mutex");
	}

	images[image_count - 1].image_notify = image_notify;

	images[image_count - 1].operation_count = 0;
	images[image_count - 1].completed_count = 0;

	/* Immediately provide a response to the client */
	struct response resp;
	resp.req_id = req->req_id;
	resp.img_id = image_count - 1;
	resp.ack = RESP_COMPLETED;

	send(conn_socket, &resp, sizeof(struct response), 0);
}
/*
void add_semaphore(SempahoreArray *sem_array) {
	sem_array->semaphores = realloc(sem_array->semaphores, (sem_array->count + 1) * sizeof(sem_t));
	if (sem_array->semaphores == NULL) {
		perror("realloc failed");
		exit(EXIT_FAILURE);
	}

	if (sem_init(&sem_array->semaphores[sem_array->count], 0, 1) != 0) {
		perror("sem_init failed");
		exit(EXIT_FAILURE);
	}

	sem_array->count++;
}

void cleanup(SemaphoreArray *sem_array) {
	for (size_t i = 0;i < sem_array->count; i++) {
		sem_destroy(&sem_array->semaphores[i]);
	}
	free(sem_array->semaphores);
	sem_array->semaphores = NULL;
	sem_array->count = 0;
}
*/
/* Main logic of the worker thread */
void * worker_main (void * arg)
{

	struct timespec now;
	struct worker_params * params = (struct worker_params *)arg;
	// sync_printf("T%d: Native ID: %lu\n", params->worker_id, pthread_self());

	/* Print the first alive message. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	sync_printf("[#WORKER#] %lf Worker Thread Alive!\n", TSPEC_TO_DOUBLE(now));

	/* Okay, now execute the main logic. */
	while (!params->worker_done) {

		int retval;
		struct request_meta req;
		struct response resp;
		struct image * img = NULL;
		uint64_t img_id;
		uint64_t operation_number;

		// sync_printf("T%d: Fetching from queue\n", params->worker_id);
		// sync_printf("T%d: Waiting for queue notify\n", params->worker_id);
		sem_wait(queue_notify);
		// sync_printf("T%d: Waiting for queue mutex\n", params->worker_id);
		sem_wait(queue_mutex);
		req = get_from_queue(params->the_queue);
		

		/* Detect wakeup after termination asserted */
		if (params->worker_done) {
			sem_post(queue_mutex);
			break;
		}
		clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);

		/* Order the current operation */
		
		img_id = req.request.img_id;
		operation_number = images[img_id].operation_count++;
		sem_post(queue_mutex);

		/* Find the image to work on */
		// sync_printf("T%d: Before While - Operation number:%ld Completed count:%ld\n", params->worker_id, operation_number, images[img_id].completed_count);


		// sync_printf("T%d: Waiting for image notify\n", params->worker_id);
		if (sem_wait(images[img_id].image_notify) != 0) {
			perror("sem_wait failed");
			exit(EXIT_FAILURE);
		} // Wait until previous request is done
		// sync_printf("T%d: Done waiting on previous request\n", params->worker_id);
		if (operation_number > images[img_id].completed_count) {
			sem_post(images[img_id].image_notify);
			// sync_printf("T%d: Busy waiting\n", params->worker_id);
			while (operation_number > images[img_id].completed_count);
			sem_wait(images[img_id].image_notify);
		}

		// sync_printf("T%d: Waiting for image array mutex\n", params->worker_id);
		sem_wait(image_array_mutex);
		img = images[img_id].image;
		sem_post(image_array_mutex);

		// sync_printf("T%d: Successfully got image, Height: %u, Width: %u\n", params->worker_id, 
		// img->height, img->width);

		assert(img != NULL);
		if (img->pixels == NULL) {
			printf("pixels are null\n");
		}

		

		
		

		// sync_printf("T%d: Entering operation\n", params->worker_id);
		// uint8_t err;

		switch (req.request.img_op) {
		case IMG_ROT90CLKW:
			// sync_printf("T%d: Entering rotate\n", params->worker_id);
			img = rotate90Clockwise(img, NULL);
			break;
		case IMG_BLUR:
			// sync_printf("T%d: Entering blur\n", params->worker_id);
		    img = blurImage(img, NULL);
			break;
		case IMG_SHARPEN:
			// sync_printf("T%d: Entering sharpen\n", params->worker_id);
		    img = sharpenImage(img, NULL);
			sync_printf("T%d: Exiting sharpen\n", params->worker_id);
			break;
		case IMG_VERTEDGES:
			// sync_printf("T%d: Entering vertedges\n", params->worker_id);
		    img = detectVerticalEdges(img, NULL);
			break;
		case IMG_HORIZEDGES:
			// sync_printf("T%d: Entering horizedges\n", params->worker_id);
		    img = detectHorizontalEdges(img, NULL);
			break;
		}
		// sync_printf("T%d: Exiting operation\n", params->worker_id);
		images[img_id].completed_count++; // Signal next request's turn

		// sync_printf("T%d: Waiting for image array mutex 2\n", params->worker_id);
		sem_wait(image_array_mutex);

		// sync_printf("T%d: Retrieving\n", params->worker_id);

		if (req.request.img_op != IMG_RETRIEVE) {
			if (req.request.overwrite) {
				/* Deallocate the previous image */
				deleteImage(images[img_id].image);
				/* Overwrite the pointer with the one to the new image */
				images[img_id].image = img;
				sem_post(images[img_id].image_notify); // Signal that current request is done
			} else {
				sem_post(images[img_id].image_notify);
				/* Generate new ID and Increase the # of registered images */
				img_id = image_count++;
				/* Reallocate array of image pointers */
				images = realloc(images, image_count * sizeof(struct image_meta));
				/* Store its pointer at the end of the global array */
				images[img_id].image = img;

				sem_t * image_notify = (sem_t *)malloc(sizeof(sem_t));
				retval = sem_init(image_notify, 0, 0);
				if (retval < 0) {
					ERROR_INFO();
					perror("Unable to initialize image notify");
				}
				images[img_id].image_notify = image_notify;
			}
		}
		sem_post(image_array_mutex);

		clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);

		// sync_printf("T%d: Waiting for conn socket\n", params->worker_id);
		// sync_printf("T%d: Waiting for conn socket mutex\n", params->worker_id);
		sem_wait(conn_socket_mutex);
		// sync_printf("T%d: Sending response\n", params->worker_id);
		/* Now provide a response! */
		resp.req_id = req.request.req_id;
		resp.ack = RESP_COMPLETED;
		resp.img_id = img_id;

		send(params->conn_socket, &resp, sizeof(struct response), 0);

		/* In case of IMG_RETRIEVE, we need to send out the
		 * actual image payload! */
		if (req.request.img_op == IMG_RETRIEVE) {
			uint8_t err = sendImage(img, params->conn_socket);

			if(err) {
				ERROR_INFO();
				perror("Unable to send image payload to client.");
			}
		}
		sem_post(conn_socket_mutex);

		sync_printf("T%d R%ld:%lf,%s,%d,%ld,%ld,%lf,%lf,%lf\n",
		       params->worker_id, req.request.req_id,
		       TSPEC_TO_DOUBLE(req.request.req_timestamp),
		       OPCODE_TO_STRING(req.request.img_op),
		       req.request.overwrite, req.request.img_id, img_id,
		       TSPEC_TO_DOUBLE(req.receipt_timestamp),
		       TSPEC_TO_DOUBLE(req.start_timestamp),
		       TSPEC_TO_DOUBLE(req.completion_timestamp));

		dump_queue_status(params->the_queue);
		// printf("T%d: Worker done: %d\n", params->worker_id, params->worker_done);
	}
	// printf("T%d: Thread exiting, worker done: %d\n", params->worker_id, params->worker_done);
	return NULL;
}

/* This function will start/stop all the worker threads wrapping
 * around the pthread_join/create() function calls */

int control_workers(enum worker_command cmd, size_t worker_count,
		    struct worker_params * common_params)
{
	/* Anything we allocate should we kept as static for easy
	 * deallocation when the STOP command is issued */
	static pthread_t * worker_pthreads = NULL;
	static struct worker_params ** worker_params = NULL;
	static int * worker_ids = NULL;


	/* Start all the workers */
	if (cmd == WORKERS_START) {
		size_t i;
		/* Allocate all structs and parameters */
		worker_pthreads = (pthread_t *)malloc(worker_count * sizeof(pthread_t));
		worker_params = (struct worker_params **)
		malloc(worker_count * sizeof(struct worker_params *));
		worker_ids = (int *)malloc(worker_count * sizeof(int));


		if (!worker_pthreads || !worker_params || !worker_ids) {
			ERROR_INFO();
			perror("Unable to allocate arrays for threads.");
			return EXIT_FAILURE;
		}


		/* Allocate and initialize as needed */
		for (i = 0; i < worker_count; ++i) {
			worker_ids[i] = -1;


			worker_params[i] = (struct worker_params *)
				malloc(sizeof(struct worker_params));


			if (!worker_params[i]) {
				ERROR_INFO();
				perror("Unable to allocate memory for thread.");
				return EXIT_FAILURE;
			}


			worker_params[i]->conn_socket = common_params->conn_socket;
			worker_params[i]->the_queue = common_params->the_queue;
			worker_params[i]->worker_done = 0;
			worker_params[i]->worker_id = i;
		}


		/* All the allocations and initialization seem okay,
		 * let's start the threads */
		for (i = 0; i < worker_count; ++i) {
			worker_ids[i] = pthread_create(&worker_pthreads[i], NULL, worker_main, worker_params[i]);


			if (worker_ids[i] < 0) {
				ERROR_INFO();
				perror("Unable to start thread.");
				return EXIT_FAILURE;
			} else {
				printf("INFO: Worker thread %ld (TID = %d) started!\n",
				       i, worker_ids[i]);
			}
		}
	}


	else if (cmd == WORKERS_STOP) {
		size_t i;


		/* Command to stop the threads issues without a start
		 * command? */
		if (!worker_pthreads || !worker_params || !worker_ids) {
			return EXIT_FAILURE;
		}


		/* First, assert all the termination flags */
		for (i = 0; i < worker_count; ++i) {
			if (worker_ids[i] < 0) {
				continue;
			}


			/* Request thread termination */
			worker_params[i]->worker_done = 1;
		}


		/* Next, unblock threads and wait for completion */
		for (i = 0; i < worker_count; ++i) {
			if (worker_ids[i] < 0) {
				continue;
			}


			sem_post(queue_notify);

		}

        for (i = 0; i < worker_count; ++i) {
            int ret = pthread_join(worker_pthreads[i],NULL);
			if (ret != 0) {
				fprintf(stderr, "Error joining thread %ld: %s\n", i, strerror(ret));
			}
            printf("INFO: Worker thread exited.\n");
        }


		/* Finally, do a round of deallocations */
		for (i = 0; i < worker_count; ++i) {
			free(worker_params[i]);
		}


		free(worker_pthreads);
		worker_pthreads = NULL;


		free(worker_params);
		worker_params = NULL;


		free(worker_ids);
		worker_ids = NULL;
	}


	else {
		ERROR_INFO();
		perror("Invalid thread control command.");
		return EXIT_FAILURE;
	}


	return EXIT_SUCCESS;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket, struct connection_params conn_params)
{
	struct request_meta * req;
	struct queue * the_queue;
	size_t in_bytes;

	/* The connection with the client is alive here. Let's start
	 * the worker thread. */
	struct worker_params common_worker_params;
	int res;

	/* Now handle queue allocation and initialization */
	the_queue = (struct queue *)malloc(sizeof(struct queue));
	queue_init(the_queue, conn_params.queue_size, conn_params.queue_policy);

	common_worker_params.conn_socket = conn_socket;
	common_worker_params.the_queue = the_queue;
	res = control_workers(WORKERS_START, conn_params.workers, &common_worker_params);

	/* Do not continue if there has been a problem while starting
	 * the workers. */
	if (res != EXIT_SUCCESS) {
		free(the_queue);

		/* Stop any worker that was successfully started */
		control_workers(WORKERS_STOP, conn_params.workers, NULL);
		return;
	}

	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	req = (struct request_meta *)malloc(sizeof(struct request_meta));

	do {
		in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
		clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

		/* Don't just return if in_bytes is 0 or -1. Instead
		 * skip the response and break out of the loop in an
		 * orderly fashion so that we can de-allocate the req
		 * and resp varaibles, and shutdown the socket. */
		if (in_bytes > 0) {

			/* Handle image registration right away! */
			if(req->request.img_op == IMG_REGISTER) {
				clock_gettime(CLOCK_MONOTONIC, &req->start_timestamp);

				sem_wait(image_array_mutex);
				register_new_image(conn_socket, &req->request);
				sem_post(image_array_mutex);

				clock_gettime(CLOCK_MONOTONIC, &req->completion_timestamp);

				sync_printf("T%ld R%ld:%lf,%s,%d,%ld,%ld,%lf,%lf,%lf\n",
				       conn_params.workers, req->request.req_id,
				       TSPEC_TO_DOUBLE(req->request.req_timestamp),
				       OPCODE_TO_STRING(req->request.img_op),
				       req->request.overwrite, req->request.img_id,
				       image_count - 1, /* Registered ID on server side */
				       TSPEC_TO_DOUBLE(req->receipt_timestamp),
				       TSPEC_TO_DOUBLE(req->start_timestamp),
				       TSPEC_TO_DOUBLE(req->completion_timestamp));

				dump_queue_status(the_queue);
				continue;
			}

			res = add_to_queue(*req, the_queue);

			/* The queue is full if the return value is 1 */
			if (res) {
				sem_wait(conn_socket_mutex);
				struct response resp;
				/* Now provide a response! */
				resp.req_id = req->request.req_id;
				resp.ack = RESP_REJECTED;
				send(conn_socket, &resp, sizeof(struct response), 0);
				sem_post(conn_socket_mutex);

				sync_printf("X%ld:%lf,%lf,%lf\n", req->request.req_id,
				       TSPEC_TO_DOUBLE(req->request.req_timestamp),
				       TSPEC_TO_DOUBLE(req->request.req_length),
				       TSPEC_TO_DOUBLE(req->receipt_timestamp)
					);
			}
		}
	} while (in_bytes > 0);


	/* Stop all the worker threads. */
	control_workers(WORKERS_STOP, conn_params.workers, NULL);

	free(req);
	shutdown(conn_socket, SHUT_RDWR);
	close(conn_socket);
	printf("INFO: Client disconnected.\n");
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval, opt;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;
	struct connection_params conn_params;
	conn_params.queue_size = 0;
	conn_params.queue_policy = QUEUE_FIFO;
	conn_params.workers = 1;

	// signal(SIGSEGV, segfault_handler);

	/* Parse all the command line arguments */
	while((opt = getopt(argc, argv, "q:w:p:")) != -1) {
		switch (opt) {
		case 'q':
			conn_params.queue_size = strtol(optarg, NULL, 10);
			printf("INFO: setting queue size = %ld\n", conn_params.queue_size);
			break;
		case 'w':
			conn_params.workers = strtol(optarg, NULL, 10);
			printf("INFO: setting worker count = %ld\n", conn_params.workers);
			/* TODO: SUPPORT MULTIPLE THREADS */
			/*
			if (conn_params.workers != 1) {
				ERROR_INFO();
				fprintf(stderr, "Only 1 worker is supported in this implementation!\n" USAGE_STRING, argv[0]);
				return EXIT_FAILURE;
			}
			*/
			break;
		case 'p':
			if (!strcmp(optarg, "FIFO")) {
				conn_params.queue_policy = QUEUE_FIFO;
			} else {
				ERROR_INFO();
				fprintf(stderr, "Invalid queue policy.\n" USAGE_STRING, argv[0]);
				return EXIT_FAILURE;
			}
			printf("INFO: setting queue policy = %s\n", optarg);
			break;
		default: /* '?' */
			fprintf(stderr, USAGE_STRING, argv[0]);
		}
	}

	if (!conn_params.queue_size) {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}

	if (optind < argc) {
		socket_port = strtol(argv[optind], NULL, 10);
		printf("INFO: setting server port as: %d\n", socket_port);
	} else {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}

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

	/* Initilize threaded printf mutex */
	printf_mutex = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(printf_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize printf mutex");
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
	/* DONE - Initialize queue protection variables */
	image_array_mutex = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(image_array_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize image array mutex");
		return EXIT_FAILURE;
	}

	conn_socket_mutex = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(conn_socket_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize conn socket mutex");
		return EXIT_FAILURE;
	}

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted, conn_params);

	free(queue_mutex);
	free(queue_notify);

	free(image_array_mutex);

	free(conn_socket_mutex);

	close(sockfd);
	return EXIT_SUCCESS;
}
