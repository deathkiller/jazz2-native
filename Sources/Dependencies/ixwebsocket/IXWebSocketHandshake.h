/*
 *  IXWebSocketHandshake.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 *  Copyright (c) 2026 Dan R.
 */

#pragma once

#include "IXCancellationRequest.h"
#include "IXHttp.h"
#include "IXSocket.h"
#include "IXWebSocketHttpHeaders.h"
#include "IXWebSocketInitResult.h"
#include "IXWebSocketPerMessageDeflate.h"
#include "IXWebSocketPerMessageDeflateOptions.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ix
{
    // Callback invoked on the server during handshake to select an application-level protocol.
    // Receives the list of protocols requested by the client.
    // Returns true to accept the connection (selectedProtocol may be empty to accept with no
    // protocol), or false to reject it with HTTP 400.
    using ProtocolSelectionCallback =
        std::function<bool(const std::vector<std::string>& requestedProtocols,
                           std::string& selectedProtocol)>;

    class WebSocketHandshake
    {
    public:
        WebSocketHandshake(std::atomic<bool>& requestInitCancellation,
                           std::unique_ptr<Socket>& _socket,
                           WebSocketPerMessageDeflatePtr& perMessageDeflate,
                           WebSocketPerMessageDeflateOptions& perMessageDeflateOptions,
                           std::atomic<bool>& enablePerMessageDeflate,
                           const std::string& serverName = std::string());

        WebSocketInitResult clientHandshake(const std::string& url,
                                            const WebSocketHttpHeaders& extraHeaders,
                                            const std::string& protocol,
                                            const std::string& host,
                                            const std::string& path,
                                            int port,
                                            int timeoutSecs);

        WebSocketInitResult serverHandshake(
            int timeoutSecs,
            bool enablePerMessageDeflate,
            const ProtocolSelectionCallback& protocolSelectionCallback = nullptr,
            HttpRequestPtr request = nullptr);

    private:
        std::string genRandomString(const int len);

        // Parse HTTP headers
        WebSocketInitResult sendErrorResponse(int code, const std::string& reason);

        bool insensitiveStringCompare(const std::string& a, const std::string& b);

        std::atomic<bool>& _requestInitCancellation;
        std::unique_ptr<Socket>& _socket;
        WebSocketPerMessageDeflatePtr& _perMessageDeflate;
        WebSocketPerMessageDeflateOptions& _perMessageDeflateOptions;
        std::atomic<bool>& _enablePerMessageDeflate;
        std::string _serverName;
    };
} // namespace ix
