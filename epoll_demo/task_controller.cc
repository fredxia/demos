//
// Fred Xia (fxia@yahoo.com)
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include "util.h"
#include "server.h"
#include "task_db.h"

using namespace std;
using namespace epoll_demo;

#define LOG(fmt, args...) do { \
   log_message(log_file(), __FILE__, __LINE__, fmt, ##args); \
} while (0)

// Default timeout for epoll_wait is 10 seconds
static const uint32_t default_timeout = 10000;

struct TaskController : public TcpServer {

  Taskdb _task_db;
  TaskCollection _tasks;
  map<int, string> _workers; // fd => worker_id
  bool _shutdown; // shutdown flag. Set when database is gone.

  TaskController(const char* db, uint16_t port, bool to_stderr)
    : TcpServer("controller", port, default_timeout, to_stderr),
      _task_db(db, log_file()), _shutdown(false)
    {}

  virtual ~TaskController() {
    for (auto it : _tasks) {
      delete it.second;
    }
    _tasks.clear();
    _workers.clear();
  }

  int init() {
    int r = _task_db.fetch_tasks(_tasks);
    if (r <= 0) {
      if (r == 0) {
        LOG("No tasks to run");
      }
      return -1;
    }
    return 0;
  }

  // Shutdown flag can be turned on during message processing. We don't want
  // to shutdown in the middle of processing to avoid data inconsistency.
  // Actual shutdown is performed in handle_timeout()
  void shutdown() {
    LOG("Database error. Shutdown everything...");
    _shutdown = true;
  }

  // Disconnect a worker client. If any task assigned to the worker mark it as
  // TaskKilled. If to_exit tell worker to exit by sending the message with an
  // empty task name.
  void disconnect_client(int fd, bool to_exit) {
    auto it = _workers.find(fd);
    if (it != _workers.end()) {
      auto worker_id = it->second;
      for (auto task_it : _tasks) {
        if (task_it.second->worker == worker_id) {
          task_it.second->state = TaskKilled;
          if (_task_db.update_task_db(task_it.second) < 0) {
            shutdown();
          } else {
            LOG("Change task %s state to TaskKilled",
                task_it.second->task_name.c_str());
          }
          break;
        }
      }
      _workers.erase(it);
    }
    if (to_exit) {
      // tell worker to exit
      uint32_t msg_len;
      char* msg = serialize_server_message("", 0, msg_len);
      if (msg) {
        ::write(fd, msg, msg_len);
        free(msg);
        LOG("Send close to worker fd %d", fd);
      }
    }
  }

  // Dispatch a task to a worker. If there is an unfinished task previously
  // assigned to the worker redispatch it. Otherwise find a new task to
  // dispatch. If no more new tasks tell the worker to exit.
  uint32_t dispatch_task(int fd) {
    auto worker_it = _workers.find(fd);
    string worker_id = worker_it->second;
    assert(worker_it != _workers.end());
    Task* t = nullptr;
    bool previous_task = false;
    for (auto it : _tasks) {
      if (it.second->worker == worker_id &&
          (it.second->state == TaskRunning ||
           it.second->state == TaskKilled)) {
        t = it.second;
        break;
      } if (it.second->state == TaskCreated ||
            it.second->state == TaskKilled) {
        t = it.second;
      }
    }
    if (t == nullptr) {
      LOG("No more task for %s to work on", worker_id.c_str());
      disconnect_client(fd, true);
      return 0;
    }
    if (t->worker == worker_id) {
      previous_task = true;
    }
    uint32_t msg_len;
    char* msg = serialize_server_message(t->task_name.c_str(),
                                         t->sleep_time,
                                         msg_len);
    if (!msg) {
      return 0;
    }
    int r = ::write(fd, msg, msg_len);
    if (r < 0) {
      LOG("Error in write(): %s", strerror(errno));
      free((void*)msg);
      disconnect_client(fd, false);
      return 0;
    }
    free((void*)msg);
    t->worker = worker_id;
    t->state = TaskRunning;
    t->assign_time = time(0);
    if (_task_db.update_task_db(t) < 0) {
      shutdown();
    } else {
      if (previous_task) {
        LOG("Re-dispatch previous task %s to worker %s",
            t->task_name.c_str(), worker_id.c_str());
      } else {
        LOG("Dispatch new task %s to %s",
            t->task_name.c_str(), t->worker.c_str());
      }
    }
    return EPOLLIN | EPOLLHUP| EPOLLET;
  }

  int handle_timeout(bool is_timeout) {
    LOG("epoll timeout %d", is_timeout);
    if (is_timeout) {
      // Check demo database sanity
      sqlite3* db = _task_db.open_task_db();
      if (!db) {
        _shutdown = true;
      } else {
        // Check workers that may be slacking off. We consider worker not
        // responsive if the elapse time is more than 10 seconds after
        // expected task finish time
        time_t current_time = time(0);
        for (auto task_it : _tasks) {
          Task* t = task_it.second;
          if (t->state != TaskRunning) {
            continue;
          }

          uint32_t elapse_time = (uint32_t)(current_time - t->assign_time);
          // LOG("Check slacker %s, task %s, sleep time %d, elapse time %d...",
          //     t->worker.c_str(), t->task_name.c_str(), t->sleep_time,
          //     elapse_time);
          if (elapse_time > t->sleep_time + 10) {
            int fd = 0;
            for (auto it : _workers) {
              if (it.second == t->worker) {
                fd = it.first;
                break;
              }
            }
            if (fd) {
              LOG("Close off slacker %s", t->worker.c_str());
              disconnect_client(fd, true);
            } else {
              // slacker is gone, just update database
              LOG("Update task %s state to TaskKilled", t->task_name.c_str());
              t->state = TaskKilled;
              if (_task_db.update_task_db(t) < 0) {
                shutdown();
              }
            }
          }
        }
        // Load more tasks
        if (_task_db.fetch_tasks(_tasks) < 0) {
          shutdown();
        }
      }
    }
    if (_shutdown) {
      for (auto worker : _workers) {
        disconnect_client(worker.first, true);
      }
    }
    if (_shutdown || _tasks.size() == 0) {
      return 1; // no more work, shutdown
    }
    return 0;
  }

  uint32_t handle_client_message(int fd) {
    LOG("handle_client_message %d", fd);
    uint32_t msg_len;
    int r = ::read(fd, (char*)&msg_len, sizeof(msg_len));
    if (r != sizeof(uint32_t)) {
      LOG("Error in read client message header: %d", r);
      disconnect_client(fd, false);
      return 0;
    }
    if (msg_len > MAX_CLIENT_MSG_LEN) {
      LOG("Error in client message len %u", msg_len);
      disconnect_client(fd, false);
      return 0;
    }
    uint32_t body_len = msg_len - sizeof(msg_len);    
    char msg[body_len];
    r = ::read(fd, msg, body_len);
    if (r != (int)body_len) {
      LOG("Error in read client msg: %d", r);
      disconnect_client(fd, false);
      return 0;
    }
    string worker;
    string task_name;
    uint32_t time_left;
    if (deserialize_client_message(msg,
                                   body_len,
                                   worker,
                                   task_name,
                                   time_left) < 0) {
      LOG("Error in deserialize_client_message");
      disconnect_client(fd, false);
      return 0;
    }
    auto worker_it = _workers.find(fd);
    if (worker_it == _workers.end()) {
      _workers[fd] = worker;
    }
    if (task_name == "") {
      // Worker requests new task
      return dispatch_task(fd);
    }
    auto task_it = _tasks.find(task_name);
    if (task_it == _tasks.end()) {
      LOG("Error: cannot find task %s", task_name.c_str());
      disconnect_client(fd, false);
      return 0;
    }
    Task* t = task_it->second;
    if (t->worker != worker) {
      LOG("Error: invalid worker %s for task %s, was %s",
          worker.c_str(), task_name.c_str(), t->worker.c_str());
      disconnect_client(fd, false);
      return 0;
    }
    if (time_left == 0) {
      t->state = TaskSuccess;
      t->complete_time = time(0);
      if (_task_db.update_task_db(t) < 0) {
        shutdown();
      }
      _tasks.erase(task_it);
      delete t;
      return dispatch_task(fd);
    }
    // a reconnect from client. update task state to running
    LOG("Reconnected to worker %s, task %s",
        worker.c_str(), task_name.c_str());
    t->state = TaskRunning;
    if (_task_db.update_task_db(t) < 0) {
      shutdown();
    }
    return EPOLLIN | EPOLLHUP | EPOLLET;
  }

  virtual uint32_t handle_new_connection(int fd) {
    LOG("handle_new_connection %d", fd);
    if (_shutdown) {
      LOG("Shutdown scheduled");
      disconnect_client(fd, true);
    }
    // Wait for worker to initiate handshake
    return EPOLLIN | EPOLLHUP | EPOLLET;
  }

  virtual uint32_t handle_connection(const epoll_event& ev) {
    // We never turn on EPOLLOUT since controller always write out immediately
    // after every read
    int fd = ev.data.fd;    
    if (_shutdown) {
      disconnect_client(fd, true);
      return 0;
    }
    if (ev.events & EPOLLIN) {
      return handle_client_message(fd);
    }
    if (ev.events & EPOLLHUP) {
      disconnect_client(fd, false);
      return 0;
    }
    return 0;
  }
};

static const char* usage = "Usage:\n"
  "task_controller [-v] -p <port> -d <database>\n"
  "\t[-v] : Log to stderr instead of log file\n"
  "\t-p <port> : Listening port\n"
  "\t-d <database> : Task database file\n";

int main(int argc, char** argv)
{
  char ch;
  int port = 0;
  string db_name;
  bool to_stderr = false;
  if (argc == 0) {
    printf(usage);
    exit(0);
  }
  while ((ch = getopt(argc, argv, "hvp:d:")) > 0) {
    switch (ch) {
    case 'h':
      printf(usage);
      exit(0);
    case 'p': {
      port = atoi(optarg);
      if (port > MAX_PORT_NUMBER) {
        fprintf(stderr, "Invalid port number %d\n", port);
        exit(1);
      }
      break;
    }
    case 'd': {
      db_name = optarg;
      struct stat statBuf;
      if (stat(db_name.c_str(), &statBuf) < 0) {
        fprintf(stderr, "Database file does not exist: %s\n", db_name.c_str());
        exit(1);
      }
      break;
    }
    case 'v':
      to_stderr = true;
      break;
    }
  }
  if (!port || db_name.empty()) {
    printf("Invalid arguments\n");
    printf(usage);
    exit(1);
  }
  TaskController controller(db_name.c_str(), port, to_stderr);
  fprintf(stderr, "Controller log file is %s\n", controller.log_file_name().c_str());
  if (controller.init() < 0) {
    fprintf(stderr, "Controller initialization failed\n");
    exit(1);
  }
  controller.run_loop();
  return 0;
}
