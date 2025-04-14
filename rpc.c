#define _POSIX_C_SOURCE 200112L
#include "rpc.h"
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#define MAX_FUNCTIONS 10
#define MAX_NAME_LEN 1000
#define MAX_DATA2_LEN 100000

typedef struct {
	char name[MAX_NAME_LEN + 1];
	rpc_handler func;
} func_info;

struct rpc_server {
    /* Add variable(s) for server state */
    int sockfd;
    int newsockfd;
    int num_functions;
	func_info functions[MAX_FUNCTIONS];
};

int create_listening_socket(char* service);

rpc_server *rpc_init_server(int port) {
    int sockfd;
    int length = snprintf(NULL, 0,"%d", port);
    char ports[length + 1];

    sprintf(ports, "%d", port);
	// Create the listening socket
	sockfd = create_listening_socket(ports);

	// Listen on socket - means we're ready to accept connections,
	// incoming connection requests will be queued, man 3 listen
	if (listen(sockfd, 5) < 0) {
		perror("listen");
		return NULL;
	}

    rpc_server* server = malloc(sizeof(*server));
    assert(server != NULL);
    server -> sockfd = sockfd;
    server -> num_functions = 0;
    return server;
}

int create_listening_socket(char* service) {
	int re, s, sockfd;
	struct addrinfo hints, *res;

	// Create address we're going to listen on (with given port number)
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;       // IPv6
	hints.ai_socktype = SOCK_STREAM; // Connection-mode byte streams
	hints.ai_flags = AI_PASSIVE;     // for bind, listen, accept
	// node (NULL means any interface), service (port), hints, res
	s = getaddrinfo(NULL, service, &hints, &res);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	// Create socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Reuse port if possible
	re = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(int)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
	// Bind address to the socket
	if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(res);

	return sockfd;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
	if (srv == NULL || name == NULL || handler == NULL){
		return -1;
	}
	// if not first time register, check if the function exists
	if (srv -> num_functions != 0){
		for (int i = 0; i < srv -> num_functions; i++){
			// cover the original function
			if (strcmp(name, srv -> functions[i].name) == 0){
				srv -> functions[i].func = handler;
				return 1;
			}
		}
	} 
	// if the function hasn't been registered
	// add a new function at the end of the function list
	if (srv -> num_functions == MAX_FUNCTIONS){
		return -1;
	}
	srv -> functions[srv -> num_functions].func = handler;
	strcpy(srv -> functions[srv -> num_functions].name, name);
	srv -> num_functions += 1;
	return 1;
}

void create_connection(rpc_server* srv){
	int newsockfd;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size;
	// Accept a connection - blocks until a connection is ready to be accepted
	// Get back a new file descriptor to communicate on
	client_addr_size = sizeof client_addr;
	newsockfd =
		accept(srv -> sockfd, (struct sockaddr*)&client_addr, &client_addr_size);
	if (newsockfd < 0) {
		perror("accept");
		return NULL;
	}
	srv -> newsockfd = newsockfd;
}

void rpc_serve_all(rpc_server *srv) {
	int n, r;
	char request[5];

	while (1){
		// allow to recieve connection request from multiple client
		create_connection(srv);
		while (1){
			if (srv == NULL){
				return;
			}

			// message recieved to determine what request to respond
			r = read(srv -> newsockfd, request, 4);
			if (r < 0) {
				perror("read");
				exit(EXIT_FAILURE);
			}
			request[r] = '\0';
			// if the "find" request is going to be processed
			if (strcmp(request, "find") == 0){
				int find = 0;
				char name[MAX_NAME_LEN + 1];
				// Read function name from the connection, then process
				n = read(srv -> newsockfd, name, MAX_NAME_LEN); // n is number of characters read
				if (n < 0) {
					perror("read");
					exit(EXIT_FAILURE);
				}
				// Null-terminate string
				name[n] = '\0';

				for (int i = 0; i < srv -> num_functions; i++){
					// function has been registered
					if (strcmp(srv -> functions[i].name, name) == 0){
						n = write(srv -> newsockfd, "1", 1);
						if (n < 0) {
							perror("write");
							exit(EXIT_FAILURE);
						}
						find = 1;
						break;
					}
				}
				if (find != 1){
					n = write(srv -> newsockfd, "0", 1);
					if (n < 0) {
						perror("write");
						exit(EXIT_FAILURE);
					}
				}
			} else if (strcmp(request, "call") == 0){ // "call" request is going to be processed
				int out = 0;
				rpc_data* data = NULL;
				rpc_data* results = NULL;
				char name[MAX_NAME_LEN + 1];
				int name_len;
				size_t data2_len;
				char data2_null[2];

				read(srv -> newsockfd, &name_len, sizeof(int));
				n = read(srv -> newsockfd, name, name_len); // read the function name
				if (n < 0) {
					perror("write");
					exit(EXIT_FAILURE);
				}
				name[n] = '\0';
				for (int i = 0; i < srv -> num_functions; i++){
					if (strcmp(srv -> functions[i].name, name) == 0){
						data = malloc(sizeof(*data));
						assert(data != NULL);
						// read data 1
						n = read(srv -> newsockfd, &(data -> data1), sizeof(int));
						// read data2_len
						n = read(srv -> newsockfd, &data2_len, sizeof(size_t));
						// read the status for data 2
						read(srv -> newsockfd, data2_null, 1);
						data2_null[1] = '\0';
						// read data 2 only if data 2 is not null and the length is not 0
						if (strcmp(data2_null, "N") == 0){
							if (data2_len != 0){
								data -> data2 = (void*) malloc(MAX_DATA2_LEN + 1);
								assert(data -> data2 != NULL);
								n = read(srv -> newsockfd, data -> data2, MAX_DATA2_LEN);
							} else if (data2_len == 0){
								results = NULL;
								break;
							}
							data -> data2_len = data2_len;
						} else { // else data 2 is NULL
							data -> data2 = NULL;
							if (data2_len != 0){
								results = NULL;
								break;
							}
							data -> data2_len = 0;
						}
						results = (srv -> functions[i].func)(data);
						break;
					}
				}

				if (results != NULL){
					out = 1;
					// write to notice that there is a result
					write(srv -> newsockfd, &out, sizeof(int));
					// write data 1
					write(srv -> newsockfd, &(results -> data1), sizeof(int));
					// write data2_len
					write(srv -> newsockfd, &(results -> data2_len), sizeof(size_t));
					// write a sign indicate whether data2 is null
					if (results -> data2 == NULL){
						write(srv -> newsockfd, "Y", 1);
					} else {
						write(srv -> newsockfd, "N", 1);
						// write data 2 only if data 2 is not null and the length is not 0
						if (results -> data2_len != 0){
							write(srv -> newsockfd, results -> data2, results -> data2_len);
						}
					}
					rpc_data_free(results);
				} else {
					// write to notice that there is no result
					write(srv -> newsockfd, &out, sizeof(int));
				}
				rpc_data_free(data);
			} else { // the current client's requests have all been processed
				// close the current connection
				close(srv -> newsockfd);
				break;
			}
		}
	}
	close(srv -> sockfd);
}

struct rpc_client {
    /* Add variable(s) for client state */
    int sockfd;
};

struct rpc_handle {
    /* Add variable(s) for handle */
	char func_name[MAX_NAME_LEN + 1];
};

rpc_client *rpc_init_client(char *addr, int port) {
    int sockfd, s;
	struct addrinfo hints, *servinfo, *rp;
    int length = snprintf(NULL, 0,"%d", port);
    char ports[length + 1];

    sprintf(ports, "%d", port);
	// Create address
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

	// Get addrinfo of server. From man page:
	// The getaddrinfo() function combines the functionality provided by the
	// gethostbyname(3) and getservbyname(3) functions into a single interface
	s = getaddrinfo(addr, ports, &hints, &servinfo);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return NULL;
	}

	// Connect to first valid result
	// Why are there multiple results? see man page (search 'several reasons')
	// How to search? enter /, then text to search for, press n/N to navigate
	for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
        // create socket
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sockfd == -1)
			continue;
        // connection
		if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break; // success

		close(sockfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return NULL;
	}
	freeaddrinfo(servinfo);

    rpc_client* client = malloc(sizeof(*client));
    assert(client != NULL);
    client -> sockfd = sockfd;
    return client;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
	char result[2];
	int n, f;

	if (cl == NULL || name == NULL){
		return NULL;
	}

	// send a pre-message, warning that find request is goind to be sent
	f = write(cl -> sockfd, "find", 4);
	if (f < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Send message to server
	n = write(cl -> sockfd, name, strlen(name));
	if (n < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Read message from server
	n = read(cl -> sockfd, result, 1);
	if (n < 0) {
		perror("read");
		exit(EXIT_FAILURE);
	}
	result[n] = '\0';

	// function name not found
	if (strcmp(result, "1") != 0){
		return NULL;
	}
	
	// function name is registered in server's state
	rpc_handle* func_handle = malloc(sizeof(*func_handle));
	assert(func_handle != NULL);
	strcpy(func_handle -> func_name, name);
    return func_handle;
    return NULL;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
	if (cl == NULL || h == NULL || payload == NULL){
		return NULL;
	}
	int c;
	size_t data2_len = payload -> data2_len;

	int result;
	int out = -1;
	int name_len = strlen(h -> func_name);
	size_t result_data2_len;
	char data2_status[2];

	// pre message, warning that call request is going to be sent
	c = write(cl -> sockfd, "call", 4);
	if (c < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	
	// write function name length
	write(cl -> sockfd, &name_len, sizeof(int));
	// write function name
	write(cl -> sockfd, h -> func_name, strlen(h -> func_name));
	// write data 1
	write(cl -> sockfd, &(payload -> data1), sizeof(int));
	// write data2_len
	write(cl -> sockfd, &data2_len, sizeof(size_t));
	// give a message on whether data2 is null or not
	if (payload -> data2 == NULL){
		write(cl -> sockfd, "Y", 1);
	} else {
		write(cl -> sockfd, "N", 1);
		// write data 2 only if the length is not 0 and data2 is not null
		if (data2_len != 0){
			write(cl -> sockfd, payload -> data2, data2_len);
		}
	}

	// Read result from server
	read(cl -> sockfd, &out, sizeof(int));
	// no rpc_data result coming
	if (out == 0){
		return NULL;
	} else {
		// read data 1
		read(cl -> sockfd, &result, sizeof(int));
		// read data 2 length
		read(cl -> sockfd, &result_data2_len, sizeof(size_t));
		// read the sign that indicate whether data2 is null or not
		read(cl -> sockfd, data2_status, 1);
		data2_status[1] = '\0';
	}

	rpc_data* results = malloc(sizeof(*results));
	assert(results != NULL);
	results -> data1 = result;
	// if data 2 not null
	if (strcmp(data2_status, "Y") != 0){
		if (result_data2_len != 0){
			results -> data2 = (void*) malloc(MAX_DATA2_LEN + 1);
			assert(results -> data2 != NULL);
			read(cl -> sockfd, results -> data2, MAX_DATA2_LEN);
		} else if (result_data2_len == 0){ // if the data2_len doesn't match the length of data2
			rpc_data_free(results);
			return NULL;
		}
		results -> data2_len = result_data2_len;
	} else { // data 2 null
		results -> data2 = NULL;
		// if data 2 is null and the data2_len is not 0
		if (result_data2_len != 0){
			rpc_data_free(results);
			return NULL;
		}
		results -> data2_len = 0;
	}
    return results;
}

void rpc_close_client(rpc_client *cl) {
	write(cl -> sockfd, "stop", 4);
	close(cl -> sockfd);
	free(cl);
}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}