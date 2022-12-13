#include "DiscordRpcClient.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)

#include "../../nCine/Base/Algorithms.h"

namespace Jazz2::UI
{
	DiscordRpcClient& DiscordRpcClient::Current()
	{
		static DiscordRpcClient current;
		return current;
	}

	DiscordRpcClient::DiscordRpcClient()
		:
		_hPipe(NULL),
		_hEventRead(NULL),
		_hEventWrite(NULL),
		_nonce(0)
	{
	}

	DiscordRpcClient::~DiscordRpcClient()
	{
		Disconnect();
	}

	bool DiscordRpcClient::Connect(const StringView& clientId)
	{
		if (_hPipe != NULL) {
			return true;
		}
		if (clientId.empty()) {
			return false;
		}

		wchar_t pipeName[32];
		for (int i = 0; i < 10; i++) {
			wsprintfW(pipeName, L"\\\\.\\pipe\\discord-ipc-%i", i);

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

		return true;
	}

	void DiscordRpcClient::Disconnect()
	{
		HANDLE pipe = (HANDLE)InterlockedExchangePointer(&_hPipe, NULL);
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
	}

	bool DiscordRpcClient::IsSupported() const
	{
		return (_hPipe != nullptr);
	}

	bool DiscordRpcClient::SetRichPresence(const RichPresence& richPresence)
	{
		if (!_pendingFrame.empty()) {
			return false;
		}

		DWORD processId = ::GetCurrentProcessId();

		char buffer[1024];
		int bufferOffset = formatString(buffer, sizeof(buffer), "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":%i,\"args\":{\"pid\":%i,\"activity\":{", ++_nonce, processId);

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
		
		_pendingFrame = String(buffer, bufferOffset);
		::SetEvent(_hEventWrite);
	}

	bool DiscordRpcClient::WriteFrame(Opcodes opcode, const char* buffer, uint32_t bufferSize)
	{
		char frameHeader[8];
		*(uint32_t*)&frameHeader[0] = (uint32_t)opcode;
		*(uint32_t*)&frameHeader[4] = bufferSize;

		DWORD bytesWritten = 0;
		return ::WriteFile(_hPipe, frameHeader, sizeof(frameHeader), &bytesWritten, NULL) && ::WriteFile(_hPipe, buffer, bufferSize, &bytesWritten, NULL);
	}

	void DiscordRpcClient::OnBackgroundThread(void* args)
	{
		DiscordRpcClient* client = reinterpret_cast<DiscordRpcClient*>(args);

		// Handshake
		char buffer[2048];
		int bufferSize = formatString(buffer, sizeof(buffer), "{\"v\":1,\"client_id\":\"%s\"}", client->_clientId.data());
		client->WriteFrame(Opcodes::Handshake, buffer, bufferSize);

		DWORD bytesRead = 0;
		OVERLAPPED ov = { };
		ov.hEvent = client->_hEventRead;
		if (!::ReadFile(client->_hPipe, buffer, sizeof(buffer), &bytesRead, &ov)) {
			DWORD error = ::GetLastError();
			if (error == ERROR_BROKEN_PIPE) {
				HANDLE pipe = (HANDLE)InterlockedExchangePointer(&client->_hPipe, NULL);
				if (pipe != NULL) {
					::CloseHandle(client->_hPipe);
				}
				return;
			}
		}

		HANDLE waitHandles[] = { client->_hEventRead, client->_hEventWrite };
		while (client->_hPipe != NULL) {
			DWORD dwEvent = ::WaitForMultipleObjects(_countof(waitHandles), waitHandles, FALSE, INFINITE);
			switch (dwEvent) {
				case WAIT_OBJECT_0: {
					if (bytesRead > 0) {
						Opcodes opcode = (Opcodes)*(uint32_t*)&buffer[0];
						uint32_t frameSize = *(uint32_t*)&buffer[4];
						if (frameSize >= sizeof(buffer)) {
							continue;
						}

						switch (opcode) {
							case Opcodes::Handshake:
							case Opcodes::Close: {
								HANDLE pipe = (HANDLE)InterlockedExchangePointer(&client->_hPipe, NULL);
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
						//	LOGW_X("Partial read (%i bytes left)", bytesRead - (frameSize + 8));
						//}
					}

					if (!::ReadFile(client->_hPipe, buffer, sizeof(buffer), &bytesRead, &ov)) {
						DWORD error = ::GetLastError();
						if (error == ERROR_BROKEN_PIPE) {
							HANDLE pipe = (HANDLE)InterlockedExchangePointer(&client->_hPipe, NULL);
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
						client->_pendingFrame = String();
					}
					break;
				}
			}	
		}
	}
}

#endif