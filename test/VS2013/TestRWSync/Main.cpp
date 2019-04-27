#include <iostream>
#include <tchar.h>

#include "../../../RWSyncContainer.h"

static RWSync::Container<int> syncedInt(3);

int _tmain(int argc, _TCHAR* argv[])
{
    syncedInt.map([](int& data){ data++; });

    int i = 0;
    syncedInt.map([&](int& data)
    {
        std::cout << "Data " << i << " = " << data << std::endl;
        ++i;
    });

    std::cout << "Press Ctrl-C to exit." << std::endl;

    char c;
    std::cin >> c;

    return 0;
}