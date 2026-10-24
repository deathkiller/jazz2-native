#pragma once

// What the files of the implementation of EditorSession share, not to be included anywhere else

#include "EditorSession.h"

#if defined(WITH_EDITOR)

#include "Document/Commands.h"
#include "Document/DocumentViewState.h"
#include "Files/EpisodeFile.h"
#include "Imaging/AccentColor.h"
#include "Runtime/EditorLevelHandler.h"
#include "Runtime/MaskOverlay.h"
#include "Runtime/PlayTestController.h"
#include "Runtime/PlayTestLevelHandler.h"
#if defined(WITH_MULTIPLAYER)
#	include "Runtime/PlayTestMpLevelHandler.h"
#endif
#include "Runtime/PlayTransition.h"
#include "Runtime/RuntimeSync.h"
#include "Runtime/TileLod.h"
#include "Session/PlayOverlays.h"
#include "Session/SavedComparison.h"
#if defined(DEATH_DEBUG)
#	include "Session/EditorDebugConsole.h"
#endif
#include "Shell/EditorShell.h"
#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include "Files/WebFileBridge.h"
#endif
#include "Shell/LayerDialogs.h"
#include "Shell/LevelDialogs.h"
#include "Shell/FindReplaceDialog.h"
#include "Shell/FxTileGenerator.h"
#include "Shell/HelpDialogs.h"
#include "Shell/LevelCheck.h"
#include "Shell/LevelStatistics.h"
#include "Shell/Navigator.h"
#include "Shell/PrefabsDialog.h"
#include "Shell/SmartFill.h"
#include "Shell/TerrainRulesDialog.h"
#include "Shell/PaletteDialog.h"
#include "Shell/TileEditor.h"
#include "Shell/TileSetApplyDialog.h"
#include "Shell/TileSetLayoutDialog.h"
#include "Shell/TileSetImportDialog.h"
#include "Shell/TileSetsDialog.h"
#include "Shell/ShellPanels.h"
#include "Tools/BlockGrab.h"
#include "Tools/EditorTool.h"
#include "Tools/GroundLineTool.h"
#include "Tools/TileTools.h"
#include "UI/UiDialogs.h"
#include "UI/UiSurface.h"
#include "../ContentResolver.h"
#include "../Tiles/TileCollisionParams.h"
#include "../Formats/TileSetFile.h"
#include "../PreferencesCache.h"
#include "../Tiles/TileMap.h"

#include "../../nCine/Application.h"
#include "../../nCine/I18n.h"
#include "../../nCine/Graphics/IGfxDevice.h"
#include "../../nCine/Graphics/Viewport.h"
#include "../../nCine/Input/IInputManager.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>

using namespace Death::IO;

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		constexpr std::size_t HistoryMemoryBudget = 8 * 1024 * 1024;
#else
		constexpr std::size_t HistoryMemoryBudget = 64 * 1024 * 1024;
#endif
	}
}

#endif
