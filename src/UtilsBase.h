#pragma once
#include <mutex>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <list>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <numbers>
#include <new>
#include <bit>
#include <ranges>
#include <array>

#pragma warning(disable : 4995)

#define QWORD unsigned __int64 // fix windows.h messup (for NEXTRAWINPUTBLOCK)
#define NOMINMAX               // for later including of windows.h

#define CONCAT(x, y) x##y

using std::mutex;
using recmutex = std::recursive_mutex;
using std::array;
using std::atomic;
using std::call_once;
using std::deque;
using std::forward;
using std::function;
using std::initializer_list;
using std::istream;
using std::list;
using std::lock_guard;
using std::make_signed_t;
using std::make_tuple;
using std::make_unsigned_t;
using std::max;
using std::min;
using std::move;
using std::numeric_limits;
using std::once_flag;
using std::ostream;
using std::popcount;
using std::size;
using std::string;
using std::string_view;
using std::stringstream;
using std::swap;
using std::tie;
using std::tuple;
using std::unique_lock;
using std::unordered_map;
using std::unordered_set;
using std::vector;
using std::wstring;
using std::wstring_view;
using std::wstringstream;
namespace views = std::ranges::views;

constexpr auto _ = std::ignore;

template <class T1, class T2>
auto DivRoundUp(T1 value, T2 by) // assumes non-negative
{
    return (value + (by - 1)) / by;
}

template <class T1, class T2>
auto RoundUp(T1 value, T2 by) // assumes non-negative
{
    int rem = value % by;
    if (rem) {
        value += by - rem;
    }
    return value;
}

template <class T1, class T2, class T3>
auto Clamp(T1 value, T2 minval, T3 maxval) -> std::common_type_t<T2, T3> {
    if (value < (T1)minval) {
        return minval;
    } else if (value > (T1)maxval) {
        return maxval;
    } else {
        return (std::common_type_t<T2, T3>)value;
    }
}

template <class IntT, class T>
IntT ClampToInt(T value) {
    if (value < (T)std::numeric_limits<IntT>::min()) {
        return std::numeric_limits<IntT>::min();
    } else if (value > (T)std::numeric_limits<IntT>::max()) {
        return std::numeric_limits<IntT>::max();
    } else if constexpr (std::is_floating_point_v<T>) {
        return (IntT)nearbyint(value);
    } else {
        return (IntT)value;
    }
}

template <class T>
tuple<T, T> SinCos(T value) {
    return {sin(value), cos(value)};
}

template <class TC, class T>
intptr_t Find(TC &c, const T &val) {
    auto iter = std::find(c.begin(), c.end(), val);
    return iter == c.end() ? -1 : iter - c.begin();
}

template <class TC, class T>
void Erase(TC &c, const T &val) {
    c.erase(std::remove(c.begin(), c.end(), val), c.end());
}

template <class TC>
auto ExtractBack(TC &c) -> TC::value_type {
    typename TC::value_type val = c.back();
    c.pop_back();
    return val;
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
class UniquePtr : public std::unique_ptr<T> {
public:
    UniquePtr() {}
    UniquePtr(nullptr_t) {}
    UniquePtr(std::unique_ptr<T> &&other) : std::unique_ptr<T>(move(other)) {}

    static UniquePtr<T> From(T *ptr) { return UniquePtr<T>(std::unique_ptr<T>(ptr)); }

    template <class... TArgs>
    static UniquePtr<T> New(TArgs... args) {
        return UniquePtr<T>(std::make_unique<T>(forward<TArgs>(args)...));
    }

    operator T *() { return this->get(); }
};

template <class T>
class SharedPtr : public std::shared_ptr<T> {
public:
    SharedPtr() {}
    SharedPtr(nullptr_t) {}
    SharedPtr(std::shared_ptr<T> &&other) : std::shared_ptr<T>(move(other)) {}

    template <class... TArgs>
    static SharedPtr<T> New(TArgs... args) {
        return SharedPtr<T>(std::make_shared<T>(forward<TArgs>(args)...));
    }

    operator T *() { return this->get(); }
};

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
    bool compare_exchange(T &expected, T value) { return atomic.compare_exchange_strong(expected, value, std::memory_order_acq_rel); }

    operator T() const { return get(); }
    T operator=(T value) {
        set(value);
        return value;
    }
    void operator=(const WeakAtomic<T> &other) { set(other.get()); }

    T operator->() { return get(); }
};

template <class F>
class CallbackList {
    mutex Mutex;
    list<function<F>> List;

public:
    using CbIter = list<function<F>>::iterator;

    CbIter Add(function<F> cb) {
        lock_guard<mutex> lock(Mutex);
        List.push_back(cb);
        return std::prev(List.end());
    }

    void Remove(const CbIter &cbIter) {
        lock_guard<mutex> lock(Mutex);
        List.erase(cbIter);
    }

    template <class... TArgs>
    void Call(TArgs... args) {
        lock_guard<mutex> lock(Mutex);
        auto iter = List.begin();
        while (iter != List.end()) {
            if ((*iter)(forward<TArgs>(args)...)) {
                ++iter;
            } else {
                iter = List.erase(iter);
            }
        }
    }
};

template <class T, class R>
T GetOutput(R(__stdcall *func)(T *)) {
    T value = {};
    func(&value);
    return value;
}

template <class S, class T, class R>
T GetOutput(R(__stdcall *func)(S, T *), S arg) {
    T value = {};
    func(forward<S>(arg), &value);
    return value;
}

template <class S, class T, class R>
T GetOutput(R(__stdcall *func)(const S *, T *), S arg) {
    T value = {};
    func(&arg, &value);
    return value;
}
