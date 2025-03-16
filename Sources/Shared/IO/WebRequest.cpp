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
		virtual ~WebAuthChallengeImpl() = default;

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

		virtual ~WebRequestImpl() = default;

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

		virtual void Start() = 0;

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
		virtual ~WebResponseImpl();

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

		virtual ~WebSessionImpl();

		virtual WebRequestImplPtr CreateRequest(WebSession& session, StringView url) = 0;
		virtual WebRequestImplPtr CreateRequestAsync(WebSessionAsync& session, StringView url, Function<void(WebRequestEvent&)>&& callback, std::int32_t id) = 0;

		void AddCommonHeader(StringView name, StringView value) {
			_headers[name] = value;
		}

		String GetTempDirectory() const;

		void SetTempDirectory(StringView dir) {
			_tempDir = dir;
		}

		virtual bool SetProxy(const WebProxy& proxy) {
			_proxy = proxy; return true;
		}
		const WebProxy& GetProxy() const {
			return _proxy;
		}

		const WebRequestHeaderMap& GetHeaders() const {
			return _headers;
		}

		virtual WebSessionHandle GetNativeHandle() const = 0;

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
			_sessionImpl(FromOwned(sessionImpl)), _session(&session), _callback(std::move(callback)), _id(id), _state(WebRequest::State::Idle),
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

		SetData(std::make_unique<MemoryStream>(_dataText.data(), _dataText.size()), contentType);
	}

	bool WebRequestImpl::SetData(std::unique_ptr<Stream> dataStream, StringView contentType, std::int64_t dataSize)
	{
		_dataStream = std::move(dataStream);

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
		if (GetResponse()) {
			return GetResponse()->GetContentLength();
		} else {
			return -1;
		}
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
		_bytesReceived += sizeReceived;
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
		StringView dataFile;

		AddRef();
		const WebRequestImplPtr request(this);

		const WebResponseImplPtr& response = GetResponse();

		WebRequestEvent e(GetId(), state, WebRequestAsync(request), WebResponse(response), failMsg);

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

		return _impl->SetData(std::move(dataStream), contentType, dataSize);
	}

	void WebRequestBase::SetStorage(Storage storage)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetStorage(storage);
	}

	WebRequestBase::Storage WebRequestBase::GetStorage() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", Storage::None);

		return _impl->GetStorage();
	}

	WebRequest::Result WebRequest::Execute() const
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", Result::Error("Invalid session object"_s));

		return _impl->Execute();
	}

	void WebRequestAsync::Start()
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );
		DEATH_ASSERT(_impl->GetState() == State::Idle, "Completed requests cannot be restarted", );

		_impl->Start();
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

	WebRequestHandle WebRequestBase::GetNativeHandle() const
	{
		return _impl ? _impl->GetNativeHandle() : nullptr;
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
				_file->Write(_readBuffer.data(), _readBuffer.size());
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

		void Start() override;

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

		HINTERNET GetHandle() const {
			return _request;
		}

		WebRequestHandle GetNativeHandle() const override {
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

		HINTERNET GetHandle() const {
			return _handle;
		}

		WebSessionHandle GetNativeHandle() const override {
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

		return WebRequestAsync(_impl->CreateRequestAsync(*this, url, std::move(callback), id));
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

	void WebSessionBase::SetTempDirectory(StringView dir)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", );

		_impl->SetTempDirectory(dir);
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

	void WebSessionBase::Close()
	{
		_impl.reset(nullptr);
	}

	WebSessionHandle WebSessionBase::GetNativeHandle() const
	{
		return (_impl ? _impl->GetNativeHandle() : nullptr);
	}

	bool WebSessionBase::EnablePersistentStorage(bool enable)
	{
		DEATH_ASSERT(_impl != nullptr, "Cannot be called with an uninitialized object", false);

		return _impl->EnablePersistentStorage(enable);
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
		: WebRequestImpl(session, sessionImpl, std::move(callback), id), _sessionImpl(sessionImpl), _url(url),
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

	void WebRequestWinHTTP::Start()
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
		for (WebRequestHeaderMap::const_iterator header = _headers.begin(); header != _headers.end(); ++header) {
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

		return WebRequestImplPtr(new WebRequestWinHTTP(session, *this, url, std::move(callback), id));
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

#endif

}}