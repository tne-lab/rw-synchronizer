#ifndef RW_SYNCED_H_INCLUDED
#define RW_SYNCED_H_INCLUDED

/*  
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include "RWSyncManager.h"

/*
 * Class to actually hold data controlled by a RWSyncManager.
 * See documentation in RWSyncManager.h.
 */

template<typename T, int maxReaders = 1>
class RWSynchronized
{
public:
    template<typename... Args>
    RWSynchronized(Args&&... args)
    {
        data.reserve(size);

        for (int i = 0; i < size - 1; ++i)
        {
            data.emplace_back(args...);
        }

        // move into the last entry, if possible
        data.emplace_back(std::forward<Args>(args)...);
    }

    bool reset()
    {
        return sync.reset();
    }

    // Call a function on each underlying data member.
    // Requires that no readers or writers exist. Returns false if
    // this condition is unmet, true otherwise.
    template<typename UnaryFunction>
    bool apply(UnaryFunction f)
    {
        RWSyncManager<maxReaders>::ScopedLockout lock(sync);
        if (!lock.isValid())
        {
            return false;
        }

        for (T& instance : data)
        {
            f(instance);
        }

        return true;
    }

    class ScopedWritePtr
    {
    public:
        ScopedWritePtr(RWSynchronized<T, maxReaders>& o)
            : owner(&o)
            , ind(o.sync)
            , valid(ind.isValid())
        {}

        void pushUpdate()
        {
            ind.pushUpdate();
        }

        // provide access to data

        T& operator*()
        {
            if (!valid)
            {
                // abort! abort!
                assert(false);
                std::abort();
            }
            return owner->data[ind];
        }

        T* operator->()
        {
            return &(operator*());
        }

        bool isValid() const
        {
            return valid;
        }

    private:
        RWSynchronized<T, maxReaders>* owner;
        typename RWSyncManager<maxReaders>::ScopedWriteIndex ind;
        const bool valid;
    };

    class ScopedReadPtr
    {
    public:
        ScopedReadPtr(RWSynchronized<T, maxReaders>& o)
            : owner(&o)
            , ind(o.sync)
            // if the ind is valid, but is equal to -1, this pointer is still invalid (for now)
            , valid(ind != -1)
        {}

        bool hasUpdate() const
        {
            return ind.hasUpdate();
        }

        void pullUpdate()
        {
            ind.pullUpdate();
            // in case ind is valid but was equal to -1:
            valid = ind != -1;
        }

        // provide access to data

        const T& operator*()
        {
            if (!valid)
            {
                // abort! abort!
                assert(false);
                std::abort();
            }
            return owner->data[ind];
        }

        const T* operator->()
        {
            return &(operator*());
        }

        bool isValid() const
        {
            return valid;
        }

    private:
        RWSynchronized<T, maxReaders>* owner;
        typename RWSyncManager<maxReaders>::ScopedReadIndex ind;
        bool valid;
    };

private:
    static const int size = maxReaders + 2;

    std::vector<T> data;
    RWSyncManager<maxReaders> sync;
};

template<typename T, int maxReaders = 1>
using AtomicScopedWritePtr = typename RWSynchronized<T, maxReaders>::ScopedWritePtr;

template<typename T, int maxReaders = 1>
using AtomicScopedReadPtr = typename RWSynchronized<T, maxReaders>::ScopedReadPtr;

#endif // RW_SYNCED_H_INCLUDED