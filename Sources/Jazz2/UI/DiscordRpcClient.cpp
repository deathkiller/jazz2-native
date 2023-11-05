#include "DiscordRpcClient.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)

#include "../../nCine/Base/Algorithms.h"

#if defined(DEATH_TARGET_UNIX)
#	include <unistd.h>
#	include <sys/socket.h>
#	include <sys/un.h>
#endif

#include <Threading/Interlocked.h>

using namespace Death::Containers::Literals;

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
		_hPipe(NULL), _hEventRead(NULL), _hEventWrite(NULL),
#else
		_sockFd(-1),
#endif
		_nonce(0)
	{
	}

	DiscordRpcClient::~DiscordRpcClient()
	{
		Disconnect();
	}

	bool DiscordRpcClient::Connect(const StringView& clientId)
	{
		if (clientId.empty()) {
			return false;
		}

#if defined(DEATH_TARGET_WINDOWS)
		if (_hPipe != NULL) {
			return true;
		}

		wchar_t pipeName[32];
		for (std::int32_t i = 0; i < 10; i++) {
			swprintf_s(pipeName, L"\\\\.\\pipe\\discord-ipc-%i", i);

			_hPipe = CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if (_hPipe != NULL && _hPipe != INVALID_HANDLE_VALUE) {
				break;
			}
		}

		if (_hPipe == NULL || _hPipe == INVALID_HANDLE_VALUE) {
			_hPipe = NULL;
			return false;
		}

		_clientId = clientId;
		_nonce = 0;
		_hEventRead = ::CreateEvent(NULL, FALSE, TRUE, NULL);
		_hEventWrite = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		_thread.Run(DiscordRpcClient::OnBackgroundThread, this);
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
		for (std::int32_t j = 0; j < countof(RpcPaths); j++) {
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
		_thread.Run(DiscordRpcClient::OnBackgroundThread, this);
#endif
		return true;
	}

	void DiscordRpcClient::Disconnect()
	{
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE pipe = Interlocked::ExchangePointer(&_hPipe, NULL);
		if (pipe != NULL) {
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
		int32_t sockFd = Interlocked::Exchange(&_sockFd, -1);
		if (sockFd >= 0) {
			_thread.Abort();
			::close(sockFd);
		}
#endif
	}

	bool DiscordRpcClient::IsSupported() const
	{
#if defined(DEATH_TARGET_WINDOWS)
		return (_hPipe != nullptr);
#else
		return (_sockFd >= 0);
#endif
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

	bool DiscordRpcClient::WriteFrame(Opcodes opcode, const char* buffer, std::uint32_t bufferSize)
	{
		char frameHeader[8];
		*(std::uint32_t*)&frameHeader[0] = (std::uint32_t)opcode;
		*(std::uint32_t*)&frameHeader[4] = bufferSize;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD bytesWritten = 0;
		return ::WriteFile(_hPipe, frameHeader, sizeof(frameHeader), &bytesWritten, NULL) && ::WriteFile(_hPipe, buffer, bufferSize, &bytesWritten, NULL);
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
		DiscordRpcClient* client = static_cast<DiscordRpcClient*>(args);

		// Handshake
		char buffer[2048];
		std::int32_t bufferSize = formatString(buffer, sizeof(buffer), "{\"v\":1,\"client_id\":\"%s\"}", client->_clientId.data());
		client->WriteFrame(Opcodes::Handshake, buffer, bufferSize);

#if defined(DEATH_TARGET_WINDOWS)
		DWORD bytesRead = 0;
		OVERLAPPED ov = { };
		ov.hEvent = client->_hEventRead;
		if (!::ReadFile(client->_hPipe, buffer, sizeof(buffer), &bytesRead, &ov)) {
			DWORD error = ::GetLastError();
			if (error == ERROR_BROKEN_PIPE) {
				HANDLE pipe = Interlocked::ExchangePointer(&client->_hPipe, NULL);
				if (pipe != NULL) {
					::CloseHandle(client->_hPipe);
				}
				return;
			}
		}

		HANDLE waitHandles[] = { client->_hEventRead, client->_hEventWrite };
		while (client->_hPipe != NULL) {
			DWORD dwEvent = ::WaitForMultipleObjects(countof(waitHandles), waitHandles, FALSE, INFINITE);
			switch (dwEvent) {
				case WAIT_OBJECT_0: {
					if (bytesRead > 0) {
						Opcodes opcode = (Opcodes)*(std::uint32_t*)&buffer[0];
						std::uint32_t frameSize = *(std::uint32_t*)&buffer[4];
						if (frameSize >= sizeof(buffer)) {
							continue;
						}

						switch (opcode) {
							case Opcodes::Handshake:
							case Opcodes::Close: {
								HANDLE pipe = Interlocked::ExchangePointer(&client->_hPipe, NULL);
								if (pipe != NULL) {
									::CloseHandle(client->_hPipe);
								}
								return;
							}
							case Opcodes::Ping: {
								client->WriteFrame(Opcodes::Pong, &buffer[8], frameSize);
								break;
							}
						}

						//if (bytesRead > frameSize + 8) {
						//	LOGW("Partial read (%i bytes left)", bytesRead - (frameSize + 8));
						//}
					}

					if (!::ReadFile(client->_hPipe, buffer, sizeof(buffer), &bytesRead, &ov)) {
						DWORD error = ::GetLastError();
						if (error == ERROR_BROKEN_PIPE) {
							HANDLE pipe = Interlocked::ExchangePointer(&client->_hPipe, NULL);
							if (pipe != NULL) {
								::CloseHandle(client->_hPipe);
							}
							return;
						}
					}
					break;
				}
				case WAIT_OBJECT_0 + 1: {
					if (!client->_pendingFrame.empty()) {
						client->WriteFrame(Opcodes::Frame, client->_pendingFrame.data(), client->_pendingFrame.size());
						client->_pendingFrame = { };
					}
					break;
				}
			}	
		}
#else
		while (client->_sockFd >= 0) {
			std::int32_t bytesRead = ::read(client->_sockFd, buffer, sizeof(buffer));
			if (bytesRead <= 0) {
				LOGE("Failed to read from socket: %i", bytesRead);
				std::int32_t sockFd = client->_sockFd;
				if (sockFd >= 0) {
					client->_sockFd = -1;
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
					std::int32_t sockFd = client->_sockFd;
					if (sockFd >= 0) {
						client->_sockFd = -1;
						::close(sockFd);
					}
					return;
				}
				case Opcodes::Ping: {
					client->WriteFrame(Opcodes::Pong, &buffer[8], frameSize);
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