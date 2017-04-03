#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class threadpool {
private:
	std::vector<std::thread> threads;
	int nthreads;
	std::mutex m;
	std::condition_variable cv;
	std::queue<void *> queue; /* bounded buffer shared resource */
	std::function<void(void *)> threadwork;

public:
	threadpool(int n, void (*func)(void *)) : nthreads(n) {
		std::cout << "Creating a threadpool with " << n << " threads.\n";
		threadwork = func;
		for (int i = 0; i < n; i++) {
			//threads.push_back(std::thread((*func), context));
			threads.push_back(std::thread(&threadpool::consumer, this));
		}
	}

	void consumer() {
		while (true) {
			std::unique_lock<std::mutex> lk(m);
			std::cout << "consumer(): queuesize = " << queue.size() << "\n";

			while (queue.size() == 0)
				cv.wait(lk, [this]{return queue.size() > 0;});
			void *tag = queue.front();
			queue.pop();
			threadwork(tag);
			lk.unlock();
		}
	}

	/* producer thread - adds to the queue */
	void addRequest(void *tag) {
		std::unique_lock<std::mutex> lk(m);
		queue.push(tag);
		cv.notify_one();
		lk.unlock();
	}
};
