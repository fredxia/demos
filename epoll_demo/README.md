# epoll_demo

**epoll_demo** is a program demonstrating client/server communication based on `epoll_wait()`.

The program has the following components:

- A sqlite3 database that store tasks to be run by clients (`task_worker` processes).

- `task_controller` is the server that loads tasks from database and dispatches 
task to worker worker processes.

- `task_worker` is the process that receives task assignment from `task_controller` 
and run the task. When the task is completed it reports back to `task_controller`, 
which may assign a new task to the `task_worker` process.

Each task is just a sleep, in number of seconds, by a worker process. A task can be
in the following states: TaskCreated, TaskRunning, TaskKilled, TaskSuccess. A python3
program `task_admin.py` is used to populate the database with randomly generated tasks.
`task_admin.py create` generate the tasks. `task_admin.py check` can check the current
status of tasks in database.

```
[root@fxia-docker epoll_demo]# ./task_admin.py -h
usage: task_admin.py [-h] {create,check} ...

positional arguments:
  {create,check}  task database commands
    create        Create tasks
    check         Check task status

optional arguments:
  -h, --help      show this help message and exit

[root@fxia-docker epoll_demo]# ./task_admin.py create -f /tmp/taskdb.db
Created 10 tasks

[root@fxia-docker epoll_demo]# ./task_admin.py check -f /tmp/taskdb.db
   Task name       Sleep time      State            Worker            Assign time           Complete time
=============================================================================================================
task_53782                 22   TaskCreated
task_21326                 26   TaskCreated
task_46785                 15   TaskCreated
task_35368                 28   TaskCreated
task_31839                  9   TaskCreated
task_5191                   1   TaskCreated
task_54760                 10   TaskCreated
task_17520                  0   TaskCreated
task_14463                  3   TaskCreated
task_37300                 10   TaskCreated

```

A `task_worker` does not have access to the sqlite3 database. `task_controller` loads from the database and
dispatches the tasks to `task_worker` workers. There can be multiple worker processes connecting to
the `task_controller`. When task is completed worker sends notification to `task_controller`, which updates
the status in database.

## Communication Protocol

`task_controller` listens on a TCP port. `task_worker` processes connect to the port. Each `task_worker` has
a unique string worker id. When connected `task_worker` sends to controller a message of
(worker id, task name, time left). The task name is empty and time left is 0 if it has no task. 

`task_controller` will look for a new task to assign to a newly connected `task_worker` that has no task
to work, by sending it a message of (task_name, sleep_time). `task_controller` also update the task
state to TaskRunning as well as the worker and assignment time in database.

If no more task to assign to a `task_worker` `task_controller` will send a message of ("", 0) to the worker
worker process, which upon receiving a message with empty task name will exit itself.

To facilitate testing a `task_worker` may be started as a slacker with `-s` option. A slacker will not finish 
the sleep in time. `task_controller` periodically check assigned tasks to see if the elapsed time is 10 seconds
more than the expected finish time. If so it will close the assigned `task_worker` connection and
update the task as TaskKilled, which makes the task available for dispatch to other worker connections.

A `task_controller` may be manually killed. This does not affect the sleep calculation of `task_worker` 
processes. The `task_worker` processes will keep trying to connect to the TCP port. When the
`task_controller` is restarted it will accept the connections but will not alter the current task
assignments.

`task_controller` periodically checks the database file to see if there are any new tasks to load.
`task_admin.py` can be used to add more tasks to the database. If the database file is removed
`task_controller` will fail to open the database and shutdown itself. The shutdown will first tell
all the `task_worker` processes to exit, and then `task_controller` itself will exit.

During the running of the `task_controller` and multiple `task_worker` processes, as well as when all is
finished, `task_admin.py` can be used to check the current status of tasks.

```
[root@fxia-docker epoll_demo]# ./task_admin.py check -f /tmp/taskdb.db
   Task name       Sleep time      State            Worker            Assign time           Complete time
=============================================================================================================
task_9501                  21   TaskSuccess        worker1        2020-10-18 16:10:26    2020-10-18 16:10:47
task_50357                 17   TaskSuccess        worker1        2020-10-18 16:10:58    2020-10-18 16:11:15
task_62705                  5   TaskSuccess        worker2        2020-10-18 16:10:30    2020-10-18 16:10:35
task_60962                  2   TaskSuccess        worker2        2020-10-18 16:10:35    2020-10-18 16:10:37
task_53046                  6   TaskSuccess        worker2        2020-10-18 16:10:37    2020-10-18 16:10:43
task_51953                 23   TaskSuccess        worker2        2020-10-18 16:10:43    2020-10-18 16:11:06
task_37478                  8   TaskSuccess        worker2        2020-10-18 16:11:06    2020-10-18 16:11:14
task_29762                  2   TaskSuccess        worker2        2020-10-18 16:11:14    2020-10-18 16:11:16
task_7043                  23   TaskSuccess        worker3        2020-10-18 16:10:28    2020-10-18 16:11:11
task_36943                 13    TaskKilled        worker3        2020-10-18 16:11:11

```

## Running Test

To run the test please following these steps:

1. Use `task_admin.py` to populate the task database, e.g.:
   ```
   ./task_admin.py create -f /tmp/taskdb.db
   ```

2. In one terminal start `task_controller` process, e.g.:
   ```
   ./task_controller -v -p 2021 -d /tmp/taskdb.db
   ```

3. In one or more terminal windows start `task_worker` processes, e.g.:
   ```
   ./task_worker -v -p 2021 -w worker_1
   ```

4. When the test is running user can do the following:
   - Kill and restart controller process
   - Kill and restart worker process, or start new worker process
   - Add more tasks to database
   - Check task status
   - Remove task database file

Each command has a few command line options that can be show by the `-h` option.
`-p` is the TCP port to listen/connect. `-d` is for the datasbase file. `-w` is
for the worker id, `-v` is to dump output to the terminal instead of a log file.
`-s` is to specify that the worker is a slacker process.

## Build Notes

The following facilities are needed to build the program on a typical Linux developer envrionment:

- gcc-4.9.2 or later
- python3, with `pip` updated
- python3 package `texttable`
- `sqlite3-devel` rpm, which will include `sqlite3` rpm


