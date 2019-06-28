/*
*  Copyright (C) 2019 Ethan Blackwood
*  This is free software released under the MIT license.
*  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
*/

#include "RWSyncContainer.h"

#include <utility>

namespace RWSync
{
    template<typename T>
    int Container<T>::numAllocatedReaders() const
    {
        return manager.getMaxReaders();
    }


    template<typename T>
    void Container<T>::increaseMaxReadersTo(int nReaders)
    {
        assert(original != nullptr); // original should be assigned in constructor if expandable

        // step 1: ensure space in data
        std::lock_guard<std::mutex> dataSizeLock(dataSizeMutex);
        int newElementsNeeded = nReaders + 2 - data.size();
        for (int i = 0; i < newElementsNeeded; ++i)
        {
            data.push_back(*original);
        }

        // step 2: allow more readers in manager
        manager.ensureSpaceForReaders(nReaders);
    }

    template<typename T>
    void Container<T>::reset()
    {
        manager.reset();
    }

    template<typename T>
    template<typename UnaryOperator>
    bool Container<T>::map(UnaryOperator f)
    {
        Manager::Lockout lock(manager);
        if (!manager.reset(lock))
        {
            return false;
        }

        std::unique_lock<std::mutex> dataSizeLock(dataSizeMutex, std::defer_lock);
        if (expandable)
        {
            // make sure we don't start expanding "data" while this is happening
            dataSizeLock.lock();

            // only need to apply to "original" if can expand, since otherwise
            // it won't be used.
            f(*original);
        }

        for (T& instance : data)
        {
            f(instance);
        }

        return true;
    }


    template<typename T>
    Container<T>::WritePtr::WritePtr(Container<T>& o)
        : owner (o)
        , ind   (o.manager)
    {}


    template<typename T>
    bool Container<T>::WritePtr::tryToMakeValid()
    {
        return ind.tryToMakeValid();
    }


    template<typename T>
    bool Container<T>::WritePtr::isValid() const
    {
        return ind.isValid();
    }


    template<typename T>
    Container<T>::WritePtr::operator T*()
    {
        if (!ind.isValid())
        {
            return nullptr;
        }

        return owner.data[ind];
    }

    template<typename T>
    T& Container<T>::WritePtr::operator*()
    {
        return **this;
    }

    
    template<typename T>
    T* Container<T>::WritePtr::operator->()
    {
        return *this;
    }


    template<typename T>
    void Container<T>::WritePtr::pushUpdate()
    {
        ind.pushUpdate();
    }


    template<typename T>
    Container<T>::ReadPtr::ReadPtr(Container<T>& o)
        : owner (o)
        , ind   (o.manager)
    {}


    template<typename T>
    bool Container<T>::ReadPtr::tryToMakeValid()
    {
        return ind.tryToMakeValid();
    }


    template<typename T>
    bool Container<T>::ReadPtr::isValid() const
    {
        return ind.isValid();
    }


    template<typename T>
    bool Container<T>::ReadPtr::canRead() const
    {
        return ind.canRead();
    }


    template<typename T>
    bool Container<T>::ReadPtr::hasUpdate() const
    {
        return ind.hasUpdate();
    }


    template<typename T>
    void Container<T>::ReadPtr::pullUpdate()
    {
        ind.pullUpdate();
    }


    template<typename T>
    Container<T>::ReadPtr::operator T*()
    {
        if (!canRead())
        {
            return nullptr;
        }

        return owner.data[ind];
    }

    template<typename T>
    T& Container<T>::ReadPtr::operator*()
    {
        return **this;
    }


    template<typename T>
    T* Container<T>::ReadPtr::operator->()
    {
        return *this;
    }


    template<typename T>
    template<typename... Args>
    Container<T>::Container(int maxReaders, Args&&... args)
        : expandable    (maxReaders == 0)
        , manager       (expandable ? 1 : maxReaders)
    {
        assert(maxReaders >= 0);

        if (expandable)
        {
            original.reset(new T(args...));
        }

        int initialCopies = manager.getMaxReaders() + 2;

        for (int i = 0; i < initialCopies - 1; ++i)
        {
            data.emplace_back(args...);
        }

        // move into last entry if possible
        data.emplace_back(std::forward<Args>(args)...);
    }

    template<typename T, int maxReaders>
    template<typename... Args>
    FixedContainer<T, maxReaders>::FixedContainer(Args&&... args)
        : Container<T>(maxReaders, std::forward<Args>(args)...)
    {
        static_assert(maxReaders >= 1, "Maximum readers of FixedContainer must be at least 1");
    }

    template<typename T>
    template<typename... Args>
    ExpandableContainer<T>::ExpandableContainer(Args&&... args)
        : Container<T>(0, std::forward<Args>(args)...)
    {
        // is_copy_constructible is broken on VS2013, sadly...
        static_assert(std::is_copy_constructible<T>::value,
            "An ExpandableContainer cannot be created of a non-copyable type.");
    }

    template<typename T>
    void ExpandableContainer<T>::increaseMaxReadersTo(int nReaders)
    {
        Container<T>::increaseMaxReadersTo(nReaders);
    }

    template<typename T>
    ExpandableContainer<T>::GuaranteedReadPtr::GuaranteedReadPtr(ExpandableContainer<T>& o)
        : Container<T>::ReadPtr(o)
    {
        while (!this->tryToMakeValid())
        {
            o.increaseMaxReadersTo(o.numAllocatedReaders() + 1);
        }        
    }
}
