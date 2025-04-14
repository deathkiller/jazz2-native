#include "DiscordRpcClient.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)

#include "../../nCine/Base/Algorithms.h"

#define SIMDJSON_EXCEPTIONS 0
#include "../../simdjson/simdjson.h"

#include <Containers/StringStlView.h>

#if defined(DEATH_TARGET_UNIX)
#	include <unistd.h>
#	include <sys/socket.h>
#	include <sys/un.h>
#endif

using namespace Death::Containers::Literals;

using namespace simdjson;
using namespace std::string_view_literals;

namespace Jazz2::UI
{
	DiscordRpcClient& DiscordRpcClient::Get()
	{
		static DiscordRpcClient current;
		return current;
	}

	DiscordRpcClient::DiscordRpcClient()
		:
#if defined(DEATH_TARGET_WINDOWS)
		_hPipe(INVALID_HANDLE_VALUE), _hEventRead(NULL), _hEventWrite(NULL),
#else
		_sockFd(-1),
#endif
		_nonce(0), _userId(0)
	{
	}

	DiscordRpcClient::~DiscordRpcClient()
	{
		Disconnect();
	}

	bool DiscordRpcClient::Connect(StringView clientId)
	{
		if (clientId.empty()) {
			return false;
		}

#if defined(DEATH_TARGET_WINDOWS)
		if (_hPipe != INVALID_HANDLE_VALUE) {
			return true;
		}

		wchar_t pipeName[32];
		for (std::int32_t i = 0; i < 10; i++) {
			swprintf_s(pipeName, L"\\\\.\\pipe\\discord-ipc-%i", i);

			_hPipe = ::CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if (_hPipe != INVALID_HANDLE_VALUE) {
				break;
			}
		}

		if (_hPipe == INVALID_HANDLE_VALUE) {
			return false;
		}

		_clientId = clientId;
		_nonce = 0;
		_hEventRead = ::CreateEvent(NULL, FALSE, TRUE, NULL);
		_hEventWrite = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		_thread = Thread(DiscordRpcClient::OnBackgroundThread, this);
#else
		if (_sockFd >= 0) {
			return true;
		}

		static const StringView RpcPaths[] = {
			"%s/discord-ipc-%i"_s,
			"%s/app/com.discordapp.Discord/discord-ipc-%i"_s,
			"%s/snap.discord-canary/discord-ipc-%i"_s,
			"%s/snap.discord/discord-ipc-%i"_s
		};

		_sockFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (_sockFd < 0) {
			LOGE("Failed to create socket");
			return false;
		}
		
#	if defined(SO_NOSIGPIPE)
		std::int32_t optval = 1;
		::setsockopt(_sockFd, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
#	endif

		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;

		StringView tempPath = ::getenv("XDG_RUNTIME_DIR");
		if (tempPath.empty()) {
			tempPath = ::getenv("TMPDIR");
			if (tempPath.empty()) {
				tempPath = ::getenv("TMP");
				if (tempPath.empty()) {
					tempPath = ::getenv("TEMP");
					if (tempPath.empty()) {
						tempPath = "/tmp"_s;
					}
				}
			}
		}

		bool isConnected = false;
		for (std::int32_t j = 0; j < std::int32_t(arraySize(RpcPaths)); j++) {
			for (std::int32_t i = 0; i < 10; i++) {
				formatString(addr.sun_path, sizeof(addr.sun_path), RpcPaths[j].data(), tempPath.data(), i);
				if (::connect(_sockFd, (struct sockaddr*)&addr, sizeof(addr)) >= 0) {
					isConnected = true;
					break;
				}
			}
		}

		if (!isConnected) {
			::close(_sockFd);
			_sockFd = -1;
			return false;
		}

		_clientId = clientId;
		_nonce = 0;
		_thread = Thread(DiscordRpcClient::OnBackgroundThread, this);
#endif
		return true;
	}

	void DiscordRpcClient::Disconnect()
	{
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE pipe = _hPipe.exchange(INVALID_HANDLE_VALUE);
		if (pipe != INVALID_HANDLE_VALUE) {
			::CancelIoEx(pipe, NULL);
			::CloseHandle(pipe);
			_thread.Join();
		}
		if (_hEventRead != NULL) {
			::CloseHandle(_hEventRead);
			_hEventRead = NULL;
		}
		if (_hEventWrite != NULL) {
			::CloseHandle(_hEventWrite);
			_hEventWrite = NULL;
		}
#else
		std::int32_t sockFd = _sockFd.exchange(-1);
		if (sockFd >= 0) {
			_thread.Abort();
			::close(sockFd);
		}
#endif
	}

	bool DiscordRpcClient::IsSupported() const
	{
#if defined(DEATH_TARGET_WINDOWS)
		return (_hPipe != INVALID_HANDLE_VALUE);
#else
		return (_sockFd >= 0);
#endif
	}

	std::uint64_t DiscordRpcClient::GetUserId() const
	{
		return _userId;
	}

	StringView DiscordRpcClient::GetUserDisplayName() const
	{
		return _userDisplayName;
	}

	bool DiscordRpcClient::SetRichPresence(const RichPresence& richPresence)
	{
#if defined(DEATH_TARGET_WINDOWS)
		if (!_pendingFrame.empty() || !IsSupported()) {
			return false;
		}

		DWORD processId = ::GetCurrentProcessId();
#else
		if (!IsSupported()) {
			return false;
		}

		pid_t processId = ::getpid();
#endif
		char buffer[1024];
		std::int32_t bufferOffset = formatString(buffer, sizeof(buffer), "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":%i,\"args\":{\"pid\":%i,\"activity\":{", ++_nonce, processId);

		if (!richPresence.State.empty()) {
			bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "\"state\":\"%s\",", richPresence.State.data());
		}
		if (!richPresence.Details.empty()) {
			bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "\"details\":\"%s\",", richPresence.Details.data());
		}

		bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "\"assets\":{");

		bool isFirst = true;
		if (!richPresence.LargeImage.empty()) {
			isFirst = false;
			bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "\"large_image\":\"%s\"", richPresence.LargeImage.data());
		}

		if (!richPresence.LargeImageTooltip.empty()) {
			if (!isFirst) {
				buffer[bufferOffset++] = ',';
			}
			isFirst = false;
			bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "\"large_text\":\"%s\"", richPresence.LargeImageTooltip.data());
		}

		if (!richPresence.SmallImage.empty()) {
			if (!isFirst) {
				buffer[bufferOffset++] = ',';
			}
			isFirst = false;
			bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "\"small_image\":\"%s\"", richPresence.SmallImage.data());
		}

		if (!richPresence.SmallImageTooltip.empty()) {
			if (!isFirst) {
				buffer[bufferOffset++] = ',';
			}
			isFirst = false;
			bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "\"small_text\":\"%s\"", richPresence.SmallImageTooltip.data());
		}

		bufferOffset += formatString(buffer + bufferOffset, sizeof(buffer) - bufferOffset, "}}}}");
		
#if defined(DEATH_TARGET_WINDOWS)
		_pendingFrame = String(buffer, bufferOffset);
		::SetEvent(_hEventWrite);
#else
		WriteFrame(Opcodes::Frame, buffer, bufferOffset);
#endif
		return true;
	}

	void DiscordRpcClient::ProcessInboundFrame(const char* json, std::size_t length, std::size_t allocated)
	{
		LOGD("%s", String(json, length).data());

		ondemand::parser parser;
		ondemand::document doc;
		if (parser.iterate(json, length, allocated).get(doc) == SUCCESS) {
			std::string_view cmd;
			ondemand::object data;
			if (doc["cmd"].get(cmd) == SUCCESS && cmd == "DISPATCH"sv && doc["data"].get(data) == SUCCESS) {
				ondemand::object user; // {"id":"123456789","username":"nick.name","discriminator":"0","global_name":"Display Name","avatar":"123456789abcdef","avatar_decoration_data":null,"bot":false,"flags":32,"premium_type":0}
				std::string_view userId, userGlobalName;
				if (data["user"].get(user) == SUCCESS && user["id"].get(userId) == SUCCESS && user["global_name"].get(userGlobalName) == SUCCESS) {
					_userId = stou64(userId.data(), userId.size());
					_userDisplayName = userGlobalName;
					LOGD("Connected to Discord as user \"%s\" (%llu)", _userDisplayName.data(), _userId);
				}
			}
		}
	}

	bool DiscordRpcClient::WriteFrame(Opcodes opcode, const char* buffer, std::uint32_t bufferSize)
	{
		char frameHeader[8];
		*(std::uint32_t*)&frameHeader[0] = (std::uint32_t)opcode;
		*(std::uint32_t*)&frameHeader[4] = bufferSize;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD bytesWritten = 0;
		return ::WriteFile(_hPipe, frameHeader, sizeof(frameHeader), &bytesWritten, NULL) &&
			   ::WriteFile(_hPipe, buffer, bufferSize, &bytesWritten, NULL);
#else
		if (::write(_sockFd, frameHeader, sizeof(frameHeader)) < 0) {
			return false;
		}

		std::int32_t bytesTotal = 0;
		while (bytesTotal < bufferSize) {
			std::int32_t bytesWritten = ::write(_sockFd, buffer + bytesTotal, bufferSize - bytesTotal);
			if (bytesWritten < 0) {
				return false;
			}
			bytesTotal += bytesWritten;
		}
		return true;
#endif
	}

	void DiscordRpcClient::OnBackgroundThread(void* args)
	{
		DiscordRpcClient* _this = static_cast<DiscordRpcClient*>(args);

		// Handshake
		char buffer[2048];
		std::int32_t bufferSize = formatString(buffer, sizeof(buffer), "{\"v\":1,\"client_id\":\"%s\"}", _this->_clientId.data());
		_this->WriteFrame(Opcodes::Handshake, buffer, bufferSize);

#if defined(DEATH_TARGET_WINDOWS)
		HANDLE hPipe = _this->_hPipe;
		OVERLAPPED ov = {};
		ov.hEvent = _this->_hEventRead;
		if (!::ReadFile(hPipe, buffer, sizeof(buffer), NULL, &ov)) {
			DWORD error = ::GetLastError();
			if (error == ERROR_BROKEN_PIPE) {
				_this->_hPipe = INVALID_HANDLE_VALUE;
				if (hPipe != INVALID_HANDLE_VALUE) {
					::CloseHandle(hPipe);
				}
				return;
			}
		}

		HANDLE waitHandles[] = { _this->_hEventRead, _this->_hEventWrite };
		while (_this->_hPipe != INVALID_HANDLE_VALUE) {
			DWORD dwEvent = ::WaitForMultipleObjects(static_cast<DWORD>(arraySize(waitHandles)), waitHandles, FALSE, INFINITE);
			switch (dwEvent) {
				case WAIT_OBJECT_0: {
					DWORD bytesRead;
					if (::GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE) && bytesRead > 0) {
						Opcodes opcode = (Opcodes)*(std::uint32_t*)&buffer[0];
						std::uint32_t frameSize = *(std::uint32_t*)&buffer[4];
						if (frameSize >= sizeof(buffer)) {
							continue;
						}

						switch (opcode) {
							case Opcodes::Handshake: // Invalid response opcode
							case Opcodes::Close: {
								_this->_hPipe = INVALID_HANDLE_VALUE;
								if (hPipe != INVALID_HANDLE_VALUE) {
									::CloseHandle(hPipe);
								}
								return;
							}
							case Opcodes::Ping: {
								_this->WriteFrame(Opcodes::Pong, &buffer[8], frameSize);
								break;
							}
							case Opcodes::Frame: {
								_this->ProcessInboundFrame(&buffer[8], frameSize, sizeof(buffer) - 8);
								break;
							}
						}

						//if (bytesRead > frameSize + 8) {
						//	LOGW("Partial read (%i bytes left)", bytesRead - (frameSize + 8));
						//}
					}

					if (!::ReadFile(hPipe, buffer, sizeof(buffer), NULL, &ov)) {
						DWORD error = ::GetLastError();
						if (error == ERROR_BROKEN_PIPE) {
							_this->_hPipe = INVALID_HANDLE_VALUE;
							if (hPipe != INVALID_HANDLE_VALUE) {
								::CloseHandle(hPipe);
							}
							return;
						}
					}
					break;
				}
				case WAIT_OBJECT_0 + 1: {
					if (!_this->_pendingFrame.empty()) {
						_this->WriteFrame(Opcodes::Frame, _this->_pendingFrame.data(), _this->_pendingFrame.size());
						_this->_pendingFrame = {};
					}
					break;
				}
			}	
		}
#else
		while (_this->_sockFd >= 0) {
			std::int32_t bytesRead = ::read(_this->_sockFd, buffer, sizeof(buffer));
			if (bytesRead <= 0) {
				LOGE("Failed to read from socket: %i", bytesRead);
				std::int32_t sockFd = _this->_sockFd.exchange(-1);
				if (sockFd >= 0) {
					::close(sockFd);
				}
				break;
			}

			Opcodes opcode = (Opcodes)*(std::uint32_t*)&buffer[0];
			std::uint32_t frameSize = *(std::uint32_t*)&buffer[4];
			if (frameSize >= sizeof(buffer)) {
				continue;
			}

			switch (opcode) {
				case Opcodes::Handshake:
				case Opcodes::Close: {
					std::int32_t sockFd = _this->_sockFd.exchange(-1);
					if (sockFd >= 0) {
						::close(sockFd);
					}
					return;
				}
				case Opcodes::Ping: {
					_this->WriteFrame(Opcodes::Pong, &buffer[8], frameSize);
					break;
				}
				case Opcodes::Frame: {
					_this->ProcessInboundFrame(&buffer[8], frameSize, sizeof(buffer) - 8);
					break;
				}
			}

			//if (bytesRead > frameSize + 8) {
			//	LOGW("Partial read (%i bytes left)", bytesRead - (frameSize + 8));
			//}
		}
#endif
	}
}

#endif