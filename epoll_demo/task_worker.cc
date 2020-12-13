/*
 * Fred Xia (fxia@yahoo.com)
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <getopt.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include "util.h"
#include "server.h"

using namespace std;
using namespace epoll_demo;

#define LOG(fmt, args...) do { \
  log_message(_log_file, __FILE__, __LINE__, fmt, ##args); \
} while (0)

// Default epoll_wait timeout in milliseconds
static const int default_timeout = 1000;

struct TaskWorker {

  uint16_t  _controller_port;   // port to connect to controller
  string    _worker_id;     // worker id assigned at launch
  int       _fd;            // server connection
  int       _epoll_fd;      // epoll file descriptor
  time_t    _sleep_start;   // start time of sleep
  string    _task_name;     // task name
  uint32_t  _sleep_time;    // task sleep time
  uint32_t  _timeout;       // timeout for epoll_pwait
  FILE*     _log_file;      // log file
  string    _log_file_name; // log file name
  bool      _is_slacker;    // slacker for testing
  struct epoll_event _ev;   // current interested events

  TaskWorker(uint16_t controller_port, const char* worker_id, bool to_stderr,
            bool is_slacker)
    : _controller_port(controller_port), _worker_id(worker_id),
      _fd(0), _epoll_fd(0), _sleep_start(0), _sleep_time(0),
      _timeout(default_timeout), _is_slacker(is_slacker) {
    
    if (to_stderr) {
      _log_file = stderr;
      _log_file_name = "stderr";
    } else {
      char buffer[128];
      snprintf(buffer, sizeof(buffer), "/tmp/%s_XXXXXX", worker_id);
      int fd = mkstemp(buffer);
      _log_file = fdopen(fd, "w");
      if (_log_file == nullptr) {
        fprintf(stderr, "Cannot open log file %s", buffer);
        _log_file = stderr;
      } else {
        fprintf(stdout, "Worker %s log file is %s\n", worker_id, buffer);
      }
    }
  }

  ~TaskWorker() {
    if (_epoll_fd) {
      close(_epoll_fd);
      _epoll_fd = 0;
    }
    if (_fd) {
      close(_fd);
      _fd = 0;
    }
    if (_log_file && _log_file != stderr) {
      fclose(_log_file);
      _log_file = nullptr;
    }
  }

  int init() {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
      LOG("Init epoll error %s", strerror(errno));
      return -1;
    }
    _epoll_fd = epoll_fd;
    return 0;
  }

  int connect_server() {
    assert(_fd == 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_controller_port);
    addr.sin_addr.s_addr = htons(INADDR_ANY);
    int conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    int r = ::connect(conn_fd, (struct sockaddr*)&addr, sizeof(addr));
    if (r < 0) {
      LOG("Error connect() to server: %s", strerror(errno));
      return -1;
    }
    // Register interest in server instruction
    _ev.data.fd = conn_fd;
    _ev.events = EPOLLIN | EPOLLHUP;
    r = epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, conn_fd, &_ev);
    if (r < 0) {
      LOG("Error in epoll_ctl(): %s", strerror(errno));
      close(conn_fd);
      return -1;
    }
    _fd = conn_fd;
    if (send_status() < 0) {
      disconnect_server();
      return -1;
    }
    if (time_left() > 0) {
      LOG("Reconnected to server, sleep for %d more secs",
                  time_left());
      _timeout = time_left() * 1000;
    }
    return _fd;
  }

  int disconnect_server() {
    if (_fd == 0) {
      return 0;
    }
    int r = epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, _fd, &_ev);
    if (r < 0) {
      LOG("epoll_ctl failed: %s", strerror(errno));
    }
    close(_fd);
    _fd = 0;
    _timeout = default_timeout;
    return r;
  }

  uint32_t time_left() {
    uint32_t time_diff = (uint32_t)(time(0) - _sleep_start);
    return (_sleep_time < time_diff ? 0 : _sleep_time - time_diff);
  }

  // Send worker status to controller
  int send_status() {
    uint32_t msg_sz;
    char* msg = serialize_client_message(_worker_id.c_str(),
                                         _task_name.c_str(),
                                         time_left(),
                                         msg_sz);
    if (msg) {
      int r = ::write(_fd, msg, msg_sz);
      free((void*)msg);
      if (r < 0) {
        LOG("Error in write(): %s", strerror(errno));
        disconnect_server();
        return -1;
      }
      LOG("Sent status to server");
      return 0;
    }
    return -1;
  }

  // Handle message from server. We only register EPOLLIN and EPOLLHUP
  int handle_connection(struct epoll_event& ev) {
    LOG("events: 0x%x", ev.events);
    if (ev.events & EPOLLIN) {
      uint32_t msg_len;
      int r = ::read(_fd, (char*)&msg_len, sizeof(msg_len));
      if (r != sizeof(uint32_t)) {
        LOG("Error in read server message header: %d", r);
        disconnect_server();
        return -1;
      }
      if (msg_len > MAX_SERVER_MSG_LEN) {
        LOG("Error in server message len %u", msg_len);
        disconnect_server();
        return -1;
      }
      uint32_t body_len = msg_len - sizeof(msg_len);
      char msg[body_len];
      r = ::read(_fd, msg, body_len);
      if (r != (int)body_len) {
        LOG("Error in read server msg: %d", r);
        disconnect_server();
        return -1;
      }
      if (deserialize_server_message(msg, body_len, _task_name,
                                     _sleep_time) < 0) {
        LOG("Error in deserialize_server_message");
        disconnect_server();
        return -1;
      }
      if (_task_name == "") {
        LOG("Task controller tells me to exit");
        return 1;
      }
      LOG("Received task from server %s, sleep time %d. I'm slacker: %d",
          _task_name.c_str(), _sleep_time, _is_slacker);
      // Start sleep
      if (_is_slacker) {
        _sleep_time += 20; // slack off on response
      }
      _sleep_start = time(0);
      _timeout = _sleep_time * 1000;
    }
    if (ev.events & EPOLLHUP) {
      disconnect_server();
      return -1;
    }
    return 0;
  }

  int run_loop() {
    int r;
    struct epoll_event events;
    while (true) {
      if (_fd == 0) {
        connect_server();
      }
      LOG("epoll wait %d", _timeout);
      memset(&events, 0, sizeof(events));
      r = epoll_wait(_epoll_fd, &events, 1, _timeout);
      if (r > 0) {
        r = handle_connection(events);
        if (r > 0) {
          break;
        }
      } else if (!_task_name.empty() && time_left() == 0) {
        send_status(); // done with task
        _timeout = default_timeout;
      }
    }
    LOG("Exiting task worker");
    disconnect_server();
    return 0;
  }
};

static const char* usage =
  "Usage:\n"
  "\ttask_worker [-v] -p <port> -w <worker_id>\n"
  "\t[-v] : log to stderr\n"
  "\t-p <port> : port of task controller\n"
  "\t-w <worker_id> : unique worker id\n"
  "\t[-s] : act as slacker\n";

int main(int argc, char** argv)
{
  char ch;
  int port = 0;
  string worker_id;
  bool to_stderr = false;
  bool is_slacker = false;
  if (argc == 0) {
    printf(usage);
    exit(0);
  }
  while ((ch = getopt(argc, argv, "hsvp:w:")) > 0) {
    switch (ch) {
    case 'h':
      printf(usage);
      exit(0);
    case 'v':
      to_stderr = true;
      break;
    case 'p': {
      port = atoi(optarg);
      if (port > MAX_PORT_NUMBER) {
        fprintf(stderr, "Invalid port number %d\n", port);
        exit(1);
      }
      break;
    }
    case 'w':
      worker_id = optarg;
      break;
    case 's':
      is_slacker = true;
      break;
    default:
      fprintf(stderr, "Invalid argument\n");
      printf(usage);
      exit(1);
    }
  }
  if (!port || worker_id.empty()) {
    printf("Invalid arguments\n");
    printf(usage);
    exit(1);
  }
  TaskWorker worker((uint16_t)port, worker_id.c_str(), to_stderr, is_slacker);
  if (worker.init() < 0) {
    return -1;
  }
  worker.run_loop();
  return 0;
}
