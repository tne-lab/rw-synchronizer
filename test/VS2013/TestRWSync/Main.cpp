/*
*  Copyright (C) 2019 Ethan Blackwood
*  This is free software released under the MIT license.
*  See attached LICENSE file for more details, or https://opensource.org/licenses/MIT.
*/

// TODO: Make a more commprehensive series of tests!

#include <iostream>
#include <cstdio>
#include <tchar.h>

#include "../../../RWSync/Source/RWSyncContainer.h"

class NonCopyable
{
public:
    NonCopyable(int i) : a(i) {}
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

private:
    int a;
};

static RWSync::ExpandableContainer<int> syncedInt(0);

int _tmain(int argc, _TCHAR* argv[])
{
    std::cout << "Container initialized with 1 reader and value 0" << std::endl;
    std::cout << "Allocated readers is now " << syncedInt.numAllocatedReaders() << std::endl;

    // Test manually expanding # of readers
    std::cout << "Increasing max readers to 3" << std::endl;
    syncedInt.increaseMaxReadersTo(3);

    std::cout << "Allocated readers is now " << syncedInt.numAllocatedReaders() << std::endl;

    std::cout << "Incrementing each instance of data by 1" << std::endl;
    syncedInt.map([](int& data){ data++; });

    std::cout << "There should be 6 instances of data: 3 readers, 2 extra, plus the 'original' "
        "for creating new copies." << std::endl;
    int i = 1;
    syncedInt.map([&](int& data)
    {
        std::cout << "Data " << i << " = " << data << std::endl;
        ++i;
    });

    // Test dynamically expanding # of readers
    std::cout << "Checking out 4 readers, which should increase the size by 1" << std::endl;
    std::deque<RWSync::GuaranteedReadPtr<int>> readers;
    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back(syncedInt);
    }

    std::cout << "Allocated readers is now " << syncedInt.numAllocatedReaders() << std::endl;

    std::cout << "Press Enter to exit." << std::endl;

    getchar();

    // this should not compile:
    // RWSync::ExpandableContainer<NonCopyable> badContainer(0);
    // badContainer.increaseMaxReadersTo(2);

    // this should work:
    RWSync::FixedContainer<NonCopyable, 2> goodContainer(0);

    return 0;
}