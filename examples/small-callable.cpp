
#include <format.h>
#include <printf.h>
#include <conduit/conduit.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <queue>
#include <random>



int foo(int x)
{
  fmt::printf("foo(%d) called\n", x);
  return x+1;
}




int main(int argc, char const *argv[])
{

  auto s = conduit::SmallCallable<void(int)>();


  s = foo;     // This binds "s" to "foo"


  if (!s)
    fmt::print("s is not bound\n");


  s(5);          // This is like calling foo(5). It prints "foo(5) called". It returns 6.


  return 0;
}

