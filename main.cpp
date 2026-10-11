/*
* Compiler: MSVC (Visual Studio 2022), C++20; /Od and /O2.
 * Files data_<N>.txt (N = 100000, 1000000, 10000000): 1st line is N,
 * then N integers from [1; 1'000'000] separated by spaces.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <execution>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr unsigned kSeed = 42;
constexpr int kMinValue = 1;
constexpr int kMaxValue = 1'000'000;
constexpr int kRepeats = 3;
constexpr int kSinTerms = 20;
constexpr double kUnreachableSum = 1000.0;
constexpr int kUnreachableValue = 2'000'000;
constexpr unsigned kMaxThreads = 1024;

const std::vector<std::size_t> kSizes = {100'000, 1'000'000, 10'000'000};

using Predicate = bool (*)(int);

struct NamedPredicate {
    std::string name;
    Predicate function;
};

struct Measurement {
    double milliseconds;
    bool result;
};


bool fast_predicate(int x) { return x > kUnreachableValue; }

bool heavy_predicate(int x) {
    double sum = 0;
    for (int i = 0; i < kSinTerms; ++i) {
        sum += std::sin(x + i);
    }
    return sum > kUnreachableSum;
}


std::string file_name(std::size_t size) {
    return "data_" + std::to_string(size) + ".txt";
}

void generate_file(const std::string& name, std::size_t size) {
    std::ofstream out(name);
    std::mt19937 gen(kSeed);
    std::uniform_int_distribution<int> dist(kMinValue, kMaxValue);
    out << size << '\n';
    for (std::size_t i = 0; i < size; ++i) {
        out << dist(gen) << ' ';
    }
}

void ensure_file_exists(const std::string& name, std::size_t size) {
    if (!std::ifstream(name).good()) {
        generate_file(name, size);
    }
}

std::vector<int> load_file(const std::string& name) {
    std::ifstream in(name);
    std::size_t size = 0;
    in >> size;
    std::vector<int> data(size);
    for (int& value : data) {
        in >> value;
    }
    return data;
}


template <typename Func>
Measurement measure(Func&& func) {
    std::vector<double> times;
    bool result = false;
    for (int i = 0; i < kRepeats; ++i) {
        const auto start = std::chrono::steady_clock::now();
        result = func();
        const auto end = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], result};
}

void print_measurement(const std::string& label, const Measurement& m) {
    std::cout << std::left << std::setw(12) << label << std::right << std::setw(12)
              << std::fixed << std::setprecision(3) << m.milliseconds
              << " ms   result = " << std::boolalpha << m.result << '\n';
}


template <typename Policy>
void test_with_policy(const std::string& label, const std::vector<int>& data,
                      Predicate pred, Policy policy) {
    print_measurement(label, measure([&] {
        return std::any_of(policy, data.begin(), data.end(), pred);
    }));
}

void test_library_any_of(const std::vector<int>& data, Predicate pred) {
    print_measurement("no policy", measure([&] {
        return std::any_of(data.begin(), data.end(), pred);
    }));
    test_with_policy("seq", data, pred, std::execution::seq);
    test_with_policy("par", data, pred, std::execution::par);
    test_with_policy("unseq", data, pred, std::execution::unseq);
    test_with_policy("par_unseq", data, pred, std::execution::par_unseq);
}


bool parallel_any_of(const std::vector<int>& data, unsigned k, Predicate pred) {
    std::vector<char> block_found(k, 0);
    std::vector<std::thread> threads;
    threads.reserve(k);

    const std::size_t base = data.size() / k;
    const std::size_t remainder = data.size() % k;
    std::size_t begin = 0;
    for (unsigned i = 0; i < k; ++i) {
        const std::size_t end = begin + base + (i < remainder ? 1 : 0);
        threads.emplace_back([&data, &block_found, i, begin, end, pred] {
            block_found[i] = std::any_of(data.begin() + begin, data.begin() + end, pred);
        });
        begin = end;
    }
    for (std::thread& t : threads) {
        t.join();
    }
    return std::any_of(block_found.begin(), block_found.end(),
                       [](char found) { return found != 0; });
}

std::vector<unsigned> make_k_values(unsigned hw) {
    std::set<unsigned> values;
    for (unsigned k = 1; k <= kMaxThreads; k *= 2) {
        values.insert(k);
    }
    values.insert({hw, 2 * hw});
    if (hw / 2 > 0) {
        values.insert(hw / 2);
    }
    return {values.begin(), values.end()};
}

void print_best_k(unsigned best_k, double best_ms, unsigned hw) {
    std::cout << "Best K = " << best_k << " (" << std::fixed << std::setprecision(3)
              << best_ms << " ms), hardware threads = " << hw
              << ", K / threads = " << std::setprecision(2)
              << static_cast<double>(best_k) / hw << '\n';
}

void test_custom_any_of(const std::vector<int>& data, Predicate pred) {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "\n" << std::left << std::setw(8) << "K" << "Time (ms)\n";

    unsigned best_k = 1;
    double best_ms = -1;
    for (unsigned k : make_k_values(hw)) {
        const Measurement m = measure([&] { return parallel_any_of(data, k, pred); });
        std::cout << std::left << std::setw(8) << k << std::fixed
                  << std::setprecision(3) << m.milliseconds << '\n';
        if (best_ms < 0 || m.milliseconds < best_ms) {
            best_ms = m.milliseconds;
            best_k = k;
        }
    }
    print_best_k(best_k, best_ms, hw);
}



}

int main() {
    std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << "\n";

    const std::vector<NamedPredicate> predicates = {
        {"fast (x > limit)", fast_predicate},
        {"heavy (sum of 20 sin)", heavy_predicate}};

    for (std::size_t size : kSizes) {
        const std::string name = file_name(size);
        ensure_file_exists(name, size);
        const std::vector<int> data = load_file(name);

        for (const NamedPredicate& p : predicates) {
            std::cout << "\n=== any_of, N = " << size << ", predicate: " << p.name
                      << " ===\n--- library std::any_of ---\n";
            test_library_any_of(data, p.function);
            std::cout << "--- custom parallel any_of ---";
            test_custom_any_of(data, p.function);
        }
    }
    return 0;
}