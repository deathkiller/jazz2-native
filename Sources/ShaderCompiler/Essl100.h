#pragma once

/**
	@file Essl100.h

	ESSL 100 (OpenGL ES 2.0, "#version 100") source-to-source emitter for ShaderCompiler.

	The tool lowers each ".shader" into a MODERN-GLSL stage source (in/out, texture(),
	"out vec4 COLOR;", std140 UBO blocks, gl_VertexID) that serves BOTH desktop GL 3.3
	("#version 330") and GLES3/WebGL2 ("#version 300 es") via runtime #version injection.
	ESSL 100 is a different dialect; this emitter transforms an already-lowered modern-GLSL
	stage source into ESSL 100:

	- Vertex stage:   "in T name;"  -> "attribute T name;" (a leading "layout(...)" is dropped —
	                  ES2 has no layout qualifier);   "out T name;" -> "varying T name;".
	- Fragment stage: "in T name;"  -> "varying T name;";   "out vec4 COLOR;" is REMOVED and the
	                  fragment output is retargeted to gl_FragColor: "COLOR" becomes a local declared
	                  at the top of main(), each "return;" is preceded by "gl_FragColor = COLOR;" and
	                  a final "gl_FragColor = COLOR;" is appended before main()'s closing brace.
	                  ES2 has ONLY gl_FragColor (no MRT), so a fragment stage with MORE than one "out"
	                  declaration is declined with a clear diagnostic (never silently mangled).
	- Both stages:    "texture(" -> "texture2D(", "textureLod(" -> "texture2DLod(" (comment-aware,
	                  whole-identifier); "#ifdef GL_ES ... #endif" is unwrapped (GL_ES is always
	                  defined under "#version 100", so the fragment precision prologue becomes
	                  unconditional). A "flat" interpolation qualifier is dropped (ES2 has none).

	The "#version" line is NOT emitted here (the engine injects it) — same as the modern path;
	ES2 wants "#version 100".

	ES2 has neither uniform buffer objects nor gl_VertexID, and both ARE transformed here: a
	"layout(std140)" block becomes loose uniforms (an array of a uniform struct for the batched
	twins), and the sprite template's gl_VertexID corner formulas are folded into the aQuadCorner /
	aInstanceIndex attributes the ES2 runtime supplies. What remains genuinely unexpressible - most
	notably multiple render targets, and the "uint"/"uvec"/round()/integer-%/float-suffix/derivative
	safety net - makes Transform() return false with a diagnostic naming the offending line.
*/

#include "ShaderParser.h"	// Diagnostic, SourceLine, ShaderParser::StripComments

namespace ShaderCompiler
{
	/** @brief Transforms an already-lowered modern-GLSL stage source into ESSL 100 (OpenGL ES 2.0) */
	class Essl100Emitter
	{
	public:
		/**
			Transforms @p modernSource (as produced by ShaderParser::BuildStageSource) into ESSL 100,
			writing the result to @p out. @p vertexStage selects the vertex-vs-fragment lowering.
			Returns false and fills @p diag (with the offending line) when the source uses a feature
			ES2 cannot express and this emitter does not rewrite (see the file comment).
		*/
		static bool Transform(StringView modernSource, bool vertexStage, String& out, Diagnostic& diag);
	};
}
