#pragma once
#include <mutex>
#include <string>
#include <vector>
#include <iomanip>
#include <list>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <new>

#define DLLIMPORT __declspec(dllimport)
#define DLLEXPORT __declspec(dllexport)
#pragma warning(disable : 4995)

#define QWORD unsigned __int64 // fix windows.h messup (for NEXTRAWINPUTBLOCK)

using std::mutex;
using recmutex = std::recursive_mutex;
using std::atomic;
using std::deque;
using std::forward;
using std::function;
using std::istream;
using std::list;
using std::lock_guard;
using std::make_shared;
using std::make_tuple;
using std::make_unique;
using std::max;
using std::min;
using std::move;
using std::ostream;
using std::shared_ptr;
using std::string;
using std::string_view;
using std::stringstream;
using std::tie;
using std::tuple;
using std::unique_lock;
using std::unique_ptr;
using std::unordered_map;
using std::unordered_set;
using std::vector;
using std::wstring;
using std::wstring_view;
using std::wstringstream;
constexpr auto _ = std::ignore;

template <class T>
T DivRoundUp(T value, T by) {
    return (value + (by - 1)) / by;
}

template <class T>
T RoundUp(T value, T by) {
    int rem = value % by;
    if (rem) {
        value += by - rem;
    }
    return value;
}

template <class T>
T Clamp(T value, T minval, T maxval) {
    return min(max(value, minval), maxval);
}

template <class TC, class T>
void Erase(TC &c, const T &val) {
    c.erase(std::remove(c.begin(), c.end(), val), c.end());
}

template <class TFunc>
class DestructorClass {
    const TFunc &mFunc;

public:
    DestructorClass(const TFunc &func) : mFunc(func) {}
    ~DestructorClass() { mFunc(); }
};

template <class TFunc>
DestructorClass<TFunc> Destructor(const TFunc &func) {
    return DestructorClass<TFunc>(func);
}

template <class T>
class WeakAtomic {
    std::atomic<T> atomic;

public:
    WeakAtomic() {}
    WeakAtomic(T value) : atomic(value) {}
    WeakAtomic(const WeakAtomic &other) : atomic(other) {}
    WeakAtomic(WeakAtomic &&other) : atomic(move(other)) {}

    T get() const { return atomic.load(std::memory_order_acquire); }
    void set(T value) { atomic.store(value, std::memory_order_release); }
    T exchange(T value) { return atomic.exchange(value, std::memory_order_acq_rel); }

    operator T() const { return get(); }
    T operator=(T value) {
        set(value);
        return value;
    }
    void operator=(const WeakAtomic<T> &other) { set(other.get()); }
};
