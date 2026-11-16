#pragma once
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace dc {

class Rng {
public:
    explicit Rng(uint32_t seed = 1) : eng_(seed), seed_(seed) {}
    void reseed(uint32_t s) { seed_ = s; eng_.seed(s); }
    uint32_t seed() const { return seed_; }

    // Целое в диапазоне [lo, hi] включительно.
    int range(int lo, int hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(eng_);
    }
    bool chance(double p) {
        std::uniform_real_distribution<double> d(0.0, 1.0);
        return d(eng_) < p;
    }
    template <class T>
    void shuffle(std::vector<T>& v) { std::shuffle(v.begin(), v.end(), eng_); }

private:
    std::mt19937 eng_;
    uint32_t seed_;
};

}  // namespace dc
