Boost C++ Libraries
...one of the most highly regarded and expertly designed C++ library projects in the world.
— Herb Sutter and Andrei Alexandrescu, C++ Coding Standards

Search...
Prev Up Home Next
C++20 Coroutines Support
Support for C++20 Coroutines is provided via the awaitable class template, the use_awaitable completion token, and the co_spawn () function. These facilities allow programs to implement asynchronous logic in a synchronous manner, in conjunction with the co_await keyword, as shown in the following example:

boost::asio::co_spawn(executor, echo(std::move(socket)), boost::asio::detached);

// ...

boost::asio::awaitable<void> echo(tcp::socket socket)
{
  try
  {
    char data[1024];
    for (;;)
    {
      std::size_t n = co_await socket.async_read_some(boost::asio::buffer(data), boost::asio::use_awaitable);
      co_await async_write(socket, boost::asio::buffer(data, n), boost::asio::use_awaitable);
    }
  }
  catch (std::exception& e)
  {
    std::printf("echo Exception: %s\n", e.what());
  }
}
The first argument to co_spawn () is an executor that determines the context in which the coroutine is permitted to execute. For example, a server's per-client object may consist of multiple coroutines; they should all run on the same strand so that no explicit synchronisation is required.

The second argument is an awaitable < R > , that is the result of the coroutine's entry point function, and in the above example is the result of the call to echo . (Alternatively, this argument can be a function object that returns the awaitable < R > .) The template parameter R is the type of return value produced by the coroutine. In the above example, the coroutine returns void .

The third argument is a completion token, and this is used by co_spawn () to produce a completion handler with signature void ( std :: exception_ptr , R ) . This completion handler is invoked with the result of the coroutine once it has finished. In the above example we pass a completion token type, boost :: asio :: detached , which is used to explicitly ignore the result of an asynchronous operation.

In this example the body of the coroutine is implemented in the echo function. When the use_awaitable completion token is passed to an asynchronous operation, the operation's initiating function returns an awaitable that may be used with the co_await keyword:

std::size_t n = co_await socket.async_read_some(boost::asio::buffer(data), boost::asio::use_awaitable);
Where an asynchronous operation's handler signature has the form:

void handler(boost::system::error_code ec, result_type result);
the resulting type of the co_await expression is result_type . In the async_read_some example above, this is size_t . If the asynchronous operation fails, the error_code is converted into a system_error exception and thrown.

Where a handler signature has the form:

void handler(boost::system::error_code ec);
the co_await expression produces a void result. As above, an error is passed back to the coroutine as a system_error exception.

Error Handling
To perform explicit error handling, rather than the default exception-throwing behaviour, use the as_tuple or redirect_error completion token adapters.

The as_tuple completion token adapter packages the completion handler arguments into a single tuple, which is then returned as the result of the awaited operation. For example:

boost::asio::awaitable<void> echo(tcp::socket socket)
{
  char data[1024];
  for (;;)
  {
    std::tuple<boost::system::error_code, std::size_t> result =
      co_await socket.async_read_some(boost::asio::buffer(data),
        boost::asio::as_tuple(boost::asio::use_awaitable));
    if (!std::get<0>(result))
    {
      // success
    }

    // ...
  }
}
The result can also be captured directly into a structured binding:

boost::asio::awaitable<void> echo(tcp::socket socket)
{
  char data[1024];
  for (;;)
  {
    auto [ec, n] = co_await socket.async_read_some(
        boost::asio::buffer(data), boost::asio::as_tuple(boost::asio::use_awaitable));
    if (!ec)
    {
      // success
    }

    // ...
  }
}
Alternatively, the redirect_error completion token adapter may be used to capture the error into a supplied error_code variable:

boost::asio::awaitable<void> echo(tcp::socket socket)
{
  char data[1024];
  for (;;)
  {
    boost::system::error_code ec;
    std::size_t n = co_await socket.async_read_some(boost::asio::buffer(data),
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (!ec)
    {
      // success
    }

    // ...
  }
}
Coroutines and Per-Operation Cancellation
All threads of execution created by co_spawn have a cancellation state that records the current state of any cancellation requests made to the coroutine. To access this state, use this_coro :: cancellation_state as follows:

boost::asio::awaitable<void> my_coroutine()
{
  boost::asio::cancellation_state cs
    = co_await boost::asio::this_coro::cancellation_state;

  // ...

  if (cs.cancelled() != boost::asio::cancellation_type::none)
    // ...
}
When first created by co_spawn , the thread of execution has a cancellation state that supports cancellation_type :: terminal values only. To change the cancellation state, call this_coro :: reset_cancellation_state .

By default, continued execution of a cancelled coroutine will trigger an exception from any subsequent co_await of an awaitable <> object. This behaviour can be changed by using this_coro :: throw_if_cancelled .

Co-ordinating Parallel Coroutines
[Note]	Note
This is an experimental feature.

The logical operators || and && have been overloaded for awaitable <> , to allow coroutines to be trivially awaited in parallel.

When awaited using && , the co_await expression waits until both operations have completed successfully. As a "short-circuit" evaluation, if one operation fails with an exception, the other is immediately cancelled. For example:

std::tuple<std::size_t, std::size_t> results =
  co_await (
    async_read(socket, input_buffer, use_awaitable)
      && async_write(socket, output_buffer, use_awaitable)
  );
Following completion of a && operation, the results of all operations are concatenated into a tuple. In the above example, the first size_t represents the non-exceptional component of the async_read result, and the second size_t is the result of the async_write .

When awaited using || , the co_await expression waits until either operation succeeds. As a "short-circuit" evaluation, if one operation succeeds without throwing an exception, the other is immediately cancelled. For example:

std::variant<std::size_t, std::monostate> results =
  co_await (
    async_read(socket, input_buffer, use_awaitable)
      || timer.async_wait(use_awaitable)
  );
Following completion of a || operation, the result of the first operation to complete non-exceptionally is placed into a std :: variant . The active index of the variant reflects which of the operations completed first. In the above example, index 0 corresponds to the async_read operation.

These operators may be enabled by adding the #include :

#include <boost/asio/experimental/awaitable_operators.hpp>
and then bringing the contents of the experimental :: awaitable_operators namespace into scope:

using namespace boost::asio::experimental::awaitable_operators;
Lightweight Coroutines Implementing Asynchonous Operations
The experimental :: co_composed template facilitates a lightweight implementation of user-defined asynchronous operations using C++20 coroutines. The following example illustrates a simple asynchronous operation that implements an echo protocol in terms of a coroutine:

template <typename CompletionToken>
auto async_echo(tcp::socket& socket,
    CompletionToken&& token)
{
  return boost::asio::async_initiate<
    CompletionToken, void(boost::system::error_code)>(
      boost::asio::experimental::co_composed<
        void(boost::system::error_code)>(
          [](auto state, tcp::socket& socket) -> void
          {
            try
            {
              state.throw_if_cancelled(true);
              state.reset_cancellation_state(
                boost::asio::enable_terminal_cancellation());

              for (;;)
              {
                char data[1024];
                std::size_t n = co_await socket.async_read_some(
                    boost::asio::buffer(data), boost::asio::deferred);

                co_await boost::asio::async_write(socket,
                    boost::asio::buffer(data, n), boost::asio::deferred);
              }
            }
            catch (const boost::system::system_error& e)
            {
              co_return {e.code()};
            }
          }, socket),
      token, std::ref(socket));
}
See Also
co_spawn , detached , as_tuple , redirect_error , awaitable , use_awaitable_t , use_awaitable , this_coro::executor , experimental::co_composed , Coroutines examples , Resumable C++20 Coroutines , Stackful Coroutines , Stackless Coroutines .

Copyright Â© 2003-2024 Christopher M. Kohlhoff
Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt )
