#pragma once

#include "Stream.h"
#include "../Containers/ComPtr.h"
#include "../Containers/Function.h"
#include "../Containers/SmallVector.h"
#include "../Containers/String.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @brief Contains the username and password to use for authenticating */
	class WebCredentials
	{
	public:
		WebCredentials(Containers::StringView user = {}, Containers::StringView password = {})
			: _user(user), _password(password) {}

		/** @brief Returns user name */
		Containers::StringView GetUser() const {
			return _user;
		}

		/** @brief Returns password */
		Containers::StringView GetPassword() const {
			return _password;
		}

	private:
		Containers::String _user;
		Containers::String _password;
	};

	class WebSessionFactory;
	class WebRequestEvent;

#ifndef DOXYGEN_GENERATING_OUTPUT
	typedef struct WebRequestHandleOpaque* WebRequestHandle;
	typedef struct WebSessionHandleOpaque* WebSessionHandle;

	class WebAuthChallengeImpl;
	class WebRequestImpl;
	class WebResponseImpl;
	class WebSessionAsync;
	class WebSessionImpl;

	typedef Containers::ComPtr<WebAuthChallengeImpl> WebAuthChallengeImplPtr;
	typedef Containers::ComPtr<WebRequestImpl> WebRequestImplPtr;
	typedef Containers::ComPtr<WebResponseImpl> WebResponseImplPtr;
	typedef Containers::ComPtr<WebSessionImpl> WebSessionImplPtr;
#endif

	/** @brief Contains the authentication challenge information */
	class WebAuthChallenge
	{
		friend class WebRequestAsync;

	public:
		/** @brief Authentication challenge information source */
		enum class Source {
			Server,			/**< Server */
			Proxy			/**< Proxy */
		};

		WebAuthChallenge();
		WebAuthChallenge(const WebAuthChallenge& other);
		WebAuthChallenge& operator=(const WebAuthChallenge& other);
		~WebAuthChallenge();

		bool IsValid() const {
			return (_impl.get() != nullptr);
		}

		Source GetSource() const;

		void SetCredentials(const WebCredentials& cred);

	private:
		WebAuthChallengeImplPtr _impl;

		explicit WebAuthChallenge(const WebAuthChallengeImplPtr& impl);
	};

	/** @brief Describes the server response of the web request */
	class WebResponse
	{
		friend class WebRequestBase;
		friend class WebRequestImpl;
		friend class WebResponseImpl;

	public:
		WebResponse();
		WebResponse(const WebResponse& other);
		WebResponse& operator=(const WebResponse& other);
		~WebResponse();

		bool IsValid() const {
			return (_impl.get() != nullptr);
		}

		std::int64_t GetContentLength() const;
		Containers::String GetURL() const;
		Containers::String GetHeader(Containers::StringView name) const;
		Containers::Array<Containers::String> GetAllHeaderValues(Containers::StringView name) const;
		Containers::String GetMimeType() const;
		Containers::String GetContentType() const;
		std::int32_t GetStatus() const;
		Containers::String GetStatusText() const;
		Stream* GetStream() const;
		Containers::String GetSuggestedFileName() const;
		Containers::String AsString() const;
		Containers::String GetDataFile() const;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		WebResponseImplPtr _impl;
#endif

		explicit WebResponse(const WebResponseImplPtr& impl);
	};

	/** @brief Base class of web requests */
	class WebRequestBase
	{
	public:
		/** @brief Request state */
		enum class State {
			Idle,			/**< Idle */
			Unauthorized,	/**< Unauthorized */
			Active,			/**< Active */
			Completed,		/**< Completed */
			Failed,			/**< Failed */
			Cancelled		/**< Cancelled */
		};

		/** @brief Storage for response data */
		enum class Storage {
			Memory,			/**< Memory */
			File,			/**< File */
			None			/**< None */
		};

		/** @brief Result of synchronous operation */
		struct Result
		{
			static Result Ok(State state = State::Active) {
				Result result;
				result.state = state;
				return result;
			}

			static Result Cancelled() {
				Result result;
				result.state = State::Cancelled;
				return result;
			}

			static Result Error(Containers::StringView error) {
				Result result;
				result.state = State::Failed;
				result.error = error;
				return result;
			}

			static Result Unauthorized(Containers::StringView error) {
				Result result;
				result.state = State::Unauthorized;
				result.error = error;
				return result;
			}

			explicit operator bool() const {
				return (state != State::Failed);
			}

			State state = State::Idle;
			Containers::String error;
		};

		bool IsValid() const {
			return (_impl.get() != nullptr);
		}

		void SetHeader(Containers::StringView name, Containers::StringView value);

		void SetMethod(Containers::StringView method);

		void SetData(Containers::StringView text, Containers::StringView contentType);

		bool SetData(std::unique_ptr<Stream> dataStream, Containers::StringView contentType, std::int64_t dataSize = -1);

		bool SetData(Stream* dataStream, Containers::StringView contentType, std::int64_t dataSize = -1) {
			return SetData(std::unique_ptr<Stream>(dataStream), contentType, dataSize);
		}

		void SetStorage(Storage storage);

		Storage GetStorage() const;

		WebResponse GetResponse() const;

		std::int64_t GetBytesSent() const;
		std::int64_t GetBytesExpectedToSend() const;
		std::int64_t GetBytesReceived() const;
		std::int64_t GetBytesExpectedToReceive() const;

		WebRequestHandle GetNativeHandle() const;

		/** @brief Security bypass flags */
		enum class Ignore {
			None = 0,					/**< None */
			Certificate = 1,			/**< Certificate */
			Host = 2,					/**< Host */
			All = Certificate | Host	/**< All */
		};

		DEATH_PRIVATE_ENUM_FLAGS(Ignore);

		void MakeInsecure(Ignore flags = Ignore::All);
		Ignore GetSecurityFlags() const;

		void DisablePeerVerify(bool disable = true) {
			MakeInsecure(disable ? Ignore::Certificate : Ignore::None);
		}

		bool IsPeerVerifyDisabled() const {
			return (GetSecurityFlags() & Ignore::Certificate) != Ignore::None;
		}

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		WebRequestImplPtr _impl;
#endif

		WebRequestBase();
		explicit WebRequestBase(const WebRequestImplPtr& impl);
		WebRequestBase(const WebRequestBase& other);
		WebRequestBase& operator=(const WebRequestBase& other);
		~WebRequestBase();
	};

	/** @brief Represents web request */
	class WebRequest : public WebRequestBase
	{
		friend class WebSession;

	public:
		WebRequest() = default;
		WebRequest(const WebRequest& other) = default;
		WebRequest& operator=(const WebRequest& other) = default;

		Result Execute() const;

	private:
		explicit WebRequest(const WebRequestImplPtr& impl)
			: WebRequestBase(impl) {}
	};

	/** @brief Represents asynchronous web request */
	class WebRequestAsync : public WebRequestBase
	{
		friend class WebSessionAsync;
		friend class WebRequestImpl;
		friend class WebResponseImpl;

	public:
		WebRequestAsync() = default;
		WebRequestAsync(const WebRequestAsync& other) = default;
		WebRequestAsync& operator=(const WebRequestAsync& other) = default;

		void Run();
		void Cancel();
		WebAuthChallenge GetAuthChallenge() const;
		std::int32_t GetId() const;
		WebSessionAsync& GetSession() const;
		State GetState() const;

	private:
		explicit WebRequestAsync(const WebRequestImplPtr& impl)
			: WebRequestBase(impl) {}
	};

	/** @brief Describes the proxy settings to be used by the session */
	class WebProxy
	{
	public:
		/** @brief Creates proxy settings from the specified URL */
		static WebProxy FromURL(Containers::StringView url) {
			return WebProxy(Type::URL, url);
		}

		/** @brief Creates proxy settings to disable proxy even if one is configured */
		static WebProxy Disable() {
			return WebProxy(Type::Disabled);
		}

		/** @brief Creates default proxy settings */
		static WebProxy Default() {
			return WebProxy(Type::Default);
		}

		/** @brief Proxy type */
		enum class Type {
			URL,			/**< URL */
			Disabled,		/**< Disabled */
			Default			/**< Default */
		};

		/** @brief Returns proxy type */
		Type GetType() const {
			return _type;
		}

		/** @brief Returns proxy URL if @ref Type::URL */
		Containers::StringView GetURL() const {
			DEATH_DEBUG_ASSERT(_type == Type::URL);
			return _url;
		}

	private:
		Type _type;
		Containers::String _url;

		WebProxy(Type type, Containers::StringView url = {})
			: _type(type), _url(url) {}
	};

	/** @brief Base class of web sessions */
	class WebSessionBase
	{
	public:
		WebSessionBase();

		WebSessionBase(const WebSessionBase& other);
		WebSessionBase& operator=(const WebSessionBase& other);
		~WebSessionBase();

		/** @brief Returns `true` if the network backend is available */
		static bool IsBackendAvailable();

		void AddCommonHeader(Containers::StringView name, Containers::StringView value);
		Containers::String GetTempDirectory() const;
		void SetTempDirectory(Containers::StringView dir);
		bool SetProxy(const WebProxy& proxy);
		bool IsOpened() const;
		void Close();
		bool EnablePersistentStorage(bool enable);
		WebSessionHandle GetNativeHandle() const;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		WebSessionImplPtr _impl;
#endif

		static WebSessionFactory* FindFactory();

		explicit WebSessionBase(const WebSessionImplPtr& impl);
	};

	/** @brief Represents web session that allows @ref WebRequest to be created */
	class WebSession : public WebSessionBase
	{
	public:
		WebSession() = default;

		WebSession(const WebSession& other) = default;
		WebSession& operator=(const WebSession& other) = default;

		/** @brief Returns the default session for synchronous requests */
		static WebSession& GetDefault();

		/** @brief Creates a new session for synchronous requests */
		static WebSession New();

		/** @brief Creates a new synchronous request */
		WebRequest CreateRequest(Containers::StringView url);

	private:
		explicit WebSession(const WebSessionImplPtr& impl)
			: WebSessionBase(impl) {}
	};

	/** @brief Represents asynchronous web session that allows @ref WebRequestAsync to be created */
	class WebSessionAsync : public WebSessionBase
	{
	public:
		WebSessionAsync() = default;

		WebSessionAsync(const WebSessionAsync& other) = default;
		WebSessionAsync& operator=(const WebSessionAsync& other) = default;

		/** @brief Returns the default session for asynchronous requests */
		static WebSessionAsync& GetDefault();

		/** @brief Creates a new session for asynchronous requests */
		static WebSessionAsync New();

		/** @brief Creates a new asynchronous request */
		WebRequestAsync CreateRequest(Containers::StringView url, Containers::Function<void(WebRequestEvent& e)>&& callback, std::int32_t id = -1);

	private:
		explicit WebSessionAsync(const WebSessionImplPtr& impl)
			: WebSessionBase(impl) {}
	};

	/** @brief Result of asynchronous web request */
	class WebRequestEvent
	{
	public:
		WebRequestEvent(std::int32_t id = -1, WebRequest::State state = WebRequest::State::Idle,
						const WebRequestAsync& request = {}, const WebResponse& response = {}, Containers::StringView errorDesc = {})
			: _id(id), _state(state), _request(request), _response(response), _errorDescription(errorDesc)
		{
		}

		WebRequestAsync::State GetState() const {
			return _state;
		}

		const WebRequestAsync& GetRequest() const {
			return _request;
		}

		const WebResponse& GetResponse() const {
			return _response;
		}

		Containers::StringView GetErrorDescription() const {
			return _errorDescription;
		}

		Containers::StringView GetDataFile() const {
			return _dataFile;
		}

		void SetDataFile(Containers::StringView dataFile) {
			_dataFile = dataFile;
		}

		Containers::ArrayView<char> GetDataBuffer() const {
			return _dataBuffer;
		}

		void SetDataBuffer(Containers::ArrayView<char> dataBuffer) {
			_dataBuffer = dataBuffer;
		}

	private:
		std::int32_t _id;
		WebRequestAsync::State _state;
		const WebRequestAsync _request;
		const WebResponse _response;
		Containers::String _dataFile;
		Containers::ArrayView<char> _dataBuffer;
		Containers::String _errorDescription;
	};

}}