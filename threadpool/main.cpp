#include <iostream>
#include <thread>
#include <functional>
#include <map>
#include <chrono>
#include <vector>
#include <mutex>

using namespace std;

std::mutex TASKS_ACCESS_MUTEX;
int RANDOM_VALUE = 10;

struct ThreadInfo
{
	ThreadInfo()
		: m_isIdle(false)
		, m_isRunning(false)
		, m_isMarkedForDeletion(false)
	{}
	bool m_isIdle;
	bool m_isRunning;
	bool m_isMarkedForDeletion;
	std::thread* m_actualThread;
};

bool isAtLeastOneThreadRunning(const map<thread::id, ThreadInfo>& threadsMap)
{
	for (map<thread::id, ThreadInfo>::const_iterator iter = threadsMap.begin(); iter != threadsMap.end(); ++iter) {
		thread::id keyThreadId = iter->first;
		ThreadInfo valueThreadInfo = iter->second;
		if (valueThreadInfo.m_isRunning) {
			return true;
		}
	}
	return false;
}

void taskFunction()
{
	this_thread::sleep_for(chrono::seconds(RANDOM_VALUE));
}

class TaskExecutor
{
public:
	TaskExecutor()
		: m_orchestratorThread(std::bind(&TaskExecutor::orchestratorFunction, this))
		, m_pollingThread(std::bind(&TaskExecutor::pollingFunction, this))
	{
	}
	~TaskExecutor()
	{
		m_orchestratorThread.join();
		m_pollingThread.join();
	}

	void addTask(std::function<void(void)> functionTask)
	{
		TASKS_ACCESS_MUTEX.lock();
		m_listOfFunctionTasks.emplace_back(functionTask);
		TASKS_ACCESS_MUTEX.unlock();
		std::cout << "Task list size: " << m_listOfFunctionTasks.size() << std::endl;
	}

private:
	void pollingFunction()
	{
		this_thread::sleep_for(chrono::milliseconds(3000)); // lazy start
		while (m_listOfFunctionTasks.size() > 0 || m_threadPool.size() > 0) {
			if (m_threadPool.size() != 0) {
				while (isAtLeastOneThreadRunning(m_threadPool)) {
					std::cout << "__________________________________________" << std::endl;
					std::cout << "                                          " << std::endl;
					std::cout << "------------- THREADS STATUS -------------" << std::endl;
					std::cout << "__________________________________________" << std::endl;
					for (map<thread::id, ThreadInfo>::const_iterator iter = m_threadPool.begin(); iter != m_threadPool.end(); ++iter) {
						const thread::id keyThreadId = iter->first;
						const ThreadInfo valueThreadInfo = iter->second;
						std::cout << "======= Thread " << keyThreadId << " is running: " << valueThreadInfo.m_isRunning << endl;
					}
					std::cout << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^" << std::endl << std::endl;

					this_thread::sleep_for(chrono::milliseconds(500));
				}
			}
		}
	}
	void orchestratorFunction()
	{
		this_thread::sleep_for(chrono::milliseconds(3000)); // lazy start
		while (m_listOfFunctionTasks.size() > 0 || m_threadPool.size() > 0) {
			if (m_listOfFunctionTasks.size() > 10) { // create thread if too many tasks
				std::thread* runningThread = new std::thread(&TaskExecutor::runningFunction, this);
				ThreadInfo info;
				info.m_actualThread = runningThread;
				m_threadPool[runningThread->get_id()] = info;
				std::cout << "------- THREAD LIST SIZE: " << m_threadPool.size() << std::endl;
				runningThread->detach();
			}
			if (m_listOfFunctionTasks.size() < 5) { // mark a tread for deletion if too few pending tasks
				bool alreadyMarkedOneForDeletion = false;

				std::map<std::thread::id, ThreadInfo>::iterator it = m_threadPool.begin();
				while (!alreadyMarkedOneForDeletion && it != m_threadPool.end()) {
					const thread::id keyThreadId = it->first;
					if (it->second.m_isMarkedForDeletion == false) {
						std::cout << "--------- XXXXXXXXXXX --------- Marking thread for deletion: " << keyThreadId << std::endl;
						it->second.m_isMarkedForDeletion = true;
						alreadyMarkedOneForDeletion = true;
					}
					it++;
				}
			}
			// delete threads that are marked for deletion
			map<std::thread::id, ThreadInfo>::iterator iter = m_threadPool.begin();
			map<std::thread::id, ThreadInfo>::iterator endIter = m_threadPool.end();
			for (; iter != endIter;) {
				const thread::id keyThreadId = iter->first;
				const ThreadInfo valueThreadInfo = iter->second;
				if (valueThreadInfo.m_isMarkedForDeletion && valueThreadInfo.m_isRunning == false) {
					std::cout << "Deleting thread " << keyThreadId << std::endl;
					delete valueThreadInfo.m_actualThread;
					iter = m_threadPool.erase(iter);
					std::cout << "------- THREAD LIST SIZE: " << m_threadPool.size() << std::endl;
				}
				else {
					++iter;
				}
			}
			this_thread::sleep_for(chrono::milliseconds(1000));
		}
	}
	void runningFunction()
	{
		const thread::id threadId = this_thread::get_id();
		std::cout << "Thread started " << threadId << endl;
		std::function<void(void)> functionToCall;
		while (!m_threadPool[threadId].m_isRunning && !m_threadPool[threadId].m_isMarkedForDeletion) {
			while (m_listOfFunctionTasks.size() == 0) // wait until the list is not empty
			{
			}
			TASKS_ACCESS_MUTEX.lock();
			if (m_listOfFunctionTasks.size() != 0) { // this is not a redundant if statement, the list can be emptied before the lock from the other thread!
				functionToCall = m_listOfFunctionTasks[0];
				m_listOfFunctionTasks.erase(m_listOfFunctionTasks.begin());
				std::cout << "Task list size: " << m_listOfFunctionTasks.size() << std::endl;
			}
			TASKS_ACCESS_MUTEX.unlock();
			if (functionToCall) { // only if the function was extracted from the vector
				m_threadPool[threadId].m_isRunning = true;
				functionToCall(); // this calls taskFunction
				m_threadPool[threadId].m_isRunning = false;
			}
		}
		std::cout << ">>>>>>>>> XXXXXXXXXXX >>>>>>>>>> Thread ended " << threadId << endl;
	}

	std::thread m_orchestratorThread;
	std::thread m_pollingThread;

	std::vector<std::function<void(void)>> m_listOfFunctionTasks;
	map<thread::id, ThreadInfo> m_threadPool;
};

int main()
{
	srand(time(NULL));
	TaskExecutor taskExecutor;

	std::thread randomValueRandomizerThread([]() {
		while (true) {
			RANDOM_VALUE = rand() % 10 + 10;
		}
	});

	auto taskCreatorFunction = [&taskExecutor] {
		int count = 0;
		while (count++ < 20) {
			taskExecutor.addTask(taskFunction);
			this_thread::sleep_for(chrono::milliseconds(1000));
		}
		};
	std::thread tasksCreatorThread(taskCreatorFunction);

	tasksCreatorThread.join();
	randomValueRandomizerThread.detach();

	return 0;
}
