'use strict';

/**
 * Pure text analysis of a `.shader` document: comment stripping, a shallow scan of the declarations,
 * cursor-context classification for completion, parsing of the compiler's stderr diagnostics, and the
 * handful of hard errors the extension can detect without running the executable.
 *
 * Deliberately free of any `vscode` import so that test/run-tests.js can exercise it in a bare JS
 * engine. Offsets are plain character indices into the original text; the caller converts them to
 * positions with the editor API.
 */

/**
 * Replaces every comment with spaces, keeping the text exactly the same length (and keeping newlines)
 * so that any offset computed on the result also indexes the original.
 * @param {string} text
 * @returns {string}
 */
function stripComments(text) {
	var out = text.split('');
	var i = 0;
	var n = text.length;
	while (i < n) {
		var c = text[i];
		if (c === '/' && i + 1 < n && text[i + 1] === '/') {
			while (i < n && text[i] !== '\n') {
				out[i] = ' ';
				i++;
			}
		} else if (c === '/' && i + 1 < n && text[i + 1] === '*') {
			out[i] = ' ';
			out[i + 1] = ' ';
			i += 2;
			while (i < n) {
				if (text[i] === '*' && i + 1 < n && text[i + 1] === '/') {
					out[i] = ' ';
					out[i + 1] = ' ';
					i += 2;
					break;
				}
				if (text[i] !== '\n') {
					out[i] = ' ';
				}
				i++;
			}
		} else if (c === '"') {
			// #include paths are the only strings in the language; leave them intact
			i++;
			while (i < n && text[i] !== '"' && text[i] !== '\n') {
				i++;
			}
			if (i < n && text[i] === '"') {
				i++;
			}
		} else {
			i++;
		}
	}
	return out.join('');
}

/**
 * Index of the brace matching the `{` at @p openIndex, or -1 when the document ends first.
 * @param {string} text comment-stripped text
 * @param {number} openIndex
 */
function matchBrace(text, openIndex) {
	var depth = 0;
	for (var i = openIndex; i < text.length; i++) {
		if (text[i] === '{') {
			depth++;
		} else if (text[i] === '}') {
			depth--;
			if (depth === 0) {
				return i;
			}
		}
	}
	return -1;
}

/** Brace depth at @p offset, ignoring comments. */
function braceDepthAt(text, offset) {
	var depth = 0;
	var end = Math.min(offset, text.length);
	for (var i = 0; i < end; i++) {
		if (text[i] === '{') {
			depth++;
		} else if (text[i] === '}') {
			depth--;
		}
	}
	return depth;
}

function lineOfOffset(text, offset) {
	var line = 0;
	var end = Math.min(offset, text.length);
	for (var i = 0; i < end; i++) {
		if (text[i] === '\n') {
			line++;
		}
	}
	return line;
}

function offsetOfLineStart(text, line) {
	if (line <= 0) {
		return 0;
	}
	var seen = 0;
	for (var i = 0; i < text.length; i++) {
		if (text[i] === '\n') {
			seen++;
			if (seen === line) {
				return i + 1;
			}
		}
	}
	return text.length;
}

/** Statement keywords that must never be mistaken for a declaration's type or name */
var CONTROL_KEYWORDS = ['if', 'else', 'for', 'while', 'do', 'switch', 'case', 'return', 'discard'];

/** All matches of @p re (which must be global) as `[match, index]` pairs. */
function allMatches(re, text) {
	var result = [];
	var m;
	re.lastIndex = 0;
	while ((m = re.exec(text)) !== null) {
		result.push({ m: m, index: m.index });
		if (m.index === re.lastIndex) {
			re.lastIndex++;			// zero-length match guard
		}
	}
	return result;
}

/**
 * Shallow scan of a document: the directives, the declarations worth completing on, the includes and
 * the entry-point body ranges. Line numbers are 0-based.
 * @param {string} rawText
 */
function scanDocument(rawText) {
	var text = stripComments(rawText);
	var scan = {
		text: text,
		programs: [],
		batched: [],
		variants: [],
		shaderType: 'custom',
		shaderTypeLine: -1,
		renderModes: [],
		precision: null,
		uniforms: [],
		blocks: [],
		structs: [],
		varyings: [],
		attributes: [],
		functions: [],
		defines: [],
		includes: [],
		entryPoints: [],
		hasIncludes: false
	};

	var i, e;

	var programMatches = allMatches(/^[ \t]*program[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;/gm, text);
	for (i = 0; i < programMatches.length; i++) {
		e = programMatches[i];
		scan.programs.push({ name: e.m[1], line: lineOfOffset(text, e.index) });
	}

	var batchedMatches = allMatches(/^[ \t]*batched[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;/gm, text);
	for (i = 0; i < batchedMatches.length; i++) {
		e = batchedMatches[i];
		scan.batched.push({ name: e.m[1], line: lineOfOffset(text, e.index) });
	}

	var variantMatches = allMatches(/^[ \t]*variant[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;/gm, text);
	for (i = 0; i < variantMatches.length; i++) {
		e = variantMatches[i];
		scan.variants.push({ name: e.m[1], line: lineOfOffset(text, e.index) });
	}

	var typeMatch = /^[ \t]*shader_type[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;/m.exec(text);
	if (typeMatch !== null) {
		scan.shaderType = typeMatch[1];
		scan.shaderTypeLine = lineOfOffset(text, typeMatch.index);
	}

	var renderMatch = /^[ \t]*render_mode[ \t]+([^;]*);/m.exec(text);
	if (renderMatch !== null) {
		var modes = renderMatch[1].split(',');
		for (i = 0; i < modes.length; i++) {
			var mode = modes[i].replace(/^[\s]+|[\s]+$/g, '');
			if (mode.length !== 0) {
				scan.renderModes.push(mode);
			}
		}
	}

	// Only the two-token form is a directive; "precision highp float;" is ordinary GLSL
	var precisionMatch = /^[ \t]*precision[ \t]+(mediump|highp)[ \t]*;/m.exec(text);
	if (precisionMatch !== null) {
		scan.precision = precisionMatch[1];
	}

	// uniform <type> <name> [: hints] ;  (samplers and scalars alike; blocks are handled below)
	var uniformMatches = allMatches(
		/^[ \t]*uniform[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*(?:\[[^\]]*\])?[ \t]*(?::([^;]*))?;/gm,
		text);
	for (i = 0; i < uniformMatches.length; i++) {
		e = uniformMatches[i];
		var unit = null;
		if (e.m[3] !== undefined && e.m[3] !== null) {
			var unitMatch = /texture_unit[ \t]*\([ \t]*(\d+)[ \t]*\)/.exec(e.m[3]);
			if (unitMatch !== null) {
				unit = parseInt(unitMatch[1], 10);
			}
		}
		scan.uniforms.push({
			type: e.m[1],
			name: e.m[2],
			unit: unit,
			line: lineOfOffset(text, e.index)
		});
	}

	// layout (std140) uniform <Block> { <members> };  (the brace may sit on the next line)
	var blockMatches = allMatches(
		/^[ \t]*(?:layout[ \t]*\([^)]*\)[ \t]*)?uniform[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*\{/gm, text);
	for (i = 0; i < blockMatches.length; i++) {
		e = blockMatches[i];
		var openAt = text.indexOf('{', e.index);
		var closeAt = matchBrace(text, openAt);
		var body = (closeAt > openAt ? text.substring(openAt + 1, closeAt) : '');
		var members = [];
		var memberMatches = allMatches(/([A-Za-z_][A-Za-z0-9_]*)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*(?:\[[^\]]*\])?[ \t]*;/g, body);
		for (var j = 0; j < memberMatches.length; j++) {
			members.push({ type: memberMatches[j].m[1], name: memberMatches[j].m[2] });
		}
		scan.blocks.push({ name: e.m[1], members: members, line: lineOfOffset(text, e.index) });
	}

	var structMatches = allMatches(/\bstruct[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*\{/g, text);
	for (i = 0; i < structMatches.length; i++) {
		e = structMatches[i];
		scan.structs.push({ name: e.m[1], line: lineOfOffset(text, e.index) });
	}

	var varyingMatches = allMatches(
		/^[ \t]*varying[ \t]+(?:(?:flat|smooth|noperspective|centroid)[ \t]+)?(?:(?:lowp|mediump|highp)[ \t]+)?([A-Za-z_][A-Za-z0-9_]*)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*(?:\[[^\]]*\])?[ \t]*;/gm,
		text);
	for (i = 0; i < varyingMatches.length; i++) {
		e = varyingMatches[i];
		scan.varyings.push({ type: e.m[1], name: e.m[2], line: lineOfOffset(text, e.index) });
	}

	var attributeMatches = allMatches(
		/^[ \t]*attribute[ \t]+(?:layout[ \t]*\([^)]*\)[ \t]*)?([A-Za-z_][A-Za-z0-9_]*)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*(?:\[[^\]]*\])?[ \t]*;/gm,
		text);
	for (i = 0; i < attributeMatches.length; i++) {
		e = attributeMatches[i];
		scan.attributes.push({ type: e.m[1], name: e.m[2], line: lineOfOffset(text, e.index) });
	}

	var defineMatches = allMatches(/^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)/gm, text);
	for (i = 0; i < defineMatches.length; i++) {
		e = defineMatches[i];
		scan.defines.push({ name: e.m[1], line: lineOfOffset(text, e.index) });
	}

	// The include path must come from the ORIGINAL text: stripComments keeps strings, but reading the
	// raw text keeps this honest even if that ever changes
	var includeMatches = allMatches(/^[ \t]*#[ \t]*include[ \t]+"([^"\n]*)"/gm, rawText);
	for (i = 0; i < includeMatches.length; i++) {
		e = includeMatches[i];
		var quoteAt = e.m[0].indexOf('"');
		scan.includes.push({
			path: e.m[1],
			line: lineOfOffset(rawText, e.index),
			startOffset: e.index + quoteAt + 1,
			endOffset: e.index + quoteAt + 1 + e.m[1].length
		});
	}
	scan.hasIncludes = (scan.includes.length !== 0);

	// Entry points, with their body ranges so the cursor context can be resolved
	var entryMatches = allMatches(/^[ \t]*void[ \t]+(vertex|fragment|fixed_function)[ \t]*\(([^)]*)\)[ \t\r\n]*\{/gm, text);
	for (i = 0; i < entryMatches.length; i++) {
		e = entryMatches[i];
		var open = text.indexOf('{', e.index + e.m[0].length - 1);
		if (open < 0) {
			open = e.index + e.m[0].length - 1;
		}
		var close = matchBrace(text, open);
		var targets = [];
		var rawTargets = e.m[2].split(',');
		for (var t = 0; t < rawTargets.length; t++) {
			var target = rawTargets[t].replace(/^[\s]+|[\s]+$/g, '');
			if (target.length !== 0) {
				targets.push(target);
			}
		}
		scan.entryPoints.push({
			name: e.m[1],
			targets: targets,
			line: lineOfOffset(text, e.index),
			bodyStart: open + 1,
			bodyEnd: (close < 0 ? text.length : close)
		});
	}

	// Helper functions at global scope (anything that is not an entry point)
	var fnMatches = allMatches(/^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(([^)]*)\)[ \t\r\n]*\{/gm, text);
	for (i = 0; i < fnMatches.length; i++) {
		e = fnMatches[i];
		if (e.m[2] === 'vertex' || e.m[2] === 'fragment' || e.m[2] === 'fixed_function') {
			continue;
		}
		// "else if (...) {" and friends look like a declaration to the regex
		if (CONTROL_KEYWORDS.indexOf(e.m[1]) >= 0 || CONTROL_KEYWORDS.indexOf(e.m[2]) >= 0) {
			continue;
		}
		if (braceDepthAt(text, e.index) !== 0) {
			continue;
		}
		scan.functions.push({
			returnType: e.m[1],
			name: e.m[2],
			params: e.m[3],
			line: lineOfOffset(text, e.index)
		});
	}

	return scan;
}

/**
 * Classifies what the cursor at @p offset is completing, so the completion provider can offer the
 * right vocabulary instead of dumping everything.
 * @param {string} rawText
 * @param {number} offset
 * @param {object} [scanned] a previous scanDocument() result, to avoid re-scanning
 * @returns {{kind: string, directive?: string, entryPoint?: string, prefix: string, linePrefix: string}}
 */
function contextAt(rawText, offset, scanned) {
	var scan = scanned || scanDocument(rawText);
	var text = scan.text;
	var lineStart = offset;
	while (lineStart > 0 && text[lineStart - 1] !== '\n') {
		lineStart--;
	}
	var linePrefix = rawText.substring(lineStart, offset);
	var strippedLinePrefix = text.substring(lineStart, offset);

	var prefixMatch = /([A-Za-z_][A-Za-z0-9_]*)$/.exec(strippedLinePrefix);
	var prefix = (prefixMatch !== null ? prefixMatch[1] : '');

	var result = { kind: 'code', prefix: prefix, linePrefix: linePrefix };

	// Inside the quotes of an #include - offer sibling files
	var includeMatch = /^[ \t]*#[ \t]*include[ \t]+"([^"\n]*)$/.exec(linePrefix);
	if (includeMatch !== null) {
		result.kind = 'includePath';
		result.prefix = includeMatch[1];
		return result;
	}

	// The argument of a top-level directive
	var directiveArg = /^[ \t]*(shader_type|render_mode|precision|program|variant|batched)[ \t]+[^;]*$/.exec(strippedLinePrefix);
	if (directiveArg !== null) {
		result.kind = 'directiveArg';
		result.directive = directiveArg[1];
		return result;
	}

	// The hint list after the ':' of a uniform declaration
	if (/^[ \t]*uniform\b[^;]*:[^;]*$/.test(strippedLinePrefix)) {
		result.kind = 'uniformHints';
		return result;
	}

	// The target list of a fixed_function signature
	if (/^[ \t]*void[ \t]+fixed_function[ \t]*\([^)]*$/.test(strippedLinePrefix)) {
		result.kind = 'fixedFunctionTargets';
		return result;
	}

	// Which entry-point body are we in?
	for (var i = 0; i < scan.entryPoints.length; i++) {
		var entry = scan.entryPoints[i];
		if (offset > entry.bodyStart && offset <= entry.bodyEnd) {
			result.entryPoint = entry.name;
			result.kind = (entry.name === 'fixed_function' ? 'fixedFunctionBody' : entry.name + 'Body');
			return result;
		}
	}

	if (braceDepthAt(text, offset) === 0) {
		result.kind = 'topLevel';
		return result;
	}
	return result;
}

/**
 * Parses one stderr line of ShaderCompiler. The tool reports in three shapes:
 * `<file>:<line>: error: <msg>`, `<file>: error: <msg>` and a bare `error: <msg>`.
 * The greedy file group is deliberate - it keeps a Windows drive letter (`C:\x.shader:12:`) intact.
 * @param {string} line
 * @returns {{file: (string|null), line: (number|null), severity: string, message: string}|null}
 */
function parseDiagnosticLine(line) {
	var withLine = /^(.+):(\d+):\s*(error|warning):\s*(.+)$/.exec(line);
	if (withLine !== null) {
		return {
			file: withLine[1],
			line: parseInt(withLine[2], 10),
			severity: withLine[3],
			message: withLine[4]
		};
	}
	var withFile = /^(.+?):\s*(error|warning):\s*(.+)$/.exec(line);
	if (withFile !== null) {
		return { file: withFile[1], line: null, severity: withFile[2], message: withFile[3] };
	}
	var bare = /^\s*(error|warning):\s*(.+)$/.exec(line);
	if (bare !== null) {
		return { file: null, line: null, severity: bare[1], message: bare[2] };
	}
	return null;
}

/** Parses a whole stderr blob into diagnostics, skipping anything that is not a diagnostic line. */
function parseDiagnostics(stderr) {
	var out = [];
	var lines = String(stderr === null || stderr === undefined ? '' : stderr).split(/\r?\n/);
	for (var i = 0; i < lines.length; i++) {
		if (lines[i].length === 0) {
			continue;
		}
		var parsed = parseDiagnosticLine(lines[i]);
		if (parsed !== null) {
			out.push(parsed);
		}
	}
	return out;
}

/**
 * Occurrences of @p identifier as a whole word, as `{line, column, length}`. Runs on the
 * comment-stripped text so a mention in a comment never reports.
 */
function findIdentifier(scan, identifier) {
	var out = [];
	var re = new RegExp('\\b' + identifier + '\\b', 'g');
	var matches = allMatches(re, scan.text);
	for (var i = 0; i < matches.length; i++) {
		var index = matches[i].index;
		var line = lineOfOffset(scan.text, index);
		out.push({ line: line, column: index - offsetOfLineStart(scan.text, line), length: identifier.length });
	}
	return out;
}

/**
 * The hard errors the extension can prove on its own, so a file gets feedback even with no executable
 * configured. Every rule here is a documented hard error of the language, and each is skipped when it
 * could be satisfied from an `#include` (the includes are expanded textually before parsing, so a
 * file that includes anything may legitimately look incomplete on its own).
 * @param {string} rawText
 * @param {object} [scanned]
 * @param {{isIncludeFragment?: boolean}} [options] set isIncludeFragment for a `.inc` file - a fragment
 *        that is textually pasted into a `.shader` file and therefore legitimately has no `program`
 *        directive, no entry points and no `shader_type` of its own
 * @returns {Array<{line: number, column: number, length: number, message: string}>}
 */
function builtinChecks(rawText, scanned, options) {
	var scan = scanned || scanDocument(rawText);
	var isIncludeFragment = (options !== undefined && options !== null && options.isIncludeFragment === true);
	var out = [];
	var i;

	function lineLength(line) {
		var lines = scan.text.split('\n');
		return (line < lines.length ? lines[line].length : 0);
	}

	function report(line, column, length, message) {
		out.push({ line: line, column: column, length: length, message: message });
	}

	// The engine injects the #version header itself
	var versionMatches = allMatches(/^[ \t]*#[ \t]*version\b.*$/gm, scan.text);
	for (i = 0; i < versionMatches.length; i++) {
		var versionLine = lineOfOffset(scan.text, versionMatches[i].index);
		report(versionLine, 0, lineLength(versionLine),
			'Do not put #version in a .shader file - the engine injects the version header and the platform defines at runtime.');
	}

	// COLOR is the fragment output; fragColor exists nowhere in generated code
	var fragColor = findIdentifier(scan, 'fragColor');
	for (i = 0; i < fragColor.length; i++) {
		report(fragColor[i].line, fragColor[i].column, fragColor[i].length,
			"Referencing 'fragColor' is a parse error - write 'COLOR', it is the fragment output variable itself.");
	}

	// A compile-time macro is resolved by the compiler and never defined in an emitted source, so
	// #ifdef / #ifndef and #if / #elif expressions are all fine but defining one is not
	var badStageForm = allMatches(
		/^[ \t]*#[ \t]*(define|undef)\b.*\b(VERTEX_STAGE|FRAGMENT_STAGE|SOFTWARE_RENDERER|NO_DYNAMIC_BRANCHING)\b.*$/gm, scan.text);
	for (i = 0; i < badStageForm.length; i++) {
		var stageLine = lineOfOffset(scan.text, badStageForm[i].index);
		report(stageLine, 0, lineLength(stageLine),
			"'" + badStageForm[i].m[2] + "' cannot be defined or undefined - it is resolved at compile time and never defined in an emitted source. Use it in #ifdef / #ifndef or in an #if / #elif expression instead.");
	}


	if (scan.programs.length > 1) {
		for (i = 1; i < scan.programs.length; i++) {
			report(scan.programs[i].line, 0, lineLength(scan.programs[i].line),
				"Duplicate 'program' directive - it must appear exactly once (the batched twin is declared with 'batched').");
		}
	}

	// 'batched' is canvas_item only. An include fragment carries no shader_type of its own, so the
	// mode it will be pasted into is unknowable from here
	if (!isIncludeFragment && scan.batched.length !== 0 && scan.shaderType !== 'canvas_item') {
		for (i = 0; i < scan.batched.length; i++) {
			report(scan.batched[i].line, 0, lineLength(scan.batched[i].line),
				"'batched' requires 'shader_type canvas_item;' - it is an error in custom mode (the default).");
		}
	}

	// 'program' has to precede shader_type, variant, batched, precision and the entry points
	if (scan.programs.length !== 0) {
		var programLine = scan.programs[0].line;
		var precede = [];
		if (scan.shaderTypeLine >= 0) {
			precede.push({ line: scan.shaderTypeLine, what: 'shader_type' });
		}
		for (i = 0; i < scan.variants.length; i++) {
			precede.push({ line: scan.variants[i].line, what: 'variant' });
		}
		for (i = 0; i < scan.batched.length; i++) {
			precede.push({ line: scan.batched[i].line, what: 'batched' });
		}
		for (i = 0; i < scan.entryPoints.length; i++) {
			precede.push({ line: scan.entryPoints[i].line, what: 'void ' + scan.entryPoints[i].name + '()' });
		}
		for (i = 0; i < precede.length; i++) {
			if (precede[i].line < programLine) {
				report(precede[i].line, 0, lineLength(precede[i].line),
					"'program' must precede '" + precede[i].what + "' - write the program directive first.");
			}
		}
	}

	// A canvas vertex() runs a real epilogue after the body, so an early return would skip it
	if (scan.shaderType === 'canvas_item') {
		for (i = 0; i < scan.entryPoints.length; i++) {
			var canvasVertex = scan.entryPoints[i];
			if (canvasVertex.name !== 'vertex') {
				continue;
			}
			var body = scan.text.substring(canvasVertex.bodyStart, canvasVertex.bodyEnd);
			var returns = allMatches(/\breturn\b[ \t]*;/g, body);
			for (var r = 0; r < returns.length; r++) {
				var returnLine = lineOfOffset(scan.text, canvasVertex.bodyStart + returns[r].index);
				report(returnLine, 0, lineLength(returnLine),
					"'return;' inside a canvas-mode vertex() is a parse error - the generated epilogue is real post-work, so restructure with if/else.");
			}
		}
	}

	// Canvas built-ins the lowering does not implement (in custom mode these are ordinary identifiers)
	if (scan.shaderType === 'canvas_item') {
		var unsupported = ['NORMAL', 'SCREEN_UV', 'SCREEN_PIXEL_SIZE', 'TIME', 'POINT_COORD'];
		for (i = 0; i < unsupported.length; i++) {
			var hits = findIdentifier(scan, unsupported[i]);
			for (var h = 0; h < hits.length; h++) {
				report(hits[h].line, hits[h].column, hits[h].length,
					"The canvas built-in '" + unsupported[i] + "' is reported as unsupported by the compiler.");
			}
		}
	}

	// Structural requirements - skipped for a file that includes anything (the includes are expanded
	// textually before parsing and may well carry the program or an entry point), and for an include
	// fragment, which is itself only ever a piece of some other file
	if (!scan.hasIncludes && !isIncludeFragment) {
		if (scan.programs.length === 0) {
			report(0, 0, lineLength(0),
				"Missing 'program <Name>;' - it is required exactly once and must come before everything else.");
		}
		var hasVertex = false;
		var hasFragment = false;
		for (i = 0; i < scan.entryPoints.length; i++) {
			if (scan.entryPoints[i].name === 'vertex') {
				hasVertex = true;
			} else if (scan.entryPoints[i].name === 'fragment') {
				hasFragment = true;
			}
		}
		if (!hasFragment) {
			report(0, 0, lineLength(0), "Missing 'void fragment()' - it is required in both modes.");
		}
		if (!hasVertex && scan.shaderType !== 'canvas_item') {
			report(0, 0, lineLength(0),
				"Missing 'void vertex()' - it is required in custom mode (only canvas_item can omit it and use the sprite template).");
		}
	}

	return out;
}

module.exports = {
	stripComments: stripComments,
	matchBrace: matchBrace,
	braceDepthAt: braceDepthAt,
	lineOfOffset: lineOfOffset,
	offsetOfLineStart: offsetOfLineStart,
	scanDocument: scanDocument,
	contextAt: contextAt,
	parseDiagnosticLine: parseDiagnosticLine,
	parseDiagnostics: parseDiagnostics,
	findIdentifier: findIdentifier,
	builtinChecks: builtinChecks
};
