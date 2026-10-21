#include "RecoveryDialog.h"

#if defined(WITH_EDITOR)

#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Document/CommandArchive.h"
#include "../Files/AutosaveService.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using Death::Containers::DateTime;

namespace Jazz2::Editor
{
	namespace
	{
		struct RecoveryCopy
		{
			String Path;
			String DisplayName;
			// The level file the copy belongs to, empty for a level that wasn't saved
			String SourcePath;
			DateTime Modified;
			std::int64_t Size = 0;
			bool HasHistory = false;
		};

		struct RecoveryState
		{
			SmallVector<RecoveryCopy, 0> Copies;
			std::uint32_t Generation = 0;
		};

		String GetRecoveryDirectory()
		{
			return fs::CombinePath({ PreferencesCache::GetDirectory(), "Editor"_s, "Recovery"_s });
		}

		void ReadCopies(RecoveryState& state)
		{
			state.Copies.clear();
			String directory = GetRecoveryDirectory();
			if (fs::DirectoryExists(directory)) {
				for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
					// Temporary files of a copy being written are left out
					if (StringUtils::lowercase(fs::GetExtension(item)) != "j2l"_s) {
						continue;
					}
					RecoveryCopy copy;
					copy.Path = item;
					copy.Modified = fs::GetLastModificationTime(item);
					copy.Size = fs::GetFileSize(item);

					auto s = fs::Open(item, FileAccess::Read);
					Formats::LevelHeader header;
					if (s->IsValid() && Formats::LevelFile::ReadHeader(*s, header)) {
						Formats::LevelReader reader(*s, header);
						Formats::LevelProperties properties;
						reader.ReadProperties(properties);
						copy.DisplayName = Death::move(properties.DisplayName);
						// Only the path is kept, of the history it's enough to know whether there's any, it can take megabytes
						SmallVector<Formats::ChunkRecord, 0> chunks;
						if (reader.SkipToChunks()) {
							reader.ReadSelectedChunks(chunks, [&copy](std::uint32_t tag, std::uint32_t size) {
								if (tag == CommandArchive::Chunk) {
									copy.HasHistory = (size > 0);
								}
								return (tag == AutosaveService::RecoveryChunk);
							});
						}
						for (const Formats::ChunkRecord& chunk : chunks) {
							if (chunk.Tag == AutosaveService::RecoveryChunk) {
								copy.SourcePath = String(reinterpret_cast<const char*>(chunk.Data.data()), chunk.Data.size());
							}
						}
					}
					state.Copies.push_back(Death::move(copy));
				}
			}
			// The newest first
			std::sort(state.Copies.begin(), state.Copies.end(), [](const RecoveryCopy& a, const RecoveryCopy& b) {
				return (a.Modified.ToUnixMilliseconds() > b.Modified.ToUnixMilliseconds());
			});
			state.Generation++;
		}

		String FormatTime(const DateTime& time)
		{
			DateTime::Tm tm = time.Partitioned();
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d", tm.Year, tm.Month + 1, tm.Day, tm.Hour, tm.Minute);
			return buffer;
		}

		/** The recovery copies, rebuilt when they're read again */
		class RecoveryList : public UiStack
		{
		public:
			RecoveryList(IEditorContext& session, std::shared_ptr<RecoveryState> state, UiDialog* dialog)
				: UiStack(UiOrientation::Vertical, 3.0f, UiThickness(0.0f, 0.0f, 3.0f, 0.0f)), _session(session), _state(Death::move(state)),
					_dialog(dialog), _shownGeneration(UINT32_MAX)
			{
			}

			void Update(float timeMult) override
			{
				if (_state->Generation != _shownGeneration) {
					Rebuild();
				}
				UiStack::Update(timeMult);
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<RecoveryState> _state;
			UiDialog* _dialog;
			std::uint32_t _shownGeneration;

			void Rebuild()
			{
				_shownGeneration = _state->Generation;
				ClearChildren();
				if (_state->Copies.empty()) {
					Add<UiLabel>(_x("editor", "There are no recovery copies, all changes were saved or discarded"), UiTheme::TextDisabled);
					return;
				}
				IEditorContext* s = &_session;
				std::shared_ptr<RecoveryState> state = _state;
				UiDialog* dialog = _dialog;
				for (const RecoveryCopy& copy : _state->Copies) {
					auto* row = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
					// TRANSLATORS: In the Recovery Copies dialog instead of the name of a level that has none
					auto* label = row->Add<UiLabel>(copy.DisplayName.empty() ? String(_x("editor", "Unnamed level")) : copy.DisplayName, UiTheme::Text);
					label->Flex = 1.0f;
					String path = copy.Path;
					// TRANSLATORS: Button next to a recovery copy in the Recovery Copies dialog, opens the level as the copy has it
					row->Add<UiButton>(_x("editor", "Restore"), EditorIcon::None, [s, dialog, path]() {
						dialog->Close();
						s->OpenRecoveryCopy(path);
					})->Tooltip = _x("editor", "Edit the level as the copy has it, with the history of its changes");
					row->Add<UiButton>(StringView(), EditorIcon::Delete, [s, state, path]() {
						UiSurface* surface = s->GetSurface();
						if (surface == nullptr) {
							return;
						}
						// TRANSLATORS: Title and buttons of the message confirming that a recovery copy is removed
						UiMessageDialog::Show(*surface, _x("editor", "Recovery Copies"), _x("editor", "The changes in this copy will be lost. Remove it?"), { _x("editor", "Remove"), _x("editor", "Cancel") },
							[state, path](std::int32_t button) {
							if (button == 0) {
								fs::RemoveFile(path);
#if defined(DEATH_TARGET_EMSCRIPTEN)
								fs::SyncToPersistent();
#endif
								ReadCopies(*state);
							}
						});
					})->Tooltip = _x("editor", "Remove the copy");

					String details;
					if (copy.SourcePath.empty()) {
						details = _x("editor", "A level that wasn't saved yet");
					} else if (!fs::FileExists(copy.SourcePath)) {
						// TRANSLATORS: In parentheses after the file of a level in the Recovery Copies dialog, the file doesn't exist anymore
						details = String(copy.SourcePath + " ("_s + _x("editor", "the file is gone") + ")"_s);
					} else {
						details = copy.SourcePath;
					}
					details = String(details + ", "_s + FormatTime(copy.Modified));
					if (copy.HasHistory) {
						// TRANSLATORS: At the end of the details of a recovery copy in the Recovery Copies dialog, the copy has the undo history of its changes too
						details = String(details + ", "_s + _x("editor", "with history"));
					}
					Add<UiLabel>(details, UiTheme::TextDim);
				}
			}
		};
	}

	void ShowRecoveryDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		auto state = std::make_shared<RecoveryState>();
		ReadCopies(*state);

		// TRANSLATORS: Title of the dialog listing the copies of levels with changes that weren't saved, kept in case the game ended unexpectedly
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Recovery Copies"));
		UiDialog* dialogPtr = dialog.get();
		UiStack* content = dialog->GetContent();
		content->Add<UiLabel>(_x("editor", "Changes that weren't saved, kept in case the game ended unexpectedly"), UiTheme::TextDim);
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 420.0f;
		scroll->Height = 200.0f;
		scroll->SetContent<RecoveryList>(session, state, dialogPtr);

		ShowModal(*surface, Death::move(dialog));
	}
}

#endif
