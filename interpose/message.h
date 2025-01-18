#include <sys/types.h>

enum OPCODE {
  OPEN,
  READ,
  WRITE,
  CLOSE
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
	int fd; 
	size_t count;
	char buf[0];
} write_req;


typedef struct {
	int fd;
} close_req;

union req_union {
	open_req open;
	write_req write;
	close_req close;
};

typedef struct {
	req_header header;
	union req_union req; 
} request;

typedef struct
{
	int ret_val;
} open_res;

typedef struct 
{
	ssize_t ret_val;
} write_res;


typedef struct {
	int ret_val;
} close_res;

union res_union {
	open_res open;
	write_res write;
	close_res close;
};

typedef struct 
{
	int errno_value;
	size_t payload_len;
} response_header;


typedef struct {
	response_header header;
	union res_union res;
} response;
