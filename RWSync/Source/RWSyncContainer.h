#ifndef RW_SYNC_CONTAINER_H_INCLUDED
#define RW_SYNC_CONTAINER_H_INCLUDED

/*  
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include "RWSyncManager.h"

#include <functional>
#include <type_traits>
#include <memory>
#include <vector>

/*
 * Class to actually hold data controlled by a RWSync::Manager.
 * See documentation in Manager.h.
 */

namespace RWSync
{
    // Abstract base class that isn't a template over maxReaders
    // and thus has an ugly constructor signature
    template<typename T>
    class Container
    {
    public:
        int numAllocatedReaders() const;

        // Reset to state where no writes have been made.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        bool reset();

        // Call a function on each underlying data member.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        //
        // UnaryOperator should be convertible to std::function<void(T&)>.
        // Using a template instead minimizes runtime cost.
        template<typename UnaryOperator>
        bool map(UnaryOperator f);

        class WritePtr
        {
        public:
            explicit WritePtr(Container<T>& o);

            bool tryToMakeValid();

            // verify that we actually have a place to write
            bool isValid() const;

            // provide access to data
            T& operator*();
            T* operator->();

            // report that a write is complete an obtain new place to write
            void pushUpdate();

        private:
            Container<T>& owner;
            WriteIndex ind;
        };

        class ReadPtr
        {
        public:
            explicit ReadPtr(Container<T>& o);

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
            Container<T>& owner;
            ReadIndex ind;
        };

    protected:
        template<typename... Args>
        Container(int maxReaders, Args&&... args);

        // Requires that T is copy-constructible.
        void increaseMaxReadersTo(int nReaders);

    private:
        const bool expandable;

        Manager manager;

        std::deque<T> data;
        std::unique_ptr<T> original; // as in "original copy"

        std::mutex dataSizeMutex;

#ifdef OPEN_EPHYS
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Container);
#endif
    };


    // Container with fixed size
    template<typename T, int maxReaders = 1>
    class FixedContainer : public Container<T>
    {
    public:
        // Creates a container that allows maxReaders readers, initializing each
        // data instance with the given arguments.
        template<typename... Args>
        FixedContainer(Args&&... args);        
    };

    template<typename T>
    class ExpandableContainer : public Container<T>
    {
    public:
        // Creates a container that allows one reader but can be expanded.
        // Use increaseMaxReadersTo() to pre-allocate the number of readers that
        // will be needed, or use a GuaranteedReadPtr to do so automatically when needed.
        template<typename... Args>
        ExpandableContainer(Args&&... args);

        void increaseMaxReadersTo(int nReaders);

        // Only for copy-constructible T
        class GuaranteedReadPtr : public Container<T>::ReadPtr
        {
        public:

            // Makes sure there is space for
            // this read pointer by increasing the max # of readers
            // if necessary. Note that the pointer could still be
            // not ready for reading if nothing has been written yet,
            // so checking isValid is still necessary.
            explicit GuaranteedReadPtr(ExpandableContainer<T>& o);
        };
    };


    // for convenience:
    template<typename T>
    using WritePtr = typename Container<T>::WritePtr;

    template<typename T>
    using ReadPtr = typename Container<T>::ReadPtr;

    template<typename T>
    using GuaranteedReadPtr = typename ExpandableContainer<T>::GuaranteedReadPtr;
}

#include "RWSyncContainer.ipp"

#endif // RW_SYNC_CONTAINER_H_INCLUDED