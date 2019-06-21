#ifndef RW_SYNC_MANAGER_H_INCLUDED
#define RW_SYNC_MANAGER_H_INCLUDED

/*
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include <atomic>
#include <mutex>
#include <deque>

#ifdef OEPLUGIN
#define OPEN_EPHYS
#endif

#ifdef OPEN_EPHYS
#include <CommonLibHeader.h>
#else
#define COMMON_LIB
#endif


namespace RWSync
{
    class COMMON_LIB Manager
    {
    public:
        explicit Manager(int maxReaders = 1);

        // Reset to state with no valid object
        // No readers or writers should be active when this is called!
        // If it does fail due to existing readers or writers, returns false
        bool reset();

        int getMaxReaders() const;

        // Expands maximum simultaneous readers (this involves allocating memory).
        // If the current max readers is already equal to or greater than the
        // input, does nothing.
        void ensureSpaceForReaders(int newMaxReaders);

        class COMMON_LIB WriteIndex
        {
        public:
            explicit WriteIndex(Manager& o);

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


        class COMMON_LIB ReadIndex
        {
        public:
            explicit ReadIndex(Manager& o);                

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
        class COMMON_LIB Lockout
        {
        public:
            explicit Lockout(Manager& o);
            ~Lockout();

            bool isValid() const;

        private:
            Manager& owner;
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

#ifdef OPEN_EPHYS
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Manager);
#endif
    };

    using WriteIndex = Manager::WriteIndex;
    using ReadIndex = Manager::ReadIndex;
}

#endif // RW_SYNC_MANAGER_H_INCLUDED
