
# Channel Views

A channel view maps from the original signature of a channel to either a subset of that signature, or an entirely new signature provided by a transformation function.

## Subset subscribe

The simplest case is a subset subscribe. This maps from the full channel signature to a subset of that signature. For example:

```c++
struct Foo {};
auto orig = reg.publish<void(int, double, Foo)>("name");
```

Given the above channel, we could subscribe to all, or any part of the signature:

```c++
reg.subscribe<void(int, double, Foo)>("name", [] (int i) {});
reg.subscribe<void(int, double, Foo)>("name", [] (double d) {});
reg.subscribe<void(int, double, Foo)>("name", [] (Foo f) {});
```

This can be useful when you want to map multiple channels back to the same subscriber function, and the information the subscriber function requires is always available on the channel (but not necessarily the only type on every channel).

A downside to subset subscribe is that the subscribe call still needs to specify the full signature of the channel. This is required because there is no requirement that producers come before subscribers. The following sections will discuss views, which do impose an ordering between a view's registration and a view's subscription.

### The type match process

When matching types on a subset subscribe, types are matched from left to right and cannot be skipped. Therefore, if a signature includes more than one instance of the same type, each instance must be matched left to right.

```c++
auto duplicate_types = reg.publish<void(int, int, int)>("name");
// if we want the second integer, we have to match and ignore the first integer
reg.subscribe<void(int, int, int)>("name", [] (int, int i) {});
```

## Subset view

Given the published channel above, we can also register "views" of that channel. This allows subsequent subscribers on those views to be unaware of the original channel signature, and to be able to subscribe without that information. For example:

```c++
// original publisher from above
auto orig = reg.publish<void(int, double, Foo)>("name");
// register views on that channel
reg.register_view<void(int)>(orig, "name.int");
reg.register_view<void(double)>(orig, "name.double");
reg.register_view<void(Foo)>(orig, "name.Foo");
reg.register_view<void(double, Foo)>(orig, "name.double_Foo");
```

This then allows the following subscribe calls:

```c++
reg.subscribe("name.int", [] (int) {});
reg.subscribe("name.double", [] (double) {});
reg.subscribe("name.Foo", [] (Foo) {});
```

Notice the above does not require the original channel signature. This allows dynamic subscription of views that meet a specific signature. The following iterates all views available in the `reg` Registrar and will subscribe to any that provide a single integer.

```c++
for (auto [name, _] : reg.views) {
    try {
        reg.subscribe(name, [] (int) {});
    } catch (const conduit::ConduitError &ex) {}
}
```

## Transformation views

It's also possible to register a view which doesn't match on a subset, but instead produces a new type, or types:

```c++
auto orig = reg.publish<void(int, double, Foo)>("name");
reg.register_view<void(int, std::string)>(orig, [] (int i) {
    return std::make_tuple(i * i, std::to_string(i * i));
}, "name.int.transformed");

reg.subscribe("name.int.transformed", [] (int, std::string) {});
```
