/*
 *  IXConnectionState.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ix
{
	/**
		@brief Callback type for connection termination events.
	*/
	using OnSetTerminatedCallback = std::function<void()>;

	/**
		@brief Represents the state of a network connection.

		Tracks termination, remote address, and unique ID for @ref SocketServer and related classes.
	*/
	class ConnectionState
	{
	public:
		/** @brief Construct a new ConnectionState. */
		ConnectionState();
		/** @brief Destructor. */
		virtual ~ConnectionState() = default;

		/** @brief Compute a unique connection ID. */
		virtual void computeId();
		/** @brief Get the connection ID. */
		virtual const std::string& getId() const;

		/** @brief Mark the connection as terminated. */
		void setTerminated();
		/** @brief Check if the connection is terminated. */
		bool isTerminated() const;

		/** @brief Get the remote IP address. */
		const std::string& getRemoteIp();
		/** @brief Get the remote port number. */
		int getRemotePort();

		/** @brief Create a new shared ConnectionState instance. */
		static std::shared_ptr<ConnectionState> createConnectionState();

	private:
		/** @brief Set the termination callback. */
		void setOnSetTerminatedCallback(const OnSetTerminatedCallback& callback);

		/** @brief Set the remote IP address. */
		void setRemoteIp(const std::string& remoteIp);
		/** @brief Set the remote port number. */
		void setRemotePort(int remotePort);

	protected:
		/** @brief True if the connection is terminated. */
		std::atomic<bool> _terminated;
		/** @brief Unique connection ID. */
		std::string _id;
		/** @brief Callback for termination event. */
		OnSetTerminatedCallback _onSetTerminatedCallback;

		/** @brief Global connection ID counter. */
		static std::atomic<uint64_t> _globalId;

		/** @brief Remote IP address. */
		std::string _remoteIp;
		/** @brief Remote port number. */
		int _remotePort;

		friend class SocketServer;
	};
}
