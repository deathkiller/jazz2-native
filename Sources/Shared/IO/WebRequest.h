#pragma once

#include "Stream.h"
#include "../Containers/ComPtr.h"
#include "../Containers/Function.h"
#include "../Containers/SmallVector.h"
#include "../Containers/String.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @brief Contains the user name and password to use for authenticating */
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
		/** @brief Authentication challenge source */
		enum class Source {
			Server,			/**< Server */
			Proxy			/**< Proxy */
		};

		WebAuthChallenge();
		WebAuthChallenge(const WebAuthChallenge& other);
		WebAuthChallenge& operator=(const WebAuthChallenge& other);
		~WebAuthChallenge();

		/** @brief Returns `true` if the object is valid */
		bool IsValid() const {
			return _impl;
		}

		/** @brief Returns authentication challenge source */
		Source GetSource() const;

		/** @brief Sets credentials */
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

		/** @brief Returns `true` if the object is valid */
		bool IsValid() const {
			return _impl;
		}

		/** @brief Returns the content length of the response data */
		std::int64_t GetContentLength() const;
		/** @brief Returns the effective URL of the request */
		Containers::String GetURL() const;
		/** @brief Returns value of the specified header */
		Containers::String GetHeader(Containers::StringView name) const;
		/** @brief Returns all values of the specified header */
		Containers::Array<Containers::String> GetAllHeaderValues(Containers::StringView name) const;
		/** @brief Returns the MIME type of the response */
		Containers::String GetMimeType() const;
		/** @brief Returns the value of the "Content-Type" header */
		Containers::String GetContentType() const;
		/** @brief Returns the status code returned by the server */
		std::int32_t GetStatus() const;
		/** @brief Returns the status text returned by the server */
		Containers::String GetStatusText() const;
		/** @brief Returns a stream which represents the response data sent by the server */
		Stream* GetStream() const;
		/** @brief Returns a suggested filename for the response data */
		Containers::String GetSuggestedFileName() const;
		/** @brief Returns all response data as a string */
		Containers::String AsString() const;
		/** @brief Returns the full path of the file to which data is being saved */
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

		/** @brief Returns `true` if the object is valid */
		bool IsValid() const {
			return _impl;
		}

		/** @brief Sets a request header which will be sent to the server by this request */
		void SetHeader(Containers::StringView name, Containers::StringView value);
		/** @brief Sets the HTTP request method */
		void SetMethod(Containers::StringView method);
		/** @brief Sets the text to be posted to the server in this request */
		void SetData(Containers::StringView text, Containers::StringView contentType);
		/** @brief Sets the binary data to be posted to the server in this request */
		bool SetData(std::unique_ptr<Stream> dataStream, Containers::StringView contentType, std::int64_t dataSize = -1);
		/** @brief Returns the destination storage where the response data will be stored */
		Storage GetStorage() const;
		/** @brief Sets the destination storage where the response data will be stored */
		void SetStorage(Storage storage);

		/** @brief Returns the server response */
		WebResponse GetResponse() const;

		/** @brief Returns the number of bytes sent to the server */
		std::int64_t GetBytesSent() const;
		/** @brief Returns the total number of bytes expected to be sent to the server */
		std::int64_t GetBytesExpectedToSend() const;
		/** @brief Returns the number of bytes received from the server */
		std::int64_t GetBytesReceived() const;
		/** @brief Returns the number of bytes expected to be received from the server */
		std::int64_t GetBytesExpectedToReceive() const;

		/** @brief Returns the native handle corresponding to this request object */
		WebRequestHandle GetNativeHandle() const;

		/** @brief Security bypass flags, supports a bitwise combination of its member values */
		enum class Ignore {
			None = 0,					/**< None */
			Certificate = 1,			/**< Certificate */
			Host = 2,					/**< Host */
			All = Certificate | Host	/**< All */
		};

		DEATH_PRIVATE_ENUM_FLAGS(Ignore);

		/** @brief Makes the connection insecure by disabling security checks */
		void MakeInsecure(Ignore flags = Ignore::All);
		/** @brief Returns security bypass flags */
		Ignore GetSecurityFlags() const;

		/** @brief Disables SSL certificate verification */
		void DisablePeerVerify(bool disable = true) {
			MakeInsecure(disable ? Ignore::Certificate : Ignore::None);
		}

		/** @brief Returns `true` if SSL certificate verification is disabled */
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

		/** @brief Executes the request synchronously */
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

		/** @brief Executes the request to the server asynchronously */
		void Run();
		/** @brief Cancels the active request */
		void Cancel();
		/** @brief Returns the current authentication challenge while the state of the request is @ref State::Unauthorized */
		WebAuthChallenge GetAuthChallenge() const;
		/** @brief Returns the ID specified while creating the request */
		std::int32_t GetId() const;
		/** @brief Returns the underlying session */
		WebSessionAsync& GetSession() const;
		/** @brief Returns the current state of the request */
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
		Type GetType() const;
		/** @brief Returns proxy URL if @ref Type::URL is selected */
		Containers::StringView GetURL() const;

	private:
		Type _type;
		Containers::String _url;

		WebProxy(Type type, Containers::StringView url = {});
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

		/** @brief Sets a request header in every request created from this session after it has been set. */
		void AddCommonHeader(Containers::StringView name, Containers::StringView value);
		/** @brief Returns the current temporary directory */
		Containers::String GetTempDirectory() const;
		/** @brief Sets the current temporary directory */
		void SetTempDirectory(Containers::StringView path);
		/** @brief Sets the proxy to use for all requests initiated by this session */
		bool SetProxy(const WebProxy& proxy);
		/** @brief Returns `true` if the session was successfully opened and can be used */
		bool IsOpened() const;
		/** @brief Closes the session */
		void Dispose();
		/** @brief Allows to enable persistent storage for the session */
		bool EnablePersistentStorage(bool enable);
		/** @brief Returns the native handle corresponding to this session object */
		WebSessionHandle GetNativeHandle() const;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		WebSessionImplPtr _impl;
#endif

		/** @brief Returns the internal session factory */
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

	/** @brief Represents web session that allows @ref WebRequestAsync to be created */
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

	/** @brief Result of the asynchronous web request */
	class WebRequestEvent
	{
	public:
		WebRequestEvent(std::int32_t id = -1, WebRequest::State state = WebRequest::State::Idle,
						const WebRequestAsync& request = {}, const WebResponse& response = {}, Containers::StringView errorDesc = {});

		/** @brief Returns the current state of the request */
		WebRequestAsync::State GetState() const;
		/** @brief 	Returns a reference to the @ref WebRequestAsync object which initiated this event */
		const WebRequestAsync& GetRequest() const;
		/** @brief Returns the response when the state is @relativeref{WebRequestBase,State::Completed} */
		const WebResponse& GetResponse() const;
		/** @brief Returns a textual error description when the state is @relativeref{WebRequestBase,State::Failed} */
		Containers::StringView GetErrorDescription() const;
		/** @brief Returns the full path of a temporary file containing the response data when the state is @relativeref{WebRequestBase,State::Completed} and storage is @relativeref{WebRequestBase,Storage::File} */
		Containers::StringView GetDataFile() const;
		/** @brief Returns the buffer */
		Containers::ArrayView<char> GetDataBuffer() const;

#ifndef DOXYGEN_GENERATING_OUTPUT
		void SetDataFile(Containers::StringView dataFile);
		void SetDataBuffer(Containers::ArrayView<char> dataBuffer);
#endif

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