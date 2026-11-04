#include "mouse_bridge/cadence.hpp"

#include <cassert>

int main() {
    using mouse_bridge::CadenceEstimator;
    using mouse_bridge::sanitize_poll_interval_us;

    assert(sanitize_poll_interval_us(0) == 1'000);
    assert(sanitize_poll_interval_us(500) == 1'000);
    assert(sanitize_poll_interval_us(7'500) == 7'500);
    assert(sanitize_poll_interval_us(20'000) == 8'000);

    CadenceEstimator estimator;
    assert(estimator.observe(1'000) == 0);
    assert(estimator.observe(2'000) == 1'000);
    assert(estimator.observe(3'050) == 1'000);
    assert(estimator.observe(3'950) == 1'000);

    estimator.reset();
    assert(estimator.observe(10'000) == 0);
    assert(estimator.observe(18'000) == 8'000);
    // A faster setting takes effect immediately.
    assert(estimator.observe(19'000) == 1'000);

    estimator.reset();
    assert(estimator.observe(100'000) == 0);
    assert(estimator.observe(101'000) == 1'000);
    // One long sample may be a dropped report, two establish a slower rate.
    assert(estimator.observe(109'000) == 1'000);
    assert(estimator.observe(117'000) == 8'000);
}
