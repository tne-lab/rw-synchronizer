#ifndef RW_SYNCHRONIZER_H_INCLUDED
#define RW_SYNCHRONIZER_H_INCLUDED

/*
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include <cassert>
#include <cstdlib>
#include <cstdint>

#include <atomic>
#include <vector>
#include <utility>

/*
 * The purpose of RWSynchronizer is to allow one "writer" thread to continally
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
 *  - In most cases, the RWSynchronizer logic can be wrapped together with data
 *    allocation and lifetime management by using the RWSynchronized<T> class template.
 *    Here, T is the type of data that needs to be exchanged.
 *
 *      * The constructor for an RWSynchronized<T, N> simply takes whatever arguments
 *        would be used to construct each T object and constructs N + 2 copies. (If a constructor
 *        with move semantics would be used, this is called for one of the copies, and the
 *        others use copying constructors instead.)
 *
 *      * Any configuration that applies to all copies can by done by calling the "apply" method
 *        with a function pointer or lambda, as long as there are no active readers or writers.
 *
 *      * To write, construct an AtomicScopedWritePtr<T, N> with the RWSynchronized<T, N>&
 *        as an argument. This can be used as a normal pointer. It can be acquired, written to,
 *        and released multiple times and will keep referring to the same instance until the
 *        pushUpdate() method is called, at which point this instance is released for reading
 *        and a new one is acquired for writing (which might need to be cleared/reset first).
 *        
 *      * To read, construct an AtomicScopedReadPtr<T, N> with the RWSynchronized<T, N>& as
 *        an argument. This can be used as a normal pointer, and if you want to get the
 *        latest update from the writer without destroying the read ptr and constructing
 *        a new one, you can call the pullUpdate() method.
 *
 *      * If you attempt to create two write pointers to the same RWSynchronized object, the
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
 *  - Using an RWSyncManager directly works similarly; the main difference is that you
 *    are responsible for allocating and accessing the data, and the RWSyncManager just
 *    tells you which index to use as a reader or writer.
 *
 *      * To write, use an AtomiSynchronizer::ScopedWriteIndex instead of an AtomicScopedWritePtr.
 *        This can be converted to int to use directly as an index, and has a pushUpdate() method
 *        that works the same way as for the write pointer. The index can be -1 if you try to
 *        create two write indices to the same synchronizer.
 *
 *      * To read, use an RWSyncManager::ScopedReadIndex instead of an AtomicScopedReadPtr.
 *        This works how you would expect and also has a pullUpdate() method. Check whether it is
 *        valid before using by calling isValid() if you're not using hasUpdate().
 *
 *      * ScopedLockout is just a try-lock for both readers and writers; it will be "valid"
 *        iff no read or write indices exist at the point of construction. By constructing
 *        one of these and proceeding only if it is valid, you can make changes to each data
 *        instance outside of the reader/writer framework (instead of RWSynchronized<T>::apply).
 *
 */

template<int maxReaders = 1>
class RWSyncManager {
    static_assert(maxReaders > 0, "An AtomicSynchronizer must have at least 1 reader");
    static_assert(maxReaders <= INT_MAX - 2, "An AtomicSynchronizer cannot have more than INT_MAX - 2 readers");
    static const int size = maxReaders + 2;

public:
    class ScopedWriteIndex
    {
    public:
        explicit ScopedWriteIndex(RWSyncManager& o)
            : owner(&o)
            , valid(o.checkoutWriter())
        {
            if (!valid)
            {
                // just to be sure - if not valid, shouldn't be able to access the synchronizer
                owner = nullptr;
            }
        }

        ScopedWriteIndex(const ScopedWriteIndex&) = delete;
        ScopedWriteIndex& operator=(const ScopedWriteIndex&) = delete;

        ~ScopedWriteIndex()
        {
            if (valid)
            {
                owner->returnWriter();
            }
        }

        // push a write to the reader without releasing writer privileges
        void pushUpdate()
        {
            if (valid)
            {
                owner->pushWrite();
            }
        }

        operator int() const
        {
            if (valid)
            {
                return owner->writerIndex;
            }
            return -1;
        }

        bool isValid() const
        {
            return valid;
        }

    private:
        RWSyncManager* owner;
        const bool valid;
    };


    class ScopedReadIndex
    {
    public:
        explicit ScopedReadIndex(RWSyncManager& o)
            : owner(&o)
            , valid(o.checkoutReader())
        {
            if (valid)
            {
                getLatest();
            }
            else
            {
                // just to be sure - if not valid, shouldn't be able to access the synchronizer
                owner = nullptr;
            }
        }

        ScopedReadIndex(const ScopedReadIndex&) = delete;
        ScopedReadIndex& operator=(const ScopedReadIndex&) = delete;

        ~ScopedReadIndex()
        {
            if (valid)
            {
                finishRead();
                owner->returnReader();
            }
        }

        bool hasUpdate() const
        {
            int newLatest = owner->latest.load(std::memory_order_relaxed);
            return valid && newLatest != -1 && newLatest != index;
            // even if the latest is different by the time it's pulled, it won't be
            // the one that this reader is currently reading.
        }

        // update the index, if a new version is available
        void pullUpdate()
        {
            if (!valid || !hasUpdate())
            {
                return;
            }
            
            finishRead();
            getLatest();
        }

        operator int() const
        {
            if (valid)
            {
                return index;
            }            
            return -1;
        }

        bool isValid() const
        {
            return valid && index != -1;
        }

    private:
        // signal that we are not longer reading from the `index`th instance
        void finishRead()
        {
            if (index != -1)
            {
                // decrement reader count for current instance
                // see comment in getLatest()
                int oldReaders = owner->readersOf[index].fetch_sub(1, std::memory_order_seq_cst);
                assert(oldReaders > 0 && oldReaders <= maxReaders);
            }
            index = -1;
        }

        // update index to refer to the latest update
        void getLatest()
        {
            /*
             We want to prevent any reader from "occupying 2 places" in readersOf by decrementing one entry
             and incrementing another that is not that actual latest while the writer is searching for the
             next write index. To accomplish this we make some of the loads and stores of readersOf and latest seq_cst.

             If the single total modification order places a write to "latest" after the decrement that
             may occur in finishRead, this call may not get that updated value of "latest," but it's OK
             because the writer thread is guaranteed to observe that decrement by the time "latest" is
             modified and the loop to find the next write index begins. If on the other hand the write
             to "latest" is ordered before the decrement, this load is guaranteed to see that updated
             value and increment the actual latest index (in the context of the current call to pushWrite())
             below, rather than some other index that might otherwise have been the next write index.
            */
            index = owner->latest.load(std::memory_order_seq_cst);

            if (index != -1)
            {
                int latestReaders = 0;
                while (!owner->readersOf[index].compare_exchange_weak(latestReaders, latestReaders + 1,
                    std::memory_order_relaxed))
                {
                    if (latestReaders == -1)
                    {
                        // can't read this anymore, it's being written to
                        // another latest must have been designated
                        index = owner->latest.load(std::memory_order_relaxed);
                        assert(index != -1); // should never be -1 again if it wasn't before
                        latestReaders = 0;
                    }
                }
            }
        }

        RWSyncManager* owner;
        bool valid;
        int index;
    };


    // Registers as a writer and maxReader readers, so no other reader or writer
    // can exist while it's held. Use to access all the underlying data without
    // conern for who has access to what, e.g. for updating settings, resizing, etc.
    class ScopedLockout
    {
    public:
        explicit ScopedLockout(RWSyncManager& o)
            : owner         (&o)
            , hasReadLock   (o.checkoutAllReaders())
            , hasWriteLock  (o.checkoutWriter())
            , valid         (hasReadLock && hasWriteLock)
        {}

        ~ScopedLockout()
        {
            if (hasReadLock)
            {
                for (int i = 0; i < maxReaders; ++i)
                {
                    owner->returnReader();
                }
            }

            if (hasWriteLock)
            {
                owner->returnWriter();
            }
        }

        bool isValid() const
        {
            return valid;
        }

    private:
        RWSyncManager* owner;
        const bool hasReadLock;
        const bool hasWriteLock;
        const bool valid;
    };


    RWSyncManager()
	: nReaders(0)
	, nWriters(0)
    {
        reset();
    }

    RWSyncManager(const RWSyncManager&) = delete;
    RWSyncManager& operator=(const RWSyncManager&) = delete;

    // Reset to state with no valid object
    // No readers or writers should be active when this is called!
    // If it does fail due to existing readers or writers, returns false
    bool reset()
    {
        ScopedLockout lock(*this);
        if (!lock.isValid())
        {
            return false;
        }

        writerIndex = 0;
        latest.store(-1, std::memory_order_relaxed);

        readersOf[0].store(-1, std::memory_order_relaxed);
        for (int i = 1; i < size; ++i)
        {
            readersOf[i].store(0, std::memory_order_relaxed);
        }

        return true;
    }

private:

    // Registers a writer. If a writer already exists,
    // returns false, else returns true. returnWriter should be called to release.
    bool checkoutWriter()
    {
        // ensure there is not already a writer
        int currWriters = 0;
        if (!nWriters.compare_exchange_strong(currWriters, 1, std::memory_order_acquire))
        {
            return false;
        }

        return true;
    }

    void returnWriter()
    {
        int oldNWriters = nWriters.exchange(0, std::memory_order_release);
        assert(oldNWriters == 1);
    }

    // Registers a reader and updates the reader index. If maxReaders readers already exist,
    // returns false, else returns true. returnReader should be called to release.
    bool checkoutReader()
    {
        // ensure there are not already maxReaders readers
        int currReaders = 0;
        while (!nReaders.compare_exchange_weak(currReaders, currReaders + 1, std::memory_order_acquire))
        {
            if (currReaders >= maxReaders)
            {
                return false;
            }
        }

        return true;
    }

    bool checkoutAllReaders()
    {
        int currReaders = 0;
        if (!nReaders.compare_exchange_strong(currReaders, maxReaders, std::memory_order_acquire))
        {
            return false;
        }

        return true;
    }

    void returnReader()
    {
        int oldNReaders = nReaders.fetch_sub(1, std::memory_order_release);
        assert(oldNReaders > 0 && oldNReaders <= maxReaders);
    }

    // should only be called by the writer
    void pushWrite()
    {
        // It's an invariant that writerIndex != -1
        // except within this method, and this method is not reentrant.
        assert(writerIndex != -1);

        readersOf[writerIndex].store(0, std::memory_order_relaxed);
        // see comment in ScopedReadIndex::getLatest() for memory order explanation
        latest.store(writerIndex, std::memory_order_seq_cst);

        // at this point, the sum of readersOf must be in the range [0, maxReaders] and all entries
        // are positive. since the length of readersOf is size == maxReaders + 2, at least 2 entries
        // must equal 0. one of these may be writerIndex a.k.a. latest, which we skip. so there must
        // be at least one instance that can be identified to write to next in the following loop.

        int newWriterIndex = -1;
        for (int i = 0; i < size; ++i)
        {
            if (i == writerIndex) { continue; } // don't overwrite what we just wrote!

            int expected = 0;
            // see comment in ScopedReadIndex::getLatest() for memory order explanation
            if (readersOf[i].compare_exchange_strong(expected, -1, std::memory_order_seq_cst))
            {
                newWriterIndex = i;
                break;
            }
        }

        assert(newWriterIndex != -1);
        writerIndex = newWriterIndex;
    }


    int writerIndex;

    std::atomic<int> latest;
    std::atomic<int> readersOf[size];
    // If readersOf[i] == -1, this indicates that it's being written to.
    // In other words, readersOf[writerIndex] == -1 (but readers should not access writerIndex directly).

    std::atomic<int> nWriters;
    std::atomic<int> nReaders;
};

#endif // RW_SYNCHRONIZER_H_INCLUDED
