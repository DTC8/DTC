/*
* Copyright [2021] JD.com, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
* 
*/
#include <fcntl.h>
#include <errno.h>

#include "base.h"
#include "log/log.h"
#include "proc_title.h"

extern int watchdog_stop;
extern int recovery_mode;

WatchDogDaemon::WatchDogDaemon(WatchDog *watchdog, int sec)
	: WatchDogObject(watchdog)
{
	if (watchdog)
		timer_list_ = watchdog->get_timer_list(sec);
}

WatchDogDaemon::~WatchDogDaemon(void)
{
}

int WatchDogDaemon::new_proc_fork()
{
	/* an error detection pipe */
	int err, fd[2];
	int unused;

	unused = pipe(fd);

	/* fork child process */
	watchdog_object_pid_ = fork();
	if (watchdog_object_pid_ == -1)
		return watchdog_object_pid_;
	if (watchdog_object_pid_ == 0) {
		// child process in.
		exec();
		err = errno;
		log4cplus_info("%s: exec(): %m", watchdog_object_name_);
		exit(-1);
	}

	// parent process in.
	attach_watch_dog();
	return watchdog_object_pid_;
}

void WatchDogDaemon::job_timer_procedure()
{
	log4cplus_debug("enter timer procedure");
	if (new_proc_fork() < 0)
		if (timer_list_)
			attach_timer(timer_list_);
	log4cplus_debug("leave timer procedure");
}

void WatchDogDaemon::killed_notify(int signo, int coredumped)
{
	if (recovery_mode)
	{
		sleep(2);
		new_proc_fork();
	}
	return;
}

void WatchDogDaemon::exited_notify(int retval)
{
	if (recovery_mode)
	{
		sleep(2);
		new_proc_fork();
	}
	return;
}