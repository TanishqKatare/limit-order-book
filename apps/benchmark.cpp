// Performance benchmark harness for the matching engine. Drives a
// cancel-dominated mixed workload and reports throughput plus per-op
// latency percentiles. Must be built in Release (-O3 -DNDEBUG) to be
// meaningful.

#include "lob/MatchingEngine.hpp"
#include "lob/OrderBook.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

// std::chrono::steady_clock on MinGW quantises to ~1µs on this box,
// which can't resolve sub-µs ops. QPC gives ~100ns.
#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
namespace bench_clock {
inline std::int64_t qpc_freq() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}
inline std::int64_t now_ticks() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
inline std::int64_t ticks_to_ns(std::int64_t ticks) {
    static const std::int64_t f = qpc_freq();
    return (ticks * 1'000'000'000LL) / f;
}
} // namespace bench_clock
#else
  #include <time.h>
namespace bench_clock {
inline std::int64_t now_ticks() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}
inline std::int64_t ticks_to_ns(std::int64_t ticks) { return ticks; }
} // namespace bench_clock
#endif

namespace {
using namespace lob;

constexpr const char* kVersionLabel = "v2b-array-of-levels";

struct Op {
    enum class Kind : std::uint8_t { Limit, Market, Cancel };
    Kind kind{};
    Side side{};
    Price price{};
    Qty qty{};
    OrderId target{};
    OrderId new_id{};
};

struct Config {
    std::size_t warmup_ops  = 200'000;
    std::size_t measure_ops = 1'000'000;
    double cancel_frac = 0.60;
    double market_frac = 0.10;
    Price  mid_price    = 10000;
    double price_stddev = 30.0;
    Qty min_qty = 1;
    Qty max_qty = 5;
    std::uint64_t seed = 0xC0FFEEULL;
};

class WorkloadGen {
public:
    explicit WorkloadGen(const Config& c) : cfg_(c), rng_(c.seed) {}

    std::vector<Op> generate(std::size_t n) {
        std::vector<Op> v;
        v.reserve(n);
        std::uniform_real_distribution<double> u01(0.0, 1.0);
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::normal_distribution<double> price_dist(0.0, cfg_.price_stddev);
        std::uniform_int_distribution<Qty> qty_dist(cfg_.min_qty, cfg_.max_qty);

        for (std::size_t i = 0; i < n; ++i) {
            const double r = u01(rng_);
            Op op{};
            const bool can_cancel = !issued_.empty();

            if (can_cancel && r < cfg_.cancel_frac) {
                // Sliding window keeps cancel targets recent enough to
                // mostly still be live — otherwise this degenerates
                // into benchmarking the hash-map miss path.
                const std::size_t lo = issued_.size() > kCancelWindow
                                            ? issued_.size() - kCancelWindow
                                            : 0;
                std::uniform_int_distribution<std::size_t> idx(lo, issued_.size() - 1);
                op.kind = Op::Kind::Cancel;
                op.target = issued_[idx(rng_)];
            } else if (r < cfg_.cancel_frac + cfg_.market_frac) {
                op.kind = Op::Kind::Market;
                op.side = side_dist(rng_) ? Side::Buy : Side::Sell;
                op.qty = qty_dist(rng_);
                op.new_id = next_id_++;
            } else {
                op.kind = Op::Kind::Limit;
                op.side = side_dist(rng_) ? Side::Buy : Side::Sell;
                const double offset = price_dist(rng_);
                Price p = static_cast<Price>(cfg_.mid_price + offset);
                // Force non-crossing so limits build depth rather than
                // instantly trading; market orders provide crossing flow.
                if (op.side == Side::Buy  && p >= cfg_.mid_price) p = cfg_.mid_price - 1;
                if (op.side == Side::Sell && p <= cfg_.mid_price) p = cfg_.mid_price + 1;
                if (p <= 0) p = 1;
                op.price = p;
                op.qty = qty_dist(rng_);
                op.new_id = next_id_++;
                issued_.push_back(op.new_id);
            }
            v.push_back(op);
        }
        return v;
    }

private:
    static constexpr std::size_t kCancelWindow = 100'000;
    Config cfg_;
    std::mt19937_64 rng_;
    OrderId next_id_ = 1;
    std::vector<OrderId> issued_;
};

struct Counters {
    std::size_t limit{}, market{}, cancel{};
    std::size_t cancel_rejected{}, market_rejected{};
};

inline void apply_op(MatchingEngine& engine, const Op& op, Counters& c) {
    switch (op.kind) {
        case Op::Kind::Limit:
            engine.submit_limit(op.new_id, op.side, op.price, op.qty);
            ++c.limit;
            return;
        case Op::Kind::Market: {
            auto evs = engine.submit_market(op.new_id, op.side, op.qty);
            for (const auto& e : evs)
                if (e.type == EventType::MarketRejected) ++c.market_rejected;
            ++c.market;
            return;
        }
        case Op::Kind::Cancel: {
            auto evs = engine.cancel(op.target);
            for (const auto& e : evs)
                if (e.type == EventType::CancelRejected) ++c.cancel_rejected;
            ++c.cancel;
            return;
        }
    }
}

} // namespace

int main() {
    Config cfg;

    std::cout << "Version:        " << kVersionLabel << "\n"
               << "Warmup ops:     " << cfg.warmup_ops << "\n"
               << "Measure ops:    " << cfg.measure_ops << "\n"
               << "Op mix:         "
               << static_cast<int>(cfg.cancel_frac * 100) << "% cancel, "
               << static_cast<int>((1 - cfg.cancel_frac - cfg.market_frac) * 100) << "% limit, "
               << static_cast<int>(cfg.market_frac * 100) << "% market\n"
               << "Mid price:      " << cfg.mid_price << "\n"
               << "Price stddev:   " << cfg.price_stddev << "\n\n";

    std::cout << "Generating workloads (pre-allocated, untimed)...\n";
    WorkloadGen gen(cfg);
    const std::vector<Op> warmup  = gen.generate(cfg.warmup_ops);
    const std::vector<Op> measure = gen.generate(cfg.measure_ops);

    OrderBook book;
    MatchingEngine engine(book);

    std::cout << "Warming up (untimed)...\n";
    Counters wc;
    for (const auto& op : warmup) apply_op(engine, op, wc);
    std::cout << "Post-warmup book depth: "
               << book.bid_level_count() << " bid levels, "
               << book.ask_level_count() << " ask levels\n\n";

    std::cout << "Measuring " << cfg.measure_ops << " ops...\n";

    // Latencies stored in QPC ticks and converted at report time —
    // avoid the divide inside the timed loop.
    std::vector<std::int64_t> lat(cfg.measure_ops);
    Counters c;

    const auto wall_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < cfg.measure_ops; ++i) {
        const Op& op = measure[i];
        const std::int64_t t0 = bench_clock::now_ticks();
        apply_op(engine, op, c);
        const std::int64_t t1 = bench_clock::now_ticks();
        lat[i] = t1 - t0;
    }
    const auto wall_end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(wall_end - wall_start).count();
    const double throughput = static_cast<double>(cfg.measure_ops) / seconds;

    std::sort(lat.begin(), lat.end());
    auto pct = [&](double p) {
        const std::size_t idx = static_cast<std::size_t>(p * (lat.size() - 1));
        return bench_clock::ticks_to_ns(lat[idx]);
    };
    const auto min_ns = bench_clock::ticks_to_ns(lat.front());
    const auto max_ns = bench_clock::ticks_to_ns(lat.back());

    std::cout << "\n--- Results (" << kVersionLabel << ") ---\n";
    std::cout << "Op counts (measured phase):\n"
               << "  limit:           " << c.limit << "\n"
               << "  market:          " << c.market
               << " (rejected: " << c.market_rejected << ")\n"
               << "  cancel:          " << c.cancel
               << " (rejected: " << c.cancel_rejected
               << ", success rate: "
               << std::fixed << std::setprecision(1)
               << (c.cancel ? 100.0 * (c.cancel - c.cancel_rejected) / c.cancel : 0.0)
               << "%)\n";

    std::cout << "Wall time:         " << std::fixed << std::setprecision(3) << seconds << " s\n"
               << "Throughput:        " << std::fixed << std::setprecision(0) << throughput
                                          << " ops/sec\n"
               << "Per-op latency (ns):\n"
               << "  min:    " << min_ns << "\n"
               << "  p50:    " << pct(0.50) << "\n"
               << "  p90:    " << pct(0.90) << "\n"
               << "  p99:    " << pct(0.99) << "\n"
               << "  p99.9:  " << pct(0.999) << "\n"
               << "  p99.99: " << pct(0.9999) << "\n"
               << "  max:    " << max_ns << "\n";
    std::cout << "Post-bench book depth: "
               << book.bid_level_count() << " bid levels, "
               << book.ask_level_count() << " ask levels\n";

    // Append CSV row for cross-version comparison.
    // Columns: version, throughput_ops_per_sec, p50_ns, p99_ns, p999_ns, max_ns
    {
        std::ofstream csv("bench_results.csv", std::ios::app);
        csv << kVersionLabel << ","
            << static_cast<long long>(throughput) << ","
            << pct(0.50) << "," << pct(0.99) << "," << pct(0.999) << ","
            << max_ns << "\n";
    }

    return 0;
}
