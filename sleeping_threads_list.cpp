#include <iostream>
#include "sleeping_threads_list.h"

SleepingThreadsList::SleepingThreadsList() {
}


/*
 * Description: This method adds a new element to the list of sleeping
 * threads. It gets the thread's id, and the time when it needs to wake up.
 * The wakeup_tv is a struct timeval (as specified in <sys/time.h>) which
 * contains the number of seconds and microseconds since the Epoch.
 * The method keeps the list sorted by the threads' wake up time.
*/
void SleepingThreadsList::add(Thread *t) {

	if(sleeping_threads.empty()){
		sleeping_threads.push_front(t);
	}
	else {
		for (auto it = sleeping_threads.begin(); it != sleeping_threads.end(); ++it){
			if(timercmp((*it)->get_time_val(), t->get_time_val(), >)){
				sleeping_threads.insert(it, t);
				return;
			}
		}
		sleeping_threads.push_back(t);
	}
}

/*
 * Description: This method removes the thread at the top of this list.
 * If the list is empty, it does nothing.
*/
void SleepingThreadsList::pop() {
	if(!sleeping_threads.empty())
		sleeping_threads.pop_front();
}

/*
 * Description: This method returns the informati
 * on about the thread (id and time it needs to wake up)
 * at the top of this list without removing it from the list.
 * If the list is empty, it returns null.
*/
Thread* SleepingThreadsList::peek(){
	if (sleeping_threads.empty())
		return nullptr;
	return sleeping_threads.at(0);
}

/*
 * returns the second element in the list if available.
 */
Thread *SleepingThreadsList::get_second() {
	if (sleeping_threads.size() < 2)
		return nullptr;
	return sleeping_threads.at(1);
}

bool SleepingThreadsList::empty() {
	return sleeping_threads.empty();
}

void SleepingThreadsList::remove_thread(int id) {
	for (auto it = sleeping_threads.begin();it != sleeping_threads.end();++it)
	{
		if (id  == (*it)->get_id())
		{
			sleeping_threads.erase(it);
			return;
		}
	}

}

unsigned long SleepingThreadsList::size()
{
	return sleeping_threads.size();
}


