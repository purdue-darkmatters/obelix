#include <cstdlib>
#include <iostream>
#include <string>

struct Blah {
    int one;
    int two;
    int three;
};

int main()
{
    Blah x = Blah{1,2,3};
    std::cout << x.one << x.two << x.three << "\n";
    return 0;
}