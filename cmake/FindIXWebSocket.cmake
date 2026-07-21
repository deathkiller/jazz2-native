#
# Author: Benjamin Sergeant
# Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
# Copyright (c) 2026 Dan R.
#

include(ncine_helpers)
include(FindPackageHandleStandardArgs)

set(_IXWEBSOCKET_ROOT "${NCINE_SOURCE_DIR}/Dependencies/ixwebsocket")

set(_IXWEBSOCKET_SOURCES
	${_IXWEBSOCKET_ROOT}/IXBench.cpp
	${_IXWEBSOCKET_ROOT}/IXCancellationRequest.cpp
	${_IXWEBSOCKET_ROOT}/IXConnectionState.cpp
	${_IXWEBSOCKET_ROOT}/IXDNSLookup.cpp
	${_IXWEBSOCKET_ROOT}/IXExponentialBackoff.cpp
	${_IXWEBSOCKET_ROOT}/IXGetFreePort.cpp
	${_IXWEBSOCKET_ROOT}/IXGzipCodec.cpp
	${_IXWEBSOCKET_ROOT}/IXHttp.cpp
	${_IXWEBSOCKET_ROOT}/IXHttpClient.cpp
	${_IXWEBSOCKET_ROOT}/IXHttpServer.cpp
	${_IXWEBSOCKET_ROOT}/IXNetSystem.cpp
	${_IXWEBSOCKET_ROOT}/IXSelectInterrupt.cpp
	${_IXWEBSOCKET_ROOT}/IXSelectInterruptFactory.cpp
	${_IXWEBSOCKET_ROOT}/IXSelectInterruptPipe.cpp
	${_IXWEBSOCKET_ROOT}/IXSelectInterruptEvent.cpp
	${_IXWEBSOCKET_ROOT}/IXSetThreadName.cpp
	${_IXWEBSOCKET_ROOT}/IXSocket.cpp
	${_IXWEBSOCKET_ROOT}/IXSocketConnect.cpp
	${_IXWEBSOCKET_ROOT}/IXSocketFactory.cpp
	${_IXWEBSOCKET_ROOT}/IXSocketServer.cpp
	${_IXWEBSOCKET_ROOT}/IXSocketTLSOptions.cpp
	${_IXWEBSOCKET_ROOT}/IXStrCaseCompare.cpp
	${_IXWEBSOCKET_ROOT}/IXUdpSocket.cpp
	${_IXWEBSOCKET_ROOT}/IXUrlParser.cpp
	${_IXWEBSOCKET_ROOT}/IXUuid.cpp
	${_IXWEBSOCKET_ROOT}/IXUserAgent.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocket.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketCloseConstants.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketHandshake.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketHttpHeaders.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketPerMessageDeflate.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketPerMessageDeflateCodec.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketPerMessageDeflateOptions.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketProxyServer.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketServer.cpp
	${_IXWEBSOCKET_ROOT}/IXWebSocketTransport.cpp
)

set(_IXWEBSOCKET_HEADERS
	${_IXWEBSOCKET_ROOT}/IXBase64.h
	${_IXWEBSOCKET_ROOT}/IXBench.h
	${_IXWEBSOCKET_ROOT}/IXCancellationRequest.h
	${_IXWEBSOCKET_ROOT}/IXConnectionState.h
	${_IXWEBSOCKET_ROOT}/IXDNSLookup.h
	${_IXWEBSOCKET_ROOT}/IXExponentialBackoff.h
	${_IXWEBSOCKET_ROOT}/IXGetFreePort.h
	${_IXWEBSOCKET_ROOT}/IXGzipCodec.h
	${_IXWEBSOCKET_ROOT}/IXHttp.h
	${_IXWEBSOCKET_ROOT}/IXHttpClient.h
	${_IXWEBSOCKET_ROOT}/IXHttpServer.h
	${_IXWEBSOCKET_ROOT}/IXNetSystem.h
	${_IXWEBSOCKET_ROOT}/IXProgressCallback.h
	${_IXWEBSOCKET_ROOT}/IXSelectInterrupt.h
	${_IXWEBSOCKET_ROOT}/IXSelectInterruptFactory.h
	${_IXWEBSOCKET_ROOT}/IXSelectInterruptPipe.h
	${_IXWEBSOCKET_ROOT}/IXSelectInterruptEvent.h
	${_IXWEBSOCKET_ROOT}/IXSetThreadName.h
	${_IXWEBSOCKET_ROOT}/IXSocket.h
	${_IXWEBSOCKET_ROOT}/IXSocketConnect.h
	${_IXWEBSOCKET_ROOT}/IXSocketFactory.h
	${_IXWEBSOCKET_ROOT}/IXSocketServer.h
	${_IXWEBSOCKET_ROOT}/IXSocketTLSOptions.h
	${_IXWEBSOCKET_ROOT}/IXStrCaseCompare.h
	${_IXWEBSOCKET_ROOT}/IXUdpSocket.h
	${_IXWEBSOCKET_ROOT}/IXUniquePtr.h
	${_IXWEBSOCKET_ROOT}/IXUrlParser.h
	${_IXWEBSOCKET_ROOT}/IXUuid.h
	${_IXWEBSOCKET_ROOT}/IXUtf8Validator.h
	${_IXWEBSOCKET_ROOT}/IXUserAgent.h
	${_IXWEBSOCKET_ROOT}/IXWebSocket.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketCloseConstants.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketCloseInfo.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketErrorInfo.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketHandshake.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketHandshakeKeyGen.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketHttpHeaders.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketInitResult.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketMessage.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketMessageType.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketOpenInfo.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketPerMessageDeflate.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketPerMessageDeflateCodec.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketPerMessageDeflateOptions.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketProxyServer.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketSendData.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketSendInfo.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketServer.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketTransport.h
	${_IXWEBSOCKET_ROOT}/IXWebSocketVersion.h
)

option(USE_TLS "Enable TLS support" FALSE)

if(USE_TLS)
	# default to securetranport on Apple if nothing is configured
	if(APPLE)
		if(NOT USE_MBED_TLS AND NOT USE_OPEN_SSL AND NOT USE_LIBRE_SSL) # unless we want something else
			set(USE_SECURE_TRANSPORT ON)
		endif()
	# default to mbedtls on windows if nothing is configured
	elseif(WIN32)
		if(NOT USE_OPEN_SSL AND NOT USE_LIBRE_SSL) # unless we want something else
			set(USE_MBED_TLS ON)
		endif()
	else() # default to OpenSSL on all other platforms
		if(NOT USE_MBED_TLS AND NOT USE_LIBRE_SSL) # Unless mbedtls or libressl is requested
			set(USE_OPEN_SSL ON)
			set(requires "openssl")
		endif()
	endif()

	if(USE_MBED_TLS)
		list(APPEND _IXWEBSOCKET_HEADERS ${_IXWEBSOCKET_ROOT}/IXSocketMbedTLS.h)
		list(APPEND _IXWEBSOCKET_SOURCES ${_IXWEBSOCKET_ROOT}/IXSocketMbedTLS.cpp)
	elseif(USE_SECURE_TRANSPORT)
		list(APPEND _IXWEBSOCKET_HEADERS ${_IXWEBSOCKET_ROOT}/IXSocketAppleSSL.h)
		list(APPEND _IXWEBSOCKET_SOURCES ${_IXWEBSOCKET_ROOT}/IXSocketAppleSSL.cpp)
	elseif(USE_OPEN_SSL OR USE_LIBRE_SSL)
		list(APPEND _IXWEBSOCKET_HEADERS ${_IXWEBSOCKET_ROOT}/IXSocketOpenSSL.h)
		list(APPEND _IXWEBSOCKET_SOURCES ${_IXWEBSOCKET_ROOT}/IXSocketOpenSSL.cpp)
	else()
		message(FATAL_ERROR "TLS Configuration error: unknown backend")
	endif()
endif()

ncine_add_dependency(IXWebSocket STATIC)

target_sources(IXWebSocket PRIVATE ${_IXWEBSOCKET_HEADERS} ${_IXWEBSOCKET_SOURCES})

target_include_directories(IXWebSocket
	PRIVATE "${_IXWEBSOCKET_ROOT}"
	INTERFACE "${NCINE_SOURCE_DIR}/Dependencies")

source_group("Header Files" FILES ${_IXWEBSOCKET_HEADERS})
source_group("Source Files" FILES ${_IXWEBSOCKET_SOURCES})

if(USE_TLS)
	target_compile_definitions(IXWebSocket PUBLIC IXWEBSOCKET_USE_TLS)
	if(USE_MBED_TLS)
		target_compile_definitions(IXWebSocket PUBLIC IXWEBSOCKET_USE_MBED_TLS)
		target_link_libraries(IXWebSocket PUBLIC MbedTLS::mbedtls)
	elseif(USE_OPEN_SSL)
		target_compile_definitions(IXWebSocket PUBLIC IXWEBSOCKET_USE_OPEN_SSL)
		target_link_libraries(IXWebSocket PUBLIC OpenSSL::SSL OpenSSL::Crypto)
	elseif(USE_LIBRE_SSL)
		target_compile_definitions(IXWebSocket PUBLIC IXWEBSOCKET_USE_LIBRE_SSL)
	elseif(USE_SECURE_TRANSPORT)
		target_compile_definitions(IXWebSocket PUBLIC IXWEBSOCKET_USE_SECURE_TRANSPORT)
		# SecureTransport lives in the Security framework and uses CoreFoundation types
		target_link_libraries(IXWebSocket PUBLIC "-framework Security" "-framework CoreFoundation")
	else()
		message(FATAL_ERROR "TLS Configuration error: Unknown backend")
	endif()
endif()

if(MSVC)
	# IXWebSocket uses classic CRT functions (strerror/sscanf/strncpy) that MSVC flags as deprecated
	# (C4996, which is an error under the stricter UWP settings)
	target_compile_definitions(IXWebSocket PRIVATE "_CRT_SECURE_NO_WARNINGS")
endif()

if(WIN32 AND NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)
	# Winsock for the sockets, Crypt32 for the OpenSSL backend's Windows certificate store loader
	target_link_libraries(IXWebSocket PUBLIC ws2_32 crypt32)
endif()

set(IXWebSocket_FOUND TRUE)
