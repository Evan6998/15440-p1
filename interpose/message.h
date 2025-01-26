/**
 * @file message.h
 * @brief Defines the message structures for an RPC-based file system service.
 *
 * This file contains the request and response message formats used in an
 * RPC protocol for remote file operations over TCP. The protocol supports
 * standard file operations such as open, read, write, close, seek, stat,
 * unlink, and directory traversal.
 *
 * Structures:
 * - `request`: Encapsulates a request header and the corresponding payload.
 * - `req_header`: Common request header with opcode and payload details.
 * - `req_union`: Union of different request types.
 *
 * - `response`: Encapsulates a response header and the corresponding result.
 * - `response_header`: Common response header with error handling.
 * - `res_union`: Union of different response types.
 *
 * Each request and response structure ensures proper serialization and
 * deserialization for efficient communication between client and server.
 */
#include <sys/types.h>

enum OPCODE {
  OPEN,
  READ,
  WRITE,
  CLOSE,
  LSEEK,
  STAT,
  UNLINK,
  GETDIRENTRIES,
  GETDIRTREE,
  FREEDIRTREE,
};

typedef struct {
  int version;
  enum OPCODE opcode;
  int flags;
  size_t payload_len;
} req_header;

typedef struct {
  int flags;
  mode_t m;
  char pathname[0];
} open_req;

typedef struct {
  int fildes;
  // void *buf;
  size_t nbyte;
} read_req;

typedef struct {
  int fd;
  size_t count;
  char buf[0];
} write_req;

typedef struct {
  int fd;
} close_req;

typedef struct {
  int fd;
  off_t offset;
  int whence;
} lseek_req;

typedef struct {
  char pathname[0];
} stat_req;

typedef struct {
  char pathname[0];
} unlink_req;

typedef struct {
  int fd;
  size_t nbytes;
} direntries_req;

typedef struct {
  char path[0];
} dirtree_req;

union req_union {
  open_req open;
  read_req read;
  write_req write;
  close_req close;
  lseek_req lseek;
  stat_req stat;
  unlink_req unlink;
  direntries_req direntries;
  dirtree_req dirtree;
};

typedef struct {
  req_header header;
  union req_union req;
} request;

typedef struct {
  int ret_val;
} open_res;

typedef struct {
  ssize_t ret_val;
} write_res;

typedef struct {
  size_t nbyte;
  char buf[0];
} read_res;

typedef struct {
  int ret_val;
} close_res;

typedef struct {
  off_t off;
} lseek_res;

typedef struct {
  int ret_val;
  struct stat statbuf;
} stat_res;

typedef struct {
  int ret_val;
} unlink_res;

typedef struct {
  ssize_t ret_val;
  off_t basep;
  char buf[0];
} direntries_res;

typedef struct {
  char buf[0];
} dirtree_res;

union res_union {
  open_res open;
  read_res read;
  write_res write;
  close_res close;
  lseek_res lseek;
  stat_res stat;
  unlink_res unlink;
  direntries_res direntries;
  dirtree_res dirtree;
};

typedef struct {
  int errno_value;
  size_t payload_len;
} response_header;

typedef struct {
  response_header header;
  union res_union res;
} response;
