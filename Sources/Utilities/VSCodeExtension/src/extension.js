'use strict';

/**
 * Editor integration for the `.shader` language: completion, hovers, the document outline, #include
 * navigation, live diagnostics from the offline ShaderCompiler, and the transform previews.
 *
 * All of the language knowledge lives in language.js and all of the text analysis in analysis.js;
 * this file only adapts them to the `vscode` API.
 */

var vscode = require('vscode');
var path = require('path');

var analysis = require('./analysis.js');
var language = require('./language.js');
var tool = require('./tool.js');

var LANGUAGE_ID = 'death-shader';
var CONFIG_SECTION = 'deathShader';
var DIAGNOSTIC_SOURCE = 'ShaderCompiler';

var diagnostics = null;
var output = null;
var statusItem = null;
var pendingTimers = new Map();
var inFlight = new Map();
var staleAfterRun = new Set();

// ------------------------------------------------------------------ small helpers

function configuration() {
	return vscode.workspace.getConfiguration(CONFIG_SECTION);
}

function workspaceRoots() {
	var roots = [];
	var folders = vscode.workspace.workspaceFolders;
	if (folders !== undefined && folders !== null) {
		for (var i = 0; i < folders.length; i++) {
			if (folders[i].uri.scheme === 'file') {
				roots.push(folders[i].uri.fsPath);
			}
		}
	}
	return roots;
}

function resolveCompiler() {
	return tool.locate(configuration().get('compilerPath', ''), workspaceRoots());
}

function isShaderDocument(document) {
	return document !== undefined && document !== null && document.languageId === LANGUAGE_ID;
}

/**
 * True for a `.inc` include fragment. Such a file is pasted textually into some `.shader` file, so it
 * legitimately has no `program` directive, no entry points and no `shader_type` of its own - the
 * structural checks stand down for it and completion stops filtering by shader mode.
 */
function isIncludeFragment(document) {
	return /\.inc$/i.test(document.fileName || document.uri.path || '');
}

/** Markdown hover/detail body with a trailing source note. */
function markdown(body) {
	var value = new vscode.MarkdownString(body);
	value.isTrusted = false;
	return value;
}

function rangeFromOffsets(document, startOffset, endOffset) {
	return new vscode.Range(document.positionAt(startOffset), document.positionAt(endOffset));
}

/** Clamps a 1-based compiler line number onto @p document, returning a whole-line range. */
function rangeForCompilerLine(document, oneBasedLine) {
	var index = (typeof oneBasedLine === 'number' && oneBasedLine > 0 ? oneBasedLine - 1 : 0);
	if (index >= document.lineCount) {
		index = document.lineCount - 1;
	}
	if (index < 0) {
		index = 0;
	}
	var line = document.lineAt(index);
	var start = line.firstNonWhitespaceCharacterIndex;
	if (start >= line.text.length) {
		start = 0;
	}
	return new vscode.Range(index, start, index, Math.max(line.text.length, start + 1));
}

// ------------------------------------------------------------------ completion

function item(label, kind, detail, doc, insert, sortPrefix) {
	var completion = new vscode.CompletionItem(label, kind);
	if (detail !== undefined && detail !== null) {
		completion.detail = detail;
	}
	if (doc !== undefined && doc !== null) {
		completion.documentation = markdown(doc);
	}
	if (typeof insert === 'string' && insert.length !== 0) {
		completion.insertText = (insert.indexOf('$') >= 0 ? new vscode.SnippetString(insert) : insert);
	}
	if (typeof sortPrefix === 'string') {
		completion.sortText = sortPrefix + label;
	}
	return completion;
}

function itemsFromTable(table, kind, sortPrefix, detailPrefix) {
	var out = [];
	for (var i = 0; i < table.length; i++) {
		var entry = table[i];
		if (typeof entry === 'string') {
			out.push(item(entry, kind, detailPrefix, null, null, sortPrefix));
		} else {
			out.push(item(entry.name, kind, entry.detail || detailPrefix, entry.doc, entry.insert, sortPrefix));
		}
	}
	return out;
}

/** Names declared by the document itself, so completion offers the real uniforms/varyings/functions. */
function documentSymbolItems(scan) {
	var out = [];
	var i, j;
	for (i = 0; i < scan.uniforms.length; i++) {
		var uniform = scan.uniforms[i];
		var unitNote = (uniform.unit !== null ? '\n\nTexture unit `' + uniform.unit + '`.' : '');
		out.push(item(uniform.name, vscode.CompletionItemKind.Variable,
			'uniform ' + uniform.type, 'Declared in this file.' + unitNote, null, '1'));
	}
	for (i = 0; i < scan.varyings.length; i++) {
		out.push(item(scan.varyings[i].name, vscode.CompletionItemKind.Variable,
			'varying ' + scan.varyings[i].type, 'Declared in this file.', null, '1'));
	}
	for (i = 0; i < scan.attributes.length; i++) {
		out.push(item(scan.attributes[i].name, vscode.CompletionItemKind.Variable,
			'attribute ' + scan.attributes[i].type, 'Declared in this file (vertex stage only).', null, '1'));
	}
	for (i = 0; i < scan.blocks.length; i++) {
		var block = scan.blocks[i];
		for (j = 0; j < block.members.length; j++) {
			out.push(item(block.members[j].name, vscode.CompletionItemKind.Field,
				block.members[j].type + ' (' + block.name + ')', 'Member of the `' + block.name + '` uniform block.', null, '1'));
		}
	}
	for (i = 0; i < scan.functions.length; i++) {
		var fn = scan.functions[i];
		out.push(item(fn.name, vscode.CompletionItemKind.Function,
			fn.returnType + ' ' + fn.name + '(' + fn.params + ')', 'Helper function declared in this file.', null, '1'));
	}
	for (i = 0; i < scan.structs.length; i++) {
		out.push(item(scan.structs[i].name, vscode.CompletionItemKind.Struct, 'struct', 'Declared in this file.', null, '1'));
	}
	for (i = 0; i < scan.defines.length; i++) {
		out.push(item(scan.defines[i].name, vscode.CompletionItemKind.Constant, '#define', 'Defined in this file.', null, '1'));
	}
	for (i = 0; i < scan.variants.length; i++) {
		out.push(item(scan.variants[i].name, vscode.CompletionItemKind.Constant, 'variant',
			'A variant of this program - compiled with `#define ' + scan.variants[i].name + ' (1)`, so guard code with `#ifdef`.', null, '1'));
	}
	return out;
}

/** Generic GLSL vocabulary offered inside any code body. */
function codeItems(scan, entryPointName, modeUnknown) {
	var out = itemsFromTable(language.GLSL_TYPES, vscode.CompletionItemKind.TypeParameter, '4', 'type')
		.concat(itemsFromTable(language.GLSL_KEYWORDS, vscode.CompletionItemKind.Keyword, '5', 'keyword'))
		.concat(itemsFromTable(language.GLSL_FUNCTIONS, vscode.CompletionItemKind.Function, '3', 'GLSL built-in'))
		.concat(itemsFromTable(language.GL_BUILTIN_VARIABLES, vscode.CompletionItemKind.Variable, '2', 'GLSL built-in variable'))
		.concat(documentSymbolItems(scan));

	// The canvas built-ins only exist in canvas mode; COLOR is the fragment output in both. An include
	// fragment cannot know which mode it will be pasted into, so it is offered everything.
	var canvasMode = (modeUnknown === true || scan.shaderType === 'canvas_item');
	var builtins = [];
	for (var i = 0; i < language.BUILTINS.length; i++) {
		var builtin = language.BUILTINS[i];
		if (!canvasMode && builtin.name !== 'COLOR') {
			continue;
		}
		if (builtin.name === 'VERTEX' && entryPointName !== 'vertex') {
			continue;
		}
		if ((builtin.name === 'TEXTURE' || builtin.name === 'PALETTE_OFFSET') && entryPointName === 'vertex') {
			continue;
		}
		builtins.push(builtin);
	}
	out = out.concat(itemsFromTable(builtins, vscode.CompletionItemKind.Constant, '0', 'built-in'));

	if (canvasMode) {
		out = out.concat(itemsFromTable(language.CANVAS_CONTRACT, vscode.CompletionItemKind.Variable, '2', 'sprite contract'));
	}
	return out;
}

function fixedFunctionItems() {
	var ff = language.FIXED_FUNCTION;
	var out = itemsFromTable(ff.statements, vscode.CompletionItemKind.Keyword, '0', 'fixed-function statement')
		.concat(itemsFromTable(ff.submits, vscode.CompletionItemKind.Function, '0', 'fixed-function'))
		.concat(itemsFromTable(ff.stripHelpers, vscode.CompletionItemKind.Function, '1', 'strip builder'))
		.concat(itemsFromTable(ff.context, vscode.CompletionItemKind.Function, '1', 'pass context'))
		.concat(itemsFromTable(ff.passFields, vscode.CompletionItemKind.Field, '2', 'pass field'))
		.concat(itemsFromTable(ff.pipelines, vscode.CompletionItemKind.Constant, '2', 'pipeline intrinsic'))
		.concat(itemsFromTable(ff.functions, vscode.CompletionItemKind.Function, '3', 'allowed in fixed_function'))
		.concat(itemsFromTable(ff.types, vscode.CompletionItemKind.TypeParameter, '3', 'fixed-function type'))
		.concat(itemsFromTable(ff.blendModes, vscode.CompletionItemKind.EnumMember, '2', 'p.blend value'))
		.concat(itemsFromTable(ff.tevPresets, vscode.CompletionItemKind.EnumMember, '2', 'p.tev value'))
		.concat(itemsFromTable(ff.builtins, vscode.CompletionItemKind.Constant, '0', 'built-in'));
	return out;
}

/** Sibling `.shader` / `.inc` files for an `#include "..."` completion. */
async function includePathItems(document, partial) {
	if (document.uri.scheme !== 'file') {
		return [];
	}
	var baseDirectory = path.dirname(document.uri.fsPath);
	var typedDirectory = partial.replace(/[^/\\]*$/, '');
	var searchDirectory = (typedDirectory.length !== 0 ? path.join(baseDirectory, typedDirectory) : baseDirectory);
	var entries;
	try {
		entries = await vscode.workspace.fs.readDirectory(vscode.Uri.file(searchDirectory));
	} catch (e) {
		return [];
	}
	var out = [];
	for (var i = 0; i < entries.length; i++) {
		var name = entries[i][0];
		var type = entries[i][1];
		if (type === vscode.FileType.Directory) {
			var directoryItem = item(name + '/', vscode.CompletionItemKind.Folder, 'directory', null, name + '/', '0');
			directoryItem.command = { command: 'editor.action.triggerSuggest', title: 'Suggest' };
			out.push(directoryItem);
		} else if (/\.(inc|shader)$/.test(name)) {
			out.push(item(name, vscode.CompletionItemKind.File, 'shader include', null, name, '1'));
		}
	}
	return out;
}

var completionProvider = {
	provideCompletionItems: async function (document, position) {
		var text = document.getText();
		var offset = document.offsetAt(position);
		var scan = analysis.scanDocument(text);
		var context = analysis.contextAt(text, offset, scan);
		var modeUnknown = isIncludeFragment(document);

		switch (context.kind) {
			case 'includePath':
				return await includePathItems(document, context.prefix);

			case 'directiveArg':
				if (context.directive === 'shader_type') {
					return itemsFromTable(language.SHADER_TYPES, vscode.CompletionItemKind.EnumMember, '0', 'shader_type');
				}
				if (context.directive === 'render_mode') {
					return itemsFromTable(language.RENDER_MODES, vscode.CompletionItemKind.EnumMember, '0', 'render_mode');
				}
				if (context.directive === 'precision') {
					return itemsFromTable(language.PRECISION_QUALIFIERS, vscode.CompletionItemKind.EnumMember, '0', 'precision');
				}
				return [];			// program / variant / batched take a fresh identifier

			case 'uniformHints':
				return itemsFromTable(language.UNIFORM_HINTS, vscode.CompletionItemKind.Property, '0', 'uniform hint');

			case 'fixedFunctionTargets':
				return itemsFromTable(language.FIXED_FUNCTION_TARGETS, vscode.CompletionItemKind.EnumMember, '0', 'fixed_function target');

			case 'fixedFunctionBody':
				return fixedFunctionItems();

			case 'topLevel': {
				var top = itemsFromTable(language.DIRECTIVES, vscode.CompletionItemKind.Keyword, '0', 'directive')
					.concat(itemsFromTable(language.ENTRY_POINTS, vscode.CompletionItemKind.Function, '1', 'entry point'))
					.concat(itemsFromTable(language.GLSL_TYPES, vscode.CompletionItemKind.TypeParameter, '3', 'type'))
					.concat(documentSymbolItems(scan));
				var include = item('#include', vscode.CompletionItemKind.Keyword, '#include "relative/path"',
					'Replaced textually by the referenced file, relative to this one, recursively up to depth 8. Diagnostics report lines of the include-expanded stream.',
					'#include "$1"', '0');
				top.push(include);
				top = top.concat(itemsFromTable(language.STAGE_MACROS, vscode.CompletionItemKind.Constant, '2', 'stage macro'));
				return top;
			}

			case 'vertexBody':
			case 'fragmentBody':
				return codeItems(scan, context.entryPoint, modeUnknown);

			default:
				return codeItems(scan, context.entryPoint, modeUnknown);
		}
	}
};

// ------------------------------------------------------------------ hover

/**
 * Builds a `name -> {detail, doc}` map over `[table, detailPrefix]` pairs. String entries are skipped
 * (they carry no documentation) and the FIRST table to define a name wins, so the pairs are ordered
 * most-specific first.
 */
function buildHoverIndex(tables) {
	var index = new Map();
	for (var i = 0; i < tables.length; i++) {
		var table = tables[i][0];
		var detail = tables[i][1];
		for (var j = 0; j < table.length; j++) {
			var entry = table[j];
			if (typeof entry === 'string' || index.has(entry.name)) {
				continue;
			}
			index.set(entry.name, { detail: entry.detail || detail, doc: entry.doc });
		}
	}
	return index;
}

/*
	A `.shader` holds TWO languages, and they overlap by name: `mix`, `min`, `max`, `abs`, `clamp`,
	`float`, `int` and `COLOR` all exist in both the GLSL stages and the `fixed_function` DSL, meaning
	different things (a fixed_function body is transpiled to C++ that the console's CPU runs once per
	draw to build a pass - it never shades a pixel, so "lowers to lerp on D3D11" is nonsense there).
	One flat index therefore had to pick a winner and be wrong half the time; these two let the hover
	answer for the vocabulary the cursor is actually in.
*/
var GLSL_HOVER_INDEX = buildHoverIndex([
	[language.DIRECTIVES, 'directive'],
	[language.ENTRY_POINTS, 'entry point'],
	[language.BUILTINS, 'built-in'],
	[language.STAGE_MACROS, 'stage macro'],
	[language.SHADER_TYPES, 'shader_type value'],
	[language.RENDER_MODES, 'render_mode value'],
	[language.PRECISION_QUALIFIERS, 'precision value'],
	[language.UNIFORM_HINTS, 'uniform hint'],
	[language.FIXED_FUNCTION_TARGETS, 'fixed_function target'],
	[language.GL_BUILTIN_VARIABLES, 'GLSL built-in variable'],
	[language.GLSL_FUNCTIONS, 'GLSL built-in'],
	[language.GLSL_TYPES, 'type'],
	[language.CANVAS_CONTRACT, 'sprite contract']
]);

var FIXED_FUNCTION_HOVER_INDEX = buildHoverIndex([
	[language.FIXED_FUNCTION.builtins, 'built-in'],
	[language.FIXED_FUNCTION.statements, 'fixed-function statement'],
	[language.FIXED_FUNCTION.submits, 'fixed-function'],
	[language.FIXED_FUNCTION.stripHelpers, 'strip builder'],
	[language.FIXED_FUNCTION.context, 'pass context'],
	[language.FIXED_FUNCTION.passFields, 'pass field'],
	[language.FIXED_FUNCTION.pipelines, 'pipeline intrinsic'],
	[language.FIXED_FUNCTION.blendModes, 'p.blend value'],
	[language.FIXED_FUNCTION.tevPresets, 'p.tev value'],
	[language.FIXED_FUNCTION.functions, 'allowed in fixed_function'],
	[language.FIXED_FUNCTION.types, 'fixed-function type']
]);

/**
 * The GLSL vocabulary a `fixed_function` body genuinely cannot use, so hovering one of these inside a
 * block says so instead of explaining a built-in that will not compile there. Deliberately narrower
 * than the whole GLSL index: a directive or a stage macro in a block is a different mistake (or a
 * legitimate `#if`), not a "wrong vocabulary" one.
 */
var GLSL_ONLY_HOVER_INDEX = buildHoverIndex([
	[language.GLSL_FUNCTIONS, 'GLSL built-in'],
	[language.BUILTINS, 'built-in'],
	[language.GL_BUILTIN_VARIABLES, 'GLSL built-in variable'],
	[language.CANVAS_CONTRACT, 'sprite contract']
]);

/**
 * The fixed-function words distinctive enough to name from a GLSL context. The pass FIELDS are
 * deliberately absent: `color`, `blend` and `tev` are perfectly ordinary local names in a fragment
 * body, and claiming they belong to a `fixed_function` block there would be wrong far more often than
 * right. They only resolve inside a block, where `p.color` is what they actually are.
 */
var FIXED_FUNCTION_ONLY_HOVER_INDEX = buildHoverIndex([
	[language.FIXED_FUNCTION.statements, 'fixed-function statement'],
	[language.FIXED_FUNCTION.submits, 'fixed-function'],
	[language.FIXED_FUNCTION.stripHelpers, 'strip builder'],
	[language.FIXED_FUNCTION.context, 'pass context'],
	[language.FIXED_FUNCTION.pipelines, 'pipeline intrinsic'],
	[language.FIXED_FUNCTION.blendModes, 'p.blend value'],
	[language.FIXED_FUNCTION.tevPresets, 'p.tev value']
]);

var NOT_IN_FIXED_FUNCTION = '**Not available in a `fixed_function` block.** That body is not GLSL — it is transpiled to ' +
	'C++ that the console\'s CPU runs once per draw, and its whole vocabulary is `pass` / `pipeline`, the pass fields, ' +
	'the `submit_*` calls, the context facilities and a small maths subset (`abs`, `ceil`, `clamp`, `cos`, `float`, ' +
	'`floor`, `int`, `max`, `min`, `mix`, `sin`, `sqrt`).';
var ONLY_IN_FIXED_FUNCTION = '**Only available inside a `fixed_function` block** — it belongs to the console ' +
	'fixed-function DSL, never to the GLSL stages.';

/** Renders one index entry (plus an optional context warning) as the hover body. */
function hoverBody(word, entry, warning) {
	var body = '**' + word + '**';
	if (entry.detail !== undefined && entry.detail !== null) {
		body += '  —  `' + entry.detail + '`';
	}
	if (warning !== undefined) {
		body += '\n\n' + warning;
	}
	if (entry.doc !== undefined && entry.doc !== null) {
		body += '\n\n' + entry.doc;
	}
	return body;
}

var hoverProvider = {
	provideHover: function (document, position) {
		var wordRange = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
		if (wordRange === undefined) {
			return null;
		}
		var word = document.getText(wordRange);
		var text = document.getText();
		var scan = analysis.scanDocument(text);
		// Resolved at the word's START, so hovering the first character of a word that opens a body
		// still classifies as the body it is in rather than the position after it
		var context = analysis.contextAt(text, document.offsetAt(wordRange.start), scan);
		var inFixedFunction = (context.kind === 'fixedFunctionBody');

		// The vocabulary the cursor is in answers first; the other one only gets to answer to say the
		// word does not belong here (and, for a block, then falls through to a plain explanation)
		var near = (inFixedFunction ? FIXED_FUNCTION_HOVER_INDEX : GLSL_HOVER_INDEX);
		var restricted = (inFixedFunction ? GLSL_ONLY_HOVER_INDEX : FIXED_FUNCTION_ONLY_HOVER_INDEX);
		var warning = (inFixedFunction ? NOT_IN_FIXED_FUNCTION : ONLY_IN_FIXED_FUNCTION);

		var known = near.get(word);
		if (known !== undefined) {
			return new vscode.Hover(markdown(hoverBody(word, known)), wordRange);
		}
		var blocked = restricted.get(word);
		if (blocked !== undefined) {
			return new vscode.Hover(markdown(hoverBody(word, blocked, warning)), wordRange);
		}
		if (inFixedFunction) {
			var elsewhere = GLSL_HOVER_INDEX.get(word);
			if (elsewhere !== undefined) {
				return new vscode.Hover(markdown(hoverBody(word, elsewhere)), wordRange);
			}
		}

		if (language.UNSUPPORTED_BUILTINS.indexOf(word) >= 0) {
			return new vscode.Hover(markdown('**' + word + '**\n\nA canvas built-in the compiler reports as **unsupported**. ' +
				'Only `COLOR`, `UV`, `TEXTURE`, `PALETTE_OFFSET` and `VERTEX` are implemented.'), wordRange);
		}
		// The grammar highlights a wider set of type-looking words than the compiler has entries for, so
		// say so here rather than let the colour imply it compiles
		if (language.UNSUPPORTED_TYPES.indexOf(word) >= 0) {
			return new vscode.Hover(markdown('**' + word + '**\n\n**Not a type this language has.** The compiler\'s type table ' +
				'(reflection, and every emitter) knows `float`, `int`, `uint`, `bool`, `vec2/3/4`, `ivec2/3/4`, `uvec2/3/4`, ' +
				'`bvec2/3/4`, `mat2`, `mat3`, `mat4`, `sampler2D`, `sampler3D`, `samplerCube` and your own `struct`s — ' +
				'nothing else. The editor colours it like a type because the grammar is deliberately permissive; the ' +
				'declaration still fails as an unknown type.'), wordRange);
		}
		if (word === 'fragColor') {
			return new vscode.Hover(markdown('**fragColor**\n\nReferencing `fragColor` anywhere in a `.shader` file is a **parse error** — ' +
				'`COLOR` is the fragment output variable itself.'), wordRange);
		}

		// Something declared by this document: show the declaration. Checked BEFORE the HLSL spellings
		// below, so a real declaration in the file always wins over a guess about where the word came from
		var declaration = findDeclaration(scan, word);
		if (declaration !== null) {
			var lineText = document.lineAt(declaration.line).text.replace(/^[\s]+|[\s]+$/g, '');
			return new vscode.Hover(markdown('```glsl\n' + lineText + '\n```\n\nDeclared in this file.'), wordRange);
		}

		// Pasted from an HLSL / Cg / Unity shader: name the spelling this language does use
		if (Object.prototype.hasOwnProperty.call(language.HLSL_SPELLINGS, word)) {
			return new vscode.Hover(markdown('**' + word + '**\n\nAn **HLSL / Cg spelling**, which this language does not accept — ' +
				'a `.shader` is written in GLSL and deliberately has no aliases. Use `' + language.HLSL_SPELLINGS[word] + '` instead.\n\n' +
				'The HLSL lowering happens inside the compiler on the way to D3D11 and the PS3 (`mix` → `lerp`, `fract` → `frac`, ' +
				'`dFdx` → `ddx`, …), so the source keeps one spelling for every backend.'), wordRange);
		}
		return null;
	}
};

/** The line a name is declared on in this document, or null. */
function findDeclaration(scan, name) {
	var tables = [scan.uniforms, scan.varyings, scan.attributes, scan.functions, scan.structs,
		scan.defines, scan.variants, scan.programs, scan.batched, scan.blocks];
	for (var i = 0; i < tables.length; i++) {
		for (var j = 0; j < tables[i].length; j++) {
			if (tables[i][j].name === name) {
				return tables[i][j];
			}
		}
	}
	for (var b = 0; b < scan.blocks.length; b++) {
		for (var m = 0; m < scan.blocks[b].members.length; m++) {
			if (scan.blocks[b].members[m].name === name) {
				return scan.blocks[b];
			}
		}
	}
	return null;
}

// ------------------------------------------------------------------ outline

var symbolProvider = {
	provideDocumentSymbols: function (document) {
		var scan = analysis.scanDocument(document.getText());
		var symbols = [];
		var i;

		function push(name, detail, kind, line) {
			var range = document.lineAt(Math.min(line, document.lineCount - 1)).range;
			var symbol = new vscode.DocumentSymbol(name, detail, kind, range, range);
			symbols.push(symbol);
		}

		for (i = 0; i < scan.programs.length; i++) {
			push(scan.programs[i].name, 'program', vscode.SymbolKind.Module, scan.programs[i].line);
		}
		for (i = 0; i < scan.batched.length; i++) {
			push(scan.batched[i].name, 'batched twin', vscode.SymbolKind.Module, scan.batched[i].line);
		}
		for (i = 0; i < scan.variants.length; i++) {
			push(scan.variants[i].name, 'variant', vscode.SymbolKind.Constant, scan.variants[i].line);
		}
		for (i = 0; i < scan.blocks.length; i++) {
			push(scan.blocks[i].name, 'uniform block', vscode.SymbolKind.Struct, scan.blocks[i].line);
		}
		for (i = 0; i < scan.uniforms.length; i++) {
			var unitSuffix = (scan.uniforms[i].unit !== null ? ' : texture_unit(' + scan.uniforms[i].unit + ')' : '');
			push(scan.uniforms[i].name, 'uniform ' + scan.uniforms[i].type + unitSuffix,
				vscode.SymbolKind.Variable, scan.uniforms[i].line);
		}
		for (i = 0; i < scan.varyings.length; i++) {
			push(scan.varyings[i].name, 'varying ' + scan.varyings[i].type, vscode.SymbolKind.Variable, scan.varyings[i].line);
		}
		for (i = 0; i < scan.attributes.length; i++) {
			push(scan.attributes[i].name, 'attribute ' + scan.attributes[i].type, vscode.SymbolKind.Variable, scan.attributes[i].line);
		}
		for (i = 0; i < scan.functions.length; i++) {
			push(scan.functions[i].name, scan.functions[i].returnType + '(' + scan.functions[i].params + ')',
				vscode.SymbolKind.Function, scan.functions[i].line);
		}
		for (i = 0; i < scan.entryPoints.length; i++) {
			var entry = scan.entryPoints[i];
			var targetSuffix = (entry.targets.length !== 0 ? '(' + entry.targets.join(', ') + ')' : '()');
			push(entry.name + targetSuffix, 'entry point', vscode.SymbolKind.Method, entry.line);
		}
		return symbols;
	}
};

// ------------------------------------------------------------------ #include navigation

function includeAt(scan, offset) {
	for (var i = 0; i < scan.includes.length; i++) {
		if (offset >= scan.includes[i].startOffset && offset <= scan.includes[i].endOffset) {
			return scan.includes[i];
		}
	}
	return null;
}

var definitionProvider = {
	provideDefinition: function (document, position) {
		var text = document.getText();
		var scan = analysis.scanDocument(text);
		var offset = document.offsetAt(position);

		var include = includeAt(scan, offset);
		if (include !== null && document.uri.scheme === 'file') {
			var target = path.resolve(path.dirname(document.uri.fsPath), include.path);
			return new vscode.Location(vscode.Uri.file(target), new vscode.Position(0, 0));
		}

		var wordRange = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
		if (wordRange === undefined) {
			return null;
		}
		var declaration = findDeclaration(scan, document.getText(wordRange));
		if (declaration === null) {
			return null;
		}
		return new vscode.Location(document.uri, document.lineAt(declaration.line).range);
	}
};

var documentLinkProvider = {
	provideDocumentLinks: function (document) {
		if (document.uri.scheme !== 'file') {
			return [];
		}
		var scan = analysis.scanDocument(document.getText());
		var links = [];
		var baseDirectory = path.dirname(document.uri.fsPath);
		for (var i = 0; i < scan.includes.length; i++) {
			var include = scan.includes[i];
			var range = rangeFromOffsets(document, include.startOffset, include.endOffset);
			var link = new vscode.DocumentLink(range, vscode.Uri.file(path.resolve(baseDirectory, include.path)));
			link.tooltip = 'Open the included file';
			links.push(link);
		}
		return links;
	}
};

// ------------------------------------------------------------------ diagnostics

/**
 * Validates one document: the extension's own hard-error checks always, plus the compiler's own
 * diagnostics when an executable can be found.
 */
async function validate(document) {
	if (!isShaderDocument(document) || diagnostics === null) {
		return;
	}
	var settings = configuration();
	if (!settings.get('validate.enable', true)) {
		diagnostics.delete(document.uri);
		return;
	}

	// Only one compiler run per document at a time. A request arriving mid-run is not dropped - it is
	// remembered and replayed once the run finishes, so a quick second save cannot leave stale squiggles.
	var key = document.uri.toString();
	if (inFlight.get(key) === true) {
		staleAfterRun.add(key);
		return;
	}
	inFlight.set(key, true);
	try {
		var collected = [];
		var text = document.getText();
		var scan = analysis.scanDocument(text);

		if (settings.get('validate.builtinChecks', true)) {
			var findings = analysis.builtinChecks(text, scan, { isIncludeFragment: isIncludeFragment(document) });
			for (var i = 0; i < findings.length; i++) {
				var finding = findings[i];
				var line = Math.min(finding.line, document.lineCount - 1);
				var lineText = document.lineAt(line).text;
				var startColumn = Math.min(finding.column, Math.max(lineText.length - 1, 0));
				var endColumn = Math.min(startColumn + Math.max(finding.length, 1), Math.max(lineText.length, startColumn + 1));
				var builtinDiagnostic = new vscode.Diagnostic(
					new vscode.Range(line, startColumn, line, endColumn),
					finding.message, vscode.DiagnosticSeverity.Error);
				builtinDiagnostic.source = 'death-shader';
				collected.push(builtinDiagnostic);
			}
		}

		var compiler = resolveCompiler();
		if (compiler.path !== null && document.uri.scheme === 'file') {
			var inputPath = document.uri.fsPath;
			var tempPath = null;
			try {
				if (document.isDirty) {
					// The compiler reads from disk, so a dirty buffer needs a copy. It has to sit next to
					// the original when the document includes anything, since include paths are resolved
					// relative to the input file.
					tempPath = tool.writeTempCopy(inputPath, text, scan.hasIncludes);
					inputPath = tempPath;
				}
				var result = await tool.run(compiler.path, [inputPath, '--check'],
					{ cwd: path.dirname(document.uri.fsPath) });
				if (result.error !== null) {
					log('ShaderCompiler could not be run: ' + result.error);
				}
				var parsed = analysis.parseDiagnostics(result.stderr);
				for (var d = 0; d < parsed.length; d++) {
					collected.push(diagnosticFromCompiler(document, scan, parsed[d]));
				}
			} finally {
				tool.removeTemp(tempPath);
			}
		}

		diagnostics.set(document.uri, collected);
	} catch (e) {
		log('validate() failed: ' + String(e && e.stack ? e.stack : e));
	} finally {
		inFlight.delete(key);
		if (staleAfterRun.delete(key)) {
			// The document changed while the compiler was running - validate the newer text
			validate(document);
		}
	}
}

function diagnosticFromCompiler(document, scan, parsed) {
	var message = parsed.message;
	var range;
	if (parsed.line !== null) {
		range = rangeForCompilerLine(document, parsed.line);
		// Line numbers refer to the include-expanded stream, so once a file includes anything the
		// number can point past the end of this document (or at the wrong line entirely). Say so
		// rather than silently anchoring the squiggle somewhere misleading.
		if (scan.hasIncludes) {
			if (parsed.line > document.lineCount) {
				message += '  [reported at line ' + parsed.line +
					' of the include-expanded stream, which is past the end of this file]';
			} else {
				message += '  [line ' + parsed.line + ' of the include-expanded stream]';
			}
		}
	} else {
		range = new vscode.Range(0, 0, 0, Math.max(document.lineAt(0).text.length, 1));
	}
	var severity = (parsed.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
	var diagnostic = new vscode.Diagnostic(range, message, severity);
	diagnostic.source = DIAGNOSTIC_SOURCE;
	return diagnostic;
}

function scheduleValidate(document) {
	if (!isShaderDocument(document)) {
		return;
	}
	var key = document.uri.toString();
	var existing = pendingTimers.get(key);
	if (existing !== undefined) {
		clearTimeout(existing);
	}
	var delay = configuration().get('validate.delay', 400);
	pendingTimers.set(key, setTimeout(function () {
		pendingTimers.delete(key);
		validate(document);
	}, delay));
}

// ------------------------------------------------------------------ transform previews

var DUMP_MODES = {
	showReflection: { flag: '--check', title: 'reflection', previewLanguage: 'plaintext' },
	showEssl100: { flag: '--essl100-check', title: 'ESSL 100', previewLanguage: 'glsl' },
	showHlsl: { flag: '--hlsl', title: 'HLSL', previewLanguage: 'hlsl' },
	showCg: { flag: '--cg', title: 'Cg', previewLanguage: 'hlsl' },
	showVulkan: { flag: '--vulkan', title: 'Vulkan GLSL', previewLanguage: 'glsl' }
};

async function showDump(modeName) {
	var mode = DUMP_MODES[modeName];
	var editor = vscode.window.activeTextEditor;
	if (editor === undefined || !isShaderDocument(editor.document)) {
		vscode.window.showInformationMessage('Open a .shader file first.');
		return;
	}
	var document = editor.document;
	var compiler = resolveCompiler();
	if (compiler.path === null) {
		reportMissingCompiler(compiler);
		return;
	}

	var text = document.getText();
	var scan = analysis.scanDocument(text);
	var inputPath;
	var tempPath = null;
	if (document.uri.scheme === 'file' && !document.isDirty) {
		inputPath = document.uri.fsPath;
	} else {
		tempPath = tool.writeTempCopy(
			document.uri.scheme === 'file' ? document.uri.fsPath : null, text, scan.hasIncludes);
		inputPath = tempPath;
	}

	var result;
	try {
		result = await vscode.window.withProgress(
			{ location: vscode.ProgressLocation.Window, title: 'ShaderCompiler ' + mode.flag },
			function () {
				return tool.run(compiler.path, [inputPath, mode.flag],
					{ cwd: document.uri.scheme === 'file' ? path.dirname(document.uri.fsPath) : undefined });
			});
	} finally {
		tool.removeTemp(tempPath);
	}

	if (result.error !== null) {
		vscode.window.showErrorMessage('ShaderCompiler could not be run: ' + result.error);
		return;
	}
	if (result.stdout.length === 0) {
		var reason = (result.stderr.length !== 0 ? result.stderr : 'the tool printed nothing');
		vscode.window.showErrorMessage('No ' + mode.title + ' output: ' + reason.split(/\r?\n/)[0]);
		log(result.stderr);
		return;
	}

	var header = '// ' + mode.title + ' transform of ' + path.basename(document.fileName) +
		' (ShaderCompiler ' + mode.flag + ')\n';
	if (result.stderr.length !== 0) {
		header += '// stderr: ' + result.stderr.split(/\r?\n/)[0] + '\n';
	}
	var preview = await vscode.workspace.openTextDocument({
		content: (mode.previewLanguage === 'plaintext' ? '' : header) + result.stdout,
		language: mode.previewLanguage
	});
	await vscode.window.showTextDocument(preview, { viewColumn: vscode.ViewColumn.Beside, preview: true });
}

// ------------------------------------------------------------------ status bar / logging

function log(message) {
	if (output !== null) {
		output.appendLine(message);
	}
}

function reportMissingCompiler(compiler) {
	vscode.window.showWarningMessage(
		'ShaderCompiler was not found (resolved from ' + compiler.origin + '). Build it, or set deathShader.compilerPath.',
		'Open Settings').then(function (choice) {
		if (choice === 'Open Settings') {
			vscode.commands.executeCommand('workbench.action.openSettings', 'deathShader.compilerPath');
		}
	});
}

function updateStatus() {
	if (statusItem === null) {
		return;
	}
	var editor = vscode.window.activeTextEditor;
	if (editor === undefined || !isShaderDocument(editor.document)) {
		statusItem.hide();
		return;
	}
	var compiler = resolveCompiler();
	if (compiler.path !== null) {
		statusItem.text = '$(check) ShaderCompiler';
		statusItem.tooltip = 'Diagnostics come from ' + compiler.path + '\n(resolved from ' + compiler.origin + ')';
	} else {
		statusItem.text = '$(warning) ShaderCompiler';
		statusItem.tooltip = 'Not found (' + compiler.origin + ') - only the extension\'s own checks are active.\n' +
			'Build the tool or set deathShader.compilerPath.';
	}
	statusItem.command = 'deathShader.showCompilerPath';
	statusItem.show();
}

// ------------------------------------------------------------------ activation

function activate(context) {
	output = vscode.window.createOutputChannel('Death™ Shader');
	diagnostics = vscode.languages.createDiagnosticCollection('death-shader');
	statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
	context.subscriptions.push(output, diagnostics, statusItem);

	var selector = { language: LANGUAGE_ID };
	context.subscriptions.push(
		vscode.languages.registerCompletionItemProvider(selector, completionProvider, '.', '#', '"', ':', '/'),
		vscode.languages.registerHoverProvider(selector, hoverProvider),
		vscode.languages.registerDocumentSymbolProvider(selector, symbolProvider),
		vscode.languages.registerDefinitionProvider(selector, definitionProvider),
		vscode.languages.registerDocumentLinkProvider(selector, documentLinkProvider)
	);

	var modeNames = Object.keys(DUMP_MODES);
	for (var i = 0; i < modeNames.length; i++) {
		context.subscriptions.push(vscode.commands.registerCommand(
			'deathShader.' + modeNames[i], (function (name) {
				return function () {
					return showDump(name);
				};
			})(modeNames[i])));
	}

	context.subscriptions.push(vscode.commands.registerCommand('deathShader.validateNow', function () {
		var editor = vscode.window.activeTextEditor;
		if (editor !== undefined && isShaderDocument(editor.document)) {
			return validate(editor.document);
		}
		return undefined;
	}));

	context.subscriptions.push(vscode.commands.registerCommand('deathShader.showCompilerPath', function () {
		var compiler = resolveCompiler();
		if (compiler.path === null) {
			reportMissingCompiler(compiler);
		} else {
			vscode.window.showInformationMessage('ShaderCompiler: ' + compiler.path + ' (from ' + compiler.origin + ')');
		}
		log('Resolved ShaderCompiler: ' + (compiler.path || '<none>') + ' (from ' + compiler.origin + ')');
	}));

	context.subscriptions.push(
		vscode.workspace.onDidOpenTextDocument(function (document) {
			if (isShaderDocument(document)) {
				validate(document);
			}
		}),
		vscode.workspace.onDidSaveTextDocument(function (document) {
			if (isShaderDocument(document)) {
				validate(document);
			}
		}),
		vscode.workspace.onDidChangeTextDocument(function (event) {
			if (!isShaderDocument(event.document)) {
				return;
			}
			if (configuration().get('validate.run', 'onSave') === 'onType') {
				scheduleValidate(event.document);
			}
		}),
		vscode.workspace.onDidCloseTextDocument(function (document) {
			diagnostics.delete(document.uri);
			var key = document.uri.toString();
			var timer = pendingTimers.get(key);
			if (timer !== undefined) {
				clearTimeout(timer);
				pendingTimers.delete(key);
			}
		}),
		vscode.window.onDidChangeActiveTextEditor(updateStatus),
		vscode.workspace.onDidChangeConfiguration(function (event) {
			if (!event.affectsConfiguration(CONFIG_SECTION)) {
				return;
			}
			updateStatus();
			var documents = vscode.workspace.textDocuments;
			for (var d = 0; d < documents.length; d++) {
				if (isShaderDocument(documents[d])) {
					validate(documents[d]);
				}
			}
		})
	);

	// A previous session may have been killed between writing a temporary copy and deleting it
	var sweepDirectories = [];
	var roots = workspaceRoots();
	for (var r = 0; r < roots.length; r++) {
		sweepDirectories.push(path.join(roots[r], 'Sources', 'Shaders'));
		sweepDirectories.push(path.join(roots[r], 'Sources', 'Shaders', 'Include'));
	}
	var swept = tool.sweepStaleTemps(sweepDirectories);
	if (swept !== 0) {
		log('Removed ' + swept + ' stale temporary shader copies.');
	}

	var open = vscode.workspace.textDocuments;
	for (var o = 0; o < open.length; o++) {
		if (isShaderDocument(open[o])) {
			validate(open[o]);
		}
	}
	updateStatus();
	log('Death™ Shader extension activated.');
}

function deactivate() {
	pendingTimers.forEach(function (timer) {
		clearTimeout(timer);
	});
	pendingTimers.clear();
}

module.exports = {
	activate: activate,
	deactivate: deactivate
};
