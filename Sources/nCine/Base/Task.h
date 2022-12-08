#pragma once

#if defined(WITH_COROUTINES)
#	include <coroutine>
#endif

#include <optional>

namespace nCine
{
	template <typename T>
	struct Task
	{

#if defined(WITH_COROUTINES)

		struct task_promise;

		using promise_type = task_promise;
		using handle_type = std::coroutine_handle<promise_type>;

		mutable handle_type m_handle;

		Task(handle_type handle)
			: m_handle(handle)
		{
		}

		Task(Task&& other) noexcept
			: m_handle(other.m_handle)
		{
			other.m_handle = nullptr;
		}

		~Task()
		{
			if (m_handle)
				m_handle.destroy();
		}

		bool await_ready()
		{
			return !m_handle || m_handle.done();
		}

		bool await_suspend(std::coroutine_handle<> handle)
		{
			return true;
		}

		bool await_suspend(std::coroutine_handle<promise_type> handle)
		{
			handle.promise().m_inner_handler = m_handle;
			m_handle.promise().m_outer_handler = handle;
			return true;
		}

		auto await_resume()
		{
			return *m_handle.promise().m_value;
		}

		// Manualy wait for finish
		bool one_step()
		{
			auto curr = m_handle;
			while (curr) {
				if (!curr.promise().m_inner_handler) {
					while (!curr.done()) {
						curr.resume();
						if (!curr.done()) {
							return true;
						}
						if (curr.promise().m_outer_handler) {
							curr = curr.promise().m_outer_handler;
							curr.promise().m_inner_handler = nullptr;
						} else {
							return false;
						}
					}
					break;
				}
				curr = curr.promise().m_inner_handler;
			}
			return !curr.done();
		}

		struct task_promise
		{
			std::optional<T> m_value { };
			std::coroutine_handle<promise_type> m_inner_handler { };
			std::coroutine_handle<promise_type> m_outer_handler { };

			auto value()
			{
				return m_value;
			}

			auto initial_suspend()
			{
				return std::suspend_never { };
			}

			auto final_suspend() noexcept
			{
				return std::suspend_always { };
			}

			void return_value(T t)
			{
				m_value = t;
				//return std::suspend_always { };
			}

			Task<T> get_return_object()
			{
				return { handle_type::from_promise(*this) };
			}

			void unhandled_exception()
			{
				std::terminate();
			}

			void rethrow_if_unhandled_exception()
			{

			}
		};
#else

		Task(T value) : _value(value) { }

		operator T() const {
			return _value;
		}

		T _value;

#endif

	};
}