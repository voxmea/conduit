
#ifndef CALLABLE_INFO_H_
#define CALLABLE_INFO_H_

#include <conduit/conduit-utility.h>
#include <conduit/function.h>
#include <tuple>
#include <functional>
#include <type_traits>

namespace conduit {
template <typename T, typename = detail::void_t<>> struct CallableInfo;

template <typename R, typename... T>
struct CallableInfo<R(T...)>
{
    using return_type = R;
    using tuple_parameter_type = std::tuple<std::decay_t<T>...>;
    using seq_type = std::index_sequence_for<T...>;
    using function_type = R (*)(std::decay_t<T>...);
    using signature = typename std::remove_pointer<function_type>::type;
};

template <typename C, typename R, typename... T>
struct CallableInfo<R (C::*)(T...)>
{
    using return_type = R;
    using tuple_parameter_type = std::tuple<std::decay_t<T>...>;
    using seq_type = std::index_sequence_for<T...>;
    using function_type = R (*)(std::decay_t<T>...);
    using signature = typename std::remove_pointer<function_type>::type;
};

template <typename C, typename R, typename... T>
struct CallableInfo<R (C::*)(T...) const>
{
    using return_type = R;
    using tuple_parameter_type = std::tuple<std::decay_t<T>...>;
    using seq_type = std::index_sequence_for<T...>;
    using function_type = R (*)(std::decay_t<T>...);
    using signature = typename std::remove_pointer<function_type>::type;
};

template <typename R, typename... T>
struct CallableInfo<R (*)(T...)>
{
    using return_type = R;
    using tuple_parameter_type = std::tuple<std::decay_t<T>...>;
    using seq_type = std::index_sequence_for<T...>;
    using function_type = R (*)(std::decay_t<T>...);
    using signature = typename std::remove_pointer<function_type>::type;
};

template <typename T_>
struct CallableInfo<T_, detail::void_t<decltype(&std::decay_t<T_>::operator ())>>
{
    using T = std::decay_t<T_>;
    using operator_call_type = decltype(&T::operator());
    using return_type = typename CallableInfo<operator_call_type>::return_type;
    using tuple_parameter_type = typename CallableInfo<operator_call_type>::tuple_parameter_type;
    using seq_type = typename CallableInfo<operator_call_type>::seq_type;
    using function_type = typename CallableInfo<operator_call_type>::function_type;
    using signature = typename std::remove_pointer<function_type>::type;
};

template <typename T_>
struct CallableInfo<conduit::Function<T_>> {
    using return_type = typename CallableInfo<T_>::return_type;
    using tuple_parameter_type = typename CallableInfo<T_>::tuple_parameter_type;
    using seq_type = typename CallableInfo<T_>::seq_type;
    using function_type = typename CallableInfo<T_>::function_type;
    using signature = typename CallableInfo<T_>::signature;
};

}

#endif
