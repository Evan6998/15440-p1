#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

// copy from client.c
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include "message.h"

#define MAXMSGLEN 65535
#define BUFLEN 2048
#define VERSION 1
#define INT_SIZE sizeof(int)

int sockfd;

// client
void makerpc(request* h, response* r);

void initialize_client() {
	char *serverip;
	char *serverport;
	unsigned short port;
	// char *msg="Hello from client";
	// char buf[MAXMSGLEN+1];
	int rv;
	struct sockaddr_in srv;
	
	// Get environment variable indicating the ip address of the server
	serverip = getenv("server15440");
	if (serverip) fprintf(stderr, "Got environment variable server15440: %s\n", serverip);
	else {
		fprintf(stderr, "Environment variable server15440 not found.  Using 127.0.0.1\n");
		serverip = "127.0.0.1";
	}
	
	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) fprintf(stderr, "Got environment variable serverport15440: %s\n", serverport);
	else {
		fprintf(stderr, "Environment variable serverport15440 not found.  Using 15440\n");
		serverport = "15440";
	}
	port = (unsigned short)atoi(serverport);
	
	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error
	
	// setup address structure to point to server
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = inet_addr(serverip);	// IP address of server
	srv.sin_port = htons(port);			// server port

	// actually connect to the server
	rv = connect(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	
	// // send message to server
	// printf("client sending to server: %s\n", msg);
	// send(sockfd, msg, strlen(msg), 0);	// send message; should check return value
	
	// // get message back
	// rv = recv(sockfd, buf, MAXMSGLEN, 0);	// get message
	// if (rv<0) err(1,0);			// in case something went wrong
	// buf[rv]=0;				// null terminate string to print
	// printf("client got messge: %s\n", buf);
	
}


void makerpc(request* h, response* r) {
	size_t len = sizeof(req_header) + h->header.payload_len;
	send(sockfd, (void*)h, len, 0);
	
	len = sizeof(response_header);
	size_t read_cnt = 0;
	fprintf(stderr, "[mylib.c] Expect %lu bytes for header\n", len);
	while (read_cnt < len) {
		// convert to char* to do pointer arithmetic
		read_cnt += recv(sockfd, (char*)r + read_cnt, len-read_cnt, 0);
		// fprintf(stderr, "Read %lu bytes already\n", read_cnt);
	}

	size_t payload_len = r->header.payload_len;
	read_cnt = 0;
	fprintf(stderr, "[mylib.c] Expect %lu bytes for body\n", payload_len);
	while (read_cnt < payload_len) {
		// convert to char* to do pointer arithmetic
		read_cnt += recv(sockfd, (char*)r + len + read_cnt, payload_len - read_cnt, 0);
		// fprintf(stderr, "Read %lu bytes already\n", read_cnt);
	}
}

// The following line declares a function pointer with the same prototype as the open function.  
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT
ssize_t (*orig_read)(int fildes, void *buf, size_t nbyte);
int (*orig_close)(int fildes);
ssize_t (*orig_write)(int fd, const void *buf, size_t count);
int (*orig_stat)(const char *restrict pathname, struct stat *restrict statbuf);
off_t (*orig_lseek)(int fd, off_t offset, int whence);
int (*orig_unlink)(const char *pathname);
ssize_t (*orig_getdirentries)(int fd, char *buf, size_t nbytes, off_t *restrict basep);
struct dirtreenode* (*orig_getdirtree)( const char *path );
void (*orig_freedirtree)( struct dirtreenode* dt );

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}

	// we just print a message, then call through to the original open function (from libc)
	fprintf(stderr, "[mylib.c]: open called for path %s\n", pathname);

	int pathname_len = strlen(pathname) + 1;
	int len = sizeof(request) + pathname_len;
	request* r = malloc(len);

	r->header.opcode = OPEN;
	r->header.payload_len = sizeof(union req_union) + pathname_len;

	r->req.open.flags = flags;
	r->req.open.m = m;
	memcpy(r->req.open.pathname, pathname, pathname_len);
	
	response* res = malloc(MAXMSGLEN);
	makerpc(r, res);

	fprintf(stderr, "[mylib.c]: rpc open return value: %d, errno: %d\n", res->res.open.ret_val, res->header.errno_value);

	free(r);
	errno = res->header.errno_value;
	return res->res.open.ret_val;
	
	// return orig_open(pathname, flags, m);
}

ssize_t read(int fildes, void *buf, size_t nbyte) {
	fprintf(stderr, "[mylib.c]: read called for fildes %d\n", fildes);

	request r = {
		.header.opcode = READ,
		.header.payload_len = sizeof(union req_union),
		.req.read.fildes = fildes,
		.req.read.nbyte = nbyte,
	};

	response* res = malloc(MAXMSGLEN);
	makerpc(&r, res);

	size_t read_size = res->res.read.nbyte;
	memcpy(buf, res->res.read.buf, read_size);
	errno = res->header.errno_value;
	return read_size;
}

ssize_t write(int fd, const void *buf, size_t count) {

	// we just print a message, then call through to the original open function (from libc)
	fprintf(stderr, "[mylib.c]: write called for fd %d\n", fd);

	int len = sizeof(request) + count;
	request* r = malloc(len);

	r->header.opcode = WRITE;
	r->header.payload_len = sizeof(union req_union) + count;

	r->req.write.count = count;
	r->req.write.fd = fd;
	memcpy(r->req.write.buf, buf, count);
	
	response* res = malloc(MAXMSGLEN);
	makerpc(r, res);

	fprintf(stderr, "[mylib.c]: rpc write return val: %lu, errno: %d\n", res->res.write.ret_val, res->header.errno_value);

	free(r);

	errno = res->header.errno_value;
	return res->res.write.ret_val;

	// return orig_write(fd, buf, count);
}

int close(int fildes) {
	// char* msg = "close\n";
	// send(sockfd, msg, strlen(msg), 0);

	fprintf(stderr, "[mylib.c]: close called for fildes %d\n", fildes);
	request r = {
		.header.opcode = CLOSE,
		.header.payload_len = sizeof(union req_union),
		.req.close.fd = fildes
	};

	response res;
	makerpc(&r, &res);

	errno = res.header.errno_value;
	return res.res.close.ret_val;

	// return orig_close(fildes);
}

int stat(const char *restrict pathname, struct stat *restrict statbuf) {
	fprintf(stderr, "[mylib.c]: stat called for file: %s\n", pathname);

	int pathname_len = strlen(pathname) + 1;
	int len = sizeof(request) + pathname_len;
	request* r = malloc(len);

	r->header.opcode = STAT;
	r->header.payload_len = sizeof(union req_union) + pathname_len;
	memcpy(r->req.stat.pathname, pathname, pathname_len);

	response res;
	makerpc(r, &res);

	errno = res.header.errno_value;
	memcpy(statbuf, &res.res.stat.statbuf, sizeof(struct stat));
	return res.res.stat.ret_val;
}

off_t lseek(int fd, off_t offset, int whence) {

	fprintf(stderr, "[mylib.c]: lseek called for fildes %d\n", fd);
	request r = {
		.header.opcode = LSEEK,
		.header.payload_len = sizeof(union req_union),
		
		.req.lseek.fd = fd,
		.req.lseek.offset = offset,
		.req.lseek.whence = whence,
	};

	response res;
	makerpc(&r, &res);

	errno = res.header.errno_value;
	return res.res.lseek.off;
}

int unlink(const char *pathname) {
	int pathname_len = strlen(pathname) + 1;
	int len = sizeof(request) + pathname_len;
	request* r = malloc(len);

	r->header.opcode = UNLINK;
	r->header.payload_len = sizeof(union req_union) + pathname_len;
	memcpy(r->req.unlink.pathname, pathname, pathname_len);

	response res;
	makerpc(r, &res);

	errno = res.header.errno_value;
	free(r);
	return res.res.unlink.ret_val;
}

ssize_t getdirentries(int fd, char *buf, size_t nbytes, off_t *restrict basep) {
	fprintf(stderr, "[mylib.c]: getdirentries called for fildes %d\n", fd);

	request req = {
		.header.opcode = GETDIRENTRIES,
		.header.payload_len = sizeof(union req_union),
		.req.direntries.fd = fd,
		.req.direntries.nbytes = nbytes,
	};
	
	response* res = malloc(MAXMSGLEN);
	makerpc(&req, res);

	errno = res->header.errno_value;
	ssize_t ret_val;
	if ((ret_val = res->res.direntries.ret_val) > 0) {
		memcpy(buf, res->res.direntries.buf, ret_val);
		*basep = res->res.direntries.basep;
	}
	free(res);
	return ret_val;
}

struct dirtreenode* getdirtree( const char *path ) {
	fprintf(stderr, "[mylib.c]: getdirtree called for file: %s\n", path);
	char* msg = "getdirtree\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_getdirtree(path);
}

void freedirtree( struct dirtreenode* dt ) {
	fprintf(stderr, "[mylib.c]: freedirtree called.\n");
	char* msg = "freedirtree\n";
	send(sockfd, msg, strlen(msg), 0);

	return orig_freedirtree(dt);
}

// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open = dlsym(RTLD_NEXT, "open");
	orig_read = dlsym(RTLD_NEXT, "read");
	orig_close = dlsym(RTLD_NEXT, "close");
	orig_write = dlsym(RTLD_NEXT, "write");
	orig_stat = dlsym(RTLD_NEXT, "stat");
	orig_lseek = dlsym(RTLD_NEXT, "lseek");
	orig_unlink = dlsym(RTLD_NEXT, "unlink");
	orig_getdirentries = dlsym(RTLD_NEXT, "getdirentries");
	orig_getdirtree = dlsym(RTLD_NEXT, "getdirtree");
	orig_freedirtree = dlsym(RTLD_NEXT, "freedirtree");
	fprintf(stderr, "[mylib.c] Init mylib\n");

	initialize_client();
}


