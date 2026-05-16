/*
 *  IXWebSocketTransport.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2018 Machine Zone, Inc. All rights reserved.
 *  Copyright (c) 2026 Dan R.
 */

#pragma once

//
// Adapted from https://github.com/dhbaird/easywsclient
//

#include "IXCancellationRequest.h"
#include "IXProgressCallback.h"
#include "IXSocketTLSOptions.h"
#include "IXWebSocketCloseConstants.h"
#include "IXWebSocketHandshake.h"
#include "IXWebSocketHttpHeaders.h"
#include "IXWebSocketPerMessageDeflate.h"
#include "IXWebSocketPerMessageDeflateOptions.h"
#include "IXWebSocketSendData.h"
#include "IXWebSocketSendInfo.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ix
{
	class Socket;

	/**
		@brief Kind of message to send (text, binary, ping)
	*/
	enum class SendMessageKind
	{
		Text,   /**< Text message */
		Binary, /**< Binary message */
		Ping    /**< Ping message */
	};

	/**
		@brief WebSocket transport implementation for sending and receiving WebSocket frames.
		
		Handles connection state, message fragmentation, compression, and ping/pong heartbeats.
		Used internally by @ref ix::WebSocket and @ref ix::WebSocketServer.
	*/
	class WebSocketTransport
	{
	public:
		/**
		 * @brief WebSocket ready state (connection lifecycle)
		 */
		enum class ReadyState
		{
			CLOSING,    /**< Connection is closing */
			CLOSED,     /**< Connection is closed */
			CONNECTING, /**< Connection is being established */
			OPEN        /**< Connection is open */
		};

		/**
		 * @brief Kind of message received or sent
		 */
		enum class MessageKind
		{
			MSG_TEXT,   /**< Text message */
			MSG_BINARY, /**< Binary message */
			PING,       /**< Ping frame */
			PONG,       /**< Pong frame */
			FRAGMENT    /**< Fragmented message */
		};

		/**
		 * @brief Result of polling the socket for events
		 */
		enum class PollResult
		{
			Succeeded,              /**< Poll succeeded */
			AbnormalClose,          /**< Abnormal close detected */
			CannotFlushSendBuffer   /**< Could not flush send buffer */
		};

		/**
		 * @brief Callback for received messages
		 * @param message Message data
		 * @param size Message size
		 * @param compressed True if message is compressed
		 * @param kind Message kind (see @ref MessageKind)
		 */
		using OnMessageCallback = std::function<void(const std::string&, size_t, bool, MessageKind)>;

		/**
		 * @brief Callback for connection close events
		 * @param code Close code
		 * @param reason Close reason
		 * @param wireSize Size of close frame on wire
		 * @param remote True if closed by remote peer
		 */
		using OnCloseCallback = std::function<void(uint16_t, const std::string&, size_t, bool)>;

		/** @brief Constructor */
		WebSocketTransport();
		/** @brief Destructor */
		~WebSocketTransport();

		/**
		 * @brief Configure compression, TLS, pong, and ping interval
		 * @param perMessageDeflateOptions Per-message deflate options
		 * @param socketTLSOptions TLS options
		 * @param enablePong Enable pong auto-response
		 * @param pingIntervalSecs Ping interval in seconds
		 */
		void configure(const WebSocketPerMessageDeflateOptions& perMessageDeflateOptions,
					   const SocketTLSOptions& socketTLSOptions,
					   bool enablePong,
					   int pingIntervalSecs);

		/**
		 * @brief Connect to a WebSocket URL (client mode)
		 * @param url Target URL
		 * @param headers HTTP headers
		 * @param timeoutSecs Timeout in seconds
		 * @return @ref WebSocketInitResult
		 */
		WebSocketInitResult connectToUrl(const std::string& url,
											 const WebSocketHttpHeaders& headers,
											 int timeoutSecs);

		/**
		 * @brief Connect to an existing socket (server mode)
		 * @param socket TCP socket
		 * @param timeoutSecs Timeout in seconds
		 * @param enablePerMessageDeflate Enable per-message deflate
		 * @param serverName Optional server name
		 * @param protocolSelectionCallback Protocol selection callback
		 * @param request Optional HTTP request
		 * @param sendTimeoutSecs Send timeout in seconds
		 * @return @ref WebSocketInitResult
		 */
		WebSocketInitResult connectToSocket(std::unique_ptr<Socket> socket,
											   int timeoutSecs,
											   bool enablePerMessageDeflate,
											   const std::string& serverName = std::string(),
											   const ProtocolSelectionCallback& protocolSelectionCallback = nullptr,
											   HttpRequestPtr request = nullptr,
											   int sendTimeoutSecs = -1);

		/**
		 * @brief Poll the socket for events
		 * @return @ref PollResult
		 */
		PollResult poll();

		/**
		 * @brief Send binary data
		 * @param message Data to send (see @ref IXWebSocketSendData)
		 * @param onProgressCallback Progress callback
		 * @return @ref WebSocketSendInfo
		 */
		WebSocketSendInfo sendBinary(const IXWebSocketSendData& message,
									   const OnProgressCallback& onProgressCallback);

		/**
		 * @brief Send text data
		 * @param message Data to send (see @ref IXWebSocketSendData)
		 * @param onProgressCallback Progress callback
		 * @return @ref WebSocketSendInfo
		 */
		WebSocketSendInfo sendText(const IXWebSocketSendData& message,
								  const OnProgressCallback& onProgressCallback);

		/**
		 * @brief Send ping frame
		 * @param message Data to send (see @ref IXWebSocketSendData)
		 * @return @ref WebSocketSendInfo
		 */
		WebSocketSendInfo sendPing(const IXWebSocketSendData& message);

		/**
		 * @brief Close the connection
		 * @param code Close code (see @ref WebSocketCloseConstants)
		 * @param reason Close reason
		 * @param closeWireSize Size of close frame on wire
		 * @param remote True if closed by remote peer
		 */
		void close(uint16_t code = WebSocketCloseConstants::kNormalClosureCode,
				  const std::string& reason = WebSocketCloseConstants::kNormalClosureMessage,
				  size_t closeWireSize = 0,
				  bool remote = false);

		/** @brief Close the underlying socket */
		void closeSocket();

		/** @brief Get current ready state */
		ReadyState getReadyState() const;
		/** @brief Set ready state */
		void setReadyState(ReadyState readyState);
		/** @brief Set close callback */
		void setOnCloseCallback(const OnCloseCallback& onCloseCallback);
		/** @brief Dispatch a message to callback */
		void dispatch(PollResult pollResult, const OnMessageCallback& onMessageCallback);
		/** @brief Get amount of buffered data */
		size_t bufferedAmount() const;

		/**
		 * @brief Set ping heartbeat message
		 * @param message Ping message
		 * @param pingType Kind of ping (see @ref SendMessageKind)
		 */
		void setPingMessage(const std::string& message, SendMessageKind pingType);

		/**
		 * @brief Send any type of ping packet (internal)
		 * @param pingType Kind of ping (see @ref SendMessageKind)
		 * @return @ref WebSocketSendInfo
		 */
		WebSocketSendInfo sendHeartBeat(SendMessageKind pingType);

	private:
		/** @brief Target URL */
		std::string _url;

		/**
		 * @brief WebSocket frame header structure
		 */
		struct wsheader_type
		{
			unsigned header_size; /**< Header size */
			bool fin;            /**< Final fragment */
			bool rsv1;           /**< Reserved bit 1 */
			bool rsv2;           /**< Reserved bit 2 */
			bool rsv3;           /**< Reserved bit 3 */
			bool mask;           /**< Masked frame */
			/**
			 * @brief WebSocket opcode type
			 */
			enum opcode_type
			{
				CONTINUATION = 0x0, /**< Continuation frame */
				TEXT_FRAME = 0x1,   /**< Text frame */
				BINARY_FRAME = 0x2, /**< Binary frame */
				CLOSE = 8,          /**< Close frame */
				PING = 9,           /**< Ping frame */
				PONG = 0xa,         /**< Pong frame */
			} opcode;
			int N0;              /**< Payload length (short) */
			uint64_t N;          /**< Payload length (long) */
			uint8_t masking_key[4]; /**< Masking key */
		};

		/**
		 * @brief Whether to mask outgoing data (client only)
		 */
		std::atomic<bool> _useMask;

		/**
		 * @brief Whether to block on send buffer flush (server mode)
		 */
		std::atomic<bool> _blockingSend;

		/**
		 * @brief Send buffer flush timeout (seconds)
		 */
		int _sendTimeoutSecs = -1;

		/**
		 * @brief Read buffer (never resized)
		 */
		std::vector<uint8_t> _readbuf;

		/**
		 * @brief Receive buffer (resized as needed)
		 */
		std::vector<uint8_t> _rxbuf;

		/**
		 * @brief Max receive buffer size (0 = unlimited)
		 */
		uint64_t _rxbufWanted = 0;

		/**
		 * @brief Send buffer (pending messages)
		 */
		std::vector<uint8_t> _txbuf;
		mutable std::mutex _txbufMutex;

		/**
		 * @brief List of message fragments (for large messages)
		 */
		std::list<std::string> _chunks;

		/**
		 * @brief Kind of fragmented message (text or binary)
		 */
		MessageKind _fragmentedMessageKind;

		/**
		 * @brief Whether received message is compressed
		 */
		bool _receivedMessageCompressed;

		/**
		 * @brief Fragment size (32K)
		 */
		static constexpr size_t kChunkSize = 1 << 15;

		/**
		 * @brief Underlying TCP socket
		 */
		std::unique_ptr<Socket> _socket;
		std::mutex _socketMutex;

		/**
		 * @brief Current connection state
		 */
		std::atomic<ReadyState> _readyState;
		std::mutex _setReadyStateMutex;

		/**
		 * @brief Close callback and reason
		 */
		OnCloseCallback _onCloseCallback;
		std::string _closeReason;
		mutable std::mutex _closeReasonMutex;
		std::atomic<uint16_t> _closeCode;
		std::atomic<size_t> _closeWireSize;
		std::atomic<bool> _closeRemote;

		/**
		 * @brief Per-message deflate compression
		 */
		WebSocketPerMessageDeflatePtr _perMessageDeflate;
		WebSocketPerMessageDeflateOptions _perMessageDeflateOptions;
		std::atomic<bool> _enablePerMessageDeflate;

		/**
		 * @brief Decompressed and compressed message buffers
		 */
		std::string _decompressedMessage;
		std::string _compressedMessage;

		/**
		 * @brief TLS options
		 */
		SocketTLSOptions _socketTLSOptions;

		/**
		 * @brief Cancel DNS/connection/upgrade
		 */
		std::atomic<bool> _requestInitCancellation;

		/**
		 * @brief Closing time tracking
		 */
		mutable std::mutex _closingTimePointMutex;
		std::chrono::time_point<std::chrono::steady_clock> _closingTimePoint;
		static const int kClosingMaximumWaitingDelayInMs;

		/**
		 * @brief Enable pong auto-response
		 */
		std::atomic<bool> _enablePong;
		static const bool kDefaultEnablePong;

		/**
		 * @brief Ping/pong interval and state
		 */
		int _pingIntervalSecs;
		std::atomic<bool> _pongReceived;
		static const int kDefaultPingIntervalSecs;

		/**
		 * @brief Custom ping message and type
		 */
		bool _setCustomMessage;
		std::string _kPingMessage;
		SendMessageKind _pingType;
		std::atomic<uint64_t> _pingCount;

		/**
		 * @brief Last ping send time tracking
		 */
		mutable std::mutex _lastSendPingTimePointMutex;
		std::chrono::time_point<std::chrono::steady_clock> _lastSendPingTimePoint;

		/**
		 * @brief Check if ping interval exceeded
		 * @return True if time to send new ping
		 */
		bool pingIntervalExceeded();
		/** @brief Initialize time points after connect */
		void initTimePointsAfterConnect();

		/**
		 * @brief Check if closing delay exceeded (after close())
		 * @return True if should force close
		 */
		bool closingDelayExceeded();

		/** @brief Send close frame */
		void sendCloseFrame(uint16_t code, const std::string& reason);

		/**
		 * @brief Close socket and switch to closed state
		 * @param code Close code
		 * @param reason Close reason
		 * @param closeWireSize Size of close frame
		 * @param remote True if closed by remote peer
		 */
		void closeSocketAndSwitchToClosedState(uint16_t code,
												   const std::string& reason,
												   size_t closeWireSize,
												   bool remote);

		/** @brief Wake up from poll */
		bool wakeUpFromPoll(uint64_t wakeUpCode);

		/** @brief Flush send buffer */
		bool flushSendBuffer();
		/** @brief Send data on socket */
		bool sendOnSocket();
		/** @brief Receive data from socket */
		bool receiveFromSocket();

		/**
		 * @brief Send data (internal)
		 * @param type Opcode type
		 * @param message Data to send
		 * @param compress Compress data
		 * @param onProgressCallback Progress callback
		 * @return @ref WebSocketSendInfo
		 */
		WebSocketSendInfo sendData(wsheader_type::opcode_type type,
								   const IXWebSocketSendData& message,
								   bool compress,
								   const OnProgressCallback& onProgressCallback = nullptr);

		/**
		 * @brief Send a message fragment (internal)
		 * @param type Opcode type
		 * @param fin Final fragment
		 * @param begin Iterator to start
		 * @param end Iterator to end
		 * @param compress Compress data
		 * @return True if sent
		 */
		template<class Iterator>
		bool sendFragment(wsheader_type::opcode_type type, bool fin, Iterator begin, Iterator end, bool compress);

		/**
		 * @brief Emit a message to callback (internal)
		 * @param messageKind Kind of message
		 * @param message Message data
		 * @param compressedMessage True if compressed
		 * @param onMessageCallback Callback
		 */
		void emitMessage(MessageKind messageKind,
					   const std::string& message,
					   bool compressedMessage,
					   const OnMessageCallback& onMessageCallback);

		/** @brief Check if send buffer is empty */
		bool isSendBufferEmpty() const;

		/**
		 * @brief Append data to send buffer (internal)
		 * @param header Frame header
		 * @param begin Iterator to start
		 * @param end Iterator to end
		 * @param message_size Message size
		 * @param masking_key Masking key
		 */
		template<class Iterator>
		void appendToSendBuffer(const std::vector<uint8_t>& header,
								 Iterator begin,
								 Iterator end,
								 uint64_t message_size,
								 uint8_t masking_key[4]);

		/** @brief Get random unsigned integer */
		unsigned getRandomUnsigned();
		/** @brief Unmask receive buffer */
		void unmaskReceiveBuffer(const wsheader_type& ws);

		/** @brief Merge message fragments into single string */
		std::string getMergedChunks() const;

		/** @brief Set close reason */
		void setCloseReason(const std::string& reason);
		/** @brief Get close reason */
		const std::string& getCloseReason() const;
	};
}
