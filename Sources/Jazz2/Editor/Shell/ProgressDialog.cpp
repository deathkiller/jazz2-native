#include "ProgressDialog.h"

#if defined(WITH_EDITOR)

#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		/** A bar of how far the work is, it closes the window when the work is done */
		class JobProgressBar : public UiElement
		{
		public:
			JobProgressBar(std::shared_ptr<BackgroundJob> job, Function<void(bool)>&& onDone, Function<void()>&& close)
				: _job(Death::move(job)), _onDone(Death::move(onDone)), _close(Death::move(close)), _finished(false)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(context.Px(240.0f), context.Px(12.0f));
			}

			void Update(float timeMult) override
			{
				UiElement::Update(timeMult);
				if (_finished || !_job->IsDone()) {
					return;
				}
				_finished = true;
				_job->Wait();
				bool cancelled = _job->GetProgress().IsCancelled();
				// The window is closed first, the results may open another one
				_close();
				if (_onDone) {
					_onDone(cancelled);
				}
			}

			void Draw(UiPainter& painter) override
			{
				painter.RoundRect(Bounds, UiTheme::InputFill);
				float value = std::clamp(_job->GetProgress().GetValue(), 0.0f, 1.0f);
				if (value > 0.0f) {
					painter.RoundRect(Rectf(Bounds.X, Bounds.Y, Bounds.W * value, Bounds.H), UiTheme::Accent);
				}
			}

		private:
			// Shared with the window, the job is stopped and waited for when the window goes away before it ended
			std::shared_ptr<BackgroundJob> _job;
			Function<void(bool)> _onDone;
			Function<void()> _close;
			bool _finished;
		};
	}

	void RunWithProgress(IEditorContext& session, StringView title, Function<void(JobProgress&)>&& work, Function<void(bool)>&& onDone)
	{
		auto job = std::make_shared<BackgroundJob>();

#if defined(NCINE_BACKGROUND_JOB_THREADED)
		UiSurface* surface = session.GetSurface();
		if (surface != nullptr) {
			auto dialog = std::make_unique<UiDialog>(title);
			UiDialog* dialogPtr = dialog.get();
			auto* content = dialog->GetContent();
			content->Add<UiLabel>(_x("editor", "It may take a moment for a large image or many levels"), UiTheme::TextDim);
			content->Add<JobProgressBar>(job, Death::move(onDone), [dialogPtr]() { dialogPtr->Close(); });
			// TRANSLATORS: Button in the dialog with the progress of a long task, cancels the task
			dialog->AddButton(_x("editor", "Stop"), [job]() { job->Cancel(); }, true);

			ShowModal(*surface, Death::move(dialog));

			// A thread that can't be started does the work right away, the window closes with it as it would after the thread
			job->Start(Death::move(work), "Editor job");
			return;
		}
#endif

		// Without threads, the user interface waits for it
		job->RunHere(Death::move(work));
		if (onDone) {
			onDone(job->GetProgress().IsCancelled());
		}
	}

	namespace
	{
		// A path as it's compared, without the separators at its end
		String NormalizeComparedPath(StringView path)
		{
			String normalized = StringUtils::lowercase(fs::FromNativeSeparators(path));
			std::size_t length = normalized.size();
			while (length > 1 && normalized[length - 1] == '/') {
				length--;
			}
			return (length == normalized.size() ? normalized : String(normalized.prefix(length)));
		}
	}

	bool IsPathInside(StringView path, StringView root)
	{
		if (path.empty() || root.empty()) {
			return false;
		}
		String a = NormalizeComparedPath(path);
		String b = NormalizeComparedPath(root);
		if (!a.hasPrefix(b)) {
			return false;
		}
		// A root of just "/" ends with the separator already
		return (a.size() == b.size() || b[b.size() - 1] == '/' || a[b.size()] == '/');
	}

	bool IsLevelOpenElsewhere(StringView levelPath, StringView ownPath)
	{
		if (levelPath.empty()) {
			return false;
		}
		auto normalize = [](StringView path) {
			String absolute = fs::GetAbsolutePath(path);
			return NormalizeComparedPath(absolute.empty() ? path : StringView(absolute));
		};
		// The lock of the level being edited is the own one
		if (!ownPath.empty() && normalize(levelPath) == normalize(ownPath)) {
			return false;
		}
		// A lock left by an editor that crashed doesn't count
		return IEditorContext::IsLevelLockedElsewhere(levelPath);
	}
}

#endif
