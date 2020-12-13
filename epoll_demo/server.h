#ifndef __task_server_h__
#define __task_server_h__
//
// Fred Xia (fxia@yahoo.com)
//
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <string>

namespace epoll_demo {

struct TcpServerImpl;

class TcpServer {
public:
  TcpServer(const char* name, uint16_t port, uint32_t epoll_timeout,
            bool log_to_stderr);
  virtual ~TcpServer();

  const char* server_name() const;
  uint16_t server_port() const;
  int server_fd() const;
  FILE* log_file();
  const std::string& log_file_name() const;

  // The main event loop
  int run_loop();
  
  // Set epoll_wait timeout value in milliseconds
  void set_timeout(uint32_t timeout);

  // Handle a newly accepted connection. Returns mask of interest for
  // epoll_wait call. 0 means connection rejected and to be closed.
  virtual uint32_t handle_new_connection(int fd) = 0;

  // Handle a connection event. Returns the mask of interst for next
  // epoll_wait call. If 0 the fd is closed.
  virtual uint32_t handle_connection(const epoll_event& event) = 0;

  // Give server object to handle work after either a timeout or a run of
  // message processing. Returns 0 to continue the loop, 1 to indicate end
  // of run loop.
  virtual int handle_timeout(bool is_timeout) = 0;

private:
  TcpServerImpl* impl;
};

};

#endif
