#ifndef RW_SYNCED_H_INCLUDED
#define RW_SYNCED_H_INCLUDED

/*  
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include "Manager.h"

#include <memory>

/*
 * Class to actually hold data controlled by a RWSync::Manager.
 * See documentation in Manager.h.
 */

namespace RWSync
{
    // Container that could have either fixed or dynamic size
    template<typename T>
    class AbstractContainer
    {
    public:
        AbstractContainer() = delete;
        
        // Reset to state where no writes have been made.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        bool reset();

        // Call a function on each underlying data member.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        template<typename UnaryFunction>
        bool map(UnaryFunction f);

        class WritePtr
        {
        public:
            WritePtr(AbstractContainer<T>& o);

            // verify that we actually have a place to write
            bool isValid() const;

            // provide access to data
            T& operator*();
            T* operator->();

            // report that a write is complete an obtain new place to write
            void pushUpdate();

        private:
            AbstractContainer<T>* owner;
            WriteIndex ind;
            const bool valid;
        };

        class ReadPtr
        {
        public:
            ReadPtr(AbstractContainer<T>& o);

            // verify that we actually have something to read
            bool isValid() const;

            // check if there's an update available
            bool hasUpdate() const;

            // get latest data from the writer
            void pullUpdate();

            // provide access to data
            const T& operator*();
            const T* operator->();

        private:
            AbstractContainer<T>* owner;
            ReadIndex ind;
            bool valid;
        };

    protected:
        AbstractContainer(int maxReaders);

        template<typename... Args>
        void initialize(Args&&... args);

    private:
        std::deque<T> data;
        std::unique_ptr<T> original;
        Manager sync;

        int size;
    };

    template<typename T>
    using WritePtr = typename AbstractContainer<T>::WritePtr;

    template<typename T>
    using ReadPtr = typename AbstractContainer<T>::ReadPtr;

    template<typename T, int maxReaders = 1>
    class Container : public AbstractContainer<T>
    {
    public:
        template<typename... Args>
        Container(Args&&... args);
    };
}

#include "Container.ipp"

#endif // RW_SYNCED_H_INCLUDED