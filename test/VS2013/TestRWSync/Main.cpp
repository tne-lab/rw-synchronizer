#include <iostream>
#include <tchar.h>

#include "../../../RWSyncContainer.h"

static RWSync::ExpandableContainer<int> syncedInt(2);

int _tmain(int argc, _TCHAR* argv[])
{
    std::cout << "Max readers is now " << syncedInt.getMaxReaders() << std::endl;

    syncedInt.increaseMaxReadersTo(3);

    std::cout << "Max readers is now " << syncedInt.getMaxReaders() << std::endl;

    syncedInt.map([](int& data){ data++; });

    int i = 0;
    syncedInt.map([&](int& data)
    {
        std::cout << "Data " << i << " = " << data << std::endl;
        ++i;
    });

    std::deque<RWSync::GuaranteedReadPtr<int>> readers;
    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back(syncedInt);
    }

    std::cout << "Max readers is now " << syncedInt.getMaxReaders() << std::endl;

    std::cout << "Press Ctrl-C to exit." << std::endl;

    char c;
    std::cin >> c;

    return 0;
}