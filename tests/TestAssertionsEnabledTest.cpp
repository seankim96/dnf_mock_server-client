#include <cassert>

#ifdef NDEBUG
#error "CTest targets must keep assert() enabled in every build configuration"
#endif

int main()
{
    assert(true);
    return 0;
}
