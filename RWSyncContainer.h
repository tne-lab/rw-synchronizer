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
    // Container that could have either fixed or dynamic size
    template<typename T>
    class Container
    {
    public:
        // Creates a container with one reader, initializing each
        // data instance with the given arguments. If T is copyable,
        // can be expanded to allow more readers by calling
        // increaseMaxReadersTo.
        template<typename... Args>
        Container(const Args&... args);

        // Factory for fixed-size container with more than one reader
        template<int maxReaders, typename... Args>
        static Container<T> createWithMaxReaders(const Args&... args);

        // = default doesn't work for these in VS2013
        Container(Container<T>&& other);
        Container<T>& operator=(Container<T>&& other);

        int getMaxReaders() const;

        // Requires that T is copy-constructible.
        void increaseMaxReadersTo(int nReaders);
        
        // Reset to state where no writes have been made.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        bool reset();

        // Call a function on each underlying data member.
        // Requires that no readers or writers exist. Returns false if
        // this condition is unmet, true otherwise.
        bool map(std::function<void(T&)> f);

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

        // Only for copy-constructible T
        class GuaranteedReadPtr : public ReadPtr
        {
        public:
            // Makes sure there is space for
            // this read pointer by increasing the max # of readers
            // if necessary. Note that the pointer could still be
            // not ready for reading if nothing has been written yet,
            // so checking isValid is still necessary.
            explicit GuaranteedReadPtr(Container<T>& o);
        };

    private:
        // unique pointers are to make sure we can move

        std::unique_ptr<Manager> manager;

        std::vector<std::unique_ptr<T>> data;
        std::unique_ptr<std::mutex> dataSizeMutex;

        std::unique_ptr<T> original; // as in "original copy"

        static const bool canExpand = std::is_copy_constructible<T>::value;
    };

    template<typename T>
    using WritePtr = typename Container<T>::WritePtr;

    template<typename T>
    using ReadPtr = typename Container<T>::ReadPtr;

    template<typename T>
    using GuaranteedReadPtr = typename Container<T>::GuaranteedReadPtr;
}

#include "RWSyncContainer.ipp"

#endif // RW_SYNC_CONTAINER_H_INCLUDED