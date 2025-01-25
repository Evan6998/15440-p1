#define _GNU_SOURCE

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
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


int get_request(request* req, int sessfd) {
	size_t header_len = sizeof(req_header);

	size_t read_cnt = 0;
	while (read_cnt < header_len) {
		// convert to char* to do pointer arithmetic
		ssize_t bytes_received = recv(sessfd, (char*)req + read_cnt, header_len-read_cnt, 0);
		if (bytes_received <= 0) {
			return -1;
		}
		read_cnt += bytes_received;
	}

	read_cnt = 0;
	fprintf(stderr, "server: func: %d, payload length: %ld\n", req->header.opcode, req->header.payload_len);
	
	while (read_cnt < req->header.payload_len)
	{
		fprintf(stderr, "try to read payload\n");
		ssize_t bytes_received = recv(sessfd, (char*)req + header_len+read_cnt, req->header.payload_len-read_cnt, 0);
		if (bytes_received <= 0) {
			return -1;
		}
		read_cnt += bytes_received;
	}

	fprintf(stderr, "server: func: %d\n", req->header.opcode);
	return 0;
}


void execute_request(request* req, int sessfd) {
	switch (req->header.opcode)
	{
	case OPEN:
		int fd = open(req->req.open.pathname, req->req.open.flags, req->req.open.m);
		response open_res = {
			.header.errno_value = errno, 
			.header.payload_len = sizeof(union res_union),
			.res.open.ret_val = fd
		};
		send(sessfd, (void*)&open_res, sizeof(response), 0);
		break;
	case READ:
		response* read_response = malloc(MAXMSGLEN);
		size_t nbyte = read(req->req.read.fildes, read_response->res.read.buf, req->req.read.nbyte);
		
		read_response->header.errno_value = errno;
		read_response->header.payload_len = sizeof(union res_union) + nbyte;
		read_response->res.read.nbyte = nbyte;

		send(sessfd, (void*)read_response, sizeof(response) + nbyte, 0);
		break;
	case WRITE:
		ssize_t cnt = write(req->req.write.fd, req->req.write.buf, req->req.write.count);
		response write_res = {
			.header.errno_value = errno,
			.header.payload_len = sizeof(union res_union),
			.res.write.ret_val = cnt
		};
		send(sessfd, (void*)&write_res, sizeof(response), 0);
		break;
	case CLOSE:
		int ret = close(req->req.close.fd);
		response close_res = {
			.header.errno_value = errno,
			.header.payload_len = sizeof(union res_union),
			.res.close.ret_val = ret
		};
		send(sessfd, (void*)&close_res, sizeof(response), 0);
		break;
	case LSEEK:
		off_t off = lseek(req->req.lseek.fd, req->req.lseek.offset, req->req.lseek.whence);
		response lseek_response = {
			.header.errno_value = errno,
			.header.payload_len = sizeof(union res_union),
			
			.res.lseek.off = off,
		};
		send(sessfd, (void*)&lseek_response, sizeof(response), 0);
		break;
	case STAT:
		response stat_response;
		stat_response.res.stat.ret_val = stat(req->req.stat.pathname, &stat_response.res.stat.statbuf);
		stat_response.header.errno_value = errno;
		stat_response.header.payload_len = sizeof(union res_union);
		send(sessfd, (void*)&stat_response, sizeof(response), 0);
		break;
	case UNLINK:
		int ret_val = unlink(req->req.unlink.pathname);
		response unlink_response = {
			.header.errno_value = errno,
			.header.payload_len = sizeof(union res_union),

			.res.unlink.ret_val = ret_val,
		};
		send(sessfd, (void*)&unlink_response, sizeof(response), 0);
		break;
	case GETDIRENTRIES:
		response* r = malloc(sizeof(response) + req->req.direntries.nbytes);
		ssize_t bytes_read = getdirentries(req->req.direntries.fd, r->res.direntries.buf, req->req.direntries.nbytes, &r->res.direntries.basep);
		
		r->header.errno_value = errno;
		r->header.payload_len = sizeof(union res_union) + bytes_read;
		r->res.direntries.ret_val = bytes_read;

		send(sessfd, (void*)r, sizeof(response) + bytes_read, 0);
		free(r);
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
		if (fork() == 0) {
			// child
			close(sockfd);
			while (1) {
				request* req = malloc(MAXMSGLEN);
				if (get_request(req, sessfd) != 0) {
					fprintf(stderr, "[server.c] Connection close.\n");
					free(req);
					close(sessfd);
					break;
				}
				execute_request(req, sessfd);
				free(req);
				fprintf(stderr, "[server.c] Finish one request.\n");
			}
			close(sessfd);
			exit(0);
		}

	}
	
	// printf("server shutting down cleanly\n");
	// close socket
	close(sockfd);

	return 0;
}

