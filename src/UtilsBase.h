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
#include <numbers>
#include <new>

#define DLLIMPORT __declspec(dllimport)
#define DLLEXPORT __declspec(dllexport)
#pragma warning(disable : 4995)

#define QWORD unsigned __int64 // fix windows.h messup (for NEXTRAWINPUTBLOCK)
#define NOMINMAX               // for later including of windows.h

using std::mutex;
using recmutex = std::recursive_mutex;
using std::atomic;
using std::deque;
using std::forward;
using std::function;
using std::istream;
using std::list;
using std::lock_guard;
using std::make_tuple;
using std::max;
using std::min;
using std::move;
using std::ostream;
using std::string;
using std::string_view;
using std::stringstream;
using std::tie;
using std::tuple;
using std::unique_lock;
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

template <class IntT, class T>
IntT ClampToInt(T value) {
    value = Clamp(value, (T)std::numeric_limits<IntT>::min(), (T)std::numeric_limits<IntT>::max());
    return (IntT)round(value);
}

template <class T>
tuple<T, T> SinCos(T value) {
    return {sin(value), cos(value)};
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
class UniquePtr : public std::unique_ptr<T> {
public:
    UniquePtr() {}
    UniquePtr(nullptr_t) {}
    UniquePtr(std::unique_ptr<T> &&other) : std::unique_ptr<T>(move(other)) {}

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
    SharedPtr(const std::shared_ptr<T> &other) : std::shared_ptr<T>(other) {}
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

    operator T() const { return get(); }
    T operator=(T value) {
        set(value);
        return value;
    }
    void operator=(const WeakAtomic<T> &other) { set(other.get()); }
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
        for (auto &cb : List) {
            cb(forward<TArgs>(args)...);
        }
    }
};
