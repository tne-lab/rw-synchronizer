#ifndef RW_SYNC_MANAGER_H_INCLUDED
#define RW_SYNC_MANAGER_H_INCLUDED

/*
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include <cstdlib>

#include <atomic>
#include <mutex>
#include <deque>

/*
 * The purpose of RWSync is to allow one "writer" thread to continally
 * update some arbitrary piece of information and N "reader" threads to retrieve
 * the latest version of that information that has been "pushed" by the writer,
 * without any thread having to wait to acquire a mutex or allocate memory
 * on the heap (aside from internal allocations depending on the data structure used).
 * For one reader and one writer, this requires three instances of whatever data type is
 * being shared to be allocated upfront; for an arbitrary number of readers N, it requires
 * N + 2 instances. During operation, "pushing" from the writer and
 * "pulling" to a reader are accomplished by exchanging atomic indices between slots
 * indicating what each instance is to be used for, rather than any actual copying or allocation.
 *
 * There are two interfaces to choose from:
 *
 *  - In most cases, the logic can be wrapped together with data
 *    allocation and lifetime management by using the RWSync::Container<T> class template.
 *    Here, T is the type of data that needs to be exchanged.
 *
 *      * The constructor for a Container<T, N> simply takes whatever arguments
 *        would be used to construct each T object and constructs N + 2 copies. (If a constructor
 *        with move semantics would be used, this is called for one of the copies, and the
 *        others use copying constructors instead.)
 *
 *      * Any configuration that applies to all copies can by done by calling the "apply" method
 *        with a function pointer or lambda, as long as there are no active readers or writers.
 *
 *      * To write, construct a RWSync::WritePtr<T, N> with the Container<T, N>&
 *        as an argument. This can be used as a normal pointer. It can be acquired, written to,
 *        and released multiple times and will keep referring to the same instance until the
 *        pushUpdate() method is called, at which point this instance is released for reading
 *        and a new one is acquired for writing (which might need to be cleared/reset first).
 *        
 *      * To read, construct a RWSync::ReadPtr<T, N> with the Container<T, N>& as
 *        an argument. This can be used as a normal pointer, and if you want to get the
 *        latest update from the writer without destroying the read ptr and constructing
 *        a new one, you can call the pullUpdate() method.
 *
 *      * If you attempt to create two write pointers to the same Container, the
 *        second one will be effectively null; you can check for this with the isValid() method
 *        (if isValid() ever returns false, this should be considered a bug). The same is true
 *        of read pointers, except that a read pointer acquired before any writes have occurred
 *        is also invalid (since there's nothing to read), so the isValid() check is necessary
 *        even if you know for sure there are never more than N simulataneous readers.
 *
 *      * The hasUpdate() method on an AtomicScopedReadPtr returns true if there is new data
 *        from the writer that has not been read by this reader yet. After a call to hasUpdate() returns
 *        true, the current read ptr is guaranteed to be valid after calling pullUpdate().
 *
 *      * The reset() method brings you back to the state where no writes have been performed yet.
 *        Must be called when no read or write pointers exist.
 *
 *  - Using an RWSync::Manager directly works similarly; the main difference is that you
 *    are responsible for allocating and accessing the data, and the Manager just
 *    tells you which index to use as a reader or writer.
 *
 *      * To write, use an RWSync::WriteIndex instead of a WritePtr.
 *        This can be converted to int to use directly as an index, and has a pushUpdate() method
 *        that works the same way as for the write pointer. The index can be -1 if you try to
 *        create two write indices to the same synchronizer.
 *
 *      * To read, use an RWSync::ReadIndex instead of a ReadPtr.
 *        This works how you would expect and also has a pullUpdate() method. Check whether it is
 *        valid before using by calling isValid() if you're not using hasUpdate().
 *
 *      * RWSync::Lockout is just a try-lock for both readers and writers; it will be "valid"
 *        iff no read or write indices exist at the point of construction. By constructing
 *        one of these and proceeding only if it is valid, you can make changes to each data
 *        instance outside of the reader/writer framework (instead of Container<T, N>::apply).
 *
 */

namespace RWSync
{
    class Manager
    {
    public:
        explicit Manager(int maxReaders = 1);

        Manager(const Manager&) = delete;
        Manager& operator=(const Manager&) = delete;

        // Reset to state with no valid object
        // No readers or writers should be active when this is called!
        // If it does fail due to existing readers or writers, returns false
        bool reset();

        int getMaxReaders() const;

        // Expands maximum simultaneous readers (this involves allocating memory).
        // If the current max readers is already equal to or greater than the
        // input, does nothing.
        void ensureSpaceForReaders(int newMaxReaders);

        class WriteIndex
        {
        public:
            explicit WriteIndex(Manager& o);

            WriteIndex(const WriteIndex&) = delete;
            WriteIndex& operator=(const WriteIndex&) = delete;

            ~WriteIndex();

            // tries to claim writer status if we don't have it
            // already - returns true if the write index is now valid.
            bool tryToMakeValid();

            // is there actually a place to write?
            bool isValid() const;

            // index to access the correct data instance
            operator int() const;

            // push a finished write to readers
            void pushUpdate();

        private:
            Manager& owner;
            bool valid;
        };


        class ReadIndex
        {
        public:
            explicit ReadIndex(Manager& o);                

            ReadIndex(const ReadIndex&) = delete;
            ReadIndex& operator=(const ReadIndex&) = delete;

            ~ReadIndex();

            // tries to claim reader status if we don't have it
            // already - returns true if the write index is now valid.
            bool tryToMakeValid();

            // check whether a reader has been checked out successfully
            bool isValid() const;

            // check whether a reader has been checked out and there
            // has been at least one write.
            bool canRead() const;

            // check whether a new write has been pushed
            bool hasUpdate() const;            

            // update the index, if a new version is available
            void pullUpdate();

            // index to access the correct data instance
            operator int() const;

        private:
            // signal that we are not longer reading from the `index`th instance
            void finishRead();

            // update index to refer to the latest update
            void getLatest();

            Manager& owner;
            bool valid;
            int index;
        };


        // Registers as a writer and maxReader readers, so no other reader or writer
        // can exist while it's held. Use to access all the underlying data without
        // conern for who has access to what, e.g. for updating settings, resizing, etc.
        class Lockout
        {
        public:
            explicit Lockout(Manager& o);
            ~Lockout();

            bool isValid() const;

        private:
            Manager* owner;
            std::unique_lock<std::mutex> sizeLock;
            const bool hasReadLock;
            const bool hasWriteLock;
            const bool valid;
        };

    private:

        int size() const;

        // Registers a writer. If a writer already exists,
        // returns false, else returns true. returnWriter should be called to release.
        bool checkoutWriter();
        void returnWriter();

        // Registers a reader and updates the reader index. If maxReaders readers already exist,
        // returns false, else returns true. returnReader should be called to release.
        bool checkoutReader();
        void returnReader();

        // Tries to register maxReaders readers, so that no other thread may have a reader
        // if it is successful. returnAllReaders should be called to release.
        bool checkoutAllReaders(std::unique_lock<std::mutex>& lockToLock);
        void returnAllReaders(std::unique_lock<std::mutex>& lockToUnlock);

        // Makes newly written data available and finds a new place to write. Should
        // only ever be called by the writer.
        void pushWrite();

        std::atomic<int> nWriters;
        std::atomic<int> nReaders;

        int writerIndex;

        std::atomic<int> latest;

        std::mutex sizeMutex; // protects the length of readersOf

        // deque is one of the few data structures that can contain atomics
        std::deque<std::atomic<int>> readersOf;
        // If readersOf[i] == -1, this indicates that it's being written to.
        // In other words, readersOf[writerIndex] == -1 (but readers should not access writerIndex directly).
    };

    using WriteIndex = Manager::WriteIndex;
    using ReadIndex = Manager::ReadIndex;
}

#endif // RW_SYNC_MANAGER_H_INCLUDED
