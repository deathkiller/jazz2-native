#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Shows all events placed in the level in a list that can be filtered

		Each row tells the kind of the event, where it is, in which difficulties and its main parameters. A click
		selects the event and moves the view to it, Ctrl+click adds it to the selection or takes it away, Shift+click
		selects all between, the selected events are then changed together in the event properties.
	*/
	void ShowEventOutline(IEditorContext& session);
}

#endif
