/*
*  Copyright (C) 2019 Ethan Blackwood
*  This is free software released under the MIT license.
*  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
*/

#include "Container.h"

#include <utility>

namespace RWSync
{
    ////// AbstractContainer //////

    template<typename T>
    AbstractContainer<T>::AbstractContainer(int maxReaders)
        : sync  (maxReaders)
        , size  (maxReaders + 2)
    {}

    template<typename T>
    template<typename... Args>
    void AbstractContainer<T>::initialize(Args&&... args)
    {
        for (int i = 0; i < size; ++i)
        {
            data.emplace_back(args...);
        }

        // move into original if possible
        original.reset(new T(std::forward<Args>(args)...));
    }


    template<typename T>
    bool AbstractContainer<T>::reset()
    {
        return sync.reset();
    }


    template<typename T>
    template<typename UnaryFunction>
    bool AbstractContainer<T>::map(UnaryFunction f)
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

        f(*original);

        return true;
    }


    /***** AbstractContainer::WritePtr ******/

    template<typename T>
    AbstractContainer<T>::WritePtr::WritePtr(AbstractContainer<T>& o)
        : owner (&o)
        , ind   (o.sync)
        , valid (ind.isValid())
    {}


    template<typename T>
    bool AbstractContainer<T>::WritePtr::isValid() const
    {
        return valid;
    }


    template<typename T>
    T& AbstractContainer<T>::WritePtr::operator*()
    {
        if (!valid)
        {
            throw new std::out_of_range("Attempt to access an invalid write pointer");
        }

        return owner->data[ind];
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
        : owner (&o)
        , ind   (o.sync)
        // if the ind is valid, but is equal to -1, this pointer is still invalid (for now)
        , valid (ind != -1)
    {}


    template<typename T>
    bool AbstractContainer<T>::ReadPtr::isValid() const
    {
        return valid;
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
        // in case ind is valid but was equal to -1:
        valid = ind != -1;
    }


    template<typename T>
    const T& AbstractContainer<T>::ReadPtr::operator*()
    {
        if (!valid)
        {
            throw new std::out_of_range("Attempt to access an invalid read pointer");
        }

        return owner->data[ind];
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
}
