#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include "message.h"

#define MAXMSGLEN 65535


// server:
// getrequest
// sendresponse


void get_request(request* req, int sessfd) {
	size_t header_len = sizeof(req_header);

	size_t read_cnt = 0;
	while (read_cnt < header_len) {
		// convert to char* to do pointer arithmetic
		read_cnt += recv(sessfd, (char*)req + read_cnt, header_len-read_cnt, 0);
	}

	read_cnt = 0;
	fprintf(stderr, "payload length: %ld\n", req->header.payload_len);
	
	while (read_cnt < req->header.payload_len)
	{
		fprintf(stderr, "try to read payload\n");
		read_cnt += recv(sessfd, (char*)req + header_len+read_cnt, req->header.payload_len-read_cnt, 0);
	}

	fprintf(stderr, "server: func: %d, pathname: %s\n", req->header.opcode, req->req.open.pathname);
	
}


void execute_request(request* req, int sessfd) {
	switch (req->header.opcode)
	{
	case 0:
		int fd = open(req->req.open.pathname, req->req.open.flags, req->req.open.m);
		response res = {
			.header.errno_value = errno, 
			.header.payload_len = sizeof(union res_union),
			.res.open.ret_val = fd
		};
		size_t len = sizeof(response);
		send(sessfd, (void*)&res, len, 0);
		break;
	default:
		break;
	}
}

int main(int argc, char**argv) {
	// char *msg="Hello from server";
	// char buf[MAXMSGLEN+1];
	char *serverport;
	unsigned short port;
	int sockfd, sessfd, rv;
	struct sockaddr_in srv, cli;
	socklen_t sa_size;
	
	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) port = (unsigned short)atoi(serverport);
	else port=15440;
	
	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error
	
	// setup address structure to indicate server port
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = htonl(INADDR_ANY);	// don't care IP address
	srv.sin_port = htons(port);			// server port

	// bind to our port
	int opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	rv = bind(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	
	// start listening for connections
	rv = listen(sockfd, 5);
	if (rv<0) err(1,0);
	
	// main server loop, handle clients one at a time, quit after 10 clients
	while (1) {
		
		// wait for next client, get session socket
		sa_size = sizeof(struct sockaddr_in);
		sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
		if (sessfd<0) err(1,0);
		
		request* req = malloc(MAXMSGLEN);
		get_request(req, sessfd);
		execute_request(req, sessfd);

		free(req);
		close(sessfd);
	}
	
	// printf("server shutting down cleanly\n");
	// close socket
	close(sockfd);

	return 0;
}

