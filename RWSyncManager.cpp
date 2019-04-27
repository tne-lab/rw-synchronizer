/*
 *  Copyright (C) 2019 Ethan Blackwood
 *  This is free software released under the MIT license.
 *  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
 */

#include "RWSyncManager.h"

#include <cstdint>
#include <cassert>
#include <exception>

namespace RWSync
{
    ////// Manager ///////

    Manager::Manager(int maxReaders)
        : nReaders      (0)
        , nWriters      (0)
    {
        if (maxReaders < 1 || maxReaders > INT_MAX - 2)
        {
            throw new std::domain_error("Max readers must be in range [1, INT_MAX - 2]");
        }

        readersOf.resize(maxReaders + 2);

        reset();
    }


    bool Manager::reset()
    {
        Lockout lock(*this);
        if (!lock.isValid())
        {
            return false;
        }

        writerIndex = 0;
        latest.store(-1, std::memory_order_relaxed);

        int currSize = readersOf.size();
        for (int i = 1; i < currSize; ++i)
        {
            readersOf[i].store(0, std::memory_order_relaxed);
        }
        readersOf[0].store(-1, std::memory_order_release);

        return true;
    }


    int Manager::getMaxReaders() const
    {
        return size() - 2;
    }


    void Manager::ensureSpaceForReaders(int newMaxReaders)
    {
        std::lock_guard<std::mutex> sizeGuard(sizeMutex);

        int currMaxReaders = getMaxReaders();
        if (currMaxReaders >= newMaxReaders)
        {
            return;
        }
        
        int nToAdd = newMaxReaders - currMaxReaders;
        for (int i = 0; i < nToAdd; ++i)
        {
            readersOf.emplace_back(0);
        }
    }


    int Manager::size() const
    {
        return readersOf.size();
    }


    bool Manager::checkoutWriter()
    {
        // ensure there is not already a writer
        int currWriters = 0;
        if (!nWriters.compare_exchange_strong(currWriters, 1, std::memory_order_acquire))
        {
            return false;
        }

        return true;
    }


    void Manager::returnWriter()
    {
        int oldNWriters = nWriters.exchange(0, std::memory_order_release);
        assert(oldNWriters == 1);
    }


    bool Manager::checkoutReader()
    {
        // ensure there are not already maxReaders readers
        int currReaders = 0;
        while (!nReaders.compare_exchange_weak(currReaders, currReaders + 1, std::memory_order_acquire))
        {
            if (currReaders >= getMaxReaders())
            {
                return false;
            }
        }

        return true;
    }


    void Manager::returnReader()
    {
        nReaders.fetch_sub(1, std::memory_order_release);
    }


    bool Manager::checkoutAllReaders(std::unique_lock<std::mutex>& lockToLock)
    {
        if (lockToLock.mutex() != &sizeMutex)
        {
            return false;
        }

        lockToLock.lock();

        int currMaxReaders = getMaxReaders();
        int currReaders = 0;
        if (!nReaders.compare_exchange_strong(currReaders, currMaxReaders, std::memory_order_acquire))
        {
            lockToLock.unlock();
            return false;
        }

        return true;
    }

    
    void Manager::returnAllReaders(std::unique_lock<std::mutex>& lockToUnlock)
    {
        if (lockToUnlock.mutex() != &sizeMutex)
        {
            return;
        }

        nReaders.store(0, std::memory_order_release);
        lockToUnlock.unlock();
    }


    void Manager::pushWrite()
    {
        // It's an invariant that writerIndex != -1
        // except within this method, and this method is not reentrant.
        assert(writerIndex != -1);

        readersOf[writerIndex].store(0, std::memory_order_relaxed);

        // see comment in ReadIndex::getLatest() for memory order explanation
        latest.store(writerIndex, std::memory_order_seq_cst);

        // at this point, the sum of readersOf must be in the range [0, maxReaders] and all entries
        // are positive. since the length of readersOf is size == maxReaders + 2, at least 2 entries
        // must equal 0. one of these may be writerIndex a.k.a. latest, which we skip. so there must
        // be at least one instance that can be identified to write to next in the following loop.

        int newWriterIndex = -1;
        int currSize = size();
        for (int i = 0; i < currSize; ++i)
        {
            if (i == writerIndex) { continue; } // don't overwrite what we just wrote!

            int expected = 0;
            // see comment in ReadIndex::getLatest() for memory order explanation
            if (readersOf[i].compare_exchange_strong(expected, -1, std::memory_order_seq_cst))
            {
                newWriterIndex = i;
                break;
            }
        }

        assert(newWriterIndex != -1);
        writerIndex = newWriterIndex;
    }

    /***** WriteIndex *****/

    WriteIndex::WriteIndex(Manager& o)
        : owner(&o)
        , valid(o.checkoutWriter())
    {
        if (!valid)
        {
            // just to be sure - if not valid, shouldn't be able to access the manager
            owner = nullptr;
        }
    }

    
    WriteIndex::~WriteIndex()
    {
        if (valid)
        {
            owner->returnWriter();
        }
    }


    bool WriteIndex::isValid() const
    {
        return valid;
    }


    WriteIndex::operator int() const
    {
        if (valid)
        {
            return owner->writerIndex;
        }
        return -1;
    }


    void WriteIndex::pushUpdate()
    {
        if (valid)
        {
            owner->pushWrite();
        }
    }

    /***** ReadIndex *****/

    ReadIndex::ReadIndex(Manager& o)
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


    ReadIndex::~ReadIndex()
    {
        if (valid)
        {
            finishRead();
            owner->returnReader();
        }
    }


    bool ReadIndex::isValid() const
    {
        return valid && index != -1;
    }


    bool ReadIndex::hasUpdate() const
    {
        int newLatest = owner->latest.load(std::memory_order_relaxed);
        return valid && newLatest != -1 && newLatest != index;
        // even if the latest is different by the time it's pulled, it won't be
        // the one that this reader is currently reading.
    }


    void ReadIndex::pullUpdate()
    {
        if (!valid || !hasUpdate())
        {
            return;
        }

        finishRead();
        getLatest();
    }


    ReadIndex::operator int() const
    {
        if (valid)
        {
            return index;
        }
        return -1;
    }


    void ReadIndex::finishRead()
    {
        if (index != -1)
        {
            // decrement reader count for current instance
            // see comment in getLatest()
            owner->readersOf[index].fetch_sub(1, std::memory_order_seq_cst);
        }
        index = -1;
    }


    void ReadIndex::getLatest()
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

    /***** Manager::Lockout *****/

    Manager::Lockout::Lockout(Manager& o)
        : owner         (&o)
        , sizeLock      (o.sizeMutex, std::defer_lock)
        , hasReadLock   (o.checkoutAllReaders(sizeLock))
        , hasWriteLock  (o.checkoutWriter())
        , valid         (hasReadLock && hasWriteLock)
    {}

    
    Manager::Lockout::~Lockout()
    {
        if (hasReadLock)
        {
            owner->returnAllReaders(sizeLock);
        }

        if (hasWriteLock)
        {
            owner->returnWriter();
        }
    }


    bool Manager::Lockout::isValid() const
    {
        return valid;
    }
}
