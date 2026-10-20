#pragma once

#if (defined(WITH_EDITOR) && defined(DEATH_DEBUG)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

using namespace nCine;

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	class DocumentFiles;
	class EditorInputRouter;
	class EditorSession;
	class TileLod;

	/**
		@brief Commands to test the level editor without input, only in debug builds

		The commands given on the command line by `/console <command>` run in order once the view of the editor exists,
		`/wait <frames>` delays the rest and `/quit` ends the game. They go through the editor as the user would, mostly
		through the same functions the input and the actions use. Also adds up the frame statistics for `/perf` and moves
		the view for `/drift`.
	*/
	class EditorDebugConsole
	{
	public:
		/** @brief Creates the commands of an editor, with what they reach past the public interface of the session */
		EditorDebugConsole(EditorSession& session, DocumentFiles& files, EditorInputRouter& input, TileLod& tileLod);
		~EditorDebugConsole();

		EditorDebugConsole(const EditorDebugConsole&) = delete;
		EditorDebugConsole& operator=(const EditorDebugConsole&) = delete;

		/** @brief Takes the commands given on the command line by `/console <command>`, they're run from the second frame */
		void ReadStartupCommands();
		/** @brief Runs the commands from the command line that are due, adds up the frame statistics and moves the view, every frame */
		void Update();
		/** @brief Runs a debug command, returns `false` if it's unknown or it failed */
		bool Execute(StringView line);
		/** @brief Returns whether the images of the layers from far away are forced on (1) or off (0) by `/lod`, -1 by the zoom as usual */
		std::int32_t GetLodMode() const {
			return _lodMode;
		}

	private:
		// The words of a debug command and a command with what it does
		struct Args;
		struct Command;

		// Frame statistics added up since `/perf reset`, from each snapshot once, see `/perf log`
		struct Perf
		{
			bool Running = false;
			std::uint32_t Sequence = 0;
			std::uint32_t Frames = 0;
			double FrameTime = 0.0;
			float MaxFrameTime = 0.0f;
			double PhaseTimes[16] = {};
			double DrawCalls = 0.0;
			double RenderCommands = 0.0;
			double Counters[8] = {};
			const char* CounterNames[8] = {};
		};

		EditorSession& _session;
		DocumentFiles& _files;
		EditorInputRouter& _input;
		TileLod& _tileLod;
		// Commands given on the command line by `/console <command>`, run once the view exists
		SmallVector<String, 0> _startupCommands;
		std::int32_t _frame = 0;
		std::int32_t _waitFrames = 0;
		// See GetLodMode()
		std::int32_t _lodMode = -1;
		Perf _perf;
		// Screen pixels `/drift` moves the view by every frame, back and forth, so it's measured as it's moved
		Vector2f _drift;
		float _driftZoom = 1.0f;
		std::int32_t _driftFrame = 0;
		std::int32_t _driftPeriod = 30;

		// Adds the snapshot of the frame statistics to `/perf` when there's a new one, moves the view by `/drift`
		void UpdatePerf();
		// The commands of Execute() by topic: the view, editing, events, files, play-tests, input as the user gives it,
		// tile sets and what is logged to check the editor
		void AddViewCommands(SmallVectorImpl<Command>& commands);
		void AddEditCommands(SmallVectorImpl<Command>& commands);
		void AddEventCommands(SmallVectorImpl<Command>& commands);
		void AddFileCommands(SmallVectorImpl<Command>& commands);
		void AddPlayTestCommands(SmallVectorImpl<Command>& commands);
		void AddInputCommands(SmallVectorImpl<Command>& commands);
		void AddTileSetCommands(SmallVectorImpl<Command>& commands);
		void AddDiagnosticCommands(SmallVectorImpl<Command>& commands);
	};
}

#endif
