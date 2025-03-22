#include "WebRequest.h"

#include "FileStream.h"
#include "FileSystem.h"
#include "MemoryStream.h"
#include "../Environment.h"
#include "../Utf8.h"
#include "../Containers/GrowableArray.h"
#include "../Containers/StringConcatenable.h"
#include "../Containers/StringUtils.h"

#include <atomic>
#include <cstdio>
#include <map>

#if defined(DEATH_TARGET_WINDOWS)
#	include <winhttp.h>
#elif defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_UNIX)
#	include <thread>
#	include <unordered_map>
#	include <unistd.h>
#	include <curl/curl.h>
#endif

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	class IRefCounted
	{
	public:
		IRefCounted()
			: _count(1) {
		}

		void AddRef() {
			++_count;
		}
		void Release() {
			if (--_count == 0) {
				delete this;
			}
		}

	protected:
		virtual ~IRefCounted() = default;

	private:
		std::atomic_int32_t _count;
	};

	using WebRequestHeaderMap = std::map<String, String>;

	class WebAuthChallengeImpl : public IRefCounted
	{
	public:
		~WebAuthChallengeImpl() override = default;

		WebAuthChallenge::Source GetSource() const {
			return _source;
		}

		virtual void SetCredentials(const WebCredentials& cred) = 0;

	protected:
		explicit WebAuthChallengeImpl(WebAuthChallenge::Source source)
			: _source(source) {}

	private:
		const WebAuthChallenge::Source _source;
	};

	class WebResponseImpl;

	class WebRequestImpl : public IRefCounted
	{
		friend class WebResponseImpl;

	public:
		using Result = WebRequestAsync::Result;

		bool IsAsync() const {
			return (_session != nullptr);
		}

		~WebRequestImpl() override = default;

		WebRequestImpl(const WebRequestImpl&) = delete;
		WebRequestImpl& operator=(const WebRequestImpl&) = delete;

		void SetHeader(StringView name, StringView value) {
			_headers[name] = value;
		}

		void SetMethod(StringView method) {
			_method = method;
		}

		void SetData(StringView text, StringView contentType);

		bool SetData(std::unique_ptr<Stream> dataStream, StringView contentType, std::int64_t dataSize = -1);

		void SetStorage(WebRequestAsync::Storage storage) {
			_storage = storage;
		}

		WebRequestAsync::Storage GetStorage() const {
			return _storage;
		}

		virtual Result Execute() = 0;

		virtual void Run() = 0;

		void Cancel();

		virtual WebResponseImplPtr GetResponse() const = 0;

		virtual WebAuthChallengeImplPtr GetAuthChallenge() const = 0;

		std::int32_t GetId() const {
			return _id;
		}

		WebSessionAsync& GetSession() const {
			return *_session;
		}

		WebSessionImpl& GetSessionImpl() const {
			return *_sessionImpl;
		}

		WebRequest::State GetState() const {
			return _state;
		}

		virtual std::int64_t GetBytesSent() const = 0;
		virtual std::int64_t GetBytesExpectedToSend() const = 0;
		virtual std::int64_t GetBytesReceived() const;
		virtual std::int64_t GetBytesExpectedToReceive() const;

		virtual WebRequestHandle GetNativeHandle() const = 0;

		void MakeInsecure(WebRequestBase::Ignore flags) {
			_securityFlags = flags;
		}

		WebRequestBase::Ignore GetSecurityFlags() const {
			return _securityFlags;
		}

		void SetState(WebRequest::State state, StringView failMessage = {});
		void ReportDataReceived(std::size_t sizeReceived);

	protected:
		WebRequest::Storage _storage;
		WebRequestBase::Ignore _securityFlags;
		String _method;
		WebRequestHeaderMap _headers;
		std::int64_t _dataSize;
		std::unique_ptr<Stream> _dataStream;

		WebRequestImpl(WebSessionAsync& session, WebSessionImpl& sessionImpl, Function<void(WebRequestEvent&)>&& callback, std::int32_t id);

		explicit WebRequestImpl(WebSessionImpl& sessionImpl);

		bool WasCancelled() const {
			return _cancelled;
		}

		String GetHTTPMethod() const;

		static Result GetResultFromHTTPStatus(const WebResponseImplPtr& response);

		void SetFinalStateFromStatus() {
			HandleResult(GetResultFromHTTPStatus(GetResponse()));
		}

		void HandleResult(const Result& result) {
			SetState(result.state, result.error);
		}

		bool CheckResult(const Result& result) {
			if (!result) {
				HandleResult(result);
				return false;
			}
			return true;
		}

	private:
		WebSessionImplPtr _sessionImpl;
		WebSessionAsync* const _session;
		Function<void(WebRequestEvent&)> _callback;
		const std::int32_t _id;
		WebRequest::State _state;
		std::int64_t _bytesReceived;
		String _dataText;
		bool _cancelled;

		virtual void DoCancel() = 0;

		void ProcessStateEvent(WebRequest::State state, StringView failMsg);
	};

	class WebResponseImpl : public IRefCounted
	{
		friend class WebRequestImpl;

	public:
		~WebResponseImpl() override;

		virtual std::int64_t GetContentLength() const = 0;

		virtual String GetURL() const = 0;

		virtual String GetHeader(StringView name) const = 0;

		virtual Array<String> GetAllHeaderValues(StringView name) const = 0;

		virtual String GetMimeType() const;

		virtual String GetContentType() const;

		virtual std::int32_t GetStatus() const = 0;

		virtual String GetStatusText() const = 0;

		virtual Stream* GetStream() const;

		virtual String GetSuggestedFileName() const;

		String AsString() const;

		virtual String GetDataFile() const;

		WebRequest::Result InitializeFileStorage();

		void ReportDataReceived(std::size_t sizeReceived);

	protected:
		WebRequestImpl& _request;

		explicit WebResponseImpl(WebRequestImpl& request);

		void* GetDataBuffer(std::size_t sizeNeeded);
		void PreAllocateBuffer(std::size_t sizeNeeded);

	private:
		SmallVector<char, 0> _readBuffer;
		mutable std::unique_ptr<FileStream> _file;
		mutable std::unique_ptr<Stream> _stream;

		void Finalize();
	};

	class WebSessionFactory
	{
	public:
		virtual WebSessionImpl* Create() = 0;
		virtual WebSessionImpl* CreateAsync() = 0;

		virtual bool Initialize() {
			return true;
		}

		virtual ~WebSessionFactory() = default;
	};

	class WebSessionImpl : public IRefCounted
	{
		friend class WebRequest;

	public:
		enum class Mode {
			Async,
			Sync
		};

		~WebSessionImpl() override;

		virtual WebRequestImplPtr CreateRequest(WebSession& session, StringView url) = 0;
		virtual WebRequestImplPtr CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id) = 0;

		void AddCommonHeader(StringView name, StringView value) {
			_headers[name] = value;
		}

		String GetTempDirectory() const;

		void SetTempDirectory(StringView path) {
			_tempDir = path;
		}

		virtual bool SetProxy(const WebProxy& proxy) {
			_proxy = proxy; return true;
		}
		const WebProxy& GetProxy() const {
			return _proxy;
		}

		const WebRequestHeaderMap& GetHeaders() const noexcept {
			return _headers;
		}

		virtual WebSessionHandle GetNativeHandle() const noexcept = 0;

		virtual bool EnablePersistentStorage(bool enable) {
			return false;
		}

	protected:
		explicit WebSessionImpl(Mode mode);

		bool IsAsync() const {
			return (_mode == Mode::Async);
		}

	private:
		const Mode _mode;
		WebRequestHeaderMap _headers;
		String _tempDir;
		WebProxy _proxy{WebProxy::Default()};
	};

	namespace
	{
		inline WebSessionImplPtr FromOwned(WebSessionImpl& impl)
		{
			impl.AddRef();
			return WebSessionImplPtr{&impl};
		}
	}

	WebAuthChallenge::WebAuthChallenge() = default;

	WebAuthChallenge::WebAuthChallenge(const WebAuthChallengeImplPtr& impl)
		: _impl(impl) {}

	WebAuthChallenge::WebAuthChallenge(const WebAuthChallenge&) = default;

	WebAuthChallenge& WebAuthChallenge::operator=(const WebAuthChallenge&) = default;

	WebAuthChallenge::~WebAuthChallenge() = default;

	WebAuthChallenge::Source WebAuthChallenge::GetSource() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", Source::Server);

		return _impl->GetSource();
	}

	void WebAuthChallenge::SetCredentials(const WebCredentials& cred)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetCredentials(cred);
	}

	WebRequestImpl::WebRequestImpl(WebSessionAsync& session, WebSessionImpl& sessionImpl, Function<void(WebRequestEvent&)>&& callback, std::int32_t id)
		: _storage(WebRequest::Storage::Memory), _headers(sessionImpl.GetHeaders()), _dataSize(0), _securityFlags(WebRequestBase::Ignore::None),
			_sessionImpl(FromOwned(sessionImpl)), _session(&session), _callback(Death::move(callback)), _id(id), _state(WebRequest::State::Idle),
			_bytesReceived(0), _cancelled(false)
	{
	}

	WebRequestImpl::WebRequestImpl(WebSessionImpl& sessionImpl)
		: _storage(WebRequest::Storage::Memory), _headers(sessionImpl.GetHeaders()), _dataSize(0), _securityFlags(WebRequestBase::Ignore::None),
			_sessionImpl(FromOwned(sessionImpl)), _session(nullptr), _id(-1), _state(WebRequest::State::Idle),
			_bytesReceived(0), _cancelled(false)
	{
	}

	void WebRequestImpl::Cancel()
	{
		DEATH_DEBUG_ASSERT(IsAsync());

		if (_cancelled) {
			return;
		}

		_cancelled = true;
		DoCancel();
	}

	String WebRequestImpl::GetHTTPMethod() const
	{
		if (!_method.empty()) {
			return StringUtils::uppercase(_method);
		}
		return (_dataSize ? "POST"_s : "GET"_s);
	}

	WebRequest::Result WebRequestImpl::GetResultFromHTTPStatus(const WebResponseImplPtr& resp)
	{
		DEATH_ASSERT(resp, "Cannot be called with an uninitialized object", Result::Error("No response object"_s));

		const std::int32_t status = resp->GetStatus();

		if (status == 401 || status == 407) {
			return Result::Unauthorized(resp->GetStatusText());
		} else if (status >= 400) {
			char errorMessage[64];
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
			::sprintf_s(errorMessage, "Error %i (%s)", status, resp->GetStatusText().data());
#else
			::snprintf(errorMessage, sizeof(errorMessage), "Error %i (%s)", status, resp->GetStatusText().data());
#endif
			return Result::Error(errorMessage);
		} else {
			return Result::Ok(WebRequest::State::Completed);
		}
	}

	void WebRequestImpl::SetData(StringView text, StringView contentType)
	{
		_dataText = text;

		SetData(std::make_unique<MemoryStream>(InPlaceInit, _dataText), contentType);
	}

	bool WebRequestImpl::SetData(std::unique_ptr<Stream> dataStream, StringView contentType, std::int64_t dataSize)
	{
		_dataStream = Death::move(dataStream);

		if (_dataStream != nullptr) {
			DEATH_ASSERT(_dataStream->IsValid(), "Cannot attach invalid stream", false);

			if (dataSize == -1) {
				_dataSize = _dataStream->GetSize();
				if (_dataSize < 0) {
					return false;
				}
				_dataStream->Seek(0, SeekOrigin::Begin);
			} else {
				_dataSize = dataSize;
			}
		} else {
			_dataSize = 0;
		}

		SetHeader("Content-Type"_s, contentType);
		return true;
	}

	std::int64_t WebRequestImpl::GetBytesReceived() const
	{
		return _bytesReceived;
	}

	std::int64_t WebRequestImpl::GetBytesExpectedToReceive() const
	{
		auto resp = GetResponse();
		return (resp ? resp->GetContentLength() : -1);
	}

	void WebRequestImpl::SetState(WebRequest::State state, StringView failMessage)
	{
		DEATH_DEBUG_ASSERT(state != _state);

		if (state == WebRequest::State::Active) {
			AddRef();
		}

		_state = state;
		ProcessStateEvent(state, failMessage);
	}

	void WebRequestImpl::ReportDataReceived(std::size_t sizeReceived)
	{
		_bytesReceived += std::int64_t(sizeReceived);
	}

	namespace
	{
		String SplitParameters(StringView s, WebRequestHeaderMap& parameters)
		{
			SmallVector<char, 64> value;
			auto it = s.begin();
			auto end = s.end();
			while (it != end && *it == ' ') {
				++it;
			}
			while (it != end && *it != ';') {
				value.push_back(*it++);
			}
			StringView valueView = value;
			valueView = valueView.trimmed();
			if (it != end) {
				++it;
			}
			parameters.clear();
			SmallVector<char, 32> pname;
			SmallVector<char, 64> pvalue;
			while (it != end) {
				pname.clear();
				pvalue.clear();
				while (it != end && *it == ' ') {
					++it;
				}
				while (it != end && *it != '=' && *it != ';') {
					pname.push_back(*it++);
				}
				StringView pnameView = pname;
				pnameView = pnameView.trimmed();

				if (it != end && *it != ';') {
					++it;
				}
				while (it != end && *it == ' ') {
					++it;
				}
				while (it != end && *it != ';') {
					if (*it == '"') {
						++it;
						while (it != end && *it != '"') {
							if (*it == '\\') {
								++it;
								if (it != end) {
									pvalue.push_back(*it++);
								}
							} else {
								pvalue.push_back(*it++);
							}
						}
						if (it != end) {
							++it;
						}
					} else if (*it == '\\') {
						++it;
						if (it != end) {
							pvalue.push_back(*it++);
						}
					} else {
						pvalue.push_back(*it++);
					}
				}

				StringView pvalueView = pvalue;
				pvalueView = pvalueView.trimmed();

				if (!pname.empty()) {
					parameters[pnameView] = pvalueView;
				}
				if (it != end) {
					++it;
				}
			}

			return valueView;
		}
	}

	void WebRequestImpl::ProcessStateEvent(WebRequest::State state, StringView failMsg)
	{
		AddRef();
		const WebRequestImplPtr request(this);

		const WebResponseImplPtr& response = GetResponse();

		WebRequestEvent e(GetId(), state, WebRequestAsync(request), WebResponse(response), failMsg);

		StringView dataFile;
		bool shouldRelease = false;
		switch (state) {
			case WebRequest::State::Idle:
				// Unreachable state
				break;

			case WebRequest::State::Active:
				break;

			case WebRequest::State::Unauthorized:
				shouldRelease = true;
				break;

			case WebRequest::State::Completed:
				if (_storage == WebRequest::Storage::File) {
					dataFile = response->GetDataFile();
					e.SetDataFile(dataFile);
				}
				DEATH_FALLTHROUGH

			case WebRequest::State::Failed:
			case WebRequest::State::Cancelled:
				if (response) {
					response->Finalize();
				}
				shouldRelease = true;
				break;
		}

		if (_callback) {
			_callback(e);
		}

		if (!dataFile.empty() && FileSystem::FileExists(dataFile)) {
			FileSystem::RemoveFile(dataFile);
		}

		if (shouldRelease) {
			Release();
		}
	}

	WebRequestBase::WebRequestBase() = default;

	WebRequestBase::WebRequestBase(const WebRequestImplPtr& impl)
		: _impl(impl) {}

	WebRequestBase::WebRequestBase(const WebRequestBase&) = default;

	WebRequestBase& WebRequestBase::operator=(const WebRequestBase&) = default;

	WebRequestBase::~WebRequestBase() = default;

	void WebRequestBase::SetHeader(StringView name, StringView value)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetHeader(name, value);
	}

	void WebRequestBase::SetMethod(StringView method)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetMethod(method);
	}

	void WebRequestBase::SetData(StringView text, StringView contentType)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetData(text, contentType);
	}

	bool WebRequestBase::SetData(std::unique_ptr<Stream> dataStream, StringView contentType, std::int64_t dataSize)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", false);

		return _impl->SetData(Death::move(dataStream), contentType, dataSize);
	}

	WebRequestBase::Storage WebRequestBase::GetStorage() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", Storage::None);

		return _impl->GetStorage();
	}

	void WebRequestBase::SetStorage(Storage storage)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetStorage(storage);
	}

	WebRequest::Result WebRequest::Execute() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", Result::Error("Invalid session object"_s));

		return _impl->Execute();
	}

	void WebRequestAsync::Run()
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );
		DEATH_ASSERT(_impl->GetState() == State::Idle, "Completed requests cannot be restarted", );

		_impl->Run();
	}

	void WebRequestAsync::Cancel()
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );
		DEATH_ASSERT(_impl->GetState() == State::Idle, "Not yet started requests cannot be cancelled", );

		_impl->Cancel();
	}

	WebResponse WebRequestBase::GetResponse() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return WebResponse(_impl->GetResponse());
	}

	WebAuthChallenge WebRequestAsync::GetAuthChallenge() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return WebAuthChallenge(_impl->GetAuthChallenge());
	}

	std::int32_t WebRequestAsync::GetId() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", -1);

		return _impl->GetId();
	}

	WebSessionAsync& WebRequestAsync::GetSession() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", WebSessionAsync::GetDefault());

		return _impl->GetSession();
	}

	WebRequest::State WebRequestAsync::GetState() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", State::Failed);

		return _impl->GetState();
	}

	std::int64_t WebRequestBase::GetBytesSent() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", -1);

		return _impl->GetBytesSent();
	}

	std::int64_t WebRequestBase::GetBytesExpectedToSend() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", -1);

		return _impl->GetBytesExpectedToSend();
	}

	std::int64_t WebRequestBase::GetBytesReceived() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", -1);

		return _impl->GetBytesReceived();
	}

	std::int64_t WebRequestBase::GetBytesExpectedToReceive() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", -1);

		return _impl->GetBytesExpectedToReceive();
	}

	WebRequestHandle WebRequestBase::GetNativeHandle() const noexcept
	{
		return (_impl ? _impl->GetNativeHandle() : nullptr);
	}

	void WebRequestBase::MakeInsecure(Ignore flags)
	{
		_impl->MakeInsecure(flags);
	}

	WebRequestBase::Ignore WebRequestBase::GetSecurityFlags() const
	{
		return _impl->GetSecurityFlags();
	}

	WebResponseImpl::WebResponseImpl(WebRequestImpl& request)
		: _request(request) {}

	WebResponseImpl::~WebResponseImpl()
	{
		auto path = _file->GetPath();
		if (FileSystem::FileExists(path)) {
			FileSystem::RemoveFile(path);
		}
	}

	WebRequest::Result WebResponseImpl::InitializeFileStorage()
	{
		if (_request.GetStorage() == WebRequest::Storage::File) {
			String tempDir = _request.GetSessionImpl().GetTempDirectory();
			std::uint64_t hash = Environment::QueryUnbiasedInterruptTime() * 11400714819323198485ULL;
			for (std::int32_t i = 0; i < 10; i++) {
				char fileName[64];
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
				::sprintf_s(fileName, "Dx-%08x-%08x", std::uint32_t((hash >> 32) & 0xffffffff), std::uint32_t(hash & 0xffffffff));
#else
				::snprintf(fileName, sizeof(fileName), "Dx-%08x-%08x", std::uint32_t((hash >> 32) & 0xffffffff), std::uint32_t(hash & 0xffffffff));
#endif
				_file = std::make_unique<FileStream>(FileSystem::CombinePath(tempDir, fileName), FileAccess::Write);
				if (_file->IsValid()) {
					break;
				}

				_file = nullptr;
				hash ^= hash >> 33;
				hash *= 0xFF51AFD7ED558CCDULL;
			}

			if (_file == nullptr || _file->IsValid()) {
				return WebRequest::Result::Error("Failed to create temporary file");
			}
		}

		return WebRequest::Result::Ok();
	}

	String WebResponseImpl::GetMimeType() const
	{
		StringView contentType = GetContentType();
		return contentType.prefix(contentType.findOr(';', contentType.end()).begin());
	}

	String WebResponseImpl::GetContentType() const
	{
		return GetHeader("Content-Type"_s);
	}

	Stream* WebResponseImpl::GetStream() const
	{
		if (_stream == nullptr) {
			switch (_request.GetStorage()) {
				case WebRequest::Storage::Memory: {
					_stream = std::make_unique<MemoryStream>(_readBuffer.data(), _readBuffer.size());
					break;
				}
				case WebRequest::Storage::File: {
					_stream = std::make_unique<FileStream>(_file->GetPath(), FileAccess::Read);
					break;
				}
				case WebRequest::Storage::None: {
					// Stream is disabled
					break;
				}
			}
		}

		return _stream.get();
	}

	String WebResponseImpl::GetSuggestedFileName() const
	{
		String suggestedFilename;

		// Try to determine filename from "Content-Disposition" header
		String contentDisp = GetHeader("Content-Disposition"_s);
		WebRequestHeaderMap params;
		String disp = SplitParameters(contentDisp, params);
		if (disp == "attachment"_s) {
			suggestedFilename = params["filename"_s];
		}

		if (suggestedFilename.empty()) {
			String uri = GetURL();
			StringView fileName = uri.trimmedSuffix("/"_s);
			suggestedFilename = fileName.suffix(fileName.findLastOr('/', fileName.begin()).end());
			suggestedFilename = suggestedFilename.prefix(suggestedFilename.findOr('?', suggestedFilename.end()).begin());
		}

		return suggestedFilename;
	}

	String WebResponseImpl::AsString() const
	{
		if (_request.GetStorage() == WebRequest::Storage::Memory) {
			return arrayView(_readBuffer);
		}

		return {};
	}

	void WebResponseImpl::ReportDataReceived(std::size_t sizeReceived)
	{
		_readBuffer.resize_for_overwrite(_readBuffer.size() + sizeReceived);
		_request.ReportDataReceived(sizeReceived);

		switch (_request.GetStorage()) {
			case WebRequest::Storage::Memory: {
				// The destination buffer is used directly
				break;
			}
			case WebRequest::Storage::File: {
				_file->Write(_readBuffer.data(), std::int64_t(_readBuffer.size()));
				_readBuffer.clear();
				break;
			}
			case WebRequest::Storage::None: {
				if (!_request.IsAsync()) {
					break;
				}

				_request.AddRef();
				const WebRequestImplPtr request(&_request);

				AddRef();
				const WebResponseImplPtr response(this);

				if (_request._callback) {
					WebRequestEvent evt(_request.GetId(), WebRequest::State::Active, WebRequestAsync(request), WebResponse(response));
					evt.SetDataBuffer(_readBuffer);
					_request._callback(evt);
				}

				_readBuffer.clear();
				break;
			}
		}
	}

	void* WebResponseImpl::GetDataBuffer(std::size_t sizeNeeded)
	{
		std::size_t prevSize = _readBuffer.size();
		_readBuffer.reserve(prevSize + sizeNeeded);
		return _readBuffer.data() + prevSize;
	}

	void WebResponseImpl::PreAllocateBuffer(std::size_t sizeNeeded)
	{
		_readBuffer.reserve(_readBuffer.size() + sizeNeeded);
	}

	String WebResponseImpl::GetDataFile() const
	{
		return _file->GetPath();
	}

	void WebResponseImpl::Finalize()
	{
		if (_request.GetStorage() == WebRequest::Storage::File) {
			_file->Dispose();
		}
	}

	WebResponse::WebResponse() = default;

	WebResponse::WebResponse(const WebResponseImplPtr& impl)
		: _impl(impl) {}

	WebResponse::WebResponse(const WebResponse&) = default;

	WebResponse& WebResponse::operator=(const WebResponse&) = default;

	WebResponse::~WebResponse() = default;

	std::int64_t WebResponse::GetContentLength() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", -1);

		return _impl->GetContentLength();
	}

	String WebResponse::GetURL() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetURL();
	}

	String WebResponse::GetHeader(StringView name) const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetHeader(name);
	}

	Array<String> WebResponse::GetAllHeaderValues(StringView name) const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetAllHeaderValues(name);
	}

	String WebResponse::GetMimeType() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetMimeType();
	}

	String WebResponse::GetContentType() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetContentType();
	}

	int WebResponse::GetStatus() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", -1);

		return _impl->GetStatus();
	}

	String WebResponse::GetStatusText() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetStatusText();
	}

	Stream* WebResponse::GetStream() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", nullptr);

		return _impl->GetStream();
	}

	String WebResponse::GetSuggestedFileName() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetSuggestedFileName();
	}

	String WebResponse::AsString() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->AsString();
	}

	String WebResponse::GetDataFile() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetDataFile();
	}

	WebProxy::WebProxy(Type type, Containers::StringView url)
		: _type(type), _url(url) {}

	WebProxy::Type WebProxy::GetType() const
	{
		return _type;
	}

	Containers::StringView WebProxy::GetURL() const
	{
		DEATH_DEBUG_ASSERT(_type == Type::URL);
		return _url;
	}

	namespace
	{
		std::unique_ptr<WebSessionFactory> _factory;
		WebSession _defaultSession;
		WebSessionAsync _defaultSessionAsync;
	}

#if defined(DEATH_TARGET_WINDOWS)

	class WebSessionWinHTTP;
	class WebRequestWinHTTP;

	class WebResponseWinHTTP : public WebResponseImpl
	{
	public:
		WebResponseWinHTTP(WebRequestWinHTTP& request);

		std::int64_t GetContentLength() const override {
			return _contentLength;
		}

		String GetURL() const override;

		String GetHeader(StringView name) const override;

		Array<String> GetAllHeaderValues(StringView name) const override;

		std::int32_t GetStatus() const override;

		String GetStatusText() const override;

		bool ReadData(DWORD* bytesRead = nullptr);

	private:
		HINTERNET _requestHandle;
		std::int64_t _contentLength;
	};

	class WebAuthChallengeWinHTTP : public WebAuthChallengeImpl
	{
	public:
		WebAuthChallengeWinHTTP(WebAuthChallenge::Source source, WebRequestWinHTTP& request);

		WebAuthChallengeWinHTTP(const WebAuthChallengeWinHTTP&) = delete;
		WebAuthChallengeWinHTTP& operator=(const WebAuthChallengeWinHTTP&) = delete;

		bool Initialize();

		WebRequest::Result DoSetCredentials(const WebCredentials& cred);

		void SetCredentials(const WebCredentials& cred) override;

	private:
		WebRequestWinHTTP& _request;
		DWORD _target;
		DWORD _selectedScheme;
	};

	class WebRequestWinHTTP : public WebRequestImpl
	{
		friend class WebAuthChallengeWinHTTP;

	public:
		WebRequestWinHTTP(WebSessionAsync& session, WebSessionWinHTTP& sessionImpl, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id);
		WebRequestWinHTTP(WebSessionWinHTTP& sessionImpl, StringView url);

		~WebRequestWinHTTP();

		WebRequest::Result Execute() override;

		void Run() override;

		WebResponseImplPtr GetResponse() const override {
			return _response;
		}

		WebAuthChallengeImplPtr GetAuthChallenge() const override {
			return _authChallenge;
		}

		std::int64_t GetBytesSent() const override {
			return _dataWritten;
		}

		std::int64_t GetBytesExpectedToSend() const override {
			return _dataSize;
		}

		void HandleCallback(DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength);

		HINTERNET GetHandle() const noexcept {
			return _request;
		}

		WebRequestHandle GetNativeHandle() const noexcept override {
			return (WebRequestHandle)GetHandle();
		}

	private:
		WebSessionWinHTTP& _sessionImpl;
		String _url;
		HINTERNET _connect;
		HINTERNET _request;
		ComPtr<WebResponseWinHTTP> _response;
		ComPtr<WebAuthChallengeWinHTTP> _authChallenge;
		SmallVector<char, 0> _dataWriteBuffer;
		std::int64_t _dataWritten;
		WebCredentials _credentialsFromURL;
		bool _tryCredentialsFromURL;
		bool _tryProxyCredentials;

		void DoCancel() override;

		Result DoPrepareRequest();

		Result DoWriteData(DWORD* bytesWritten = nullptr);

		Result SendRequest();

		void WriteData();

		Result CreateResponse();

		Result InitializeAuthIfNeeded();

		Result Fail(StringView operation, DWORD errorCode);

		Result FailWithLastError(StringView operation) {
			return Fail(operation, ::GetLastError());
		}

		void SetFailed(StringView operation, DWORD errorCode) {
			return HandleResult(Fail(operation, errorCode));
		}

		void SetFailedWithLastError(StringView operation) {
			return HandleResult(FailWithLastError(operation));
		}
	};

	class WebSessionWinHTTP : public WebSessionImpl
	{
	public:
		explicit WebSessionWinHTTP(Mode mode);

		~WebSessionWinHTTP();

		static bool Initialize();

		WebRequestImplPtr CreateRequest(WebSession& session, StringView url) override;
		WebRequestImplPtr CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id) override;

		bool SetProxy(const WebProxy& proxy) override;

		HINTERNET GetHandle() const noexcept {
			return _handle;
		}

		WebSessionHandle GetNativeHandle() const noexcept override {
			return (WebSessionHandle)GetHandle();
		}

		bool HasProxyCredentials() const {
			return !_proxyCredentials.GetUser().empty();
		}

		const WebCredentials& GetProxyCredentials() const {
			return _proxyCredentials;
		}

	private:
		HINTERNET _handle;
		WebCredentials _proxyCredentials;
		String _proxyURLWithoutCredentials;

		bool Open();
	};

	class WebSessionFactoryWinHTTP : public WebSessionFactory
	{
	public:
		WebSessionImpl* Create() override {
			return new WebSessionWinHTTP(WebSessionImpl::Mode::Sync);
		}

		WebSessionImpl* CreateAsync() override {
			return new WebSessionWinHTTP(WebSessionImpl::Mode::Async);
		}

		bool Initialize() override {
			return WebSessionWinHTTP::Initialize();
		}
	};

#elif defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_UNIX)

	class WebAuthChallengeCURL;
	class WebRequestCURL;
	class WebSessionCURL;
	class WebSessionAsyncCURL;

	class WebResponseCURL : public WebResponseImpl
	{
	public:
		explicit WebResponseCURL(WebRequestCURL& request);

		WebResponseCURL(const WebResponseCURL&) = delete;
		WebResponseCURL& operator=(const WebResponseCURL&) = delete;

		std::int64_t GetContentLength() const override;

		String GetURL() const override;

		String GetHeader(StringView name) const override;

		Array<String> GetAllHeaderValues(StringView name) const override;

		std::int32_t GetStatus() const override;

		String GetStatusText() const override {
			return _statusText;
		}

		// Methods called from libcurl callbacks
		std::size_t CURLOnWrite(void* buffer, std::size_t size);
		std::size_t CURLOnHeader(const char* buffer, std::size_t size);
		std::int32_t CURLOnProgress(curl_off_t);

	private:
		using AllHeadersMap = std::map<String, SmallVector<String, 1>>;
		AllHeadersMap _headers;
		String _statusText;
		std::int64_t _knownDownloadSize;

		CURL* GetHandle() const;
	};

	class WebRequestCURL : public WebRequestImpl
	{
	public:
		WebRequestCURL(WebSessionCURL& sessionImpl, StringView url);
		WebRequestCURL(WebSessionAsync& session, WebSessionAsyncCURL& sessionImpl, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id);

		~WebRequestCURL() override;

		WebRequestCURL(const WebRequestCURL&) = delete;
		WebRequestCURL& operator=(const WebRequestCURL&) = delete;

		WebRequest::Result Execute() override;

		void Run() override;

		WebResponseImplPtr GetResponse() const override;

		WebAuthChallengeImplPtr GetAuthChallenge() const override ;

		std::int64_t GetBytesSent() const override;

		std::int64_t GetBytesExpectedToSend() const override;

		CURL* GetHandle() const noexcept {
			return _handle;
		}

		WebRequestHandle GetNativeHandle() const noexcept override {
			return (WebRequestHandle)GetHandle();
		}

		bool StartRequest();

		void HandleCompletion();

		String GetError() const;

		// Method called from libcurl callback
		std::size_t CURLOnRead(char* buffer, std::size_t size);

	private:
		// This is only used for async requests
		WebSessionAsyncCURL* const _sessionCURL;

		// This pointer is only owned by this object when using async requests
		CURL* const _handle;
		char _errorBuffer[CURL_ERROR_SIZE];
		struct curl_slist* _headerList = nullptr;
		ComPtr<WebResponseCURL> _response;
		ComPtr<WebAuthChallengeCURL> _authChallenge;
		std::int64_t _bytesSent;

		void DoStartPrepare(StringView url);
		WebRequest::Result DoFinishPrepare();
		WebRequest::Result DoHandleCompletion();
		void DoCancel() override;
	};

	class WebSessionBaseCURL : public WebSessionImpl
	{
	public:
		explicit WebSessionBaseCURL(Mode mode);
		~WebSessionBaseCURL() override;

		static bool CurlRuntimeAtLeastVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch);

	protected:
		static std::int32_t _activeSessions;
		static std::uint32_t _runtimeVersion;
	};

	class WebSessionCURL : public WebSessionBaseCURL
	{
	public:
		WebSessionCURL();
		~WebSessionCURL() override;

		WebSessionCURL(const WebSessionCURL&) = delete;
		WebSessionCURL& operator=(const WebSessionCURL&) = delete;

		WebRequestImplPtr CreateRequest(WebSession& session, StringView url) override;
		WebRequestImplPtr CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id) override;

		WebSessionHandle GetNativeHandle() const noexcept override {
			return (WebSessionHandle)_handle;
		}

		CURL* GetHandle() const {
			return _handle;
		}

	private:
		CURL* _handle;
	};

	class WebSessionAsyncCURL : public WebSessionBaseCURL
	{
	public:
		WebSessionAsyncCURL();
		~WebSessionAsyncCURL() override;

		WebSessionAsyncCURL(const WebSessionAsyncCURL&) = delete;
		WebSessionAsyncCURL& operator=(const WebSessionAsyncCURL&) = delete;

		WebRequestImplPtr CreateRequest(WebSession& session, StringView url) override;
		WebRequestImplPtr CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id = -1) override;

		WebSessionHandle GetNativeHandle() const noexcept override {
			return (WebSessionHandle)_handle;
		}

		bool StartRequest(WebRequestCURL& request);
		void CancelRequest(WebRequestCURL* request);
		void RequestHasTerminated(WebRequestCURL* request);

	private:
		using TransferSet = std::unordered_map<CURL*, WebRequestCURL*>;
		using CurlSocketMap = std::unordered_map<CURL*, curl_socket_t>;

		TransferSet _activeTransfers;
		CurlSocketMap _activeSockets;
		CURLM* _handle;
		std::thread _workerThread;
		std::atomic_bool _workerThreadRunning;

		static int SocketCallback(CURL*, curl_socket_t, int, void*, void*);

		void ProcessSocketCallback(CURL*, curl_socket_t, int);
		void CheckForCompletedTransfers();
		void StopActiveTransfer(CURL*);
		void RemoveActiveSocket(CURL*);

		static void OnWorkerThread(WebSessionAsyncCURL* _this);
	};

	class WebSessionFactoryCURL : public WebSessionFactory
	{
	public:
		WebSessionImpl* Create() override {
			return new WebSessionCURL();
		}

		WebSessionImpl* CreateAsync() override {
			return new WebSessionAsyncCURL();
		}
	};

#endif

	WebSessionImpl::WebSessionImpl(Mode mode)
		: _mode(mode)
	{
		AddCommonHeader("User-Agent"_s, "Death::IO::WebRequest"_s);
	}

	WebSessionImpl::~WebSessionImpl() = default;

	String WebSessionImpl::GetTempDirectory() const
	{
		if (_tempDir.empty()) {
			return FileSystem::GetTempDirectory();
		}

		return _tempDir;
	}

	WebSessionBase::WebSessionBase() = default;

	WebSessionBase::WebSessionBase(const WebSessionImplPtr& impl)
		: _impl(impl) {}

	WebSessionBase::WebSessionBase(const WebSessionBase&) = default;

	WebSessionBase& WebSessionBase::operator=(const WebSessionBase&) = default;

	WebSessionBase::~WebSessionBase() = default;

	WebSession& WebSession::GetDefault()
	{
		if (!_defaultSession.IsOpened()) {
			_defaultSession = WebSession::New();
		}
		return _defaultSession;
	}

	WebSessionAsync& WebSessionAsync::GetDefault()
	{
		if (!_defaultSessionAsync.IsOpened()) {
			_defaultSessionAsync = WebSessionAsync::New();
		}
		return _defaultSessionAsync;
	}

	WebSessionFactory* WebSessionBase::FindFactory()
	{
		if (_factory == nullptr) {
#if defined(DEATH_TARGET_WINDOWS)
			std::unique_ptr<WebSessionFactory> factory = std::make_unique<WebSessionFactoryWinHTTP>();
			if (!factory->Initialize()) {
				return nullptr;
			}
			_factory = Death::move(factory);
#elif defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_UNIX)
			std::unique_ptr<WebSessionFactory> factory = std::make_unique<WebSessionFactoryCURL>();
			if (!factory->Initialize()) {
				return nullptr;
			}
			_factory = Death::move(factory);
#else
#	pragma message("Unsupported platform for Death::IO::WebRequest")
#endif
		}

		return _factory.get();
	}

	WebSession WebSession::New()
	{
		WebSessionImplPtr impl;

		WebSessionFactory* factory = FindFactory();
		if (factory != nullptr) {
			impl = factory->Create();
		}

		return WebSession(impl);
	}

	WebSessionAsync WebSessionAsync::New()
	{
		WebSessionImplPtr impl;

		WebSessionFactory* factory = FindFactory();
		if (factory != nullptr) {
			impl = factory->CreateAsync();
		}

		return WebSessionAsync(impl);
	}

	bool WebSessionBase::IsBackendAvailable()
	{
		WebSessionFactory* factory = FindFactory();
		return (factory != nullptr);
	}

	WebRequest WebSession::CreateRequest(StringView url)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return WebRequest(_impl->CreateRequest(*this, url));
	}

	WebRequestAsync WebSessionAsync::CreateRequest(StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return WebRequestAsync(_impl->CreateRequestAsync(*this, url, Death::move(callback), id));
	}

	void WebSessionBase::AddCommonHeader(StringView name, StringView value)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->AddCommonHeader(name, value);
	}

	String WebSessionBase::GetTempDirectory() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", {});

		return _impl->GetTempDirectory();
	}

	void WebSessionBase::SetTempDirectory(StringView path)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetTempDirectory(path);
	}

	bool WebSessionBase::SetProxy(const WebProxy& proxy)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", false);

		return _impl->SetProxy(proxy);
	}

	bool WebSessionBase::IsOpened() const
	{
		return _impl.get() != nullptr;
	}

	void WebSessionBase::Dispose()
	{
		_impl.reset(nullptr);
	}

	WebSessionHandle WebSessionBase::GetNativeHandle() const noexcept
	{
		return (_impl ? _impl->GetNativeHandle() : nullptr);
	}

	bool WebSessionBase::EnablePersistentStorage(bool enable)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", false);

		return _impl->EnablePersistentStorage(enable);
	}

	WebRequestEvent::WebRequestEvent(std::int32_t id, WebRequest::State state, const WebRequestAsync& request,
		const WebResponse& response, Containers::StringView errorDesc)
		: _id(id), _state(state), _request(request), _response(response), _errorDescription(errorDesc) {}

	WebRequestAsync::State WebRequestEvent::GetState() const
	{
		return _state;
	}

	const WebRequestAsync& WebRequestEvent::GetRequest() const
	{
		return _request;
	}

	const WebResponse& WebRequestEvent::GetResponse() const
	{
		return _response;
	}

	Containers::StringView WebRequestEvent::GetErrorDescription() const
	{
		return _errorDescription;
	}

	Containers::StringView WebRequestEvent::GetDataFile() const
	{
		return _dataFile;
	}

	void WebRequestEvent::SetDataFile(Containers::StringView dataFile)
	{
		_dataFile = dataFile;
	}

	Containers::ArrayView<char> WebRequestEvent::GetDataBuffer() const
	{
		return _dataBuffer;
	}

	void WebRequestEvent::SetDataBuffer(Containers::ArrayView<char> dataBuffer)
	{
		_dataBuffer = dataBuffer;
	}

#if defined(DEATH_TARGET_WINDOWS)

	constexpr std::int32_t WebRequestBufferSize = 64 * 1024;

	class WinHTTP
	{
	public:
		static bool LoadLibrary() {
			_winhttp = ::LoadLibrary(L"winhttp.dll");
			if (_winhttp == NULL) {
				return false;
			}

			bool result = true;

			#define LoadFunction(name)									\
				name = (name ## _t)::GetProcAddress(_winhttp, #name);	\
				result &= (name != nullptr);

			LoadFunction(WinHttpQueryOption)
			LoadFunction(WinHttpQueryHeaders)
			LoadFunction(WinHttpSetOption)
			LoadFunction(WinHttpWriteData)
			LoadFunction(WinHttpCloseHandle)
			LoadFunction(WinHttpReceiveResponse)
			LoadFunction(WinHttpCrackUrl)
			LoadFunction(WinHttpCreateUrl)
			LoadFunction(WinHttpConnect)
			LoadFunction(WinHttpOpenRequest)
			LoadFunction(WinHttpSetStatusCallback)
			LoadFunction(WinHttpSendRequest)
			LoadFunction(WinHttpReadData)
			LoadFunction(WinHttpQueryAuthSchemes)
			LoadFunction(WinHttpSetCredentials)
			LoadFunction(WinHttpOpen)

			#undef LoadFunction

			if (!result) {
				::FreeLibrary(_winhttp);
			}

			return result;
		}

		typedef BOOL(WINAPI* WinHttpQueryOption_t)(HINTERNET, DWORD, LPVOID, LPDWORD);
		static WinHttpQueryOption_t WinHttpQueryOption;
		typedef BOOL(WINAPI* WinHttpQueryHeaders_t)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
		static WinHttpQueryHeaders_t WinHttpQueryHeaders;
		typedef BOOL(WINAPI* WinHttpSetOption_t)(HINTERNET, DWORD, LPVOID, DWORD);
		static WinHttpSetOption_t WinHttpSetOption;
		typedef BOOL(WINAPI* WinHttpWriteData_t)(HINTERNET, LPCVOID, DWORD, LPDWORD);
		static WinHttpWriteData_t WinHttpWriteData;
		typedef BOOL(WINAPI* WinHttpCloseHandle_t)(HINTERNET);
		static WinHttpCloseHandle_t WinHttpCloseHandle;
		typedef BOOL(WINAPI* WinHttpReceiveResponse_t)(HINTERNET, LPVOID);
		static WinHttpReceiveResponse_t WinHttpReceiveResponse;
		typedef BOOL(WINAPI* WinHttpCrackUrl_t)(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTS);
		static WinHttpCrackUrl_t WinHttpCrackUrl;
		typedef BOOL(WINAPI* WinHttpCreateUrl_t)(LPURL_COMPONENTS, DWORD, LPWSTR, LPDWORD);
		static WinHttpCreateUrl_t WinHttpCreateUrl;
		typedef HINTERNET(WINAPI* WinHttpConnect_t)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
		static WinHttpConnect_t WinHttpConnect;
		typedef HINTERNET(WINAPI* WinHttpOpenRequest_t)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
		static WinHttpOpenRequest_t WinHttpOpenRequest;
		typedef WINHTTP_STATUS_CALLBACK(WINAPI* WinHttpSetStatusCallback_t)(HINTERNET, WINHTTP_STATUS_CALLBACK, DWORD, DWORD_PTR);
		static WinHttpSetStatusCallback_t WinHttpSetStatusCallback;
		typedef BOOL(WINAPI* WinHttpSendRequest_t)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
		static WinHttpSendRequest_t WinHttpSendRequest;
		typedef BOOL(WINAPI* WinHttpReadData_t)(HINTERNET, LPVOID, DWORD, LPDWORD);
		static WinHttpReadData_t WinHttpReadData;
		typedef BOOL(WINAPI* WinHttpQueryAuthSchemes_t)(HINTERNET, LPDWORD, LPDWORD, LPDWORD);
		static WinHttpQueryAuthSchemes_t WinHttpQueryAuthSchemes;
		typedef BOOL(WINAPI* WinHttpSetCredentials_t)(HINTERNET, DWORD, DWORD, LPCWSTR, LPCWSTR, LPVOID);
		static WinHttpSetCredentials_t WinHttpSetCredentials;
		typedef HINTERNET(WINAPI* WinHttpOpen_t)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
		static WinHttpOpen_t WinHttpOpen;

	private:
		static HMODULE _winhttp;
	};

	HMODULE WinHTTP::_winhttp;
	WinHTTP::WinHttpQueryOption_t WinHTTP::WinHttpQueryOption;
	WinHTTP::WinHttpQueryHeaders_t WinHTTP::WinHttpQueryHeaders;
	WinHTTP::WinHttpSetOption_t WinHTTP::WinHttpSetOption;
	WinHTTP::WinHttpWriteData_t WinHTTP::WinHttpWriteData;
	WinHTTP::WinHttpCloseHandle_t WinHTTP::WinHttpCloseHandle;
	WinHTTP::WinHttpReceiveResponse_t WinHTTP::WinHttpReceiveResponse;
	WinHTTP::WinHttpCrackUrl_t WinHTTP::WinHttpCrackUrl;
	WinHTTP::WinHttpCreateUrl_t WinHTTP::WinHttpCreateUrl;
	WinHTTP::WinHttpConnect_t WinHTTP::WinHttpConnect;
	WinHTTP::WinHttpOpenRequest_t WinHTTP::WinHttpOpenRequest;
	WinHTTP::WinHttpSetStatusCallback_t WinHTTP::WinHttpSetStatusCallback;
	WinHTTP::WinHttpSendRequest_t WinHTTP::WinHttpSendRequest;
	WinHTTP::WinHttpReadData_t WinHTTP::WinHttpReadData;
	WinHTTP::WinHttpQueryAuthSchemes_t WinHTTP::WinHttpQueryAuthSchemes;
	WinHTTP::WinHttpSetCredentials_t WinHTTP::WinHttpSetCredentials;
	WinHTTP::WinHttpOpen_t WinHTTP::WinHttpOpen;

	// Define potentially missing constants
	#if !defined(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY)
	#	define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
	#endif
	#if !defined(WINHTTP_PROTOCOL_FLAG_HTTP2)
	#	define WINHTTP_PROTOCOL_FLAG_HTTP2 0x1
	#endif
	#if !defined(WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL)
	#	define WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL 133
	#endif
	#if !defined(WINHTTP_DECOMPRESSION_FLAG_ALL)
	#	define WINHTTP_DECOMPRESSION_FLAG_GZIP 0x00000001
	#	define WINHTTP_DECOMPRESSION_FLAG_DEFLATE 0x00000002
	#	define WINHTTP_DECOMPRESSION_FLAG_ALL (			\
				WINHTTP_DECOMPRESSION_FLAG_GZIP |		\
				WINHTTP_DECOMPRESSION_FLAG_DEFLATE)
	#endif
	#if !defined(WINHTTP_OPTION_DECOMPRESSION)
	#	define WINHTTP_OPTION_DECOMPRESSION 118
	#endif
	#if !defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1)
	#	define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
	#endif
	#if !defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2)
	#	define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
	#endif

	namespace
	{
		struct URLComponents : URL_COMPONENTS
		{
			URLComponents() {
				std::memset(this, 0, sizeof(*this));
				dwStructSize = sizeof(URL_COMPONENTS);
				dwSchemeLength =
					dwHostNameLength =
					dwUserNameLength =
					dwPasswordLength =
					dwUrlPathLength =
					dwExtraInfoLength = DWORD(-1);
			}

			bool HasCredentials() const {
				return (dwUserNameLength > 0);
			}

			WebCredentials GetCredentials() const {
				return WebCredentials(Utf8::FromUtf16(lpszUserName, dwUserNameLength),
					Utf8::FromUtf16(lpszPassword, dwPasswordLength));
			}
		};

		Array<String> WinHTTPQueryAllHeaderStrings(HINTERNET hRequest, DWORD dwInfoLevel, LPCWSTR pwszName = WINHTTP_HEADER_NAME_BY_INDEX)
		{
			Array<String> result;
			DWORD nextIndex = 0;
			do {
				DWORD bufferLen = 0;
				DWORD currentIndex = nextIndex;
				WinHTTP::WinHttpQueryHeaders(hRequest, dwInfoLevel, pwszName, nullptr, &bufferLen, &nextIndex);
				if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					if (bufferLen == 0 || (bufferLen % sizeof(wchar_t)) != 0) {
						return {};
					}

					SmallVector<wchar_t, 1000> resBuf(bufferLen / sizeof(wchar_t));
					if (WinHTTP::WinHttpQueryHeaders(hRequest, dwInfoLevel, pwszName, resBuf.data(), &bufferLen, &nextIndex)) {
						arrayAppend(result, Utf8::FromUtf16(resBuf.data(), bufferLen));
					}
				}

				if (nextIndex <= currentIndex) {
					break;
				}
			} while (::GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND);

			return result;
		}

		String WinHTTPQueryHeaderString(HINTERNET hRequest, DWORD dwInfoLevel, LPCWSTR pwszName = WINHTTP_HEADER_NAME_BY_INDEX)
		{
			auto result = WinHTTPQueryAllHeaderStrings(hRequest, dwInfoLevel, pwszName);
			return (result.empty() ? String{} : result.back());
		}

		String WinHTTPQueryOptionString(HINTERNET hInternet, DWORD dwOption)
		{
			String result;
			DWORD bufferLength = 0;
			WinHTTP::WinHttpQueryOption(hInternet, dwOption, nullptr, &bufferLength);
			if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				if (bufferLength == 0 || (bufferLength % sizeof(wchar_t)) != 0) {
					return {};
				}

				SmallVector<wchar_t, 1000> buffer(bufferLength / sizeof(wchar_t));
				if (WinHTTP::WinHttpQueryOption(hInternet, dwOption, buffer.data(), &bufferLength)) {
					result = Utf8::FromUtf16(buffer.data(), bufferLength);
				}
			}

			return result;
		}

		inline void WinHTTPSetOption(HINTERNET hInternet, DWORD dwOption, DWORD dwValue)
		{
			WinHTTP::WinHttpSetOption(hInternet, dwOption, &dwValue, sizeof(dwValue));
		}

		void CALLBACK RequestStatusCallback(HINTERNET hInternet, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength)
		{
			if (dwContext != NULL) {
				WebRequestWinHTTP* request = reinterpret_cast<WebRequestWinHTTP*>(dwContext);
				request->HandleCallback(dwInternetStatus, lpvStatusInformation, dwStatusInformationLength);
			}
		}
	}

	WebRequestWinHTTP::WebRequestWinHTTP(WebSessionAsync& session, WebSessionWinHTTP& sessionImpl, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id)
		: WebRequestImpl(session, sessionImpl, Death::move(callback), id), _sessionImpl(sessionImpl), _url(url),
			_connect(NULL), _request(NULL), _dataWritten(0), _tryCredentialsFromURL(false),
			_tryProxyCredentials(sessionImpl.HasProxyCredentials())
	{
	}

	WebRequestWinHTTP::WebRequestWinHTTP(WebSessionWinHTTP& sessionImpl, StringView url)
		: WebRequestImpl(sessionImpl), _sessionImpl(sessionImpl), _url(url), _connect(NULL), _request(NULL),
			_dataWritten(0), _tryCredentialsFromURL(false), _tryProxyCredentials(sessionImpl.HasProxyCredentials())
	{
	}

	WebRequestWinHTTP::~WebRequestWinHTTP()
	{
		if (_request != NULL) {
			WinHTTP::WinHttpCloseHandle(_request);
		}
		if (_connect != NULL) {
			WinHTTP::WinHttpCloseHandle(_connect);
		}
	}

	void WebRequestWinHTTP::HandleCallback(DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength)
	{
		switch (dwInternetStatus) {
			case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE: {
				WriteData();
				break;
			}
			case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
				if (dwStatusInformationLength > 0) {
					_response->ReportDataReceived(dwStatusInformationLength);
					if (!_response->ReadData() && !WasCancelled()) {
						SetFailedWithLastError("Reading data"_s);
					}
				} else {
					SetFinalStateFromStatus();
				}
				break;
			}
			case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE: {
				DWORD bytesWritten = *(reinterpret_cast<LPDWORD>(lpvStatusInformation));
				_dataWritten += bytesWritten;
				WriteData();
				break;
			}
			case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
				LPWINHTTP_ASYNC_RESULT asyncResult = reinterpret_cast<LPWINHTTP_ASYNC_RESULT>(lpvStatusInformation);
				if (asyncResult->dwError == ERROR_WINHTTP_OPERATION_CANCELLED && WasCancelled()) {
					SetState(WebRequest::State::Cancelled);
				} else {
					SetFailed("Async request"_s, asyncResult->dwError);
				}
				break;
			}
		}
	}

	void WebRequestWinHTTP::WriteData()
	{
		if (_dataWritten == _dataSize) {
			if (!CheckResult(CreateResponse())) {
				return;
			}

			const Result result = InitializeAuthIfNeeded();
			switch (result.state) {
				case WebRequest::State::Unauthorized:
					switch (_authChallenge->GetSource()) {
						case WebAuthChallenge::Source::Proxy: {
							if (_tryProxyCredentials) {
								_tryProxyCredentials = false;

								_authChallenge->SetCredentials(_sessionImpl.GetProxyCredentials());
								return;
							}
							break;
						}
						case WebAuthChallenge::Source::Server: {
							if (_tryCredentialsFromURL) {
								_tryCredentialsFromURL = false;

								// If the request is redirected, the authentication can be required again
								if (_sessionImpl.HasProxyCredentials()) {
									_tryProxyCredentials = true;
								}

								_authChallenge->SetCredentials(_credentialsFromURL);
								return;
							}
							break;
						}
					}
					DEATH_FALLTHROUGH

				case WebRequest::State::Failed:
					HandleResult(result);
					return;

				case WebRequest::State::Active:
					break;

				case WebRequest::State::Idle:
				case WebRequest::State::Completed:
				case WebRequest::State::Cancelled:
					// Unreachable state
					break;
			}

			if (!_response->ReadData()) {
				SetFailedWithLastError("Reading data"_s);
			}
			return;
		}

		CheckResult(DoWriteData());
	}

	WebRequest::Result WebRequestWinHTTP::DoWriteData(DWORD* bytesWritten)
	{
		DEATH_DEBUG_ASSERT(_dataWritten < _dataSize);

		std::int32_t dataWriteSize = WebRequestBufferSize;
		if (_dataWritten + dataWriteSize > _dataSize) {
			dataWriteSize = std::int32_t(_dataSize - _dataWritten);
		}

		_dataWriteBuffer.resize_for_overwrite(dataWriteSize);
		std::int64_t bytesRead = _dataStream->Read(_dataWriteBuffer.data(), dataWriteSize);
		if (bytesRead < 0) {
			return FailWithLastError("Reading request data from stream"_s);
		}

		if (!WinHTTP::WinHttpWriteData(_request, _dataWriteBuffer.data(), DWORD(bytesRead), bytesWritten)) {
			return FailWithLastError("Writing request data"_s);
		}

		return Result::Ok();
	}

	WebRequest::Result WebRequestWinHTTP::CreateResponse()
	{
		if (!WinHTTP::WinHttpReceiveResponse(_request, nullptr)) {
			return FailWithLastError("Receiving response"_s);
		}

		_response.reset(new WebResponseWinHTTP(*this));

		return _response->InitializeFileStorage();
	}

	WebRequest::Result WebRequestWinHTTP::InitializeAuthIfNeeded()
	{
		std::int32_t status = _response->GetStatus();
		if (status == HTTP_STATUS_DENIED || status == HTTP_STATUS_PROXY_AUTH_REQ) {
			_authChallenge.reset(new WebAuthChallengeWinHTTP(
				status == HTTP_STATUS_PROXY_AUTH_REQ
					? WebAuthChallenge::Source::Proxy
					: WebAuthChallenge::Source::Server,
				*this));

			if (!_authChallenge->Initialize()) {
				return FailWithLastError("Initializing authentication challenge"_s);
			}

			return Result::Unauthorized(_response->GetStatusText());
		}

		return Result::Ok();
	}

	const char* __GetWin32ErrorSuffix(DWORD error);

	WebRequest::Result WebRequestWinHTTP::Fail(StringView operation, DWORD errorCode)
	{
		char message[256];
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		::sprintf_s(message, "%s failed with error %08x%s", String::nullTerminatedView(operation).data(), errorCode, __GetWin32ErrorSuffix(errorCode));
#else
		::snprintf(message, sizeof(message), "%s failed with error %08x%s", String::nullTerminatedView(operation).data(), errorCode, __GetWin32ErrorSuffix(errorCode));
#endif
		return Result::Error(message);
	}

	WebRequest::Result WebRequestWinHTTP::Execute()
	{
		Result result = DoPrepareRequest();
		if (!result) {
			return result;
		}

		while (true) {
			result = SendRequest();
			if (!result) {
				return result;
			}

			while (_dataWritten < _dataSize) {
				DWORD bytesWritten = 0;
				result = DoWriteData(&bytesWritten);
				if (!result) {
					return result;
				}
				if (bytesWritten == 0) {
					break;
				}
				_dataWritten += bytesWritten;
			}

			result = CreateResponse();
			if (!result) {
				return result;
			}

			result = InitializeAuthIfNeeded();
			if (!result) {
				return result;
			}

			if (result.state != WebRequest::State::Unauthorized) {
				break;
			}

			switch (_authChallenge->GetSource()) {
				case WebAuthChallenge::Source::Proxy: {
					if (!_tryProxyCredentials) {
						return result;
					}

					_tryProxyCredentials = false;

					result = _authChallenge->DoSetCredentials(_sessionImpl.GetProxyCredentials());
					if (!result) {
						return result;
					}
					break;
				}
				case WebAuthChallenge::Source::Server: {
					if (!_tryCredentialsFromURL) {
						return result;
					}

					_tryCredentialsFromURL = false;

					result = _authChallenge->DoSetCredentials(_credentialsFromURL);
					if (!result) {
						return result;
					}

					if (_sessionImpl.HasProxyCredentials()) {
						_tryProxyCredentials = true;
					}

					if (_dataStream != nullptr) {
						_dataStream->Seek(0, SeekOrigin::Begin);
						_dataWritten = 0;
					}
					break;
				}
			}
			continue;
		}

		while (true) {
			DWORD bytesRead = 0;
			if (!_response->ReadData(&bytesRead)) {
				return FailWithLastError("Reading data"_s);
			}
			if (bytesRead == 0) {
				break;
			}
			_response->ReportDataReceived(bytesRead);
		}

		return GetResultFromHTTPStatus(_response);
	}

	WebRequest::Result WebRequestWinHTTP::DoPrepareRequest()
	{
		auto urlW = Utf8::ToUtf16(_url);
		URLComponents urlComps;
		if (!WinHTTP::WinHttpCrackUrl(urlW.data(), DWORD(urlW.size()), 0, &urlComps)) {
			return FailWithLastError("Parsing URL"_s);
		}

		if (urlComps.HasCredentials()) {
			_credentialsFromURL = urlComps.GetCredentials();
			_tryCredentialsFromURL = true;
		}

		Array<wchar_t> hostNameW(NoInit, urlComps.dwHostNameLength + 1);
		std::memcpy(hostNameW.data(), urlComps.lpszHostName, urlComps.dwHostNameLength * sizeof(wchar_t));
		hostNameW[urlComps.dwHostNameLength] = L'\0';

		_connect = WinHTTP::WinHttpConnect(_sessionImpl.GetHandle(), hostNameW, urlComps.nPort, NULL);
		if (_connect == NULL) {
			return FailWithLastError("Connecting"_s);
		}

		SmallVector<wchar_t, 500> objectNameW;
		objectNameW.reserve(urlComps.dwExtraInfoLength + urlComps.dwExtraInfoLength + 1);
		objectNameW.append(urlComps.lpszUrlPath, urlComps.lpszUrlPath + urlComps.dwUrlPathLength);
		if (urlComps.dwExtraInfoLength != 0) {
			objectNameW.append(urlComps.lpszExtraInfo, urlComps.lpszExtraInfo + urlComps.dwExtraInfoLength);
		}
		objectNameW.push_back(L'\0');

		String method = GetHTTPMethod();
		static const wchar_t* acceptedTypesW[] = { L"*/*", nullptr };
		_request = WinHTTP::WinHttpOpenRequest(_connect, Utf8::ToUtf16(method), objectNameW.data(), nullptr,
			WINHTTP_NO_REFERER, acceptedTypesW, urlComps.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
		if (_request == nullptr) {
			return FailWithLastError("Opening request"_s);
		}

		auto flags = GetSecurityFlags();
		if (flags != WebRequest::Ignore::None) {
			DWORD optValue = 0;

			if ((flags & WebRequest::Ignore::Certificate) != WebRequest::Ignore::None) {
				optValue |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
							SECURITY_FLAG_IGNORE_UNKNOWN_CA |
							SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
			}
			if ((flags & WebRequest::Ignore::Host) != WebRequest::Ignore::None) {
				optValue |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
			}

			WinHTTPSetOption(_request, WINHTTP_OPTION_SECURITY_FLAGS, optValue);
		}

		return Result::Ok();
	}

	void WebRequestWinHTTP::Run()
	{
		if (!CheckResult(DoPrepareRequest())) {
			return;
		}

		if (WinHTTP::WinHttpSetStatusCallback(_request, RequestStatusCallback, WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
				WINHTTP_CALLBACK_FLAG_WRITE_COMPLETE | WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
				WINHTTP_CALLBACK_FLAG_REQUEST_ERROR, NULL) == WINHTTP_INVALID_STATUS_CALLBACK) {
			SetFailedWithLastError("Setting up callbacks"_s);
			return;
		}

		SetState(WebRequest::State::Active);
		CheckResult(SendRequest());
	}

	WebRequest::Result WebRequestWinHTTP::SendRequest()
	{
		SmallVector<char, 1000> allHeaders;
		for (auto header = _headers.begin(); header != _headers.end(); ++header) {
			allHeaders.append(header->first.begin(), header->first.end());
			allHeaders.push_back(':');
			allHeaders.push_back(' ');
			allHeaders.append(header->second.begin(), header->second.end());
			allHeaders.push_back('\n');
		}

		if (_dataSize != 0) {
			_dataWritten = 0;
		}

		auto allHeadersW = Utf8::ToUtf16(allHeaders.data(), std::int32_t(allHeaders.size()));
		if (!WinHTTP::WinHttpSendRequest(_request, allHeadersW, DWORD(allHeadersW.size()), nullptr, 0, DWORD(_dataSize), DWORD_PTR(this))) {
			return FailWithLastError("Sending request"_s);
		}

		return Result::Ok();
	}

	void WebRequestWinHTTP::DoCancel()
	{
		WinHTTP::WinHttpCloseHandle(_request);
		_request = nullptr;
	}

	WebResponseWinHTTP::WebResponseWinHTTP(WebRequestWinHTTP& request)
		: WebResponseImpl(request), _requestHandle(request.GetHandle()), _contentLength(0)
	{
		String contentLengthStr = WinHTTPQueryHeaderString(_requestHandle, WINHTTP_QUERY_CONTENT_LENGTH);
		if (!contentLengthStr.empty()) {
			_contentLength = strtoll(contentLengthStr.data(), nullptr, 10);
		}
	}

	String WebResponseWinHTTP::GetURL() const
	{
		return WinHTTPQueryOptionString(_requestHandle, WINHTTP_OPTION_URL);
	}

	String WebResponseWinHTTP::GetHeader(StringView name) const
	{
		return WinHTTPQueryHeaderString(_requestHandle, WINHTTP_QUERY_CUSTOM, Utf8::ToUtf16(name));
	}

	Array<String> WebResponseWinHTTP::GetAllHeaderValues(StringView name) const
	{
		return WinHTTPQueryAllHeaderStrings(_requestHandle, WINHTTP_QUERY_CUSTOM, Utf8::ToUtf16(name));
	}

	std::int32_t WebResponseWinHTTP::GetStatus() const
	{
		DWORD status = 0;
		DWORD statusSize = sizeof(status);
		if (!WinHTTP::WinHttpQueryHeaders(_requestHandle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, 0)) {
			// Failed to get status code
		}

		return status;
	}

	String WebResponseWinHTTP::GetStatusText() const
	{
		return WinHTTPQueryHeaderString(_requestHandle, WINHTTP_QUERY_STATUS_TEXT);
	}

	bool WebResponseWinHTTP::ReadData(DWORD* bytesRead)
	{
		return WinHTTP::WinHttpReadData(_requestHandle, GetDataBuffer(WebRequestBufferSize), WebRequestBufferSize, bytesRead);
	}

	WebAuthChallengeWinHTTP::WebAuthChallengeWinHTTP(WebAuthChallenge::Source source, WebRequestWinHTTP& request)
		: WebAuthChallengeImpl(source), _request(request), _target(0), _selectedScheme(0) {}

	bool WebAuthChallengeWinHTTP::Initialize()
	{
		DWORD supportedSchemes, firstScheme;
		if (!WinHTTP::WinHttpQueryAuthSchemes(_request.GetHandle(), &supportedSchemes, &firstScheme, &_target)) {
			return false;
		}

		if (supportedSchemes & WINHTTP_AUTH_SCHEME_NEGOTIATE) {
			_selectedScheme = WINHTTP_AUTH_SCHEME_NEGOTIATE;
		} else if (supportedSchemes & WINHTTP_AUTH_SCHEME_NTLM) {
			_selectedScheme = WINHTTP_AUTH_SCHEME_NTLM;
		} else if (supportedSchemes & WINHTTP_AUTH_SCHEME_PASSPORT) {
			_selectedScheme = WINHTTP_AUTH_SCHEME_PASSPORT;
		} else if (supportedSchemes & WINHTTP_AUTH_SCHEME_DIGEST) {
			_selectedScheme = WINHTTP_AUTH_SCHEME_DIGEST;
		} else if (supportedSchemes & WINHTTP_AUTH_SCHEME_BASIC) {
			_selectedScheme = WINHTTP_AUTH_SCHEME_BASIC;
		} else {
			_selectedScheme = 0;
		}

		return (_selectedScheme != 0);
	}

	WebRequest::Result WebAuthChallengeWinHTTP::DoSetCredentials(const WebCredentials& cred)
	{
		if (!WinHTTP::WinHttpSetCredentials(_request.GetHandle(), _target, _selectedScheme, Utf8::ToUtf16(cred.GetUser()), Utf8::ToUtf16(cred.GetPassword()), NULL)) {
			return _request.FailWithLastError("Setting credentials"_s);
		}

		return WebRequest::Result::Ok();
	}

	void WebAuthChallengeWinHTTP::SetCredentials(const WebCredentials& cred)
	{
		if (!_request.CheckResult(DoSetCredentials(cred))) {
			return;
		}

		if (_request.GetState() != WebRequest::State::Active) {
			_request.SetState(WebRequest::State::Active);
		}

		_request.CheckResult(_request.SendRequest());
	}

	WebSessionWinHTTP::WebSessionWinHTTP(Mode mode)
		: WebSessionImpl(mode), _handle(NULL) {}

	WebSessionWinHTTP::~WebSessionWinHTTP()
	{
		if (_handle != NULL) {
			WinHTTP::WinHttpCloseHandle(_handle);
		}
	}

	bool WebSessionWinHTTP::Initialize()
	{
		return WinHTTP::LoadLibrary();
	}

	bool WebSessionWinHTTP::Open()
	{
		DWORD accessType = 0;
		Array<wchar_t> proxyName;

		const WebProxy& proxy = GetProxy();
		switch (proxy.GetType()) {
			case WebProxy::Type::URL: {
				accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
				proxyName = Utf8::ToUtf16(_proxyURLWithoutCredentials);
				break;
			}
			case WebProxy::Type::Disabled: {
				accessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
				break;
			}
			case WebProxy::Type::Default: {
				if (Environment::IsWindows8()) {
					accessType = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
				} else {
					accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
				}
				break;
			}
		}

		DWORD flags = 0;
		if (IsAsync()) {
			flags |= WINHTTP_FLAG_ASYNC;
		}

		const auto& headers = GetHeaders();
		auto userAgent = headers.find("User-Agent"_s);
		_handle = WinHTTP::WinHttpOpen(userAgent != headers.end() ? Utf8::ToUtf16(userAgent->second) : nullptr,
			accessType, (!proxyName.empty() ? proxyName : WINHTTP_NO_PROXY_NAME), WINHTTP_NO_PROXY_BYPASS, flags);
		if (_handle == NULL) {
			return false;
		}

		// Enable HTTP/2 (available since Windows 10 1607)
		WinHTTPSetOption(_handle, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL,
						 WINHTTP_PROTOCOL_FLAG_HTTP2);

		// Enable GZIP and DEFLATE (available since Windows 8.1)
		WinHTTPSetOption(_handle, WINHTTP_OPTION_DECOMPRESSION,
						 WINHTTP_DECOMPRESSION_FLAG_ALL);

		// Enable modern TLS on older Windows versions
		if (Environment::IsWindows8()) {
			WinHTTPSetOption(_handle, WINHTTP_OPTION_SECURE_PROTOCOLS,
							 WINHTTP_FLAG_SECURE_PROTOCOL_SSL3 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
							 WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2);
		}

		return true;
	}

	WebRequestImplPtr WebSessionWinHTTP::CreateRequest(WebSession& session, StringView url)
	{
		DEATH_ASSERT(!IsAsync(), "Cannot be called in asynchronous sessions", {});

		if (_handle == NULL) {
			if (!Open()) {
				return {};
			}
		}

		return WebRequestImplPtr{new WebRequestWinHTTP{*this, url}};
	}

	WebRequestImplPtr WebSessionWinHTTP::CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id)
	{
		DEATH_ASSERT(IsAsync(), "Cannot be called in synchronous sessions", {});

		if (_handle == NULL) {
			if (!Open()) {
				return {};
			}
		}

		return WebRequestImplPtr(new WebRequestWinHTTP(session, *this, url, Death::move(callback), id));
	}

	bool WebSessionWinHTTP::SetProxy(const WebProxy& proxy)
	{
		DEATH_ASSERT(_handle == NULL, "Proxy must be set before the first request is made", false);

		if (proxy.GetType() == WebProxy::Type::URL) {
			auto url = proxy.GetURL();
			auto urlW = Utf8::ToUtf16(url);
			URLComponents urlComps;
			if (!WinHTTP::WinHttpCrackUrl(urlW.data(), DWORD(urlW.size()), 0, &urlComps)) {
				return false;
			}

			if (urlComps.HasCredentials()) {
				_proxyCredentials = urlComps.GetCredentials();

				urlComps.lpszUserName = nullptr;
				urlComps.dwUserNameLength = 0;
				urlComps.lpszPassword = nullptr;
				urlComps.dwPasswordLength = 0;

				SmallVector<wchar_t, 500> buffer(urlW.size());
				DWORD length = DWORD(buffer.size());
				if (!WinHTTP::WinHttpCreateUrl(&urlComps, 0, buffer.data(), &length)) {
					return false;
				}

				_proxyURLWithoutCredentials = Utf8::FromUtf16(buffer.data(), length);
			} else {
				_proxyURLWithoutCredentials = url;
			}

			// WinHttpOpen() doesn't accept trailing slashes in the URL
			while (_proxyURLWithoutCredentials.hasSuffix('/')) {
				_proxyURLWithoutCredentials = _proxyURLWithoutCredentials.exceptSuffix(1);
			}
		}

		return WebSessionImpl::SetProxy(proxy);
	}

#elif defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_UNIX)

	class WebAuthChallengeCURL : public WebAuthChallengeImpl
	{
	public:
		WebAuthChallengeCURL(WebAuthChallenge::Source source, WebRequestCURL& request);

		WebAuthChallengeCURL(const WebAuthChallengeCURL&) = delete;
		WebAuthChallengeCURL& operator=(const WebAuthChallengeCURL&) = delete;

		void SetCredentials(const WebCredentials& cred) override;

	private:
		WebRequestCURL& _request;
	};

	// Define symbols that might be missing from older libcurl headers
	#if !defined(CURL_AT_LEAST_VERSION)
	#	define CURL_VERSION_BITS(x,y,z) ((x)<<16|(y)<<8|z)
	#	define CURL_AT_LEAST_VERSION(x,y,z) (LIBCURL_VERSION_NUM >= CURL_VERSION_BITS(x, y, z))
	#endif

	// The new name was introduced in curl 7.21.6
	#if !defined(CURLOPT_ACCEPT_ENCODING)
	#	define CURLOPT_ACCEPT_ENCODING CURLOPT_ENCODING
	#endif

	namespace
	{
		std::size_t CURLWriteData(void* buffer, std::size_t size, std::size_t nmemb, void* userdata)
		{
			DEATH_DEBUG_ASSERT(userdata != nullptr);

			return static_cast<WebResponseCURL*>(userdata)->CURLOnWrite(buffer, size * nmemb);
		}

		std::size_t CURLHeader(char* buffer, std::size_t size, std::size_t nitems, void* userdata)
		{
			DEATH_DEBUG_ASSERT(userdata != nullptr);

			return static_cast<WebResponseCURL*>(userdata)->CURLOnHeader(buffer, size * nitems);
		}

		int CURLXferInfo(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
		{
			DEATH_DEBUG_ASSERT(clientp != nullptr);

			auto* response = reinterpret_cast<WebResponseCURL*>(clientp);
			return response->CURLOnProgress(dltotal);
		}

		int CURLProgress(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow)
		{
			return CURLXferInfo(clientp, static_cast<curl_off_t>(dltotal), static_cast<curl_off_t>(dlnow),
				static_cast<curl_off_t>(ultotal), static_cast<curl_off_t>(ulnow));
		}

		void CURLSetOpt(CURL* handle, CURLoption option, long long value)
		{
			CURLcode res = curl_easy_setopt(handle, option, value);
			if (res != CURLE_OK) {
				LOGD("curl_easy_setopt(%d, %lld) failed: %s", static_cast<int>(option), value, curl_easy_strerror(res));
			}
		}

		void CURLSetOpt(CURL* handle, CURLoption option, unsigned long value)
		{
			CURLcode res = curl_easy_setopt(handle, option, value);
			if (res != CURLE_OK) {
				LOGD("curl_easy_setopt(%d, %lx) failed: %s", static_cast<int>(option), value, curl_easy_strerror(res));
			}
		}

		void CURLSetOpt(CURL* handle, CURLoption option, long value)
		{
			CURLcode res = curl_easy_setopt(handle, option, value);
			if (res != CURLE_OK) {
				LOGD("curl_easy_setopt(%d, %ld) failed: %s", static_cast<int>(option), value, curl_easy_strerror(res));
			}
		}

		void CURLSetOpt(CURL* handle, CURLoption option, int value)
		{
			CURLSetOpt(handle, option, static_cast<long>(value));
		}

		void CURLSetOpt(CURL* handle, CURLoption option, const void* value)
		{
			CURLcode res = curl_easy_setopt(handle, option, value);
			if (res != CURLE_OK) {
				LOGD("curl_easy_setopt(%d, %p) failed: %s", static_cast<int>(option), value, curl_easy_strerror(res));
			}
		}

		template<typename R, typename ...Args>
		void CURLSetOpt(CURL* handle, CURLoption option, R(*func)(Args...))
		{
			CURLSetOpt(handle, option, reinterpret_cast<void*>(func));
		}

		void CURLSetOpt(CURL* handle, CURLoption option, const char* value)
		{
			CURLcode res = curl_easy_setopt(handle, option, value);
			if (res != CURLE_OK) {
				LOGD("curl_easy_setopt(%d, \"%s\") failed: %s", static_cast<int>(option), value, curl_easy_strerror(res));
			}
		}

		void CURLSetOpt(CURL* handle, CURLoption option, StringView value)
		{
			CURLSetOpt(handle, option, String::nullTerminatedView(value).data());
		}
	}

	WebResponseCURL::WebResponseCURL(WebRequestCURL& request)
		: WebResponseImpl(request)
	{
		_knownDownloadSize = 0;

		CURLSetOpt(GetHandle(), CURLOPT_WRITEDATA, this);
		CURLSetOpt(GetHandle(), CURLOPT_HEADERDATA, this);

#if CURL_AT_LEAST_VERSION(7, 32, 0)
		if (WebSessionCURL::CurlRuntimeAtLeastVersion(7, 32, 0)) {
			CURLSetOpt(GetHandle(), CURLOPT_XFERINFOFUNCTION, CURLXferInfo);
			CURLSetOpt(GetHandle(), CURLOPT_XFERINFODATA, this);
		} else
#endif
		{
			DEATH_IGNORE_DEPRECATED_PUSH
			CURLSetOpt(GetHandle(), CURLOPT_PROGRESSFUNCTION, CURLProgress);
			CURLSetOpt(GetHandle(), CURLOPT_PROGRESSDATA, this);
			DEATH_IGNORE_DEPRECATED_POP
		}

		CURLSetOpt(GetHandle(), CURLOPT_NOPROGRESS, 0L);
	}

	std::size_t WebResponseCURL::CURLOnWrite(void* buffer, std::size_t size)
	{
		void* buf = GetDataBuffer(size);
		std::memcpy(buf, buffer, size);
		ReportDataReceived(size);
		return size;
	}

	std::size_t WebResponseCURL::CURLOnHeader(const char* buffer, std::size_t size)
	{
		StringView hdr = StringView(buffer, size).trimmed();

		if (hdr.hasPrefix("HTTP/"_s)) {
			// First line of the headers contains status text after version and status
			hdr = hdr.suffix(hdr.findOr(' ', hdr.end()).begin());
			_statusText = hdr.suffix(hdr.findOr(' ', hdr.end()).begin());
			_headers.clear();
		} else if (!hdr.empty()) {
			if (StringView sep = hdr.find(':')) {
				String hdrName = StringUtils::uppercase(hdr.prefix(sep.begin()).trimmedSuffix());
				String hdrValue = hdr.suffix(sep.end()).trimmedPrefix();
				_headers[hdrName].push_back(Death::move(hdrValue));
			}
		}

		return size;
	}

	std::int32_t WebResponseCURL::CURLOnProgress(curl_off_t total)
	{
		if (_knownDownloadSize != total) {
			if (_request.GetStorage() == WebRequest::Storage::Memory) {
				PreAllocateBuffer(static_cast<std::size_t>(total));
			}
			_knownDownloadSize = total;
		}

		return 0;
	}

	std::int64_t WebResponseCURL::GetContentLength() const
	{
#if CURL_AT_LEAST_VERSION(7, 55, 0)
		curl_off_t len = 0;
		curl_easy_getinfo(GetHandle(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
		return len;
#else
		double len = 0;
		curl_easy_getinfo(GetHandle(), CURLINFO_CONTENT_LENGTH_DOWNLOAD, &len);
		return std::int64_t(len);
#endif
	}

	String WebResponseCURL::GetURL() const
	{
		char* urlp = nullptr;
		curl_easy_getinfo(GetHandle(), CURLINFO_EFFECTIVE_URL, &urlp);
		return urlp;
	}

	String WebResponseCURL::GetHeader(StringView name) const
	{
		const auto it = _headers.find(StringUtils::uppercase(name));
		if (it != _headers.end()) {
			return it->second.back();
		}
		return {};
	}

	Array<String> WebResponseCURL::GetAllHeaderValues(StringView name) const
	{
		const auto it = _headers.find(StringUtils::uppercase(name));
		if (it != _headers.end()) {
			return Array(InPlaceInit, arrayView(it->second));
		}
		return {};
	}

	std::int32_t WebResponseCURL::GetStatus() const
	{
		long status = 0;
		curl_easy_getinfo(GetHandle(), CURLINFO_RESPONSE_CODE, &status);
		return std::int32_t(status);
	}

	CURL* WebResponseCURL::GetHandle() const
	{
		return static_cast<WebRequestCURL&>(_request).GetHandle();
	}

	static std::size_t CURLRead(char* buffer, std::size_t size, std::size_t nitems, void* userdata)
	{
		DEATH_DEBUG_ASSERT(userdata != nullptr);

		return static_cast<WebRequestCURL*>(userdata)->CURLOnRead(buffer, size * nitems);
	}

	WebRequestCURL::WebRequestCURL(WebSessionCURL& sessionImpl, StringView url)
		: WebRequestImpl(sessionImpl), _sessionCURL(nullptr), _handle(sessionImpl.GetHandle()), _bytesSent(0)
	{
		DoStartPrepare(url);
	}

	WebRequestCURL::WebRequestCURL(WebSessionAsync& session, WebSessionAsyncCURL& sessionImpl, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id)
		: WebRequestImpl(session, sessionImpl, Death::move(callback), id), _sessionCURL(&sessionImpl), _handle(curl_easy_init()), _bytesSent(0)
	{
		DoStartPrepare(url);
	}

	void WebRequestCURL::DoStartPrepare(StringView url)
	{
		DEATH_ASSERT(_handle != nullptr, "libcurl initialization failed", );

		// Set error buffer to get more detailed CURL status
		_errorBuffer[0] = '\0';
		CURLSetOpt(_handle, CURLOPT_ERRORBUFFER, _errorBuffer);
		CURLSetOpt(_handle, CURLOPT_URL, url);

		// Set callback functions
		CURLSetOpt(_handle, CURLOPT_WRITEFUNCTION, CURLWriteData);
		CURLSetOpt(_handle, CURLOPT_HEADERFUNCTION, CURLHeader);
		CURLSetOpt(_handle, CURLOPT_READFUNCTION, CURLRead);
		CURLSetOpt(_handle, CURLOPT_READDATA, this);
		CURLSetOpt(_handle, CURLOPT_ACCEPT_ENCODING, "");
		// Enable redirection handling
		CURLSetOpt(_handle, CURLOPT_FOLLOWLOCATION, 1L);
		// Limit redirect to HTTP
#if CURL_AT_LEAST_VERSION(7, 85, 0)
		if (WebSessionCURL::CurlRuntimeAtLeastVersion(7, 85, 0)) {
			CURLSetOpt(_handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
		} else
#endif
		{
			DEATH_IGNORE_DEPRECATED_PUSH
			CURLSetOpt(_handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
			DEATH_IGNORE_DEPRECATED_POP
		}

		const WebProxy& proxy = GetSessionImpl().GetProxy();
		bool usingProxy = true;
		switch (proxy.GetType()) {
			case WebProxy::Type::URL:
				CURLSetOpt(_handle, CURLOPT_PROXY, proxy.GetURL());
				break;

			case WebProxy::Type::Disabled:
				// This is a special value disabling use of proxy
				CURLSetOpt(_handle, CURLOPT_PROXY, "");
				usingProxy = false;
				break;

			case WebProxy::Type::Default:
				// Nothing to do, libcurl will use the standard http_proxy and other similar environment variables by default
				break;
		}

		// Enable all supported authentication methods
		CURLSetOpt(_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
		if (usingProxy) {
			CURLSetOpt(_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
		}
	}

	WebRequestCURL::~WebRequestCURL()
	{
		if (_headerList != nullptr) {
			curl_slist_free_all(_headerList);
		}

		if (IsAsync()) {
			_sessionCURL->RequestHasTerminated(this);
			curl_easy_cleanup(_handle);
		}
	}

	WebRequest::Result WebRequestCURL::DoFinishPrepare()
	{
		_response.reset(new WebResponseCURL(*this));

		auto result = _response->InitializeFileStorage();
		if (!result) {
			return result;
		}

		String method = GetHTTPMethod();

		if (method == "GET"_s) {
			// Nothing to do, libcurl defaults to GET
		} else if (method == "POST"_s) {
			CURLSetOpt(_handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<long long>(_dataSize));
			CURLSetOpt(_handle, CURLOPT_POST, 1L);
		} else if (method == "HEAD"_s) {
			CURLSetOpt(_handle, CURLOPT_NOBODY, 1L);
		} else if (method != "PUT"_s || _dataSize == 0) {
			CURLSetOpt(_handle, CURLOPT_CUSTOMREQUEST, method);
		}
		//else: PUT will be used by default if we have any data to send (and if we don't, which should never
		//		happen but is nevertheless formally allowed, we've set it as custom request above).

		// POST is handled specially by libcurl, but for everything else, including PUT as well as any other method,
		// such as e.g. DELETE, we need to explicitly request uploading the data, if any.
		if (_dataSize != 0 && method != "POST"_s) {
			CURLSetOpt(_handle, CURLOPT_UPLOAD, 1L);
			CURLSetOpt(_handle, CURLOPT_INFILESIZE_LARGE, static_cast<long long>(_dataSize));
		}

		for (auto& header : _headers) {
			String hdrStr = header.first + ": "_s + header.second;
			_headerList = curl_slist_append(_headerList, hdrStr.data());
		}
		CURLSetOpt(_handle, CURLOPT_HTTPHEADER, _headerList);

		WebRequest::Ignore securityFlags = GetSecurityFlags();
		if ((securityFlags & WebRequest::Ignore::Certificate) == WebRequest::Ignore::Certificate) {
			CURLSetOpt(_handle, CURLOPT_SSL_VERIFYPEER, 0);
		}
		if ((securityFlags & WebRequest::Ignore::Host) == WebRequest::Ignore::Host) {
			CURLSetOpt(_handle, CURLOPT_SSL_VERIFYHOST, 0);
		}
		return Result::Ok();
	}

	WebRequest::Result WebRequestCURL::Execute()
	{
		auto result = DoFinishPrepare();
		if (result.state == WebRequest::State::Failed) {
			return result;
		}

		const CURLcode err = curl_easy_perform(_handle);
		if (err != CURLE_OK) {
			// This ensures that DoHandleCompletion() returns failure and uses libcurl error message
			_response.reset();
		}

		return DoHandleCompletion();
	}

	void WebRequestCURL::Run()
	{
		if (!CheckResult(DoFinishPrepare())) {
			return;
		}
		StartRequest();
	}

	WebResponseImplPtr WebRequestCURL::GetResponse() const
	{
		return _response;
	}

	WebAuthChallengeImplPtr WebRequestCURL::GetAuthChallenge() const
	{
		return _authChallenge;
	}

	bool WebRequestCURL::StartRequest()
	{
		_bytesSent = 0;

		if (!_sessionCURL->StartRequest(*this)) {
			SetState(WebRequest::State::Failed);
			return false;
		}

		return true;
	}

	void WebRequestCURL::DoCancel()
	{
		_sessionCURL->CancelRequest(this);
	}

	WebRequest::Result WebRequestCURL::DoHandleCompletion()
	{
		// This is a special case, we want to use libcurl error message if there is no response at all
		if (_response == nullptr || _response->GetStatus() == 0) {
			return Result::Error(GetError());
		}
		return GetResultFromHTTPStatus(WebResponseImplPtr(_response));
	}

	void WebRequestCURL::HandleCompletion()
	{
		HandleResult(DoHandleCompletion());

		if (GetState() == WebRequest::State::Unauthorized) {
			_authChallenge.reset(new WebAuthChallengeCURL(_response->GetStatus() == 407
				? WebAuthChallenge::Source::Proxy : WebAuthChallenge::Source::Server, *this));
		}
	}

	String WebRequestCURL::GetError() const
	{
		return _errorBuffer;
	}

	std::size_t WebRequestCURL::CURLOnRead(char* buffer, std::size_t size)
	{
		if (_dataStream == nullptr) {
			return 0;
		}

		std::int64_t bytesRead = _dataStream->Read(buffer, std::int64_t(size));
		if (bytesRead <= 0) {
			return 0;
		}

		_bytesSent += bytesRead;
		return bytesRead;
	}

	std::int64_t WebRequestCURL::GetBytesSent() const
	{
		return _bytesSent;
	}

	std::int64_t WebRequestCURL::GetBytesExpectedToSend() const
	{
		return _dataSize;
	}

	WebAuthChallengeCURL::WebAuthChallengeCURL(WebAuthChallenge::Source source, WebRequestCURL& request)
		: WebAuthChallengeImpl(source), _request(request) {}

	void WebAuthChallengeCURL::SetCredentials(const WebCredentials& cred)
	{
		String authStr = cred.GetUser() + ':' + cred.GetPassword();
		CURLSetOpt(_request.GetHandle(), (GetSource() == WebAuthChallenge::Source::Proxy
			? CURLOPT_PROXYUSERPWD : CURLOPT_USERPWD), authStr.data());

		_request.StartRequest();
	}

	std::int32_t WebSessionBaseCURL::_activeSessions = 0;
	std::uint32_t WebSessionBaseCURL::_runtimeVersion = 0;

	WebSessionBaseCURL::WebSessionBaseCURL(Mode mode)
		: WebSessionImpl(mode)
	{
		if (_activeSessions == 0) {
			if (curl_global_init(CURL_GLOBAL_ALL)) {
				LOGE("Failed to initialize libcurl library");
			} else {
				curl_version_info_data* data = curl_version_info(CURLVERSION_NOW);
				_runtimeVersion = data->version_num;
			}
		}

		++_activeSessions;
	}

	WebSessionBaseCURL::~WebSessionBaseCURL()
	{
		--_activeSessions;
		if (_activeSessions == 0) {
			curl_global_cleanup();
		}
	}

	WebSessionCURL::WebSessionCURL()
		: WebSessionBaseCURL(Mode::Sync), _handle(nullptr) {}

	WebSessionCURL::~WebSessionCURL()
	{
		if (_handle != nullptr) {
			curl_easy_cleanup(_handle);
		}
	}

	WebRequestImplPtr WebSessionCURL::CreateRequest(WebSession& session, StringView url)
	{
		if (_handle == nullptr) {
			// Allocate it the first time we need it and keep it later
			_handle = curl_easy_init();
		} else {
			// But when reusing it subsequently, we must reset all the previously set options to prevent
			// the settings from one request from applying to the subsequent ones.
			curl_easy_reset(_handle);
		}

		return WebRequestImplPtr(new WebRequestCURL(*this, url));
	}

	WebRequestImplPtr WebSessionCURL::CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id)
	{
		DEATH_ASSERT(false, "Cannot be called in synchronous sessions", {});
		return {};
	}

	WebSessionAsyncCURL::WebSessionAsyncCURL()
		: WebSessionBaseCURL(Mode::Async), _handle(nullptr), _workerThreadRunning(false) {}

	WebSessionAsyncCURL::~WebSessionAsyncCURL()
	{
		if (_handle != nullptr) {
			curl_multi_cleanup(_handle);
		}

		_workerThread.detach();
	}

	WebRequestImplPtr WebSessionAsyncCURL::CreateRequest(WebSession& session, StringView url)
	{
		DEATH_ASSERT(false, "Cannot be called in asynchronous sessions", {});
		return {};
	}

	WebRequestImplPtr WebSessionAsyncCURL::CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id)
	{
		// Allocate our handle on demand
		if (_handle == nullptr) {
			_handle = curl_multi_init();
			DEATH_ASSERT(_handle != nullptr, "curl_multi_init() failed", {});

			curl_multi_setopt(_handle, CURLMOPT_SOCKETDATA, this);
			curl_multi_setopt(_handle, CURLMOPT_SOCKETFUNCTION, SocketCallback);
		}

		return WebRequestImplPtr(new WebRequestCURL(session, *this, url, Death::move(callback), id));
	}

	bool WebSessionAsyncCURL::StartRequest(WebRequestCURL& request)
	{
		// Add request easy handle to multi handle
		CURL* curl = request.GetHandle();
		int code = curl_multi_add_handle(_handle, curl);
		if (code != CURLM_OK) {
			return false;
		}

		request.SetState(WebRequest::State::Active);
		_activeTransfers[curl] = &request;

		// Report a timeout to curl to initiate this transfer
		int runningHandles;
		curl_multi_socket_action(_handle, CURL_SOCKET_TIMEOUT, 0, &runningHandles);

		if (!_workerThreadRunning.exchange(true)) {
			_workerThread = std::thread(OnWorkerThread, this);
		}

		return true;
	}

	void WebSessionAsyncCURL::CancelRequest(WebRequestCURL* request)
	{
		CURL* curl = request->GetHandle();
		StopActiveTransfer(curl);

		request->SetState(WebRequest::State::Cancelled);
	}

	void WebSessionAsyncCURL::RequestHasTerminated(WebRequestCURL* request)
	{
		CURL* curl = request->GetHandle();
		StopActiveTransfer(curl);
	}

	bool WebSessionBaseCURL::CurlRuntimeAtLeastVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
	{
		return (_runtimeVersion >= CURL_VERSION_BITS(major, minor, patch));
	}

	int WebSessionAsyncCURL::SocketCallback(CURL* curl, curl_socket_t sock, int what, void* userp, void* sp)
	{
		auto* session = static_cast<WebSessionAsyncCURL*>(userp);
		session->ProcessSocketCallback(curl, sock, what);
		return CURLM_OK;
	}

	void WebSessionAsyncCURL::ProcessSocketCallback(CURL* curl, curl_socket_t s, int what)
	{
		switch (what) {
			case CURL_POLL_IN:
			case CURL_POLL_OUT:
			case CURL_POLL_INOUT: {
				_activeSockets[curl] = s;
				break;
			}

			case CURL_POLL_REMOVE:
				RemoveActiveSocket(curl);
				break;
		}
	}

	void WebSessionAsyncCURL::CheckForCompletedTransfers()
	{
		std::int32_t msgQueueCount;
		while (CURLMsg* msg = curl_multi_info_read(_handle, &msgQueueCount)) {
			if (msg->msg == CURLMSG_DONE) {
				CURL* curl = msg->easy_handle;
				auto it = _activeTransfers.find(curl);
				if (it != _activeTransfers.end()) {
					WebRequestCURL* request = it->second;
					curl_multi_remove_handle(_handle, curl);
					request->HandleCompletion();
					_activeTransfers.erase(it);
					RemoveActiveSocket(curl);
				}
			}
		}
	}

	void WebSessionAsyncCURL::StopActiveTransfer(CURL* curl)
	{
		auto it = _activeTransfers.find(curl);
		if (it != _activeTransfers.end()) {
			curl_socket_t activeSocket = CURL_SOCKET_BAD;
			auto it2 = _activeSockets.find(curl);
			if (it2 != _activeSockets.end()) {
				activeSocket = it2->second;
			}

			// Remove the CURL easy handle from the CURLM multi handle
			curl_multi_remove_handle(_handle, curl);

			// If the transfer was active, close its socket
			if (activeSocket != CURL_SOCKET_BAD) {
				::close(activeSocket);
			}

			RemoveActiveSocket(curl);
			_activeTransfers.erase(it);
		}
	}

	void WebSessionAsyncCURL::RemoveActiveSocket(CURL* curl)
	{
		auto it = _activeSockets.find(curl);
		if (it != _activeSockets.end()) {
			_activeSockets.erase(it);
		}
	}

	void WebSessionAsyncCURL::OnWorkerThread(WebSessionAsyncCURL* _this)
	{
		int runningHandles;
		do {
			if (curl_multi_perform(_this->_handle, &runningHandles) != CURLM_OK) {
				LOGE("curl_multi_perform() failed");
				break;
			}
			if (curl_multi_wait(_this->_handle, nullptr, 0, 10000, nullptr) != CURLM_OK) {
				LOGE("curl_multi_wait() failed");
				break;
			}

			_this->CheckForCompletedTransfers();
		} while(runningHandles > 0);

		_this->_workerThread.detach();
		_this->_workerThreadRunning = false;
	}

#endif

}}