
#ifndef CONDUIT_INCLUDE_BINDER_H_
#define CONDUIT_INCLUDE_BINDER_H_

namespace conduit
{

// This class helps create non-owning bindings to member functions.

template <typename... T> struct Binder;
template <typename C, typename R, typename... T>
struct Binder<C, R (C::*)(T...)> {
    C *c;
    R (C::*p)(T...);
    Binder(C *c_, R (C::*p_)(T...)) : c(c_), p(p_) {}
    R operator()(T... t)
    {
        return (c->*p)(t...);
    }
};
template <typename C, typename R, typename... T>
struct Binder<C, R (C::*)(T...) const> {
    const C *c;
    R (C::*p)(T...) const;
    Binder(C *c_, R (C::*p_)(T...) const) : c(c_), p(p_) {}
    R operator()(T... t)
    {
        return (c->*p)(t...);
    }
};


//
// Suggested usage, assuming "a" is an instance of class "A"...
//   auto func = conduit::make_binder(&a, &A::foo);    // Create a handle for calling "a.foo()"  
//   func(5);                                          // This is like calling "a.foo(5)".
//

template <typename C, typename R, typename... T>
Binder<C, R (C::*)(T...)> make_binder(C *c, R (C::*p)(T...))
{
    return Binder<C, R (C::*)(T...)>(c, p);
}
template <typename C, typename R, typename... T>
Binder<C, R (C::*)(T...) const> make_binder(C *c, R (C::*p)(T...) const)
{
    return Binder<C, R (C::*)(T...) const>(c, p);
}


} // namespace-conduit


#endif




