//
// Fred Xia (fxia@yahoo.com)
//
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"

using namespace std;

namespace epoll_demo {

char* serialize_client_message(const char* worker,
                               const char* task_name,
                               uint32_t time_left,
                               uint32_t& sz)
{
  uint32_t msg_len = strlen(worker) + strlen(task_name) + sizeof(time_left) +
                     sizeof(uint32_t) + 2;
  char* msg = (char*)malloc(msg_len);
  if (!msg) {
    return nullptr;
  }
  char* p = msg;
  memcpy(p, &msg_len, sizeof(msg_len));
  p += sizeof(msg_len);
  memcpy(p, worker, strlen(worker) + 1);
  p += strlen(worker) + 1;
  memcpy(p, task_name, strlen(task_name) + 1);
  p += strlen(task_name) + 1;
  memcpy(p, &time_left, sizeof(time_left));
  sz = msg_len;
  return msg;
}

char* serialize_server_message(const char* task_name,
                               uint32_t sleep_time,
                               uint32_t& sz)
{
  uint32_t msg_len = strlen(task_name) + sizeof(sleep_time) +
                     sizeof(uint32_t) + 1;
  char* msg = (char*)malloc(msg_len);
  if (!msg) {
    return nullptr;
  }
  char* p = msg;
  memcpy(p, &msg_len, sizeof(msg_len));
  p += sizeof(msg_len);
  memcpy(p, task_name, strlen(task_name) + 1);
  p += strlen(task_name) + 1;
  memcpy(p, &sleep_time, sizeof(sleep_time));
  sz = msg_len;
  return msg;
}

int deserialize_client_message(const char* msg,
                               uint32_t msg_len,
                               string& worker,
                               string& task_name,
                               uint32_t& time_left)
{
  const char* p = msg;
  const char* end = msg + msg_len;

  int i = 0;  
  char id_buffer[MAX_TASK_NAME_LEN];  
  while (p < end && *p && i < MAX_TASK_NAME_LEN) {
    id_buffer[i++] = *p++;
  }
  if (i == MAX_TASK_NAME_LEN || p == end) {
    fprintf(stderr, "invalid worker id");
    return -1;
  }
  id_buffer[i] = 0;
  p++;

  i = 0;
  char name_buffer[MAX_TASK_NAME_LEN];
  while (p < end && *p && i < MAX_TASK_NAME_LEN) {
    name_buffer[i++] = *p++;
  }
  if (i == MAX_TASK_NAME_LEN || p == end) {
    fprintf(stderr, "invalid task_name");    
    return -1;
  }
  name_buffer[i] = 0;
  p++;
  
  if (end - p != sizeof(time_left)) {
    fprintf(stderr, "invalid time left %d, %ld, ", msg_len, end - p);
    return -1;
  }
  worker = id_buffer;
  task_name = name_buffer;
  memcpy(&time_left, p, sizeof(time_left));
  return 0;
}

int deserialize_server_message(const char* msg,
                               uint32_t msg_len,
                               string& task_name,
                               uint32_t& sleep_time)
{
  char name_buffer[MAX_TASK_NAME_LEN];
  const char* p = msg;
  const char* end = msg + msg_len;
  int i = 0;
  while (p < end && *p && i < MAX_TASK_NAME_LEN) {
    name_buffer[i++] = *p++;
  }
  if (i == MAX_TASK_NAME_LEN || p == end) {
    return -1;
  }
  name_buffer[i] = 0;
  p++;
  if (end - p != sizeof(sleep_time)) {
    return -1;
  }
  task_name = name_buffer;
  memcpy(&sleep_time, p, sizeof(sleep_time));
  return 0;
}

int set_fd_non_block(int fd)
{
  int opts = fcntl(fd, F_GETFL);
  if (opts < 0) {
    return -1;
  }
  opts |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, opts) < 0) {
    return -1;
  }
  return 0;
}

void log_message(FILE* log_file, const char* file_name, uint32_t line,
                 const char* fmt, ...)
{
  fprintf(log_file, "%.16s:%d ", file_name, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(log_file, fmt, args);
  va_end(args);
  fprintf(log_file, "\n");
}

};

