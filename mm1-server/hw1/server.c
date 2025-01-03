/*******************************************************************************
* Simple FIFO Order Server Implementation
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch.
*
* Usage:
*     <build directory>/server <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Last Update:
*     September 9, 2024
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. For debugging or more details, refer
*     to the accompanying documentation and logs.
*
*******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"
#include "timelib.h"
#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s <port_number>\n"
typedef struct {
		uint64_t request_id;
		struct timespec timestamp;
		struct timespec length;
	} request_t;
#define REQUEST_SIZE sizeof(request_t)
typedef struct {
	uint64_t request_id;
	uint64_t x;
	uint8_t status;
} response_t;
#define RESPONSE_SIZE sizeof(response_t)

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
static void handle_connection(int conn_socket)
{
	// Initialize variables
	request_t request;
	response_t response;
	long sent_sec, sent_nsec, length_sec, length_nsec, rec_sec, rec_nsec, com_sec, com_nsec;
	struct timespec sent_timestamp, length, receipt_timestamp, completion_timestamp;
	uint64_t req_id;
	double sent_time, length_time, rec_time, com_time;

	// Get receipt time
	clock_gettime(CLOCK_MONOTONIC, &receipt_timestamp);

	// Start loop
	while(1) {

		// Receive request
		ssize_t bytes_received = recv(conn_socket, &request, REQUEST_SIZE, 0);
		if (bytes_received < 0) {
			perror("recv failed");
			return;
		} else if (bytes_received == 0) {
			printf("Client disconnected.\n");
			break;
		} else if (bytes_received < REQUEST_SIZE) {
			fprintf(stderr, "Incomplete request received\n");
			break;
		}

		// Manage request info
		// Sent timestamp
		sent_timestamp = request.timestamp;
		sent_sec = sent_timestamp.tv_sec;
		sent_nsec = sent_timestamp.tv_nsec;
		sent_time = (double)sent_sec + (double)sent_nsec / 1e9;

		// Length timestamp
		length = request.length;
		length_sec = length.tv_sec;
		length_nsec = length.tv_nsec;
		length_time = (double)length_sec + (double)length_nsec / 1e9;

		// Received timestamp
		rec_sec = receipt_timestamp.tv_sec;
		rec_nsec = receipt_timestamp.tv_nsec;
		rec_time = (double)rec_sec + (double)rec_nsec / 1e9;

		// Get id
		req_id = request.request_id;

		// Run busywait
		get_elapsed_busywait(length_sec, length_nsec);
		// Get completion time
		clock_gettime(CLOCK_MONOTONIC, &completion_timestamp);

		// Completion timestamp
		com_sec = completion_timestamp.tv_sec;
		com_nsec = completion_timestamp.tv_nsec;
		com_time = (double)com_sec + (double)com_nsec / 1e9;

		// Manage response info
		response.request_id = req_id;
		response.x = 0;
		response.status = 0;
		// Send response
		ssize_t bytes_sent = send(conn_socket, &response, RESPONSE_SIZE, 0);
		if (bytes_sent < 0) {
			perror("Send failed");
		} else {
			printf("R%lu:%.6f,%.6f,%.6f,%.6f\n", 
				req_id, sent_time, length_time, rec_time, com_time);
		}
	}
	close(conn_socket);
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

	/* Get port to bind our socket to */
	if (argc > 1) {
		socket_port = strtol(argv[1], NULL, 10);
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

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted);

	close(sockfd);
	return EXIT_SUCCESS;

}
