/**
 * @file mylib.c
 * @brief Implements an RPC-based interposition library for remote file
 * operations.
 *
 * This library intercepts standard file system calls (e.g., open, read, write,
 * close, stat, unlink, etc.) and redirects them to a remote file server via an
 * RPC mechanism. It dynamically replaces standard system calls using function
 * interposition with `dlsym()`, enabling transparent remote file access.
 *
 * Features:
 * - **RPC Communication**: Uses `makerpc()` to send requests and receive
 * responses.
 * - **Remote File Descriptors**: Tracks remote file descriptors using
 * `open_fds[]`.
 * - **Client Initialization**: Connects to the file server based on environment
 * variables.
 * - **Function Interposition**: Overrides system calls via `dlsym(RTLD_NEXT)`.
 * - **Directory Tree Handling**: Supports `getdirtree()` and `freedirtree()`.
 *
 * The `_init()` function initializes the library, setting up function pointers
 * and establishing a connection to the remote server. The implementation
 * ensures compatibility with standard file operations while providing seamless
 * remote access.
 */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>

#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// copy from client.c
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../include/dirtree.h"
#include "message.h"

#define MAXMSGLEN 1048575
#define BUFLEN 2048
#define VERSION 1
#define INT_SIZE sizeof(int)
#define REMOTE_FD 32768
#define MAXIMUM_FD 65536

int sockfd;
int open_fds[MAXIMUM_FD] = {0};

// client
void makerpc(request *h, response *r);

int remote_fd(int fd) {
  if (fd >= REMOTE_FD) {
    int real_fd = fd - REMOTE_FD;
    if (real_fd >= MAXIMUM_FD) {
      fprintf(stderr, "please consider to add MAXIMUM_FD\n");
      exit(1);
    }
    if (open_fds[real_fd])
      return 1;
  }
  return 0;
}

void initialize_client() {
  char *serverip;
  char *serverport;
  unsigned short port;
  int rv;
  struct sockaddr_in srv;

  serverip = getenv("server15440");
  if (serverip)
    fprintf(stderr, "Got environment variable server15440: %s\n", serverip);
  else {
    fprintf(stderr,
            "Environment variable server15440 not found.  Using 127.0.0.1\n");
    serverip = "127.0.0.1";
  }

  serverport = getenv("serverport15440");
  if (serverport)
    fprintf(stderr, "Got environment variable serverport15440: %s\n",
            serverport);
  else {
    fprintf(stderr,
            "Environment variable serverport15440 not found.  Using 15440\n");
    serverport = "15440";
  }
  port = (unsigned short)atoi(serverport);

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    err(1, 0);

  memset(&srv, 0, sizeof(srv));
  srv.sin_family = AF_INET;
  srv.sin_addr.s_addr = inet_addr(serverip);
  srv.sin_port = htons(port);

  rv = connect(sockfd, (struct sockaddr *)&srv, sizeof(struct sockaddr));
  if (rv < 0)
    err(1, 0);
}

void makerpc(request *h, response *r) {
  size_t len = sizeof(req_header) + h->header.payload_len;
  send(sockfd, (void *)h, len, 0);

  len = sizeof(response_header);
  size_t read_cnt = 0;
  while (read_cnt < len) {
    read_cnt += recv(sockfd, (char *)r + read_cnt, len - read_cnt, 0);
  }

  size_t payload_len = r->header.payload_len;
  read_cnt = 0;
  while (read_cnt < payload_len) {
    read_cnt +=
        recv(sockfd, (char *)r + len + read_cnt, payload_len - read_cnt, 0);
  }
}

// The following line declares a function pointer with the same prototype as the
// open function.
int (*orig_open)(const char *pathname, int flags,
                 ...); // mode_t mode is needed when flags includes O_CREAT
ssize_t (*orig_read)(int fildes, void *buf, size_t nbyte);
int (*orig_close)(int fildes);
ssize_t (*orig_write)(int fd, const void *buf, size_t count);
int (*orig_stat)(const char *restrict pathname, struct stat *restrict statbuf);
off_t (*orig_lseek)(int fd, off_t offset, int whence);
int (*orig_unlink)(const char *pathname);
ssize_t (*orig_getdirentries)(int fd, char *buf, size_t nbytes,
                              off_t *restrict basep);
struct dirtreenode *(*orig_getdirtree)(const char *path);
void (*orig_freedirtree)(struct dirtreenode *dt);

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
  mode_t m = 0;
  if (flags & O_CREAT) {
    va_list a;
    va_start(a, flags);
    m = va_arg(a, mode_t);
    va_end(a);
  }

  int pathname_len = strlen(pathname) + 1;
  int len = sizeof(request) + pathname_len;
  request *r = malloc(len);

  r->header.opcode = OPEN;
  r->header.payload_len = sizeof(union req_union) + pathname_len;

  r->req.open.flags = flags;
  r->req.open.m = m;
  memcpy(r->req.open.pathname, pathname, pathname_len);

  response *res = malloc(MAXMSGLEN);
  makerpc(r, res);

  fprintf(stderr, "[mylib.c]: rpc open return value: %d, errno: %d\n",
          res->res.open.ret_val, res->header.errno_value);

  free(r);
  errno = res->header.errno_value;
  int ret_val = res->res.open.ret_val;
  free(res);
  if (ret_val == -1) {
    return ret_val;
  }
  open_fds[ret_val] = 1;
  return ret_val + REMOTE_FD;
}

ssize_t read(int fildes, void *buf, size_t nbyte) {
  fprintf(stderr, "[mylib.c]: read called for fildes %d\n", fildes);

  if (!remote_fd(fildes)) {
    return orig_read(fildes, buf, nbyte);
  }
  fildes -= REMOTE_FD;

  request r = {
      .header.opcode = READ,
      .header.payload_len = sizeof(union req_union),
      .req.read.fildes = fildes,
      .req.read.nbyte = nbyte,
  };

  response *res = malloc(MAXMSGLEN);
  makerpc(&r, res);

  size_t read_size = res->res.read.nbyte;
  memcpy(buf, res->res.read.buf, read_size);
  errno = res->header.errno_value;
  free(res);
  return read_size;
}

ssize_t write(int fd, const void *buf, size_t count) {

  fprintf(stderr, "[mylib.c]: write called for fd %d, size: %lu \n", fd, count);

  if (!remote_fd(fd)) {
    return orig_write(fd, buf, count);
  }
  fd -= REMOTE_FD;

  int len = sizeof(request) + count;
  request *r = malloc(len);

  r->header.opcode = WRITE;
  r->header.payload_len = sizeof(union req_union) + count;

  r->req.write.count = count;
  r->req.write.fd = fd;
  memcpy(r->req.write.buf, buf, count);

  response *res = malloc(MAXMSGLEN);
  makerpc(r, res);

  fprintf(stderr, "[mylib.c]: rpc write return val: %lu, errno: %d\n",
          res->res.write.ret_val, res->header.errno_value);

  free(r);

  errno = res->header.errno_value;
  int ret_val = res->res.write.ret_val;
  free(res);
  return ret_val;
}

int close(int fildes) {

  if (!remote_fd(fildes)) {
    return orig_close(fildes);
  }
  fildes -= REMOTE_FD;

  fprintf(stderr, "[mylib.c]: close called for fildes %d\n", fildes);
  request r = {.header.opcode = CLOSE,
               .header.payload_len = sizeof(union req_union),
               .req.close.fd = fildes};

  response res;
  makerpc(&r, &res);

  errno = res.header.errno_value;
  open_fds[fildes] = 0;
  return res.res.close.ret_val;
}

int stat(const char *restrict pathname, struct stat *restrict statbuf) {
  fprintf(stderr, "[mylib.c]: stat called for file: %s\n", pathname);

  int pathname_len = strlen(pathname) + 1;
  int len = sizeof(request) + pathname_len;
  request *r = malloc(len);

  r->header.opcode = STAT;
  r->header.payload_len = sizeof(union req_union) + pathname_len;
  memcpy(r->req.stat.pathname, pathname, pathname_len);

  response res;
  makerpc(r, &res);

  errno = res.header.errno_value;
  memcpy(statbuf, &res.res.stat.statbuf, sizeof(struct stat));
  free(r);
  return res.res.stat.ret_val;
}

off_t lseek(int fd, off_t offset, int whence) {

  fprintf(stderr, "[mylib.c]: lseek called for fildes %d\n", fd);
  if (!remote_fd(fd)) {
    return orig_lseek(fd, offset, whence);
  }
  fd -= REMOTE_FD;
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
  request *r = malloc(len);

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

  fd -= REMOTE_FD;

  request req = {
      .header.opcode = GETDIRENTRIES,
      .header.payload_len = sizeof(union req_union),
      .req.direntries.fd = fd,
      .req.direntries.nbytes = nbytes,
  };

  response *res = malloc(MAXMSGLEN);
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

struct dirtreenode *deserialize_to_dirtree(char *buf, size_t *nbyte) {
  if (buf == NULL) {
    fprintf(stderr, "This shouldn't happen\n");
    exit(255);
  };
  struct dirtreenode *tree = malloc(sizeof(struct dirtreenode));
  char *offset = buf;

  size_t entryname_len = strlen(offset) + 1;
  tree->name = malloc(entryname_len);
  memcpy(tree->name, offset, entryname_len);
  offset += entryname_len;

  tree->num_subdirs = *(int *)offset;
  offset += sizeof(int);

  tree->subdirs = malloc(tree->num_subdirs * sizeof(struct dirtreenode *));
  size_t n = 0;
  for (int i = 0; i < tree->num_subdirs; i++) {
    tree->subdirs[i] = deserialize_to_dirtree(offset, &n);
    offset += n;
  }
  if (nbyte != NULL) {
    *nbyte = offset - buf;
  }
  return tree;
}

struct dirtreenode *getdirtree(const char *path) {
  fprintf(stderr, "[mylib.c]: getdirtree called for file: %s\n", path);

  int path_len = strlen(path) + 1;
  int len = sizeof(request) + path_len;
  request *r = malloc(len);

  r->header.opcode = GETDIRTREE;
  r->header.payload_len = sizeof(union req_union) + path_len;
  memcpy(r->req.dirtree.path, path, path_len);

  response *res = malloc(MAXMSGLEN);
  makerpc(r, res);

  errno = res->header.errno_value;
  struct dirtreenode *tree = deserialize_to_dirtree(res->res.dirtree.buf, NULL);
  free(r);
  free(res);
  return tree;
}

void recursive_free(struct dirtreenode *dt) {
  if (dt == NULL)
    return;
  for (int i = 0; i < dt->num_subdirs; i++) {
    recursive_free(dt->subdirs[i]);
  }
  free(dt->name);
  free(dt->subdirs);
  free(dt);
}

void freedirtree(struct dirtreenode *dt) {
  fprintf(stderr, "[mylib.c]: freedirtree called.\n");
  recursive_free(dt);
  dt = NULL;
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
