//
// Fred Xia (fxia@yahoo.com)
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include "task_db.h"

using namespace std;

#define LOG(fmt, args...) do { \
   log_message(_log_file, __FILE__, __LINE__, fmt, ##args); \
} while (0)

namespace epoll_demo {

sqlite3* Taskdb::open_task_db()
{
  sqlite3* db{nullptr};
  int rc = sqlite3_open_v2(_db_name.c_str(), &db, SQLITE_OPEN_READWRITE, 0);
  if (rc != SQLITE_OK) {
    LOG("Cannot open database %s: %s", _db_name.c_str(), sqlite3_errmsg(db));
    sqlite3_close(db);
    return nullptr;
  }
  return db;
}

int Taskdb::fetch_tasks(TaskCollection& tasks)
{
  static const char* sql = "select * from demo_task where state != 3";
  sqlite3* db = open_task_db();
  if (db == nullptr) {
    return -1;
  }
  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    LOG("Error: prepare sql '%s': %s", sql, sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return -1;
  }
  int count = 0;
  rc = sqlite3_step(stmt);
  while (rc == SQLITE_ROW) {
    string task_name = (char*)sqlite3_column_text(stmt, 0);
    if (tasks.find(task_name) == tasks.end()) {
      Task* task = new Task();
      task->task_name = task_name;
      task->sleep_time = (uint32_t)sqlite3_column_int(stmt, 1);
      task->state = (TaskState)sqlite3_column_int(stmt, 2);
      task->worker = (char*)sqlite3_column_text(stmt, 3);
      task->assign_time = (uint64_t)sqlite3_column_int64(stmt, 4);
      task->complete_time = 0;
      tasks[task->task_name] = task;
      count++;
    }
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  LOG("Loaded %d new tasks, total count %d", count, (int)tasks.size());
  return count;
}

int Taskdb::update_task_db(const Task* task)
{
  static const char* running_sql =
    "update demo_task set state = 1, worker = ?, assign_time = ? "
    "where task_name = ?";
  static const char* kill_sql =
    "update demo_task set state = 2 where task_name = ?";
  static const char* complete_sql =
    "update demo_task set state = 3, complete_time = ? "
    "where task_name = ?";
  sqlite3* db = open_task_db();
  if (db == nullptr) {
    return -1;
  }
  const char* sql;
  switch (task->state) {
  case TaskRunning:
    sql = running_sql;
    break;
  case TaskKilled:
    sql = kill_sql;
    break;
  case TaskSuccess:
    assert(task->complete_time >= task->assign_time);
    sql = complete_sql;
    break;
  default:
    LOG("Invalid update state");
    return -1;
  }
  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    LOG("Error: prepare sql '%s': %s", sql, sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return -1;
  }
  switch (task->state) {
  case TaskRunning:
    sqlite3_bind_text(stmt, 1, task->worker.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, task->assign_time);
    sqlite3_bind_text(stmt, 3, task->task_name.c_str(), -1, SQLITE_STATIC);
    break;
  case TaskKilled:
    sqlite3_bind_text(stmt, 1, task->task_name.c_str(), -1, SQLITE_STATIC);
    break;
  case TaskSuccess:
    sqlite3_bind_int64(stmt, 1, task->complete_time);
    sqlite3_bind_text(stmt, 2, task->task_name.c_str(), -1, SQLITE_STATIC);
    break;
  default:
    // Should not hit here. Avoid compiler warning
    assert(false);
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return -1;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}

}
