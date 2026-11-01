// Documentation only, never compiled by the game

/** @page preprocessor Preprocessor directives
@brief Lines starting with `#` that are handled before a script is compiled

Before the game compiles a script, it goes through lines starting with `#` and handles them, then the lines are
replaced by whitespace, so the line numbers of errors still match the file. Put each directive on its own line,
preferably at the top of the file and outside of any code blocks.

@section preprocessor-include #include

@code{.as}
#include "library.asc"
@endcode

Adds the code of another file to the script, as if it was written in the same file. Functions and classes of both
files can use each other. The name may also be enclosed in single quotes. The code always goes to the global scope,
writing the directive inside of a namespace doesn't put the included code into that namespace. An included file may
have directives of its own, including more includes.

The file is searched for next to the script that includes it first, then among the files of the original game. Letter
case of the name doesn't matter.

@note The game doesn't skip a file that was already included, so including the same file from two places compiles it
twice and its declarations collide. Include each file only once.

The includes of the MLLE level editor (`MLLE-Include-*.asc`) are provided by the game itself, the files aren't loaded.

@section preprocessor-if #if and #endif

@code{.as}
#if Resurrection
	// Compiled only by this game
#endif
@endcode

Code between `#if` and the matching `#endif` is compiled only if the given symbol is defined, otherwise it's left out
entirely. Only a single symbol can follow `#if`, there are no expressions, `#else`, `#elif` or `#define`. Blocks can be
nested. The following symbols are defined:

- `Resurrection` --- always, so a script can tell this game apart from others
- `TARGET_WINDOWS`, `TARGET_WINDOWS_RT`, `TARGET_UNIX`, `TARGET_APPLE`, `TARGET_IOS`, `TARGET_ANDROID`,
	`TARGET_SWITCH`, `TARGET_EMSCRIPTEN` --- the platform the game runs on
- `TARGET_BIG_ENDIAN` --- on big-endian platforms
- `TARGET_CONSTRAINED_MEMORY` --- on platforms with little memory
- `WITH_AUDIO`, `WITH_VORBIS`, `WITH_OPENMPT`, `WITH_XMP` --- audio support and the music formats the game can play
- `WITH_THREADS` --- the game can use more threads
- `WITH_IMGUI` --- debugging tools are included

@section preprocessor-pragma #pragma

@code{.as}
#pragma target Resurrection
@endcode

Switches the script to the native API of the game. Without it, the script uses the JJ2+ API described in this reference.

Other pragmas, such as `#pragma require`, `#pragma offer`, `#pragma name` and `#pragma description`, are accepted, so
existing scripts still compile, but they have no effect. The files a level needs are found the same way as by
`#include`.

@section preprocessor-shebang #!

A line starting with `#!`, such as an interpreter line at the very top of a file, is ignored.
*/
