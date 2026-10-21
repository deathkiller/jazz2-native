#include "DocumentFiles.h"

#if defined(WITH_EDITOR)

#include "../EditorInitialization.h"
#include "../Document/CommandArchive.h"
#include "../Events/EventCatalog.h"
#include "../Files/AtomicFile.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Files/EditorSettings.h"
#include "../Files/TileSetDataCache.h"
#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include "../Files/WebFileBridge.h"
#endif
#include "../Imaging/LevelThumbnail.h"
#include "../Imaging/PngCodec.h"
#include "../Imaging/TileImages.h"
#include "../Shell/TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../UI/EditorIcons.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../UI/UiTheme.h"
#include "../UI/UiWidgets.h"
#include "../../ContentResolver.h"
#include "../../IRootController.h"
#include "../../PreferencesCache.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Graphics/IGfxDevice.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

#include <cstdio>

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	include <shellapi.h>
#endif
#if (defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_APPLE)) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	include <cerrno>
#	include <fcntl.h>
#	include <signal.h>
#	include <sys/wait.h>
#	include <unistd.h>
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// How often the files are looked at, in frames at 60 FPS
		constexpr float WatchInterval = 60.0f;
		// How often the lock is written again, in frames at 60 FPS, and how old it gets before it's taken as left behind,
		// in milliseconds
		constexpr float LockRefreshInterval = 30.0f * 60.0f;
		constexpr std::int64_t LockTimeout = 90 * 1000;

		std::int64_t GetNowMilliseconds()
		{
			return Death::Containers::DateTime::UtcNow().ToUnixMilliseconds();
		}

		std::int64_t GetModificationTime(StringView path)
		{
			return (!path.empty() && fs::FileExists(path) ? fs::GetLastModificationTime(path).ToUnixMilliseconds() : 0);
		}

		// The process of this editor, stored in the lock, so a lock left behind by an editor of this computer that ended
		// (killed or crashed) isn't taken for an open one
		std::uint32_t GetOwnProcessId()
		{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
			return std::uint32_t(::GetCurrentProcessId());
#elif (defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_APPLE)) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_EMSCRIPTEN)
			return std::uint32_t(::getpid());
#else
			return 0;
#endif
		}

		// Unknown means running, the lock is then only a warning that can be dismissed
		bool IsProcessRunning(std::uint32_t processId)
		{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
			HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(processId));
			if (process == NULL) {
				return (::GetLastError() == ERROR_ACCESS_DENIED);
			}
			DWORD exitCode = 0;
			bool running = (::GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE);
			::CloseHandle(process);
			return running;
#elif (defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_APPLE)) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_EMSCRIPTEN)
			return (::kill(pid_t(processId), 0) == 0 || errno == EPERM);
#else
			static_cast<void>(processId);
			return true;
#endif
		}

		// A lock is "<host>/<process>/<random>", one of this computer whose process ended is left behind
		// Process of this computer that holds a lock, 0 if it's of another computer or it can't be told
		std::uint32_t GetLockProcess(StringView holder)
		{
			String host = Application::GetDeviceHostname();
			if (host.empty() || !holder.hasPrefix(host) || holder.size() <= host.size() || holder[host.size()] != '/') {
				return 0;
			}
			StringView rest = holder.exceptPrefix(host.size() + 1);
			std::uint32_t processId = 0;
			std::size_t digits = 0;
			while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9' && digits < 10) {
				processId = processId * 10 + std::uint32_t(rest[digits] - '0');
				digits++;
			}
			if (digits == 0 || digits >= rest.size() || rest[digits] != '/') {
				// Written by an older build without the process, can't tell
				return 0;
			}
			return processId;
		}

		bool IsLockLeftBehind(StringView holder)
		{
			std::uint32_t processId = GetLockProcess(holder);
			return (processId != 0 && processId != GetOwnProcessId() && !IsProcessRunning(processId));
		}

		// A lock of this very editor, the session of a level opened again lives until the new one has taken over,
		// there's never more than one session in a process otherwise
		bool IsOwnProcessLock(StringView holder)
		{
			std::uint32_t processId = GetLockProcess(holder);
			return (processId != 0 && processId == GetOwnProcessId());
		}

		// Splits the command the script is edited with into the program and its arguments (see EditorSettings::ScriptEditor):
		// a path of a program that exists is taken whole even with spaces, otherwise the words are split by spaces, words in
		// quotes kept together. The script is put in for {file}, or after the arguments.
		bool SplitEditorCommand(StringView command, StringView file, String& program, SmallVectorImpl<String>& arguments)
		{
			command = command.trimmed();
			SmallVector<String, 8> words;
			if (fs::FileExists(command)) {
				words.push_back(String(command));
			} else {
				String word;
				bool quoted = false, inWord = false;
				for (char c : command) {
					if (c == '"') {
						quoted = !quoted;
						inWord = true;
					} else if ((c == ' ' || c == '\t') && !quoted) {
						if (inWord) {
							words.push_back(Death::move(word));
							word = String();
							inWord = false;
						}
					} else {
						word += StringView(&c, 1);
						inWord = true;
					}
				}
				if (inWord) {
					words.push_back(Death::move(word));
				}
			}
			if (words.empty() || words[0].empty()) {
				return false;
			}
			program = Death::move(words[0]);
			bool placed = false;
			for (std::size_t i = 1; i < words.size(); i++) {
				if (words[i].contains("{file}"_s)) {
					arguments.push_back(StringUtils::replaceAll(words[i], "{file}"_s, file));
					placed = true;
				} else {
					arguments.push_back(Death::move(words[i]));
				}
			}
			if (!placed) {
				arguments.push_back(String(file));
			}
			return true;
		}

		// Starts a program with arguments without waiting for it, returns false if it couldn't be started
		bool StartProgram(StringView program, ArrayView<const String> arguments)
		{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
			// Through the shell, as a file is opened, a name found in PATH or in the registered applications is enough
			String parameters;
			for (const String& argument : arguments) {
				if (!parameters.empty()) {
					parameters += " "_s;
				}
				if (argument.empty() || argument.contains(' ') || argument.contains('\t')) {
					parameters += String("\""_s + argument + "\""_s);
				} else {
					parameters += argument;
				}
			}
			auto programW = Death::Utf8::ToUtf16(program);
			auto parametersW = Death::Utf8::ToUtf16(parameters);
			// Without the error box of the shell, which would stop the editor until closed, the editor tells it itself
			SHELLEXECUTEINFOW info = {};
			info.cbSize = sizeof(info);
			info.fMask = SEE_MASK_FLAG_NO_UI;
			info.lpVerb = L"open";
			info.lpFile = programW.data();
			info.lpParameters = parametersW.data();
			info.nShow = SW_SHOWNORMAL;
			return (::ShellExecuteExW(&info) != FALSE);
#elif (defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_APPLE)) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_EMSCRIPTEN) && \
	!defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS)
			SmallVector<String, 8> owned;
			owned.push_back(String(program));
			for (const String& argument : arguments) {
				owned.push_back(argument);
			}
			SmallVector<char*, 8> argv;
			for (String& argument : owned) {
				argv.push_back(argument.data());
			}
			argv.push_back(nullptr);

			// A pipe closed by a successful exec tells it was started, the error is written into it otherwise; the program
			// is the child of a child that ends at once, so it never stays behind as a zombie of the editor
			int status[2];
			if (::pipe(status) != 0) {
				return false;
			}
			::fcntl(status[1], F_SETFD, FD_CLOEXEC);
			pid_t child = ::fork();
			if (child < 0) {
				::close(status[0]);
				::close(status[1]);
				return false;
			}
			if (child == 0) {
				::close(status[0]);
				pid_t grandchild = ::fork();
				if (grandchild == 0) {
					::setsid();
					::execvp(argv[0], argv.data());
					int error = errno;
					ssize_t written = ::write(status[1], &error, sizeof(error));
					static_cast<void>(written);
					::_exit(127);
				}
				::_exit(grandchild < 0 ? 1 : 0);
			}
			::close(status[1]);
			int childStatus = 0;
			::waitpid(child, &childStatus, 0);
			int error = 0;
			ssize_t bytesRead = ::read(status[0], &error, sizeof(error));
			::close(status[0]);
			return (WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0 && bytesRead <= 0);
#else
			static_cast<void>(program);
			static_cast<void>(arguments);
			return false;
#endif
		}

		String ReadSmallFile(StringView path)
		{
			auto s = fs::Open(path, FileAccess::Read);
			std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
			if (size <= 0 || size > 4096) {
				return {};
			}
			String text{NoInit, std::size_t(size)};
			if (s->Read(text.data(), size) != size) {
				return {};
			}
			return text;
		}

		// Why a level can't be opened in the editor, read from the start of its file, or an empty string if it can
		String GetOpenProblem(StringView path)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return _x("editor", "The file isn't a level of this version of the game");
			}
			// A level exported only to be played has a flag the editor doesn't open it with
			if ((header.Flags & LevelFlags::NotEditable) == LevelFlags::NotEditable) {
				return _x("editor", "The level was exported only to be played, it can't be edited");
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			if (ContentTileSetLibrary::FindTileSetFile(properties.DefaultTileSet).empty()) {
				return _fx("editor", "The tile set \"{}\" of the level wasn't found", properties.DefaultTileSet);
			}
			return {};
		}

		// Whether two paths, as the file dialogs give them, are the same file, the case doesn't matter on Windows
		bool IsSameFile(StringView a, StringView b)
		{
			if (a.empty() || b.empty()) {
				return false;
			}
			String normalizedA = fs::FromNativeSeparators(String(a));
			String normalizedB = fs::FromNativeSeparators(String(b));
#if defined(DEATH_TARGET_WINDOWS)
			return (StringUtils::lowercase(normalizedA) == StringUtils::lowercase(normalizedB));
#else
			return (normalizedA == normalizedB);
#endif
		}
	}

	String IEditorContext::GetHotkeysPath()
	{
		return fs::CombinePath(PreferencesCache::GetDirectory(), "Jazz2.EditorHotkeys.json"_s);
	}

	String IEditorContext::MakeLevelName(StringView path)
	{
		String episode = fs::GetFileName(fs::GetDirectoryName(path));
		String name = fs::GetFileNameWithoutExtension(path);
		String levelName = (episode.empty() ? String("unknown"_s) : episode) + "/"_s + name;
		StringUtils::lowercaseInPlace(levelName);
		return levelName;
	}

	bool IEditorContext::IsLevelLockedElsewhere(StringView levelPath)
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (levelPath.empty()) {
			return false;
		}
		String lockPath = levelPath + ".lock"_s;
		std::int64_t lockTime = GetModificationTime(lockPath);
		if (lockTime == 0 || GetNowMilliseconds() - lockTime >= LockTimeout) {
			return false;
		}
		// A lock that can't be read is taken as held, it's being written maybe
		String holder = ReadSmallFile(lockPath);
		return (holder.empty() || (!IsOwnProcessLock(holder) && !IsLockLeftBehind(holder)));
#else
		static_cast<void>(levelPath);
		return false;
#endif
	}

	DocumentFiles::DocumentFiles(IEditorContext& context, IDocumentFilesHost& host, IRootController& root)
		: _context(context), _host(host), _root(root)
	{
		_autosave.BeforeSave = [this]() { _host.StoreViewState(); };
	}

	DocumentFiles::~DocumentFiles()
	{
		ReleaseLock();
	}

	void DocumentFiles::SetFile(StringView path, StringView levelName)
	{
		_filePath = path;
		_levelName = levelName;
		_autosave.SetSource(_filePath);
	}

	void DocumentFiles::OnDocumentReplaced(StringView path, StringView levelName)
	{
		// The copy kept without a choice is of the file edited until now, another file has a copy of its own, which is
		// written as usual; new levels share a single copy, it's still kept from them
		if (_recoveryKept && AutosaveService::GetRecoveryPath(_filePath) != AutosaveService::GetRecoveryPath(path)) {
			_recoveryKept = false;
		}
		SetFile(path, levelName);
	}

	bool DocumentFiles::CanSaveInPlace() const
	{
		if (_filePath.empty()) {
			return false;
		}
		// The cache is rebuilt from the original levels, nothing saved there would last
		StringView cachePath = ContentResolver::Get().GetCachePath();
		if (!cachePath.empty() && _filePath.hasPrefix(cachePath)) {
			return false;
		}
		return fs::IsWritable(fs::GetDirectoryName(_filePath));
	}

	void DocumentFiles::GetFileRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots)
	{
		auto& resolver = ContentResolver::Get();

		StringView userContent = resolver.GetWritableContentPath(forSaving);
		if (!userContent.empty()) {
			// TRANSLATORS: Name of a place in the file dialog, the folder of content added by the user
			roots.push_back({ _x("editor", "User content"), fs::CombinePath(userContent, "Episodes"_s), true });
		}
		StringView content = resolver.GetContentPath();
		if (!content.empty() && content != userContent) {
			bool writable = fs::IsWritable(content);
			if (writable || !forSaving) {
				// TRANSLATORS: Name of a place in the file dialog, the folder of the content of the game
				roots.push_back({ _x("editor", "Content"), fs::CombinePath(content, "Episodes"_s), writable });
			}
		}
		StringView cache = resolver.GetCachePath();
		if (!cache.empty() && !forSaving) {
			// TRANSLATORS: Name of a place in the file dialog, the folder of the converted content of the original game
			roots.push_back({ _x("editor", "Cache"), fs::CombinePath(cache, "Episodes"_s), false });
		}
	}

	void DocumentFiles::GetImageRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots)
	{
		StringView userContent = ContentResolver::Get().GetWritableContentPath(forSaving);
		if (!userContent.empty()) {
			// TRANSLATORS: Name of a place in the file dialog, the folder of content added by the user
			roots.push_back({ _x("editor", "User content"), String(userContent), true });
		}
		if (!_filePath.empty()) {
			String levelDirectory = fs::GetDirectoryName(_filePath);
			bool writable = fs::IsWritable(levelDirectory);
			if (writable || !forSaving) {
				// TRANSLATORS: Name of a place in the file dialog, the folder the edited level is in
				roots.push_back({ _x("editor", "Level folder"), Death::move(levelDirectory), writable });
			}
		}
		String home = fs::GetHomeDirectory();
		if (!home.empty() && fs::DirectoryExists(home)) {
			// TRANSLATORS: Name of a place in the file dialog, the home folder of the user
			roots.push_back({ _x("editor", "Home"), Death::move(home), true });
		}
	}

	String DocumentFiles::FindDefaultTileSet()
	{
		if (_context.GetTileSetLibrary().GetTileCount("castle1"_s) >= 0) {
			return "castle1"_s;
		}
		auto& resolver = ContentResolver::Get();
		for (StringView root : { resolver.GetCachePath(), resolver.GetContentPath() }) {
			String directory = fs::CombinePath(root, "Tilesets"_s);
			for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
				if (fs::GetExtension(item) == "j2t"_s) {
					return fs::GetFileNameWithoutExtension(item);
				}
			}
		}
		return {};
	}

	void DocumentFiles::OpenFile(StringView path)
	{
		String pathCopy = path;
		ConfirmDiscard([this, pathCopy]() {
			EditorInitialization editorInit;
			editorInit.FilePath = pathCopy;
			_root.OpenEditor(Death::move(editorInit));
		});
	}

	void DocumentFiles::OpenRecoveryCopy(StringView recoveryPath)
	{
		// The file the copy belongs to is kept in the copy
		String filePath;
		{
			auto s = fs::Open(recoveryPath, FileAccess::Read);
			Formats::LevelHeader header;
			if (s->IsValid() && Formats::LevelFile::ReadHeader(*s, header)) {
				Formats::LevelReader reader(*s, header);
				SmallVector<Formats::ChunkRecord, 0> chunks;
				if (reader.SkipToChunks()) {
					reader.ReadChunks(chunks, true);
				}
				for (const Formats::ChunkRecord& chunk : chunks) {
					if (chunk.Tag == AutosaveService::RecoveryChunk) {
						filePath = String(reinterpret_cast<const char*>(chunk.Data.data()), chunk.Data.size());
					}
				}
			}
		}
		// A file that is gone is saved as a new level
		if (!filePath.empty() && !fs::FileExists(filePath)) {
			filePath = {};
		}
		String recoveryCopy = recoveryPath;
		ConfirmDiscard([this, recoveryCopy, filePath]() {
			String levelName = (filePath.empty() ? String("unknown/untitled"_s) : IEditorContext::MakeLevelName(filePath));
			if (!RestoreRecoveryCopy(recoveryCopy, filePath, levelName)) {
				_context.ShowMessage(_x("editor", "The changes couldn't be restored"));
			}
		});
	}

	bool DocumentFiles::RestoreRecoveryCopy(StringView recoveryPath, StringView filePath, StringView levelName)
	{
		LevelDocument document;
		{
			auto s = fs::Open(recoveryPath, FileAccess::Read);
			if (!s->IsValid() || !document.Load(*s, &_context.GetTileSetLibrary())) {
				return false;
			}
		}
		// The history of the changes is kept in the copy, so they can be undone, it's not a part of the level
		SmallVector<std::uint8_t, 0> archive;
		if (const Formats::ChunkRecord* chunk = document.FindChunk(CommandArchive::Chunk)) {
			archive = chunk->Data;
		}
		document.SetChunk(CommandArchive::Chunk, {});
		document.SetChunk(AutosaveService::RecoveryChunk, {});
		// The path and the name may be those of the level edited until now, which are replaced
		String filePathCopy = filePath, levelNameCopy = levelName;
		_host.ReplaceDocument(Death::move(document), filePathCopy, levelNameCopy);
		// Restored, so it was chosen about, the copy is written again with what follows
		_recoveryKept = false;
		CommandHistory& history = _context.GetHistory();
		if (archive.empty() || !CommandArchive::Read(archive, _context.GetDocument(), history)) {
			// Still not saved, without a history it can't get back to how it was saved
			history.MarkDirty();
		} else if (!history.IsDirty()) {
			// Restored as it was saved, the file may have been changed since all the same
			history.MarkDirty();
		}
		UpdateWindowTitle();
		return true;
	}

	bool DocumentFiles::OfferRecovery()
	{
		String recoveryPath = AutosaveService::FindRecovery(_filePath);
		if (recoveryPath.empty()) {
			return false;
		}

		UiMessageDialog::Show(*_context.GetSurface(), _x("editor", "Unsaved Changes"), _x("editor", "Changes of this level that weren't saved were found.\nDo you want to restore them?"),
			// TRANSLATORS: Buttons of the message box about found changes that weren't saved, restore them or throw them away
			{ _x("editor", "Restore"), _x("editor", "Discard") }, [this, recoveryPath](std::int32_t button) {
			if (button < 0) {
				// Closed without choosing, the changes are kept and offered again the next time the level is opened,
				// nothing is saved over them until then (saving the level on purpose still replaces them)
				_recoveryKept = true;
				return;
			}
			if (button != 0) {
				_autosave.Discard();
				return;
			}
			String filePath = _filePath, levelName = _levelName;
			if (!RestoreRecoveryCopy(recoveryPath, filePath, levelName)) {
				_context.ShowMessage(_x("editor", "The changes couldn't be restored"));
			}
		});
		return true;
	}

	void DocumentFiles::ConfirmDiscard(Function<void()>&& proceed)
	{
		if (!_context.GetHistory().IsDirty()) {
			proceed();
			return;
		}

		auto sharedProceed = std::make_shared<Function<void()>>(Death::move(proceed));
		UiMessageDialog::Show(*_context.GetSurface(), _x("editor", "Unsaved Changes"), _x("editor", "The level was changed since it was saved.\nDo you want to save the changes?"),
			// TRANSLATORS: Buttons of the message box asking to save the changed level before leaving it
			{ _("Save"), _x("editor", "Don't Save"), _x("editor", "Cancel") }, [this, sharedProceed](std::int32_t button) {
			if (button == 0) {
				if (CanSaveInPlace()) {
					if (SaveTo(_filePath, false)) {
						(*sharedProceed)();
					} else {
						_context.ShowMessage(_x("editor", "The level couldn't be saved"));
					}
				} else {
					ShowSaveAsDialog(false, [sharedProceed]() { (*sharedProceed)(); });
				}
			} else if (button == 1) {
				if (!_recoveryKept) {
					// Changes kept from before, not chosen about yet, aren't these
					_autosave.Discard();
				}
				(*sharedProceed)();
			}
		});
	}

	void DocumentFiles::ExitEditor()
	{
		ConfirmDiscard([this]() {
			_root.GoToMainMenu(false);
		});
	}

	void DocumentFiles::ShowOpenDialog()
	{
		SmallVector<UiFileRoot, 4> roots;
		GetFileRoots(false, roots);
		String directory = (!_filePath.empty() ? String(fs::GetDirectoryName(_filePath)) : String());
		auto open = [this](StringView path) {
			// Told here, rather than leaving this session for a level that won't be opened
			String problem = GetOpenProblem(path);
			if (!problem.empty()) {
				_context.ShowMessage(problem);
				return;
			}
			// Opened in a new session, so nothing of this one is left behind
			EditorInitialization editorInit;
			editorInit.FilePath = path;
			_root.OpenEditor(Death::move(editorInit));
		};
		// TRANSLATORS: Title of the file dialog that opens a level to edit
		UiFileDialog* dialog = UiFileDialog::Show(*_context.GetSurface(), _x("editor", "Open Level"), UiFileDialogMode::Open, Death::move(roots), ".j2l"_s, directory, {}, open);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// The browser has the files of the computer, a level is uploaded into the game
		// TRANSLATORS: Button in the Open Level dialog of the web version, picks a level file of the computer to upload
		dialog->GetContent()->Add<UiButton>(_x("editor", "From This Computer"), EditorIcon::Folder, [this, dialog, open]() mutable {
			dialog->Close();
			WebFileBridge::PickFile(".j2l"_s, Death::move(open));
		})->Tooltip = _x("editor", "Upload a level, saving it downloads it back");
#else
		static_cast<void>(dialog);
#endif
	}

	void DocumentFiles::ShowNewLevelDialog()
	{
		struct NewLevelSettings {
			String TileSet;
			float Width = 128.0f;
			float Height = 64.0f;
		};
		auto settings = std::make_shared<NewLevelSettings>();
		settings->TileSet = _context.GetDocument().Properties.DefaultTileSet;

		// TRANSLATORS: Title of the dialog that creates an empty level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "New Level"));
		UiDialog* dialogPtr = dialog.get();
		UiStack* content = dialog->GetContent();

		auto* tileSetRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// TRANSLATORS: Label in the New Level dialog, the name of the tile set of the level, keep it short
		tileSetRow->Add<UiLabel>(_x("editor", "Tile set"), UiTheme::TextDim)->Width = 60.0f;
		auto* tileSetBox = tileSetRow->Add<UiTextBox>(settings->TileSet);
		tileSetBox->Width = 120.0f;
		tileSetBox->OnChanged = [settings](StringView text) { settings->TileSet = text; };

		auto* widthRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// TRANSLATORS: Label in the New Level dialog, the width of the level in tiles, keep it short
		widthRow->Add<UiLabel>(_x("editor", "Width"), UiTheme::TextDim)->Width = 60.0f;
		widthRow->Add<UiNumberBox>([settings]() { return settings->Width; }, [settings](float value, bool) { settings->Width = value; },
			16.0f, 1024.0f, 1.0f, 0)->Width = 60.0f;
		auto* heightRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// TRANSLATORS: Label in the New Level dialog, the height of the level in tiles, keep it short
		heightRow->Add<UiLabel>(_x("editor", "Height"), UiTheme::TextDim)->Width = 60.0f;
		heightRow->Add<UiNumberBox>([settings]() { return settings->Height; }, [settings](float value, bool) { settings->Height = value; },
			16.0f, 1024.0f, 1.0f, 0)->Width = 60.0f;

		// TRANSLATORS: Button in the New Level dialog, creates the level
		dialog->AddButton(_x("editor", "Create"), [this, dialogPtr, settings, tileSetBox]() {
			String tileSet = tileSetBox->Text.trimmed();
			std::int32_t tileCount = _context.GetTileSetLibrary().GetTileCount(tileSet);
			if (tileCount < 0) {
				_context.ShowMessage(_x("editor", "The tile set doesn't exist"));
				return;
			}
			dialogPtr->Close();
			LevelDocument document;
			document.CreateNew(tileSet, tileCount, Vector2i(std::int32_t(settings->Width), std::int32_t(settings->Height)));
			// Made by whoever plays as the player name of the options
			document.Properties.Author = PreferencesCache::GetEffectivePlayerName();
			_host.ReplaceDocument(Death::move(document), {}, "unknown/untitled"_s);
		}, true);
		dialog->AddButton(_x("editor", "Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		ShowModal(*_context.GetSurface(), Death::move(dialog));
	}

	void DocumentFiles::Save()
	{
		if (!CanSaveInPlace()) {
			// A level of the game itself is saved as a new one, so the original stays as it is
			ShowSaveAsDialog(false);
			return;
		}
		if (SaveTo(_filePath, false)) {
			// TRANSLATORS: Status message after the level was saved
			_context.ShowMessage(_x("editor", "Saved"));
		} else {
			_context.ShowMessage(_x("editor", "The level couldn't be saved"));
		}
	}

	bool DocumentFiles::SaveTo(StringView path, bool exportOnly)
	{
		// A stroke of a tool writes some of what it does into the level as it goes, a level saved under it would differ
		// from the level once the stroke is cancelled
		_host.PrepareForSave();

		LevelDocument& document = _context.GetDocument();
		// The list of custom levels shows it small, it's made again each time, it's not a change of the level
		{
			SmallVector<std::uint8_t, 0> preview;
			LevelThumbnail::MakePreview(document, _context.GetTileMap(), _context.GetTileSetDataCache(), preview);
			document.SetChunk(Formats::LevelFile::PreviewChunk, preview);
		}

		// Written next to the target first, every write checked and flushed, then it replaces the target in one step,
		// so a failed write --- a full disk too --- never leaves a damaged level behind
		LevelFlags extraFlags = (exportOnly && _exportNotEditable ? LevelFlags::NotEditable : LevelFlags::None);
		if (!AtomicFile::Write(path, [&document, exportOnly, extraFlags](Stream& s) {
			return document.Save(s, exportOnly, 9, extraFlags);
		})) {
			LOGE("Level cannot be saved to \"{}\"", path);
			return false;
		}

		CommandHistory& history = _context.GetHistory();
		if (exportOnly && IsSameFile(path, _filePath)) {
			// The file of the level now lacks what only the editor uses, it has to be saved again to keep it
			history.MarkDirty();
			UpdateWindowTitle();
		}
		if (!exportOnly) {
			history.MarkSaved();
			_autosave.Discard();
			// Saved on purpose, changes kept from before are older than the file now, saving copies goes on
			_recoveryKept = false;
			EditorSettings& settings = _context.GetSettings();
			settings.AddRecentFile(path);
			settings.Save();
			UpdateWindowTitle();
		}
		LOGI("Level saved to \"{}\"", path);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Only in the memory of the game, the browser saves it to the computer
		WebFileBridge::DownloadFile(path);
#endif
		return true;
	}

	bool DocumentFiles::SaveAsFile(StringView path, bool move)
	{
		String previousPath = _filePath;
		// A level that can't be saved where it is, as in the cache, which is rebuilt from the original levels, stays there
		bool removePrevious = (move && CanSaveInPlace() && !IsSameFile(previousPath, path));
		fs::CreateDirectories(fs::GetDirectoryName(path));
		if (!SaveTo(path, false)) {
			return false;
		}

		// The script of the level goes along
		if (!previousPath.empty()) {
			String scriptFrom = fs::CombinePath(fs::GetDirectoryName(previousPath), String(fs::GetFileNameWithoutExtension(previousPath) + ".j2as"_s));
			String scriptTo = fs::CombinePath(fs::GetDirectoryName(path), String(fs::GetFileNameWithoutExtension(path) + ".j2as"_s));
			if (fs::FileExists(scriptFrom) && !IsSameFile(scriptFrom, scriptTo)) {
				if (removePrevious) {
					// The level replaced the file that was there, so its script replaces the script of that one too, the level
					// it belongs to is gone and nothing would be left of it otherwise
					if (fs::FileExists(scriptTo)) {
						LOGI("Script \"{}\" is replaced by the script of the level moved there", scriptTo);
						if (!fs::RemoveFile(scriptTo)) {
							LOGW("Script \"{}\" cannot be replaced, the script of the level stays in \"{}\"", scriptTo, scriptFrom);
						}
					}
					if (!fs::FileExists(scriptTo) && !fs::Move(scriptFrom, scriptTo)) {
						LOGW("Script \"{}\" cannot be moved to \"{}\"", scriptFrom, scriptTo);
					}
				} else if (!fs::FileExists(scriptTo)) {
					fs::Copy(scriptFrom, scriptTo, false);
				}
			}
		}
		SetFile(path, IEditorContext::MakeLevelName(path));
		LoadEventExtensions(false);
		UpdateWindowTitle();
		// The script and the lock go with the file, the lock of the previous file is released
		WatchFiles();
		AcquireLock();

		if (removePrevious) {
			// Its recovery copy was removed by saving, the lock is released already
			if (!fs::RemoveFile(previousPath)) {
				LOGW("Level \"{}\" was saved to \"{}\", but cannot be removed", previousPath, path);
			}
			EditorSettings& settings = _context.GetSettings();
			settings.RemoveRecentFile(previousPath);
			settings.Save();
		}
		return true;
	}

	void DocumentFiles::ShowSaveAsDialog(bool exportOnly, Function<void()>&& onSaved)
	{
		SmallVector<UiFileRoot, 4> roots;
		GetFileRoots(true, roots);
		if (roots.empty()) {
			_context.ShowMessage(_x("editor", "There is no place to save levels to"));
			return;
		}

		// A level of the game goes into the same episode of the user content
		String directory;
		if (CanSaveInPlace()) {
			directory = fs::GetDirectoryName(_filePath);
		} else {
			auto p = _levelName.partition('/');
			directory = fs::CombinePath(roots[0].Path, p[0]);
		}
		String name;
		if (_filePath.empty()) {
			name = "untitled.j2l"_s;
		} else if (exportOnly) {
			// Not the file of the level by default, the export leaves out what only the editor uses
			name = String(fs::GetFileNameWithoutExtension(_filePath) + "-export.j2l"_s);
		} else {
			name = fs::GetFileName(_filePath);
		}

		UiSurface& surface = *_context.GetSurface();
		auto sharedOnSaved = std::make_shared<Function<void()>>(Death::move(onSaved));
		// TRANSLATORS: Titles of the file dialog that exports the level to be played, or saves it under another name
		UiFileDialog* fileDialog = UiFileDialog::Show(surface, exportOnly ? _x("editor", "Export Level") : _x("editor", "Save Level As"), UiFileDialogMode::Save, Death::move(roots), ".j2l"_s,
			directory, name, [this, exportOnly, sharedOnSaved](StringView chosenPath) {
			String path = chosenPath;
			auto save = [this, exportOnly, sharedOnSaved, path]() {
				SaveAsConfirmed(path, exportOnly, sharedOnSaved);
			};

			bool overSource = (exportOnly && IsSameFile(path, _filePath));
			if (overSource && _exportNotEditable) {
				// The editor couldn't open its own level again
				UiMessageDialog::Show(*_context.GetSurface(), _x("editor", "Export Level"), _x("editor", "The level can't be exported over its own file without allowing editing, it couldn't be opened in the editor anymore.\nChoose another file."),
					{ _x("editor", "OK") }, {});
				return;
			}

			// The same level in another place either hides the new file or is hidden by it when the game is played
			String warning;
			SmallVector<UiFileRoot, 4> allRoots;
			GetFileRoots(false, allRoots);
			std::int32_t targetRoot = -1;
			String relative;
			for (std::size_t i = 0; i < allRoots.size(); i++) {
				if (path.hasPrefix(allRoots[i].Path)) {
					targetRoot = std::int32_t(i);
					relative = path.exceptPrefix(allRoots[i].Path.size());
					break;
				}
			}
			for (std::size_t i = 0; i < allRoots.size() && targetRoot >= 0; i++) {
				if (std::int32_t(i) != targetRoot && fs::FileExists(String(allRoots[i].Path + relative))) {
					warning = (std::int32_t(i) > targetRoot
						// TRANSLATORS: Warning when saving a level, {} is the name of a place in the file dialog, e.g. "User content"
						? _fx("editor", "The game will play this level instead of the one in {}.", allRoots[i].Name)
						// TRANSLATORS: Warning when saving a level, {} is the name of a place in the file dialog, e.g. "User content"
						: _fx("editor", "The game will play the level in {} instead of this one.", allRoots[i].Name));
					break;
				}
			}
			if (overSource) {
				String text = _x("editor", "This is the file of the level being edited. The exported level leaves out what only the editor uses - layer names, notes, bookmarks and ground lines - so save the level again to keep them.");
				warning = (warning.empty() ? text : String(text + "\n"_s + warning));
			} else if (fs::FileExists(path)) {
				warning = (warning.empty() ? String(_x("editor", "The file already exists.")) : String(_x("editor", "The file already exists.") + "\n"_s + warning));
			}

			if (warning.empty()) {
				save();
			} else {
				UiMessageDialog::Show(*_context.GetSurface(), exportOnly ? _x("editor", "Export Level") : _x("editor", "Save Level As"), String(warning + "\n"_s + _x("editor", "Do you want to continue?")),
					{ _("Save"), _x("editor", "Cancel") }, [save](std::int32_t button) {
					if (button == 0) {
						save();
					}
				});
			}
		});

		if (exportOnly && fileDialog != nullptr) {
			// Whether the editor can open the exported level again, chosen along with where it goes
			_exportNotEditable = false;
			// TRANSLATORS: Check box in the Export Level dialog, keeps the exported level from being opened in the editor
			auto* notEditable = fileDialog->GetContent()->Add<UiCheckBox>(_x("editor", "Don't allow editing"),
				[this]() { return _exportNotEditable; }, [this](bool value) { _exportNotEditable = value; });
			notEditable->Tooltip = _x("editor", "Keep the exported level from being opened in the editor, keep the level you edit to change it later");
		}
	}

	void DocumentFiles::SaveAsConfirmed(StringView path, bool exportOnly, const std::shared_ptr<Function<void()>>& onSaved)
	{
		auto sharedOnSaved = onSaved;
		{
			bool saved;
			if (exportOnly) {
				fs::CreateDirectories(fs::GetDirectoryName(path));
				saved = SaveTo(path, true);
			} else {
				saved = SaveAsFile(path, false);
			}
			if (!saved) {
				_context.ShowMessage(_x("editor", "The level couldn't be saved"));
				return;
			}
			// TRANSLATORS: Status messages after the level was exported or saved
			_context.ShowMessage(exportOnly ? _x("editor", "Exported") : _x("editor", "Saved"));
			if (sharedOnSaved != nullptr && *sharedOnSaved) {
				(*sharedOnSaved)();
			}
		}
	}

	void DocumentFiles::ShowExportDialog()
	{
		String problem;
		if (!_context.GetDocument().Validate(&problem)) {
			UiMessageDialog::Show(*_context.GetSurface(), _x("editor", "Export Level"), _fx("editor", "The level can't be exported:\n{}", _x("editor", problem.data())), { _x("editor", "OK") }, {});
			return;
		}

		SmallVector<String, 4> warnings;
		CollectExportWarnings(warnings);
		if (warnings.empty()) {
			ShowSaveAsDialog(true);
			return;
		}

		String text;
		for (const String& warning : warnings) {
			text += "- "_s + warning + "\n"_s;
		}
		text += _x("editor", "Do you want to export it anyway?");
		// TRANSLATORS: Title and buttons of the message box listing the problems found before exporting the level
		UiMessageDialog::Show(*_context.GetSurface(), _x("editor", "Export Level"), text, { _x("editor", "Export Anyway"), _x("editor", "Cancel") }, [this](std::int32_t button) {
			if (button == 0) {
				ShowSaveAsDialog(true);
			}
		});
	}

	void DocumentFiles::CollectExportWarnings(SmallVectorImpl<String>& warnings)
	{
		const LevelDocument& document = _context.GetDocument();
		ContentTileSetLibrary& tileSets = _context.GetTileSetLibrary();
		constexpr std::uint16_t LevelStartEvent = 1;
		bool hasStart = false;
		for (const DocumentEvent& event : document.Events) {
			if (event.Record.Type == LevelStartEvent) {
				hasStart = true;
				break;
			}
		}
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			if (event.Event.Type == LevelStartEvent) {
				hasStart = true;
				break;
			}
		}
		if (!hasStart) {
			warnings.push_back(_x("editor", "The level has no start position for players."));
		}

		if (tileSets.GetTileCount(document.Properties.DefaultTileSet) < 0) {
			warnings.push_back(_fx("editor", "The tile set \"{}\" doesn't exist.", document.Properties.DefaultTileSet));
		}
		for (const Formats::ExtraTileSetRecord& tileSet : document.ExtraTileSets) {
			if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && tileSets.GetTileCount(tileSet.Name) < 0) {
				warnings.push_back(_fx("editor", "The tile set \"{}\" doesn't exist.", tileSet.Name));
			}
		}

		auto& resolver = ContentResolver::Get();
		auto episode = _levelName.partition('/')[0];
		for (const String* next : { &document.Properties.NextLevel, &document.Properties.SecretLevel, &document.Properties.BonusLevel }) {
			if (next->empty() || next->hasPrefix(':')) {
				continue;
			}
			String fullName = (next->contains('/') ? *next : String(episode + "/"_s + *next));
			if (!resolver.LevelExists(fullName)) {
				warnings.push_back(_fx("editor", "The level \"{}\" that follows doesn't exist.", *next));
			}
		}
	}

	void DocumentFiles::ShowExportTileSetImageDialog()
	{
		StringView tileSetName = _context.GetDocument().Properties.DefaultTileSet;
		const Formats::TileSetData* tileSet = _context.GetTileSetData(tileSetName);
		if (tileSet == nullptr) {
			_context.ShowMessage(_x("editor", "The tile set can't be read"));
			return;
		}
		SmallVector<UiFileRoot, 4> roots;
		GetImageRoots(true, roots);
		String initialName = fs::GetFileName(tileSetName) + ".png"_s;
		// TRANSLATORS: Title of the file dialog that saves the tiles of a tile set as an image
		UiFileDialog::Show(*_context.GetSurface(), _x("editor", "Export Tile Set Image"), UiFileDialogMode::Save, Death::move(roots), ".png"_s, {}, initialName, [this, tileSet](StringView path) {
			// Rows of 10, as the original tile sets were drawn, the masks into an image of their own next to it
			constexpr std::int32_t Columns = 10;
			PngImage sheet = MakeTileSetSheet(*tileSet, ContentResolver::Get().GetPalettes().data(), Columns);
			PngImage masks = MakeTileSetMaskSheet(*tileSet, Columns);
			String maskPath = PngCodec::GetMasksPath(path);
			// Through temporary files, a failed write leaves the images that were there, and neither is replaced
			// unless both were written whole
			bool saved = AtomicFile::Stage(path, [&sheet](Stream& s) { return PngCodec::Encode(s, sheet); });
			if (saved && !AtomicFile::Stage(maskPath, [&masks](Stream& s) { return PngCodec::Encode(s, masks); })) {
				AtomicFile::Discard(path);
				saved = false;
			}
			if (saved && !AtomicFile::Commit(path)) {
				AtomicFile::Discard(path);
				AtomicFile::Discard(maskPath);
				saved = false;
			}
			if (saved && !AtomicFile::Commit(maskPath)) {
				AtomicFile::Discard(maskPath);
				saved = false;
			}
			if (!saved) {
				_context.ShowMessage(_x("editor", "The images couldn't be saved"));
				return;
			}
#if defined(DEATH_TARGET_EMSCRIPTEN)
			fs::SyncToPersistent();
#endif
			// TRANSLATORS: Status message after exporting a tile set image, {} are the file names of the image and of its masks
			_context.ShowMessage(_fx("editor", "Saved {} and {}", fs::GetFileName(path), fs::GetFileName(maskPath)));
		});
	}

	void DocumentFiles::NoteLevelFileMoved(StringView from, StringView to)
	{
		// In its place in the list, the level is still as recently used as it was
		EditorSettings& settings = _context.GetSettings();
		bool changed = false;
		for (String& recent : settings.RecentFiles) {
			if (IsSameFile(recent, from)) {
				recent = to;
				changed = true;
			}
		}
		if (!changed) {
			return;
		}
		// Only once, the file it went to may have been in the list already
		bool seen = false;
		for (std::size_t i = 0; i < settings.RecentFiles.size(); ) {
			if (IsSameFile(settings.RecentFiles[i], to)) {
				if (seen) {
					settings.RecentFiles.erase(settings.RecentFiles.begin() + i);
					continue;
				}
				seen = true;
			}
			i++;
		}
		settings.Save();
	}

	void DocumentFiles::UpdateAutosave(float timeMult)
	{
		if (!_recoveryKept) {
			_autosave.Update(timeMult, _context.GetDocument(), _context.GetHistory());
		}
	}

	void DocumentFiles::SaveRecoveryCopyNow()
	{
		if (!_recoveryKept) {
			// Written before the application may be ended, not on a thread
			_autosave.SaveNow(_context.GetDocument(), _context.GetHistory(), true);
		}
	}

	String DocumentFiles::GetScriptPath() const
	{
		if (_filePath.empty()) {
			return {};
		}
		return fs::CombinePath(fs::GetDirectoryName(_filePath), String(fs::GetFileNameWithoutExtension(_filePath) + ".j2as"_s));
	}

	void DocumentFiles::EditScript()
	{
		String path = GetScriptPath();
		if (path.empty()) {
			_context.ShowMessage(_x("editor", "Save the level first, its script is kept next to it"));
			return;
		}
		if (!fs::FileExists(path)) {
			// Something to start from, with how the script describes its events for the editor
			String name = fs::GetFileNameWithoutExtension(_filePath);
			String text = "// Script of the level \""_s + name + "\", it runs while the level is played\r\n"_s
				"//\r\n"_s
				"// Events the script reacts to can be described for the level editor, each by a line such as this one,\r\n"_s
				"// without the slashes, see the event packs in Content/Editor/Events for all that can be described:\r\n"_s
				"// #pragma editor-event {\"type\": 255, \"name\": \"My Event\", \"category\": \"other\", \"params\": [{\"name\": \"Speed\", \"type\": \"uint\", \"offset\": 0, \"bits\": 4}]}\r\n"_s
				"\r\n"_s
				"void onLevelBegin()\r\n"_s
				"{\r\n"_s
				"}\r\n"_s;
			if (!AtomicFile::Write(path, text.data(), std::int64_t(text.size()))) {
				_context.ShowMessage(_x("editor", "The script couldn't be made"));
				return;
			}
			_watchedScriptTime = GetModificationTime(path);
		}
		// In the program set in the settings file, if there's one, the events the script describes are read again once
		// it's saved
		StringView scriptEditor = _context.GetSettings().ScriptEditor;
		if (!scriptEditor.trimmed().empty()) {
			String program;
			SmallVector<String, 4> arguments;
			if (SplitEditorCommand(scriptEditor, path, program, arguments) && StartProgram(program, arguments)) {
				return;
			}
			LOGW("Script editor \"{}\" cannot be started", scriptEditor);
			_context.ShowMessage(_fx("editor", "\"{}\" couldn't be started, the script is opened as the system opens it", program.empty() ? String(scriptEditor) : program));
		}
		// Otherwise in whatever the system opens such files with
		String url = "file:///"_s + fs::FromNativeSeparators(path);
		if (!theApplication().OpenUrl(url)) {
			_context.ShowMessage(_fx("editor", "The script is in \"{}\"", path));
		}
	}

	void DocumentFiles::LoadEventExtensions(bool packs)
	{
		auto readText = [](StringView path) -> String {
			auto s = fs::Open(path, FileAccess::Read);
			std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
			if (size <= 0 || size > 4 * 1024 * 1024) {
				return {};
			}
			String text{NoInit, std::size_t(size)};
			std::int64_t read = s->Read(text.data(), size);
			if (read != size) {
				LOGW("File \"{}\" couldn't be read whole", path);
				return {};
			}
			return text;
		};

		std::int32_t count = 0;
		auto& resolver = ContentResolver::Get();
		if (packs) {
			// Event packs of the content, then of the user content, which can describe the same events differently
			EventCatalog::ClearExtensions(EventExtensionScope::Global);
			SmallVector<String, 2> directories;
			directories.push_back(fs::CombinePath({ resolver.GetContentPath(), "Editor"_s, "Events"_s }));
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				directories.push_back(fs::CombinePath({ userContent, "Editor"_s, "Events"_s }));
			}
			for (const String& directory : directories) {
				if (!fs::DirectoryExists(directory)) {
					continue;
				}
				for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
					if (StringUtils::lowercase(item).hasSuffix(".json"_s)) {
						count += EventCatalog::LoadExtensions(readText(item), item, EventExtensionScope::Global);
					}
				}
			}
		}

		// The script of the level describes the events it reacts to, each by a line "#pragma editor-event {...}"
		EventCatalog::ClearExtensions(EventExtensionScope::Level);
		String scriptPath = GetScriptPath();
		if ((scriptPath.empty() || !fs::IsReadableFile(scriptPath)) && !_levelName.empty()) {
			scriptPath = resolver.FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(_levelName) + ".j2as"_s)));
		}
		if (!scriptPath.empty() && fs::IsReadableFile(scriptPath)) {
			String script = readText(scriptPath);
			constexpr StringView Pragma = "#pragma editor-event"_s;
			StringView rest = script;
			while (!rest.empty()) {
				auto parts = rest.partition('\n');
				StringView line = parts[0].trimmed();
				rest = parts[2];
				if (line.hasPrefix(Pragma)) {
					count += EventCatalog::LoadExtensions(line.exceptPrefix(Pragma.size()).trimmed(), scriptPath, EventExtensionScope::Level);
				}
			}
		}
		if (count > 0) {
			LOGI("{} events described from outside of the game", count);
		}
		// Shown only once the interface exists, see EditorSession::Initialize() for the first time
		ShowEventExtensionErrors();
	}

	void DocumentFiles::ShowEventExtensionErrors()
	{
		std::int32_t errors = std::int32_t(EventCatalog::GetExtensionErrors().size());
		if (errors > 0 && _context.GetSurface() != nullptr) {
			_context.ShowMessage(_fnx("editor", "{} event description couldn't be read, it's left out", "{} event descriptions couldn't be read, they're left out", errors, errors));
		}
	}

	void DocumentFiles::WatchFiles()
	{
		_watchedScriptTime = GetModificationTime(GetScriptPath());
		_watchedTileSets.clear();
		auto watchTileSet = [this](StringView name) {
			String path = ContentTileSetLibrary::FindTileSetFile(name);
			if (!path.empty()) {
				WatchedTileSet& watched = _watchedTileSets.emplace_back();
				watched.Name = name;
				watched.Path = Death::move(path);
				watched.Time = GetModificationTime(watched.Path);
			}
		};
		const LevelDocument& document = _context.GetDocument();
		watchTileSet(document.Properties.DefaultTileSet);
		for (const Formats::ExtraTileSetRecord& record : document.ExtraTileSets) {
			if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
				watchTileSet(record.Name);
			}
		}
		_watchTime = 0.0f;
	}

	bool DocumentFiles::UpdateFileWatches(float timeMult)
	{
		_watchTime += timeMult;
		_lockRefreshTime += timeMult;
		if (_lockRefreshTime >= LockRefreshInterval && !_lockPath.empty()) {
			_lockRefreshTime = 0.0f;
			WriteLock();
		}
		if (_watchTime < WatchInterval) {
			return false;
		}
		_watchTime = 0.0f;

		// The events the script describes are read again once it's saved, a kept play-test runs the old script, so it
		// starts again when it's resumed
		std::int64_t scriptTime = GetModificationTime(GetScriptPath());
		if (scriptTime != _watchedScriptTime) {
			bool known = (_watchedScriptTime != 0);
			_watchedScriptTime = scriptTime;
			if (known || scriptTime != 0) {
				LoadEventExtensions(false);
				_host.OnScriptChanged();
				_context.ShowMessage(_x("editor", "The script of the level changed, the events it describes were read again"));
			}
		}

		// A tile set changed by another program, asked about once, while nothing else is asked
		UiSurface* surface = _context.GetSurface();
		if (surface == nullptr || surface->HasModal() || _context.IsPlaying()) {
			return true;
		}
		for (WatchedTileSet& watched : _watchedTileSets) {
			std::int64_t time = GetModificationTime(watched.Path);
			if (time == watched.Time || time == 0) {
				continue;
			}
			watched.Time = time;
			String name = watched.Name;
			UiMessageDialog::Show(*surface, _x("editor", "Tile Set Changed"), _fx("editor", "The tile set \"{}\" was changed outside of the editor.\nDo you want to load it again?", name),
				// TRANSLATORS: Buttons of the message box about a tile set changed outside of the editor
				{ _x("editor", "Load Again"), _x("editor", "Not Now") }, [this, name](std::int32_t button) {
				if (button == 0) {
					_host.ReloadTileSet(name);
					_context.ShowMessage(_fx("editor", "The tile set \"{}\" was loaded again", name));
				}
			});
			break;
		}
		return true;
	}

	void DocumentFiles::NoteTileSetWritten(StringView name)
	{
		// Written by the editor itself, not to be asked about
		for (WatchedTileSet& watched : _watchedTileSets) {
			if (watched.Name == name) {
				watched.Time = GetModificationTime(watched.Path);
			}
		}
	}

	void DocumentFiles::AcquireLock()
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		ReleaseLock();
		// A level that can't be saved where it is (e.g. in the cache) is saved under another name, nothing to guard
		if (_filePath.empty() || !CanSaveInPlace()) {
			return;
		}
		if (_lockId.empty()) {
			// Tells this editor from the others, on this computer and on others sharing the folder
			char id[40];
			std::snprintf(id, sizeof(id), "%u/%016llx", (unsigned)GetOwnProcessId(), (unsigned long long)(std::uint64_t(std::uintptr_t(this)) ^ std::uint64_t(GetNowMilliseconds())));
			_lockId = String(Application::GetDeviceHostname() + "/"_s + StringView(id));
		}

		String lockPath = _filePath + ".lock"_s;
		std::int64_t lockTime = GetModificationTime(lockPath);
		if (lockTime != 0 && GetNowMilliseconds() - lockTime < LockTimeout) {
			String holder = ReadSmallFile(lockPath);
			// The session the level was open in before, when it's opened again in this editor, holds it still, the new
			// one is created before the old one is gone (see GameEventHandler::OpenEditor()) and takes the lock over
			UiSurface* surface = _context.GetSurface();
			if (!holder.empty() && holder != _lockId && !IsOwnProcessLock(holder) && !IsLockLeftBehind(holder) && surface != nullptr) {
				// Another editor writes it every half a minute, so it's still open there
				UiMessageDialog::Show(*surface, _x("editor", "Level Open Elsewhere"), _x("editor", "The level seems to be open in another editor, the changes made in one would be lost by saving the other.\nDo you want to edit it anyway?"),
					// TRANSLATORS: Buttons of the message box about the level being open in another editor
					{ _x("editor", "Edit Anyway"), _x("editor", "Open Another Level") }, [this](std::int32_t button) {
					if (button == 1) {
						ConfirmDiscard([this]() { ShowOpenDialog(); });
					}
				});
			}
		}
		_lockPath = Death::move(lockPath);
		_lockRefreshTime = 0.0f;
		WriteLock();
#endif
	}

	void DocumentFiles::WriteLock()
	{
		// A folder that can't be written to has no lock, it's only a warning anyway
		if (!_lockPath.empty() && !AtomicFile::Write(_lockPath, _lockId.data(), std::int64_t(_lockId.size()))) {
			_lockPath = {};
		}
	}

	void DocumentFiles::ReleaseLock()
	{
		if (_lockPath.empty()) {
			return;
		}
		// Only the own lock, another editor may have taken it over
		if (ReadSmallFile(_lockPath) == _lockId) {
			fs::RemoveFile(_lockPath);
		}
		_lockPath = {};
	}

	void DocumentFiles::UpdateWindowTitle()
	{
		// Called for every change, as each step of a stroke, the title is made again only when what it shows changed
		const LevelDocument& document = _context.GetDocument();
		bool dirty = _context.GetHistory().IsDirty();
		if (!_windowTitle.empty() && dirty == _windowTitleDirty && document.Properties.DisplayName == _windowTitleName && _filePath == _windowTitlePath) {
			return;
		}
		_windowTitleDirty = dirty;
		_windowTitleName = document.Properties.DisplayName;
		_windowTitlePath = _filePath;

		String name = (!document.Properties.DisplayName.empty() ? document.Properties.DisplayName : String(fs::GetFileName(_filePath)));
		if (dirty) {
			name += "*"_s;
		}
		// The window is told only when the title is another one
		String title = String(NCINE_APP_NAME " - "_s + _("Level Editor") + " - "_s + name);
		if (title != _windowTitle) {
			theApplication().GetGfxDevice().setWindowTitle(title);
			_windowTitle = Death::move(title);
		}
	}
}

#endif
