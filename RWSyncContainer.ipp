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
    template<typename... Args>
    Container<T>::Container(const Args&... args)
        : manager       (new Manager())
        , dataSizeMutex (new std::mutex())
        , original      (new T(args...))
    {
        for (int i = 0; i < 3; ++i)
        {
            data.emplace_back(args...);
        }
    }


    template<typename T>
    template<int maxReaders, typename... Args>
    Container<T> Container<T>::createWithMaxReaders(const Args&... args)
    {
        Container<T> container(args...);
        container.manager->ensureSpaceForReaders(maxReaders);

        // add additional copies
        for (int i = 1; i < maxReaders; ++i)
        {
            container.data.emplace_back(args...);
        }

        return container; // moves (or constructs in place with RVO)
    }


    template<typename T>
    Container<T>::Container(Container<T>&& other)
        : manager       (std::move(other.manager))
        , data          (std::move(other.data))
        , dataSizeMutex (std::move(other.dataSizeMutex))
        , original      (std::move(other.original))
    {}


    template<typename T>
    Container<T>& Container<T>::operator=(Container<T>&& other)
    {
        if (this != &other)
        {
            manager = std::move(other.manager);
            data = std::move(other.data);
            dataSizeMutex = std::move(other.dataSizeMutex);
            original = std::move(original);
        }
        return *this;
    }


    template<typename T>
    int Container<T>::getMaxReaders() const
    {
        return manager->getMaxReaders();
    }


    template<typename T>
    void Container<T>::increaseMaxReadersTo(int nReaders)
    {
        static_assert(canExpand, "Only Containers of copy-constructible types can be expanded");

        // step 1: ensure space in data
        std::lock_guard<std::mutex> dataSizeLock(*dataSizeMutex);
        int newElementsNeeded = nReaders + 2 - data.size();
        for (int i = 0; i < newElementsNeeded; ++i)
        {
            data.push_back(*original);
        }

        // step 2: allow more readers in manager manager
        manager->ensureSpaceForReaders(nReaders);
    }


    template<typename T>
    bool Container<T>::reset()
    {
        return manager->reset();
    }


    template<typename T>
    bool Container<T>::map(std::function<void(T&)> f)
    {
        Manager::Lockout lock(*manager);
        if (!lock.isValid())
        {
            return false;
        }

        std::unique_lock<std::mutex> dataSizeLock(*dataSizeMutex, std::defer_lock);
        if (canExpand)
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
        , ind   (*o.manager)
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
    T& Container<T>::WritePtr::operator*()
    {
        if (!ind.isValid())
        {
            throw new std::out_of_range("Attempt to access an invalid write pointer");
        }

        return owner.data[ind];
    }

    
    template<typename T>
    T* Container<T>::WritePtr::operator->()
    {
        return &(operator*());
    }


    template<typename T>
    void Container<T>::WritePtr::pushUpdate()
    {
        ind.pushUpdate();
    }


    template<typename T>
    Container<T>::ReadPtr::ReadPtr(Container<T>& o)
        : owner (o)
        , ind   (*o.manager)
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
    const T& Container<T>::ReadPtr::operator*()
    {
        if (!canRead())
        {
            throw new std::out_of_range("Attempt to access an invalid read pointer");
        }

        return owner.data[ind];
    }


    template<typename T>
    const T* Container<T>::ReadPtr::operator->()
    {
        return &(operator*());
    }


    template<typename T>
    Container<T>::GuaranteedReadPtr::GuaranteedReadPtr(Container<T>& o)
        : ReadPtr(o)
    {
        while (!tryToMakeValid())
        {
            o.increaseMaxReadersTo(o.getMaxReaders() + 1);
        }        
    }
}
