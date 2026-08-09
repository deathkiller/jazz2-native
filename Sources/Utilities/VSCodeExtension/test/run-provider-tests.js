// Functional tests for the editor providers in src/extension.js, driven against a mock `vscode`
// module and a mock TextDocument. The mock is wrapped in a Proxy that throws on any member it does
// not define, so a typo'd `vscode.*` API name fails loudly here instead of at runtime in the editor.
//
//     node test/run-provider-tests.js
//     gjs  test/run-provider-tests.js
//
// src/extension.js is loaded as source and evaluated with an appended line that exposes its
// module-level providers, so the shipped file needs no test-only exports.

'use strict';

var IS_GJS = (typeof imports !== 'undefined' && typeof imports.gi !== 'undefined');
var log = (typeof print === 'function' ? print : console.log);
var BASE;
var readSource;
var exitFailure;

if (IS_GJS) {
	var GLib = imports.gi.GLib;
	var scriptDirectory = GLib.path_get_dirname(new Error().fileName || 'test/run-provider-tests.js');
	BASE = GLib.build_filenamev([scriptDirectory, '..']) + '/';
	readSource = function (relative) {
		var bytes = GLib.file_get_contents(BASE + relative)[1];
		return (typeof TextDecoder !== 'undefined' ? new TextDecoder().decode(bytes) : imports.byteArray.toString(bytes));
	};
	exitFailure = function () {
		imports.system.exit(1);
	};
} else {
	var nodeFs = require('fs');
	var nodePath = require('path');
	BASE = nodePath.join(__dirname, '..') + nodePath.sep;
	readSource = function (relative) {
		return nodeFs.readFileSync(BASE + relative.split('/').join(nodePath.sep), 'utf8');
	};
	exitFailure = function () {
		process.exitCode = 1;
	};
}

function loadPureModule(relative) {
	var box = { exports: {} };
	(new Function('module', 'exports', readSource(relative)))(box, box.exports);
	return box.exports;
}

// ---- mock vscode: only the members extension.js is allowed to touch. Any other access throws, so a
// ---- typo'd API name fails loudly instead of silently returning undefined.
function enumOf(names) { const o = {}; names.forEach((n, i) => o[n] = i); return o; }
class Position { constructor(line, character) { this.line = line; this.character = character; } }
class Range {
	constructor(a, b, c, d) {
		if (a instanceof Position) { this.start = a; this.end = b; }
		else { this.start = new Position(a, b); this.end = new Position(c, d); }
	}
}
class MarkdownString { constructor(v) { this.value = v; } }
class SnippetString { constructor(v) { this.value = v; } }
class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
class Hover { constructor(contents, range) { this.contents = contents; this.range = range; } }
class DocumentSymbol { constructor(name, detail, kind, range, sel) { this.name = name; this.detail = detail; this.kind = kind; this.range = range; this.selectionRange = sel; } }
class Location { constructor(uri, rangeOrPos) { this.uri = uri; this.range = rangeOrPos; } }
class DocumentLink { constructor(range, target) { this.range = range; this.target = target; } }
class Diagnostic { constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity; } }

const configValues = { compilerPath: '', 'validate.enable': true, 'validate.run': 'onSave', 'validate.delay': 400, 'validate.builtinChecks': true };
const vscodeMock = {
	Position, Range, MarkdownString, SnippetString, CompletionItem, Hover, DocumentSymbol, Location, DocumentLink, Diagnostic,
	CompletionItemKind: enumOf(['Text','Method','Function','Constructor','Field','Variable','Class','Struct','Interface','Module','Property','Event','Operator','Unit','Value','Constant','Enum','EnumMember','Keyword','Snippet','Color','File','Reference','Folder','TypeParameter']),
	SymbolKind: enumOf(['File','Module','Namespace','Package','Class','Method','Property','Field','Constructor','Enum','Interface','Function','Variable','Constant','String','Number','Boolean','Array','Object','Key','Null','EnumMember','Struct','Event','Operator','TypeParameter']),
	DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
	FileType: { Unknown: 0, File: 1, Directory: 2, SymbolicLink: 64 },
	ViewColumn: { Beside: -2 },
	ProgressLocation: { Window: 10 },
	StatusBarAlignment: { Left: 1, Right: 2 },
	Uri: { file: (p) => ({ scheme: 'file', fsPath: p, toString: () => 'file://' + p }) },
	workspace: {
		getConfiguration: () => ({ get: (k, d) => (configValues[k] !== undefined ? configValues[k] : d) }),
		workspaceFolders: [{ uri: { scheme: 'file', fsPath: '/home/dan/Stažené/jazz2' } }],
		textDocuments: [],
		fs: { readDirectory: async (uri) => { throw new Error('not exercised'); } },
		openTextDocument: async () => ({}),
		onDidOpenTextDocument: () => ({ dispose() {} }), onDidSaveTextDocument: () => ({ dispose() {} }),
		onDidChangeTextDocument: () => ({ dispose() {} }), onDidCloseTextDocument: () => ({ dispose() {} }),
		onDidChangeConfiguration: () => ({ dispose() {} })
	},
	window: {
		activeTextEditor: undefined,
		createOutputChannel: () => ({ appendLine() {}, dispose() {} }),
		createStatusBarItem: () => ({ show() {}, hide() {}, dispose() {} }),
		showInformationMessage: () => ({ then() {} }), showWarningMessage: () => ({ then() {} }), showErrorMessage: () => {},
		showTextDocument: async () => ({}), withProgress: (o, t) => t(),
		onDidChangeActiveTextEditor: () => ({ dispose() {} })
	},
	languages: {
		createDiagnosticCollection: () => ({ set() {}, delete() {}, dispose() {} }),
		registerCompletionItemProvider: () => ({ dispose() {} }), registerHoverProvider: () => ({ dispose() {} }),
		registerDocumentSymbolProvider: () => ({ dispose() {} }), registerDefinitionProvider: () => ({ dispose() {} }),
		registerDocumentLinkProvider: () => ({ dispose() {} })
	},
	commands: { registerCommand: () => ({ dispose() {} }), executeCommand: () => {} }
};
const guarded = new Proxy(vscodeMock, {
	get(target, prop) {
		if (!(prop in target)) { throw new Error('extension.js used vscode.' + String(prop) + ', which the mock does not define'); }
		return target[prop];
	}
});

// ---- mock TextDocument -------------------------------------------------------------------------
function makeDocument(text, fsPath) {
	const lines = text.split('\n');
	const starts = []; let acc = 0;
	for (const l of lines) { starts.push(acc); acc += l.length + 1; }
	return {
		languageId: 'death-shader', isDirty: false, fileName: fsPath,
		uri: { scheme: 'file', fsPath, toString: () => 'file://' + fsPath },
		lineCount: lines.length,
		getText(range) {
			if (!range) return text;
			return text.substring(starts[range.start.line] + range.start.character, starts[range.end.line] + range.end.character);
		},
		lineAt(i) {
			const t = lines[i];
			const nw = t.search(/\S/);
			return { text: t, range: new Range(i, 0, i, t.length), firstNonWhitespaceCharacterIndex: (nw < 0 ? t.length : nw) };
		},
		offsetAt(p) { return starts[p.line] + p.character; },
		positionAt(o) {
			for (let i = lines.length - 1; i >= 0; i--) { if (o >= starts[i]) return new Position(i, o - starts[i]); }
			return new Position(0, 0);
		},
		getWordRangeAtPosition(p, re) {
			const t = lines[p.line];
			let s = p.character, e = p.character;
			const ok = (ch) => /[A-Za-z0-9_]/.test(ch);
			while (s > 0 && ok(t[s - 1])) s--;
			while (e < t.length && ok(t[e])) e++;
			if (s === e) return undefined;
			return new Range(p.line, s, p.line, e);
		}
	};
}

// ---- load extension.js with the internals exposed ----------------------------------------------
var extensionSource = readSource('src/extension.js') +
	'\nmodule.exports._internals = { completionProvider, hoverProvider, symbolProvider, definitionProvider,' +
	' documentLinkProvider, DUMP_MODES, findDeclaration, rangeForCompilerLine, diagnosticFromCompiler };\n';
const box = { exports: {} };
(new Function('module', 'exports', 'require', extensionSource))(box, box.exports, function (name) {
	if (name === 'vscode') return guarded;
	if (name === './analysis.js') return loadPureModule('src/analysis.js');
	if (name === './language.js') return loadPureModule('src/language.js');
	if (name === './tool.js') {
		return {
			locate: function () { return { path: null, origin: 'mock' }; },
			run: async function () { return { code: 0, stdout: '', stderr: '', error: null }; },
			writeTempCopy: function () { return '/tmp/mock.shader'; },
			removeTemp: function () {}, sweepStaleTemps: function () { return 0; },
			EXECUTABLE_NAME: 'ShaderCompiler', TEMP_INFIX: '.mock.'
		};
	}
	if (name === 'path') {
		return {
			dirname: function (p) { return p.substring(0, p.lastIndexOf('/')); },
			basename: function (p) { return p.split('/').pop(); },
			resolve: function (a, b) { return a + '/' + b; },
			join: function () { return Array.prototype.slice.call(arguments).join('/'); },
			isAbsolute: function (p) { return p.indexOf('/') === 0; }
		};
	}
	throw new Error('extension.js required an unexpected module: ' + name);
});
const I = box.exports._internals;

let pass = 0, fail = 0;
function check(name, cond, extra) { if (cond) pass++; else { fail++; log('FAIL: ' + name + (extra ? ' -- ' + extra : '')); } }
function labels(items) { return (items || []).map(i => String(i.label)); }
function has(items, label) { return labels(items).indexOf(label) >= 0; }

const SRC = [
	'program Colorized;',                    // 0
	'shader_type canvas_item;',              // 1
	'variant USE_PALETTE;',                  // 2
	'uniform sampler2D uTexture : texture_unit(0);', // 3
	'uniform vec4 uTint;',                   // 4
	'varying vec2 vSpecial;',                // 5
	'vec4 helper(vec4 c) { return c; }',     // 6
	'void fragment() {',                     // 7
	'\tCOLOR = helper(texture(TEXTURE, UV)) * uTint;', // 8
	'}',                                     // 9
	'void fixed_function(pvr) {',            // 10
	'\tpass p;',                             // 11
	'\tp.color = COLOR;',                    // 12
	'\tsubmit_quad(p);',                     // 13
	'}',                                     // 14
	''
].join('\n');
const doc = makeDocument(SRC, '/home/dan/Stažené/jazz2/Sources/Shaders/Colorized.shader');
const uTintColumn = SRC.split('\n')[8].indexOf('uTint') + 1;

// ----- completion in each context
(async function () {
	const top = await I.completionProvider.provideCompletionItems(doc, new Position(15, 0));
	check('top level offers directives', has(top, 'program') && has(top, 'variant') && has(top, 'render_mode'));
	check('top level offers entry points', has(top, 'vertex') && has(top, 'fragment') && has(top, 'fixed_function'));
	check('top level offers #include', has(top, '#include'));
	check('top level offers stage macros', has(top, 'VERTEX_STAGE') && has(top, 'SOFTWARE_RENDERER'));
	check('top level offers this file\'s uniforms', has(top, 'uTexture') && has(top, 'uTint'));

	const frag = await I.completionProvider.provideCompletionItems(doc, new Position(8, 10));
	check('fragment offers COLOR/UV/TEXTURE', has(frag, 'COLOR') && has(frag, 'UV') && has(frag, 'TEXTURE'));
	check('fragment offers the helper function', has(frag, 'helper'));
	check('fragment offers declared varying', has(frag, 'vSpecial'));
	check('fragment offers the variant name', has(frag, 'USE_PALETTE'));
	check('fragment offers GLSL builtins', has(frag, 'texture') && has(frag, 'mix'));
	check('fragment offers the sprite contract in canvas mode', has(frag, 'vTexCoords'));
	check('fragment does NOT offer VERTEX (vertex-only)', !has(frag, 'VERTEX'));
	check('fragment does NOT offer the fixed-function DSL', !has(frag, 'submit_quad'));

	const ff = await I.completionProvider.provideCompletionItems(doc, new Position(13, 4));
	check('fixed_function offers submit_quad', has(ff, 'submit_quad') && has(ff, 'submit_strip'));
	check('fixed_function offers pass fields', has(ff, 'color') && has(ff, 'offset_color') && has(ff, 'luma_gain'));
	check('fixed_function offers blend/tev values', has(ff, 'MATERIAL') && has(ff, 'LUMA_RAMP'));
	check('fixed_function offers context facilities', has(ff, 'texel_size') && has(ff, 'has_uniform'));
	check('fixed_function offers pipeline intrinsics', has(ff, 'tile_map_mesh'));
	check('fixed_function does NOT offer GLSL texture()', !has(ff, 'texture'));

	const typeDoc = makeDocument('shader_type can\n', '/x/y.shader');
	const typeItems = await I.completionProvider.provideCompletionItems(typeDoc, new Position(0, 15));
	check('shader_type offers only its two values', labels(typeItems).sort().join(',') === 'canvas_item,custom', labels(typeItems).join(','));

	const modeDoc = makeDocument('render_mode ble\n', '/x/y.shader');
	const modeItems = await I.completionProvider.provideCompletionItems(modeDoc, new Position(0, 15));
	check('render_mode offers exactly six modes', modeItems.length === 6, String(modeItems.length));

	const hintDoc = makeDocument('uniform sampler2D u : tex\n', '/x/y.shader');
	const hintItems = await I.completionProvider.provideCompletionItems(hintDoc, new Position(0, 25));
	check('uniform hints offered', has(hintItems, 'texture_unit') && has(hintItems, 'source_color') && hintItems.length === 7);

	const targetDoc = makeDocument('void fixed_function(p\n', '/x/y.shader');
	const targetItems = await I.completionProvider.provideCompletionItems(targetDoc, new Position(0, 21));
	check('fixed_function targets offered', labels(targetItems).sort().join(',') === 'gs,gu,gx,pvr', labels(targetItems).join(','));

	// custom mode must not offer the canvas built-ins
	const customDoc = makeDocument('program P;\nvoid vertex() {\n\tgl_Position = vec4(0.0);\n}\nvoid fragment() {\n\tCOLOR = vec4(1.0);\n}\n', '/x/c.shader');
	const customFrag = await I.completionProvider.provideCompletionItems(customDoc, new Position(5, 6));
	check('custom mode offers COLOR', has(customFrag, 'COLOR'));
	check('custom mode does NOT offer TEXTURE/UV', !has(customFrag, 'TEXTURE') && !has(customFrag, 'UV'));
	check('custom mode does NOT offer the sprite contract', !has(customFrag, 'vTexCoords'));

	// snippets must be SnippetString when they carry placeholders
	const programItem = top[labels(top).indexOf('program')];
	check('directive insert is a SnippetString', programItem.insertText instanceof SnippetString, String(programItem.insertText));

	// ----- hover
	const h1 = I.hoverProvider.provideHover(doc, new Position(0, 3));
	check('hover on "program"', h1 !== null && h1.contents.value.indexOf('exactly once') >= 0);
	const h2 = I.hoverProvider.provideHover(doc, new Position(8, 3));
	check('hover on COLOR', h2 !== null && h2.contents.value.indexOf('fragment output') >= 0);
	const h3 = I.hoverProvider.provideHover(doc, new Position(12, 6));
	check('hover on pass field "color"', h3 !== null && h3.contents.value.indexOf('pass colour') >= 0, h3 && h3.contents.value);
	const h4 = I.hoverProvider.provideHover(doc, new Position(8, uTintColumn));
	check('hover on a declared uniform shows its line', h4 !== null && h4.contents.value.indexOf('uniform vec4 uTint;') >= 0, h4 && h4.contents.value);
	const h5 = I.hoverProvider.provideHover(makeDocument('fragColor = x;\n', '/x/y.shader'), new Position(0, 3));
	check('hover on fragColor warns', h5 !== null && h5.contents.value.indexOf('parse error') >= 0);
	const h6 = I.hoverProvider.provideHover(doc, new Position(9, 0));
	check('hover on nothing returns null', h6 === null);

	// ----- outline
	const symbols = I.symbolProvider.provideDocumentSymbols(doc);
	const names = symbols.map(s => s.name);
	check('outline has the program', names.indexOf('Colorized') >= 0);
	check('outline has the variant', names.indexOf('USE_PALETTE') >= 0);
	check('outline has the uniforms', names.indexOf('uTexture') >= 0 && names.indexOf('uTint') >= 0);
	check('outline has the helper', names.indexOf('helper') >= 0);
	check('outline has both entry points', names.indexOf('fragment()') >= 0 && names.indexOf('fixed_function(pvr)') >= 0, names.join(','));
	check('outline records the texture unit', symbols.filter(s => s.name === 'uTexture')[0].detail.indexOf('texture_unit(0)') >= 0);

	// ----- include links & definition
	const incDoc = makeDocument('program P;\n#include "Include/LightingFs.inc"\n', '/repo/Sources/Shaders/L.shader');
	const links = I.documentLinkProvider.provideDocumentLinks(incDoc);
	check('one include link', links.length === 1);
	check('include link targets the file', links.length === 1 && String(links[0].target.fsPath).indexOf('Include/LightingFs.inc') >= 0, links.length && links[0].target.fsPath);
	const def = I.definitionProvider.provideDefinition(incDoc, new Position(1, 15));
	check('go-to-definition on an include path', def !== null && String(def.uri.fsPath).indexOf('LightingFs.inc') >= 0);
	const declDef = I.definitionProvider.provideDefinition(doc, new Position(8, uTintColumn));
	check('go-to-definition on a uniform lands on line 4', declDef !== null && declDef.range.start.line === 4, declDef && declDef.range.start.line);

    // ----- compiler diagnostic mapping
	const scan = loadPureModule('src/analysis.js');
	const plainScan = scan.scanDocument(SRC);
	const d1 = I.diagnosticFromCompiler(doc, plainScan, { file: 'x', line: 9, severity: 'error', message: 'boom' });
	check('diagnostic anchors on the 1-based line', d1.range.start.line === 8, String(d1.range.start.line));
	check('diagnostic message untouched without includes', d1.message === 'boom', d1.message);
	const incScan = scan.scanDocument('program P;\n#include "a.inc"\n');
	const incDocSmall = makeDocument('program P;\n#include "a.inc"\n', '/x/y.shader');
	const d2 = I.diagnosticFromCompiler(incDocSmall, incScan, { file: 'x', line: 120, severity: 'error', message: 'boom' });
	check('past-the-end line is clamped', d2.range.start.line === incDocSmall.lineCount - 1, String(d2.range.start.line));
	check('past-the-end line is explained', d2.message.indexOf('past the end of this file') >= 0, d2.message);
	const d3 = I.diagnosticFromCompiler(incDocSmall, incScan, { file: 'x', line: 2, severity: 'warning', message: 'hmm' });
	check('in-range line with includes is annotated', d3.message.indexOf('include-expanded stream') >= 0, d3.message);
	check('warning severity mapped', d3.severity === 1, String(d3.severity));
	const d4 = I.diagnosticFromCompiler(doc, plainScan, { file: null, line: null, severity: 'error', message: 'usage' });
	check('line-less diagnostic goes to line 0', d4.range.start.line === 0);

	// an .inc fragment cannot know its shader mode, so completion must stop filtering by it
	const incDocument = makeDocument('uniform sampler2D uTexture : texture_unit(0);\nvoid fragment() {\n\tCOLOR = texture(TEXTURE, UV);\n}\n',
		'/repo/Sources/Shaders/Include/LightingFs.inc');
	const incItems = await I.completionProvider.provideCompletionItems(incDocument, new Position(2, 10));
	check('an .inc fragment is offered the canvas built-ins despite no shader_type',
		has(incItems, 'TEXTURE') && has(incItems, 'UV') && has(incItems, 'COLOR'));
	check('an .inc fragment is offered the sprite contract', has(incItems, 'vTexCoords'));
	check('an .inc fragment still offers its own uniforms', has(incItems, 'uTexture'));

	// ----- dump modes cover every documented inspection flag
	const flags = Object.keys(I.DUMP_MODES).map(k => I.DUMP_MODES[k].flag).sort().join(' ');
	check('all five dump flags wired', flags === '--cg --check --essl100-check --hlsl --vulkan', flags);

	log('');
	log(pass + ' passed, ' + fail + ' failed');
	if (fail !== 0) exitFailure();
})().catch(e => { log('THREW: ' + e + '\n' + (e.stack || '')); exitFailure(); });
