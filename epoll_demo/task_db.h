#ifndef __task_db_h__
#define __task_db_h__
//
// Fred Xia (fxia@yahoo.com)
//

#include <stdint.h>
#include <time.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string>
#include <map>

namespace epoll_demo {

enum TaskState {
  TaskCreated,
  TaskRunning,
  TaskKilled,
  TaskSuccess
};

struct Task {
  std::string   task_name;
  uint32_t      sleep_time;
  TaskState     state;
  std::string   worker;
  time_t        assign_time;
  time_t        complete_time;
};

// task_name => task
typedef std::map<std::string, Task*> TaskCollection;

class Taskdb {
public:
  Taskdb(const char* db_file_name, FILE* log_file)
    : _db_name(db_file_name), _log_file(log_file)
  {}
    
  ~Taskdb()
  {}

  // open a database 
  sqlite3* open_task_db();

  // Fetch unfinished tasks from database and load into tasks
  // Returns number of new tasks loaded, or -1 if error
  int fetch_tasks(TaskCollection& tasks);

  // Update task information in database. Returns 0 for success
  // -1 for failure
  int update_task_db(const Task* task);

private:
  std::string _db_name;
  FILE* _log_file;
};

}

#endif
