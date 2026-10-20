#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	/**
		@brief Identifiers of the actions of the level editor

		Every place that finds an action by its identifier (@ref ActionRegistry::Find(), @ref ActionRegistry::Execute(),
		the menus, the toolbar, the options of the tools, the presets of the shortcuts) names it by one of these, so a
		typo is a compile error instead of an item that silently goes missing. The identifiers are kept in the file of
		the shortcuts the user changed, so they never change between versions. They're plain C strings, they fit the
		tables of `const char*` the menus are made from, a `nullptr` among them being a separator.
	*/
	namespace ActionIds
	{
		// Areas of identifiers, actions of them don't end what the pointer is in the middle of, see EditorSession::CreateActions()
		constexpr const char* ViewPrefix = "view.";
		constexpr const char* HelpPrefix = "help.";

		// File
		constexpr const char* FileNew = "file.new";
		constexpr const char* FileOpen = "file.open";
		constexpr const char* FileSave = "file.save";
		constexpr const char* FileSaveAs = "file.saveAs";
		constexpr const char* FileExport = "file.export";
		constexpr const char* FileExportPackage = "file.exportPackage";
		constexpr const char* FileRecovery = "file.recovery";
		constexpr const char* FileExit = "file.exit";

		// Edit
		constexpr const char* EditUndo = "edit.undo";
		constexpr const char* EditRedo = "edit.redo";
		constexpr const char* EditCut = "edit.cut";
		constexpr const char* EditCopy = "edit.copy";
		constexpr const char* EditPaste = "edit.paste";
		constexpr const char* EditDelete = "edit.delete";
		constexpr const char* EditSelectAll = "edit.selectAll";
		constexpr const char* EditDeselect = "edit.deselect";
		constexpr const char* EditHistory = "edit.history";
		constexpr const char* EditFindReplace = "edit.findReplace";
		constexpr const char* EditPrefabs = "edit.prefabs";
		constexpr const char* EditFillGround = "edit.fillGround";
		constexpr const char* EditBeautify = "edit.beautify";
		constexpr const char* EditBakeGround = "edit.bakeGround";
		constexpr const char* EditContinueRow = "edit.continueRow";
		constexpr const char* EditTerrainRules = "edit.terrainRules";
		constexpr const char* EditSmartRetile = "edit.smartRetile";
		constexpr const char* EditTraceGround = "edit.traceGround";
		constexpr const char* EditMirrorH = "edit.mirrorH";
		constexpr const char* EditMirrorV = "edit.mirrorV";
		constexpr const char* EditMirrorSwapTeams = "edit.mirrorSwapTeams";
		constexpr const char* EditCopyLayers = "edit.copyLayers";
		constexpr const char* EditPasteLayers = "edit.pasteLayers";
		constexpr const char* EditModifierNormal = "edit.modifierNormal";
		constexpr const char* EditModifierTranslucent = "edit.modifierTranslucent";
		constexpr const char* EditModifierInvisible = "edit.modifierInvisible";
		constexpr const char* EditModifierHeat = "edit.modifierHeat";

		// Selection
		constexpr const char* SelectInvert = "select.invert";
		constexpr const char* SelectGrow = "select.grow";
		constexpr const char* SelectShrink = "select.shrink";
		constexpr const char* SelectSameTiles = "select.sameTiles";
		constexpr const char* SelectMoveLeft = "select.moveLeft";
		constexpr const char* SelectMoveRight = "select.moveRight";
		constexpr const char* SelectMoveUp = "select.moveUp";
		constexpr const char* SelectMoveDown = "select.moveDown";

		// Events
		constexpr const char* EventsSelectSameType = "events.selectSameType";
		constexpr const char* EventsCopyToMatchingTiles = "events.copyToMatchingTiles";
		constexpr const char* EventsFillSelection = "events.fillSelection";
		constexpr const char* EventsList = "events.list";
		constexpr const char* EventsModePaint = "events.modePaint";
		constexpr const char* EventsModeRectangle = "events.modeRectangle";
		constexpr const char* EventsModeFill = "events.modeFill";
		constexpr const char* EventsModeSelect = "events.modeSelect";

		// Stamp
		constexpr const char* StampFlipX = "stamp.flipX";
		constexpr const char* StampFlipY = "stamp.flipY";
		constexpr const char* StampRandom = "stamp.random";
		constexpr const char* StampReroll = "stamp.reroll";
		constexpr const char* StampRotate = "stamp.rotate";
		constexpr const char* StampRotateCCW = "stamp.rotateCCW";
		constexpr const char* StampRandomFlipX = "stamp.randomFlipX";
		constexpr const char* StampRandomFlipY = "stamp.randomFlipY";
		constexpr const char* StampWeights = "stamp.weights";

		// Tools
		constexpr const char* ToolBrush = "tool.brush";
		constexpr const char* ToolEraser = "tool.eraser";
		constexpr const char* ToolRectangle = "tool.rectangle";
		constexpr const char* ToolFill = "tool.fill";
		constexpr const char* ToolEyedropper = "tool.eyedropper";
		constexpr const char* ToolSelect = "tool.select";
		constexpr const char* ToolWand = "tool.wand";
		constexpr const char* ToolHand = "tool.hand";
		constexpr const char* ToolEvent = "tool.event";
		constexpr const char* ToolMeasure = "tool.measure";
		constexpr const char* ToolPath = "tool.path";
		constexpr const char* ToolSmart = "tool.smart";
		constexpr const char* ToolGround = "tool.ground";
		constexpr const char* ToolShape = "tool.shape";

		// Options of the tools
		constexpr const char* ToolsModifierKeep = "tools.modifierKeep";
		constexpr const char* ToolsModifierNormal = "tools.modifierNormal";
		constexpr const char* ToolsModifierTranslucent = "tools.modifierTranslucent";
		constexpr const char* ToolsModifierInvisible = "tools.modifierInvisible";
		constexpr const char* ToolsModifierHeat = "tools.modifierHeat";
		constexpr const char* ToolsModifierOnly = "tools.modifierOnly";
		constexpr const char* ToolsAcrossSeams = "tools.acrossSeams";
		constexpr const char* ToolsIgnoreFlips = "tools.ignoreFlips";
		constexpr const char* ToolsOnlyEmpty = "tools.onlyEmpty";
		constexpr const char* ToolsWandGlobal = "tools.wandGlobal";
		constexpr const char* ShapeLine = "shape.line";
		constexpr const char* ShapeRectangle = "shape.rectangle";
		constexpr const char* ShapeEllipse = "shape.ellipse";
		constexpr const char* ShapeFilledEllipse = "shape.filledEllipse";
		constexpr const char* MeasureWalk = "measure.walk";
		constexpr const char* MeasureDash = "measure.dash";
		constexpr const char* MeasureDoubleJump = "measure.doubleJump";
		constexpr const char* MeasureCopter = "measure.copter";
		constexpr const char* MeasureCollisions = "measure.collisions";

		// View and Show
		constexpr const char* ViewNavigator = "view.navigator";
		constexpr const char* ViewAddNote = "view.addNote";
		constexpr const char* ViewGrid = "view.grid";
		constexpr const char* ViewFlat = "view.flat";
		constexpr const char* ViewMasks = "view.masks";
		constexpr const char* ViewEvents = "view.events";
		constexpr const char* ViewPauseLayers = "view.pauseLayers";
		constexpr const char* ViewZoomIn = "view.zoomIn";
		constexpr const char* ViewZoomOut = "view.zoomOut";
		constexpr const char* ViewZoomGame = "view.zoomGame";
		constexpr const char* ViewFit = "view.fit";
		constexpr const char* ViewUiLarger = "view.uiLarger";
		constexpr const char* ViewUiSmaller = "view.uiSmaller";
		constexpr const char* ViewUiAuto = "view.uiAuto";
		constexpr const char* ViewLayoutAuto = "view.layoutAuto";
		constexpr const char* ViewLayoutDesktop = "view.layoutDesktop";
		constexpr const char* ViewLayoutCompact = "view.layoutCompact";
		constexpr const char* ViewExpert = "view.expert";
		constexpr const char* ViewHighContrast = "view.highContrast";
		constexpr const char* ViewReducedMotion = "view.reducedMotion";
		constexpr const char* ViewDrawerTiles = "view.drawerTiles";
		constexpr const char* ViewDrawerLayers = "view.drawerLayers";
		constexpr const char* ViewPlayPath = "view.playPath";
		constexpr const char* ViewDeaths = "view.deaths";
		constexpr const char* ViewReachability = "view.reachability";
		constexpr const char* ViewReachAny = "view.reachAny";
		constexpr const char* ViewReachJazz = "view.reachJazz";
		constexpr const char* ViewReachSpaz = "view.reachSpaz";
		constexpr const char* ViewReachLori = "view.reachLori";
		constexpr const char* ViewReachFromMpStarts = "view.reachFromMpStarts";
		constexpr const char* ViewCompareSaved = "view.compareSaved";
		constexpr const char* ViewScreenFrame = "view.screenFrame";
		constexpr const char* ViewScreenFrameGame = "view.screenFrameGame";
		constexpr const char* ViewScreenFrame4x3 = "view.screenFrame4x3";
		constexpr const char* ViewScreenFrame16x9 = "view.screenFrame16x9";
		constexpr const char* ViewScreenFrameSplit = "view.screenFrameSplit";

		// Bookmarks, followed by the number of the slot (1 to 9)
		constexpr const char* BookmarkSetPrefix = "bookmark.set";
		constexpr const char* BookmarkGoPrefix = "bookmark.go";

		// Layer
		constexpr const char* LayerAdd = "layer.add";
		constexpr const char* LayerDuplicate = "layer.duplicate";
		constexpr const char* LayerRemove = "layer.remove";
		constexpr const char* LayerUp = "layer.up";
		constexpr const char* LayerDown = "layer.down";
		constexpr const char* LayerResize = "layer.resize";
		constexpr const char* LayerSolo = "layer.solo";
		constexpr const char* LayerPrevious = "layer.previous";
		constexpr const char* LayerNext = "layer.next";
		// Followed by the number of the layer (1 to 9)
		constexpr const char* LayerSelectPrefix = "layer.select";

		// Level
		constexpr const char* LevelProperties = "level.properties";
		constexpr const char* LevelTexts = "level.texts";
		constexpr const char* LevelEpisode = "level.episode";
		constexpr const char* LevelStatistics = "level.statistics";
		constexpr const char* LevelCheck = "level.check";
		constexpr const char* LevelSnapOffGrid = "level.snapOffGrid";
		constexpr const char* LevelAnimations = "level.animations";
		constexpr const char* LevelTileSets = "level.tilesets";
		constexpr const char* LevelPalette = "level.palette";
		constexpr const char* LevelEditTile = "level.editTile";
		constexpr const char* LevelExportTileSet = "level.exportTileSet";
		constexpr const char* LevelNewTileSet = "level.newTileSet";
		constexpr const char* LevelImportTileSet = "level.importTileSet";
		constexpr const char* LevelAddTiles = "level.addTiles";
		constexpr const char* LevelFxTiles = "level.fxTiles";
		constexpr const char* LevelApplyTileChanges = "level.applyTileChanges";
		constexpr const char* LevelRearrangeTileSet = "level.rearrangeTileSet";
		constexpr const char* LevelReloadEvents = "level.reloadEvents";
		constexpr const char* LevelEditScript = "level.editScript";

		// Play
		constexpr const char* PlayResume = "play.resume";
		constexpr const char* PlayHere = "play.here";
		constexpr const char* PlayStart = "play.start";
		constexpr const char* PlayStop = "play.stop";
		constexpr const char* PlayJazz = "play.jazz";
		constexpr const char* PlaySpaz = "play.spaz";
		constexpr const char* PlayLori = "play.lori";
		constexpr const char* PlayEasy = "play.easy";
		constexpr const char* PlayNormal = "play.normal";
		constexpr const char* PlayHard = "play.hard";
		constexpr const char* PlayAlone = "play.alone";
		constexpr const char* PlayCooperation = "play.cooperation";
		constexpr const char* PlayBattle = "play.battle";
		constexpr const char* PlayTeamBattle = "play.teamBattle";
		constexpr const char* PlayRace = "play.race";
		constexpr const char* PlayTreasureHunt = "play.treasureHunt";
		constexpr const char* PlayCaptureTheFlag = "play.captureTheFlag";
		// Followed by the number of players (1 to LevelInitialization::MaxPlayerCount), the menu names each of them
		constexpr const char* PlayPlayersPrefix = "play.players";
		constexpr const char* PlayPlayers1 = "play.players1";
		constexpr const char* PlayPlayers2 = "play.players2";
		constexpr const char* PlayPlayers3 = "play.players3";
		constexpr const char* PlayPlayers4 = "play.players4";
		constexpr const char* PlayHitboxes = "play.hitboxes";
		constexpr const char* PlayEventZones = "play.eventZones";
		constexpr const char* PlaySlowMotion = "play.slowMotion";
		constexpr const char* PlayPause = "play.pause";
		constexpr const char* PlayStep = "play.step";
		constexpr const char* PlayTeleport = "play.teleport";
		constexpr const char* PlayClearPaths = "play.clearPaths";

		// Help
		constexpr const char* HelpCommands = "help.commands";
		constexpr const char* HelpShortcuts = "help.shortcuts";
	}
}

#endif
