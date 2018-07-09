
#include <format.h>
#include <printf.h>
#include <ostream.h>
#include <conduit/conduit.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <queue>
#include <random>



// This file shows how to create a delayed function call (including delayed args).





int foo(int x)
{
  fmt::printf("foo(%d) called\n", x);
  return x+1;
}



int main(int argc, char const *argv[])
{
 
  auto my_delayed_call = conduit::make_delayed(foo, 5);     // This says, "I want to call 'foo(5)' at some point in the future."




  // Blah, blah, blah. Sometetime later...




  my_delayed_call();   // This is like calling "foo(5)". It prints "foo(5) called". It also returns 6.


  

  return 0;
}

