#ifndef RW_SYNC_CONTAINER_H_INCLUDED
#define RW_SYNC_CONTAINER_H_INCLUDED

/*  
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include "RWSyncManager.h"

#include <functional>

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

        int getMaxReaders() const;
        
        // Reset to state where no writes have been made.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        bool reset();

        // Call a function on each underlying data member.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        virtual bool map(std::function<void(T&)> f);

        class WritePtr
        {
        public:
            explicit WritePtr(AbstractContainer<T>& o);

            bool tryToMakeValid();

            // verify that we actually have a place to write
            bool isValid() const;

            // provide access to data
            T& operator*();
            T* operator->();

            // report that a write is complete an obtain new place to write
            void pushUpdate();

        private:
            AbstractContainer<T>& owner;
            WriteIndex ind;
        };

        class ReadPtr
        {
        public:
            explicit ReadPtr(AbstractContainer<T>& o);

            bool tryToMakeValid();

            // check whether this read pointer is registered as a
            // legitimate reader, i.e. it hasn't been locked out
            bool isValid() const;

            // verify that we actually have something to read
            bool canRead() const;

            // check if there's an update available
            bool hasUpdate() const;

            // get latest data from the writer
            void pullUpdate();

            // provide access to data
            const T& operator*();
            const T* operator->();

        private:
            AbstractContainer<T>& owner;
            ReadIndex ind;
        };

    protected:
        AbstractContainer(int maxReaders);

        template<typename... Args>
        void initialize(Args&&... args);

        std::deque<T> data;
        Manager sync;
    };

    // Fixed-size container
    template<typename T, int maxReaders = 1>
    class Container : public AbstractContainer<T>
    {
    public:
        template<typename... Args>
        Container(Args&&... args);
    };

    // Dynamic-size container
    // Requires that T is copyable.
    template<typename T>
    class ExpandableContainer : public AbstractContainer<T>
    {
    public:
        // Initially allows 1 reader
        template<typename... Args>
        ExpandableContainer(Args&&... args);

        void increaseMaxReadersTo(int nReaders);

        bool map(std::function<void(T&)> f) override;

        class ReadPtr : public AbstractContainer<T>::ReadPtr
        {
        public:
            // Makes sure there is space for
            // this read pointer by increasing the max # of readers
            // if necessary. Note that the pointer could still be
            // not ready for reading if nothing has been written yet,
            // so checking isValid is still necessary.
            explicit ReadPtr(ExpandableContainer<T>& o);
        };

    private:
        std::mutex dataSizeMutex;
        T original; // as in "original copy"
    };

    template<typename T>
    using WritePtr = typename AbstractContainer<T>::WritePtr;

    template<typename T>
    using ReadPtr = typename AbstractContainer<T>::ReadPtr;

    template<typename T>
    using GuaranteedReadPtr = typename ExpandableContainer<T>::ReadPtr;
}

#include "RWSyncContainer.ipp"

#endif // RW_SYNC_CONTAINER_H_INCLUDED