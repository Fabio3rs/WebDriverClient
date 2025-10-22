Outcome 2.2 library documentation

Outcome 2.2 github repo
Acknowledgements
Prerequisites
Build and install
Review of Error Handling Frameworks
Motivation
Future ABI stability guarantees
Tutorial
Recipes
ASIO/Networking TS : Boost < 1.70
ASIO/Networking TS: Boost >= 1.70
Extending
OUTCOME_TRY
Rust FFI
Experimental
API reference
Frequently asked questions
Videos
Changelog
History
Search...
ASIO/Networking TS: Boost >= 1.70
Thanks to Christos Stratopoulos for this Outcome recipe.

Compatibility note
This recipe targets Boost versions including and after 1.70, where coroutine support is based around the asio::use_awaitable completion token. For integration with Boost versions before 1.70, see this recipe.

Use case
Boost.ASIO and standalone ASIO provide the async_result customisation point for adapting arbitrary third party libraries, such as Outcome, into ASIO.

Historically in ASIO you need to pass completion handler instances to the ASIO asynchronous i/o initiation functions. These get executed when the i/o completes.

  // Dynamically allocate a buffer to read into. This must be move-only
  // so it can be attached to the completion handler, hence the unique_ptr.
  auto buffer = std::make_unique<std::vector<byte>>(1024);

  // Begin an asynchronous socket read, upon completion invoke
  // the lambda function specified
  skt.async_read_some(asio::buffer(buffer->data(), buffer->size()),

                      // Retain lifetime of the i/o buffer until completion
                      [buffer = std::move(buffer)](const error_code &ec, size_t bytes) {
                        // Handle the buffer read
                        if(ec)
                        {
                          std::cerr << "Buffer read failed with " << ec << std::endl;
                          return;
                        }
                        std::cout << "Read " << bytes << " bytes into buffer" << std::endl;

                        // buffer will be dynamically freed now
                      });
View this code on Github
One of the big value adds of the Coroutines TS is the ability to not have to write so much boilerplate if you have a Coroutines supporting compiler:

  // As coroutines suspend the calling thread whilst an asynchronous
  // operation executes, we can use stack allocation instead of dynamic
  // allocation
  char buffer[1024];

  // Asynchronously read data, suspending this coroutine until completion,
  // returning the bytes of the data read into the result.
  try
  {
    // The use_awaitable completion token represents the current coroutine
    // (requires Coroutines TS)
    size_t bytesread =  //
    co_await skt.async_read_some(asio::buffer(buffer), asio::use_awaitable);
    std::cout << "Read " << bytesread << " bytes into buffer" << std::endl;
  }
  catch(const std::system_error &e)
  {
    std::cerr << "Buffer read failed with " << e.what() << std::endl;
  }
View this code on Github
The default ASIO implementation always throws exceptions on failure through its coroutine token transformation. The redirect_error token transformation recovers the option to use the error_code interface, but it suffers from the same drawbacks that make pure error codes unappealing in the synchronous case.

This recipe fixes that by making it possible for coroutinised i/o in ASIO to return a result<T>:

  // Asynchronously read data, suspending this coroutine until completion,
  // returning the bytes of the data read into the result, or any failure.
  outcome::result<size_t, error_code> bytesread =  //
  co_await skt.async_read_some(asio::buffer(buffer), as_result(asio::use_awaitable));

  // Usage is exactly like ordinary Outcome. Note the lack of exception throw!
  if(bytesread.has_error())
  {
    std::cerr << "Buffer read failed with " << bytesread.error() << std::endl;
    return;
  }
  std::cout << "Read " << bytesread.value() << " bytes into buffer" << std::endl;
View this code on Github
Implementation
The below involves a lot of ASIO voodoo. NO SUPPORT WILL BE GIVEN HERE FOR THE ASIO CODE BELOW. Please raise any questions or problems that you have with how to implement this sort of stuff in ASIO on Stackoverflow #boost-asio.

The real world, production-level recipe can be found at the bottom of this page. You ought to use that in any real world use case.

It is however worth providing a walkthrough of a simplified edition of the real world recipe, as a lot of barely documented ASIO voodoo is involved. You should not use the code presented next in your own code, it is too simplified. But it should help you understand how the real implementation works.

Firstly we need to define some helper type sugar and a factory function for wrapping any arbitrary third party completion token with that type sugar:

namespace detail
{
  // Type sugar for wrapping an external completion token
  template <class CompletionToken> struct as_result_t
  {
    CompletionToken token;
  };
}  // namespace detail

// Factory function for wrapping a third party completion token with
// our type sugar
template <class CompletionToken>  //
inline auto as_result(CompletionToken &&token)
{
  return detail::as_result_t<std::decay_t<CompletionToken>>{std::forward<CompletionToken>(token)};
};
View this code on Github
Next we tell ASIO about a new completion token it ought to recognise by specialising async_result:

// Tell ASIO about a new kind of completion token, the kind returned
// from our as_result() factory function. This implementation is
// for functions with handlers void(error_code, T) only.
template <class CompletionToken, class T>                        //
struct asio::async_result<detail::as_result_t<CompletionToken>,  //
                          void(error_code, T)>                   //

{
  // The result type we shall return
  using result_type = outcome::result<T, error_code>;
  // The awaitable type to be returned by the initiating function,
  // the co_await of which will yield a result_type
  using return_type = //
  typename asio::async_result<CompletionToken, void(result_type)> //
  ::return_type;
View this code on Github
There are a couple tricky parts to understand. First of all, we want our async_result specialization to work, in particular, with the async_result for ASIO’s use_awaitable_t completion token. With this token, the async_result specialization takes the form with a static initiate method which defers initiation of the asynchronous operation until, for example, co_await is called on the returned awaitable. Thus, our async_result specialization will take the same form. With this in mind, we need only understand how our specialization will implement its initiate method. The trick is that it will pass the initiation work off to an async_result for the supplied completion token type with a completion handler which consumes result<T>. Our async_result is thus just a simple wrapper over this underlying async_result, but we inject a completion handler with the void(error_code, size_t) signature which constructs from that a result:

  // Wrap a completion handler with void(error_code, T) converting
  // handler
  template <class Handler>
  struct completion_handler {
    // Our completion handler spec
    void operator()(error_code ec, T v)
    {
      // Call the underlying completion handler, whose
      // completion function is void(result_type)
      if(ec)
      {
        // Complete with a failed result
        _handler(result_type(outcome::failure(ec)));
        return;
      }
      // Complete with a successful result
      _handler(result_type(outcome::success(v)));
    }

    Handler _handler;
  };

  // NOTE the initiate member function initiates the async operation,
  // and we want to defer to what would be the initiation of the
  // async_result whose handler signature is void(result_type).
  template <class Initiation, class... Args>
  static return_type
  initiate(
    Initiation&& init,
    detail::as_result_t<CompletionToken>&& token,
    Args&&... args)
  {
    // The async_initiate<CompletionToken, void(result_type)> helper
    // function will invoke the async initiation method of the
    // async_result<CompletionToken, void(result_type)>, as desired.
    // Instead of CompletionToken and void(result_type)	we start with
    // detail::as_result_t<CompletionToken> and void(ec, T), so
    // the inputs need to be massaged then passed along.
    return asio::async_initiate<CompletionToken, void(result_type)>(
      // create a new initiation which wraps the provided init
      [init = std::forward<Initiation>(init)](
        auto&& handler, auto&&... initArgs) mutable {
        std::move(init)(
          // we wrap the handler in the converting completion_handler from
          // above, and pass along the args
          completion_handler<std::decay_t<decltype(handler)>>{
            std::forward<decltype(handler)>(handler)},
          std::forward<decltype(initArgs)>(initArgs)...);
      },
      // the new initiation is called with the handler unwrapped from
      // the token, and the original initiation arguments.
      token.token,
      std::forward<Args>(args)...);
  }
};
View this code on Github
To use, simply wrap the third party completion token with as_result to cause ASIO to return from co_await a result instead of throwing exceptions on failure:

char buffer[1024];

outcome::result<size_t, error_code> bytesread =
  co_await skt.async_read_some(asio::buffer(buffer), as_result(asio::use_awaitable));
The real world production-level implementation below is a lot more complex than the above which has been deliberately simplified to aid exposition. The above should help you get up and running with the below, eventually.

One again I would like to remind you that Outcome is not the appropriate place to seek help with ASIO voodoo. Please ask on Stackoverflow #boost-asio.

Here follows the real world, production-level adapation of Outcome into ASIO, written and maintained by Christos Stratopoulos. If the following does not load due to Javascript being disabled, you can visit the gist at https://gist.github.com/cstratopoulos/901b5cdd41d07c6ce6d83798b09ecf9b/863c1dbf3b063a5ff9ff2bdd834242ead556e74e.

/*
A Boost.ASIO async_result adapter for outcome (https://github.com/ned14/outcome)
Sample use:
    boost::outcome_v2::result<std::size_t> read =
        co_await stream.async_read(buffer, as_result(boost::asio::use_awaitable));
*/

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/outcome/outcome.hpp>
#include <boost/system/error_code.hpp>

#include <type_traits>
#include <utility>

template<typename CompletionToken>
struct as_result_t {
    CompletionToken token_;
};

template<typename CompletionToken>
inline as_result_t<std::decay_t<CompletionToken>>
as_result(CompletionToken&& completion_token)
{
    return as_result_t<std::decay_t<CompletionToken>>{
        std::forward<CompletionToken>(completion_token)};
}

namespace detail {

// Class to adapt as_outcome_t as a completion handler
template<typename Handler>
struct outcome_result_handler {
    void operator()(const boost::system::error_code& ec)
    {
        using Result = boost::outcome_v2::result<void, boost::system::error_code>;

        if (ec)
            handler_(Result{ec});
        else
            handler_(Result{boost::outcome_v2::success()});
    }

    void operator()(std::exception_ptr ex)
    {
        using Result = boost::outcome_v2::result<void, std::exception_ptr>;

        if (ex)
            handler_(Result{ex});
        else
            handler_(Result{boost::outcome_v2::success()});
    }

    template<typename T>
    void operator()(const boost::system::error_code& ec, T t)
    {
        using Result = boost::outcome_v2::result<T, boost::system::error_code>;

        if (ec)
            handler_(Result{ec});
        else
            handler_(Result{std::move(t)});
    }

    template<typename T>
    void operator()(std::exception_ptr ex, T t)
    {
        using Result = boost::outcome_v2::result<T, std::exception_ptr>;
        if (ex)
            handler_(Result{ex});
        else
            handler_(Result{std::move(t)});
    }

    Handler handler_;
};

template<typename Handler>
inline void*
asio_handler_allocate(std::size_t size, outcome_result_handler<Handler>* this_handler)
{
    return boost_asio_handler_alloc_helpers::allocate(size, this_handler->handler_);
}

template<typename Handler>
inline void asio_handler_deallocate(
    void* pointer, std::size_t size, outcome_result_handler<Handler>* this_handler)
{
    boost_asio_handler_alloc_helpers::deallocate(pointer, size, this_handler->handler_);
}

template<typename Handler>
inline bool asio_handler_is_continuation(outcome_result_handler<Handler>* this_handler)
{
    return boost_asio_handler_cont_helpers::is_continuation(this_handler->handler_);
}

template<typename Function, typename Handler>
inline void
asio_handler_invoke(Function& function, outcome_result_handler<Handler>* this_handler)
{
    boost_asio_handler_invoke_helpers::invoke(function, this_handler->handler_);
}

template<typename Function, typename Handler>
inline void asio_handler_invoke(
    const Function& function, outcome_result_handler<Handler>* this_handler)
{
    boost_asio_handler_invoke_helpers::invoke(function, this_handler->handler_);
}

template<typename Signature>
struct result_signature;

template<>
struct result_signature<void(boost::system::error_code)> {
    using type = void(boost::outcome_v2::result<void, boost::system::error_code>);
};

template<>
struct result_signature<void(const boost::system::error_code&)>
    : result_signature<void(boost::system::error_code)> {};

template<>
struct result_signature<void(std::exception_ptr)> {
    using type = void(boost::outcome_v2::result<void, std::exception_ptr>);
};

template<typename T>
struct result_signature<void(boost::system::error_code, T)> {
    using type = void(boost::outcome_v2::result<T, boost::system::error_code>);
};

template<typename T>
struct result_signature<void(const boost::system::error_code&, T)>
    : result_signature<void(boost::system::error_code, T)> {};

template<typename T>
struct result_signature<void(std::exception_ptr, T)> {
    using type = void(boost::outcome_v2::result<T, std::exception_ptr>);
};

template<typename Signature>
using result_signature_t = typename result_signature<Signature>::type;

} // namespace detail

namespace boost::asio {

template<typename CompletionToken, typename Signature>
class async_result<as_result_t<CompletionToken>, Signature> {
public:
    using result_signature = ::detail::result_signature_t<Signature>;

    using return_type =
        typename async_result<CompletionToken, result_signature>::return_type;

    template<typename Initiation, typename... Args>
    static return_type
    initiate(Initiation&& initiation, as_result_t<CompletionToken>&& token, Args&&... args)
    {
        return async_initiate<CompletionToken, result_signature>(
            [init = std::forward<Initiation>(initiation)](
                auto&& handler, auto&&... callArgs) mutable {
                std::move(init)(
                    ::detail::outcome_result_handler<std::decay_t<decltype(handler)>>{
                        std::forward<decltype(handler)>(handler)},
                    std::forward<decltype(callArgs)>(callArgs)...);
            },
            token.token_,
            std::forward<Args>(args)...);
    }
};

template<typename Handler, typename Executor>
struct associated_executor<::detail::outcome_result_handler<Handler>, Executor> {
    typedef typename associated_executor<Handler, Executor>::type type;

    static type
    get(const ::detail::outcome_result_handler<Handler>& h,
        const Executor& ex = Executor()) noexcept
    {
        return associated_executor<Handler, Executor>::get(h.handler_, ex);
    }
};

template<typename Handler, typename Allocator>
struct associated_allocator<::detail::outcome_result_handler<Handler>, Allocator> {
    typedef typename associated_allocator<Handler, Allocator>::type type;

    static type
    get(const ::detail::outcome_result_handler<Handler>& h,
        const Allocator& a = Allocator()) noexcept
    {
        return associated_allocator<Handler, Allocator>::get(h.handler_, a);
    }
};

} // namespace boost::asio
view rawas_result.hpp hosted with ❤ by GitHub
ASIO/Networking TS : Boost < 1.70
Extending OUTCOME_TRY
asio networking-ts
 Last update on 06/08/2019
 Improve this page
