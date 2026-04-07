#include <iostream>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

#define NSEC_PER_SEC 1000000000ULL

// 计算时间差 (纳秒)
inline int64_t cal_diff_ns(struct timespec t1, struct timespec t2) {
    return (int64_t)(t1.tv_sec - t2.tv_sec) * NSEC_PER_SEC + (t1.tv_nsec - t2.tv_nsec);
}

int main() {
    // 1. 锁住内存，防止 Page Fault
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        std::cerr << "Warning: mlockall failed!" << std::endl;
    }

    // 2. 设置最高实时优先级
    struct sched_param param;
    param.sched_priority = 98;
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        std::cerr << "Error: Run as root!" << std::endl;
        return -1;
    }

    // 修改了打印提示信息
    std::cout << "--- Starting Jitter Test (500Hz / 2ms period) ---" << std::endl;

    struct timespec next_wakeup, now;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup);

    int64_t max_jitter = 0;
    int64_t min_jitter = 99999999;
    int64_t sum_jitter = 0;
    int loops = 0;

    // 周期设为 2ms (2,000,000 纳秒)，即 500Hz
    const int64_t interval_ns = 2000000; 

    while (true) {
        // 计算下一次应该醒来的绝对时间
        next_wakeup.tv_nsec += interval_ns;
        while (next_wakeup.tv_nsec >= NSEC_PER_SEC) {
            next_wakeup.tv_nsec -= NSEC_PER_SEC;
            next_wakeup.tv_sec++;
        }

        // 使用最高精度的绝对时间休眠
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL);

        // 醒来瞬间，立刻抓取当前真实时间
        clock_gettime(CLOCK_MONOTONIC, &now);

        // 计算延迟：真实醒来时间 - 期望醒来时间
        int64_t jitter_ns = cal_diff_ns(now, next_wakeup);

        if (jitter_ns > max_jitter) max_jitter = jitter_ns;
        if (jitter_ns < min_jitter) min_jitter = jitter_ns;
        sum_jitter += jitter_ns;
        loops++;

        // 【显示逻辑修改】每 5000 次 (10秒) 打印一次统计信息
        if (loops % 5000 == 0) {
            std::cout << "Loops: " << loops 
                      << " | Cur: " << jitter_ns / 1000 << " us"
                      << " | Avg: " << (sum_jitter / loops) / 1000 << " us"
                      << " | Max: " << max_jitter / 1000 << " us" 
                      << std::endl;
        }
    }
    return 0;
}
