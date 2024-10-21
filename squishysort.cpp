#include <iostream>
#include <vector>
#include <thread>
#include <future>
#include <algorithm>
#include <functional>
#include <iterator>
#include <type_traits>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <random>
#include <chrono>

class ThreadPool {
public:
    ThreadPool(size_t threads = std::thread::hardware_concurrency()) : stop(false) {
        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                for(;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this]{ return stop || !tasks.empty(); });
                        if(stop && tasks.empty())
                            return;
                        task = std::move(tasks.front());
                        tasks.pop_front();
                    }
                    task();
                }
            });
    }

    template<class F>
    auto enqueue(F&& f) -> std::future<typename std::invoke_result<F>::type> {
        using return_type = typename std::invoke_result<F>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(f));
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace_back([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker : workers)
            worker.join();
    }

    size_t thread_count() const {
        return workers.size();
    }

private:
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

template<typename RandomIt>
void parallel_sort_internal(RandomIt first, RandomIt last, ThreadPool& pool, size_t depth_limit) {
    const size_t min_per_thread = 10000;
    size_t len = std::distance(first, last);

    if(len <= min_per_thread || depth_limit == 0) {
        std::sort(first, last);
    } else {
        RandomIt mid = first;
        std::advance(mid, len / 2);

        auto future = pool.enqueue([=, &pool]() mutable {
            parallel_sort_internal(first, mid, pool, depth_limit - 1);
        });

        parallel_sort_internal(mid, last, pool, depth_limit - 1);
        future.get();
        std::inplace_merge(first, mid, last);
    }
}

template<typename RandomIt>
void parallel_sort(RandomIt first, RandomIt last) {
    ThreadPool pool;
    size_t max_depth = std::log2(pool.thread_count()) + 4;
    parallel_sort_internal(first, last, pool, max_depth);
}

int main() {
    size_t N = 10000000;
    std::vector<int> data(N);

    std::iota(data.begin(), data.end(), 0);

    std::mt19937 gen(std::random_device{}());
    std::shuffle(data.begin(), data.end(), gen);

    auto start = std::chrono::high_resolution_clock::now();
    parallel_sort(data.begin(), data.end());
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> parallel_duration = end - start;

    if(std::is_sorted(data.begin(), data.end())) {
        std::cout << "Parallel sort succeeded in " << parallel_duration.count() << " seconds." << std::endl;
    } else {
        std::cout << "Parallel sort failed!" << std::endl;
    }

    return 0;
}
