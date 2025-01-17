#include <sys/types.h>

enum OPCODE {
  OPEN = 0,
  READ,
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


union req_union {
	open_req open;
};

typedef struct {
	req_header header;
	union req_union req; 
} request;

typedef struct
{
	int ret_val;
} open_res;

union res_union {
	open_res open;
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
