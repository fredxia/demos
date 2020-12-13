
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string>
#include <map>
#include "util.h"
#include "server.h"

using namespace std;

#define LOG(fmt, args...) do { \
  log_message(_log_file, __FILE__, __LINE__, fmt, ##args); \
} while (0)

namespace epoll_demo {

struct TcpServerImpl {
  TcpServer* _server;
  string _server_name;
  uint16_t _server_port;
  uint32_t _timeout;    
  int _server_fd;
  int _epoll_fd;
  struct sockaddr_in _server_addr;
  map<int, epoll_event*> _conn_events;
  FILE* _log_file;
  string _log_file_name;

  TcpServerImpl(TcpServer* svr, const char* name, uint16_t port,
                uint32_t timeout, bool to_stderr)
    : _server(svr), _server_name(name), _server_port(port), _timeout(timeout),
      _server_fd(0), _epoll_fd(0) {
    if (!to_stderr) {
      char buffer[128];
      snprintf(buffer, sizeof(buffer), "/tmp/%s_XXXXXX", name);
      int fd = mkstemp(buffer);
      _log_file = fdopen(fd, "w");
      if (_log_file == nullptr) {
        fprintf(stderr, "Cannot open log file %s", buffer);
        _log_file = stderr;
        _log_file_name = "stderr";
      } else {
        _log_file_name = buffer;
      }
    } else {
      _log_file = stderr;
      _log_file_name = "stderr";
    }
  }
  
  ~TcpServerImpl() {
    if (_epoll_fd) {
      close(_epoll_fd);
      _epoll_fd = 0;
    }
    for (auto it : _conn_events) {
      close(it.first);
      delete it.second;
    }
    _conn_events.clear();
    if (_server_fd) {
      close(_server_fd);
      _server_fd = 0;
    }
    if (_log_file && _log_file != stderr) {
      fclose(_log_file);
      _log_file = nullptr;
    }
  }

  int init_server() {
    assert(_server_fd == 0);
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
      return -1;
    }
    int yes = 1;
    int r = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (r < 0) {
      LOG("Error in setsockopt(): %s", strerror(errno));
      return -1;
    }
    r = set_fd_non_block(sock_fd);
    if (r < 0) {
      LOG("Error in set_fd_non_block(): %s", strerror(errno));
      return -1;
    }
    memset(&_server_addr, 0, sizeof(_server_addr));
    _server_addr.sin_port = htons(_server_port);
    _server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    r = ::bind(sock_fd, (struct sockaddr*)&_server_addr, sizeof(_server_addr));
    if (r < 0) {
      LOG("Error in bind(): %s", strerror(errno));
      return -1;
    }
    r = ::listen(sock_fd, 5);
    if (r < 0) {
      LOG("Error in listen(): %s", strerror(errno));
      return -1;
    }
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
      LOG("Error in epoll_create1(): %s", strerror(errno));
      return -1;
    }
    struct epoll_event ev;
    ev.data.fd = sock_fd;
    ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
    r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev);
    if (r < 0) {
      LOG("Error in epoll_ctl(): %s", strerror(errno));
      return -1;
    }
    LOG("Server port initialized: %d", _server_port);
    _server_fd = sock_fd;
    _epoll_fd = epoll_fd;
    return 0;
  }

  int run_loop() {
    while (true) {
      size_t max_events = _conn_events.size() + 1;
      struct epoll_event events[max_events];
      memset((char*)events, 0, sizeof(events));
      int r = epoll_wait(_epoll_fd, events, max_events, _timeout);
//      LOG("epoll wait: %d", r);
      if (r == 0) {
        if (_server->handle_timeout(true) != 0) {
          break; // server exit
        }
      } else {
        for (int i = 0; i < r; i++) {
          if (events[i].data.fd == _server_fd) {
            handle_server_fd(events[i]);
          } else {
            handle_connection(events[i]);
          }
        }
        if (_server->handle_timeout(false) != 0) {
          break;
        }
      }
    }
    return 0;
  }

  int handle_server_fd(const epoll_event& event) {
    socklen_t len = sizeof(_server_addr);
    LOG("handle_server_fd");
    while (true) {
      int fd = accept(_server_fd, (struct sockaddr*)&_server_addr, &len);
      if (fd < 0) {
        if (errno == EAGAIN) {
          break;
        }
        LOG("Error in accept(): %s", strerror(errno));
        return -1;
      }
      if (set_fd_non_block(fd) < 0) {
        LOG("Error in set_fd_non_block(): %s", strerror(errno));
        return -1;
      }
      uint32_t what_to_do = _server->handle_new_connection(fd);
      if (what_to_do == 0) {
        LOG("Connection rejected");
        close(fd);
        continue;
      }
      struct epoll_event ev;
      ev.events = what_to_do;
      ev.data.fd = fd;
      if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG("Error in epoll_ctl(): %s", strerror(errno));
        return -1;
      }
      struct epoll_event* client_ev = new epoll_event;
      memcpy(client_ev, &ev, sizeof(ev));
      _conn_events[fd] = client_ev;
      LOG("Added connection %d, %x", fd, what_to_do);
    }
    return 0;
  }

  int handle_connection(const epoll_event& event) {
    uint32_t what_to_do = _server->handle_connection(event);
    auto it = _conn_events.find(event.data.fd);
    assert(it != _conn_events.end());
    if (what_to_do == 0) {
      // close connection
      LOG("Close connection %d", event.data.fd);
      epoll_event* ev = it->second;
      _conn_events.erase(it);
      epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, ev->data.fd, ev);
      close(ev->data.fd);
      delete ev;
    } else {
      if (what_to_do != it->second->events) {
        it->second->events = what_to_do;
        if (epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, it->first, it->second) < 0) {
          LOG("Error epoll_ctl(): %s", strerror(errno));
          return -1;
        }
      }
    }
    return 0;
  }
};

TcpServer::TcpServer(const char* name, uint16_t port, uint32_t timeout,
                     bool to_stderr)
{
  impl = new TcpServerImpl(this, name, port, timeout, to_stderr);
}

TcpServer::~TcpServer()
{
  if (impl) {
    delete impl;
    impl = nullptr;
  }
}

const char* TcpServer::server_name() const
{
  return impl->_server_name.c_str();
}

uint16_t TcpServer::server_port() const
{
  return impl->_server_port;
}

int TcpServer::server_fd() const
{
  return impl->_server_fd;
}

FILE* TcpServer::log_file()
{
  return impl->_log_file;
}

const string& TcpServer::log_file_name() const
{
  return impl->_log_file_name;
}

void TcpServer::set_timeout(uint32_t timeout)
{
  impl->_timeout = timeout;
}

int TcpServer::run_loop()
{
  if (impl->init_server() < 0) {
    return -1;
  }
  return impl->run_loop();
}

}
