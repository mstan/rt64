//
// RT64
//

#include <cmath>
#include <thread>

#include "rt64_timer.h"

namespace RT64 {
    // Timer

    void Timer::initialize() {
    }

    Timestamp Timer::current() {
        return std::chrono::high_resolution_clock::now();
    }

    int64_t Timer::deltaMicroseconds(const Timestamp t1, const Timestamp t2) {
        return std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    }

    void Timer::preciseSleepUntil(const Timestamp endTime) {
        using clock = std::chrono::high_resolution_clock;

        // Work in fractional seconds for the running estimate; bail out if the
        // deadline is already behind us.
        const double secondsLeft0 = std::chrono::duration<double>(endTime - clock::now()).count();
        if (secondsLeft0 <= 0.0) {
            return;
        }
        double secondsLeft = secondsLeft0;

        // Length of each coarse sleep request.
        constexpr int64_t coarseSleepNanos = 100'000;
        // Coarse sleeps that overrun this are treated as outliers and excluded
        // from the running statistics (expressed in seconds).
        constexpr double outlierSeconds = 2'000'000 / 1'000'000'000.0;
        // How many standard deviations of headroom the estimate carries.
        constexpr double sigmaMargin = 2.0;
        constexpr double coarseSleepSeconds = coarseSleepNanos / 1'000'000'000.0;

        // Welford accumulators for the observed coarse-sleep duration. They are
        // thread_local so the estimate keeps improving across successive calls.
        thread_local double estimate = coarseSleepSeconds;
        thread_local double runningMean = coarseSleepSeconds;
        thread_local double sumSqDiff = 0.0;
        thread_local uint64_t sampleCount = 1;

        // While there is comfortably more than one coarse sleep left to wait,
        // keep sleeping and fold each measured duration back into the estimate.
        Timestamp mark = clock::now();
        while (secondsLeft > estimate) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(coarseSleepNanos));

            const Timestamp woke = clock::now();
            const double observed = std::chrono::duration<double>(woke - mark).count();
            mark = woke;

            secondsLeft -= observed;

            // Fold well-behaved samples into the running mean/variance and refresh
            // the estimate; skip pathological scheduling stalls.
            if (observed < outlierSeconds) {
                ++sampleCount;
                const double diff = observed - runningMean;
                runningMean += diff / sampleCount;
                sumSqDiff += diff * (observed - runningMean);
                const double stddev = sqrt(sumSqDiff / (sampleCount - 1));
                estimate = runningMean + sigmaMargin * stddev;
            }
        }

        // Burn the final sub-sleep slice on a busy wait for tight accuracy.
        while (clock::now() < endTime) {
        }
    }
};
