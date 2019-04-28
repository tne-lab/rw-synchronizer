/*
*  Copyright (C) 2019 Ethan Blackwood
*  This is free software released under the MIT license.
*  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
*/

#include "RWSyncContainer.h"

#include <utility>

namespace RWSync
{
    ////// AbstractContainer //////

    template<typename T>
    AbstractContainer<T>::AbstractContainer(int maxReaders)
        : sync  (maxReaders)
    {}

    template<typename T>
    template<typename... Args>
    void AbstractContainer<T>::initialize(Args&&... args)
    {
        int size = sync.getMaxReaders() + 2;
        for (int i = 0; i < size - 1; ++i)
        {
            data.emplace_back(args...);
        }

        // move into last element if possible
        data.emplace_back(std::forward<Args>(args)...);
    }


    template<typename T>
    int AbstractContainer<T>::getMaxReaders() const
    {
        return sync.getMaxReaders();
    }


    template<typename T>
    bool AbstractContainer<T>::reset()
    {
        return sync.reset();
    }


    template<typename T>
    bool AbstractContainer<T>::map(std::function<void(T&)> f)
    {
        Manager::Lockout lock(sync);
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


    /***** AbstractContainer::WritePtr ******/

    template<typename T>
    AbstractContainer<T>::WritePtr::WritePtr(AbstractContainer<T>& o)
        : owner (o)
        , ind   (o.sync)
    {}


    template<typename T>
    bool AbstractContainer<T>::WritePtr::tryToMakeValid()
    {
        return ind.tryToMakeValid();
    }


    template<typename T>
    bool AbstractContainer<T>::WritePtr::isValid() const
    {
        return ind.isValid();
    }


    template<typename T>
    T& AbstractContainer<T>::WritePtr::operator*()
    {
        if (!ind.isValid())
        {
            throw new std::out_of_range("Attempt to access an invalid write pointer");
        }

        return owner.data[ind];
    }

    
    template<typename T>
    T* AbstractContainer<T>::WritePtr::operator->()
    {
        return &(operator*());
    }


    template<typename T>
    void AbstractContainer<T>::WritePtr::pushUpdate()
    {
        ind.pushUpdate();
    }

    /***** AbstractContainer::WritePtr ******/

    template<typename T>
    AbstractContainer<T>::ReadPtr::ReadPtr(AbstractContainer<T>& o)
        : owner (o)
        , ind   (o.sync)
    {}


    template<typename T>
    bool AbstractContainer<T>::ReadPtr::tryToMakeValid()
    {
        return ind.tryToMakeValid();
    }


    template<typename T>
    bool AbstractContainer<T>::ReadPtr::isValid() const
    {
        return ind.isValid();
    }


    template<typename T>
    bool AbstractContainer<T>::ReadPtr::canRead() const
    {
        return ind.canRead();
    }


    template<typename T>
    bool AbstractContainer<T>::ReadPtr::hasUpdate() const
    {
        return ind.hasUpdate();
    }


    template<typename T>
    void AbstractContainer<T>::ReadPtr::pullUpdate()
    {
        ind.pullUpdate();
    }


    template<typename T>
    const T& AbstractContainer<T>::ReadPtr::operator*()
    {
        if (!canRead())
        {
            throw new std::out_of_range("Attempt to access an invalid read pointer");
        }

        return owner.data[ind];
    }


    template<typename T>
    const T* AbstractContainer<T>::ReadPtr::operator->()
    {
        return &(operator*());
    }

    ////// Container //////

    template<typename T, int maxReaders>
    template<typename... Args>
    Container<T, maxReaders>::Container(Args&&... args)
        : AbstractContainer<T>(maxReaders)
    {
        initialize(std::forward<Args>(args)...);
    }

    ////// ExpandableContainer //////

    template<typename T>
    template<typename... Args>
    ExpandableContainer<T>::ExpandableContainer(Args&&... args)
        : AbstractContainer<T>(1)
        , original(std::forward<Args>(args)...)
    {
        // copy original to ensure that T is copy-constructible
        initialize(original);
    }


    template<typename T>
    void ExpandableContainer<T>::increaseMaxReadersTo(int nReaders)
    {
        // step 1: ensure space in data
        std::lock_guard<std::mutex> dataSizeLock(dataSizeMutex);
        int newElementsNeeded = nReaders + 2 - data.size();
        for (int i = 0; i < newElementsNeeded; ++i)
        {
            data.push_back(original);
        }

        // step 2: allow more readers in sync manager
        sync.ensureSpaceForReaders(nReaders);
    }


    template <typename T>
    bool ExpandableContainer<T>::map(std::function<void(T&)> f)
    {
        // make sure we don't start expanding "data" while this is happening
        std::lock_guard<std::mutex> dataSizeLock(dataSizeMutex);

        // apply to data
        if (AbstractContainer<T>::map(f))
        {
            // apply to "original"
            f(original);
            return true;
        }
        return false;
    }


    template<typename T>
    ExpandableContainer<T>::ReadPtr::ReadPtr(ExpandableContainer<T>& o)
        : AbstractContainer<T>::ReadPtr(o)
    {
        while (!tryToMakeValid())
        {
            o.increaseMaxReadersTo(o.getMaxReaders() + 1);
        }        
    }
}
