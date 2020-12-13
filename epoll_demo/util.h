#ifndef __task_util_h__
#define __task_util_h__
//
// Fred Xia (fxia@yahoo.com)
//

#include <stdint.h>
#include <string>

#define DEFAULT_TIMEOUT     1000
#define MAX_TASK_NAME_LEN   32
#define MAX_PORT_NUMBER     8192

#define MAX_CLIENT_MSG_LEN \
  (MAX_TASK_NAME_LEN + MAX_TASK_NAME_LEN + sizeof(uint32_t))

#define MAX_SERVER_MSG_LEN \
  (MAX_TASK_NAME_LEN + sizeof(uint32_t))

namespace epoll_demo {

char* serialize_client_message(const char* worker,
                               const char* task_name,
                               uint32_t time_left,
                               uint32_t& sz);

char* serialize_server_message(const char* task_name,
                               uint32_t sleep_time,
                               uint32_t& sz);

int deserialize_client_message(const char* msg,
                               uint32_t msg_len,
                               std::string& worker,
                               std::string& task_name,
                               uint32_t& time_left);

int deserialize_server_message(const char* msg,
                               uint32_t msg_len,
                               std::string& task_name,
                               uint32_t& sleep_time);

void log_message(FILE* log_file, const char* src_file, uint32_t line,
                 const char* fmt, ...)
  __attribute__((format (printf, 4, 5)));

int set_fd_non_block(int fd);

}

#endif
