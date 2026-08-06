// Tests for the pure modules of the extension (src/analysis.js and src/language.js) - the parts that
// hold the real logic and never touch the `vscode` API. They run in any plain JS engine, so no Node
// installation or npm dependency is needed:
//
//     node test/run-tests.js
//     gjs  test/run-tests.js
//
// A non-zero exit code (or a printed FAIL) means something regressed.

'use strict';

var isGjs = (typeof imports !== 'undefined' && typeof imports.gi !== 'undefined');
var log = (typeof print === 'function' ? print : console.log);

var analysis;
var language;

if (isGjs) {
	var GLib = imports.gi.GLib;
	var here = GLib.path_get_dirname(new Error().fileName || 'test/run-tests.js');
	var loadModule = function (relative) {
		var path = GLib.build_filenamev([here, '..', 'src', relative]);
		var parts = GLib.file_get_contents(path);
		var bytes = parts[1];
		var source = (typeof TextDecoder !== 'undefined'
			? new TextDecoder().decode(bytes)
			: imports.byteArray.toString(bytes));
		var box = { exports: {} };
		var factory = new Function('module', 'exports', source);
		factory(box, box.exports);
		return box.exports;
	};
	analysis = loadModule('analysis.js');
	language = loadModule('language.js');
} else {
	analysis = require('../src/analysis.js');
	language = require('../src/language.js');
}

var passed = 0;
var failed = 0;

function check(name, condition, extra) {
	if (condition) {
		passed++;
	} else {
		failed++;
		log('FAIL: ' + name + (extra !== undefined ? ' -- ' + extra : ''));
	}
}

function equal(name, actual, expected) {
	check(name, actual === expected, 'expected ' + JSON.stringify(expected) + ', got ' + JSON.stringify(actual));
}

function contains(name, haystack, needle) {
	check(name, String(haystack).indexOf(needle) >= 0, 'expected to contain ' + JSON.stringify(needle) + ' in ' + JSON.stringify(haystack));
}

// ---------------------------------------------------------------- stripComments

(function () {
	var text = 'a // b\nc /* d\ne */ f\n"g // h"\n';
	var stripped = analysis.stripComments(text);
	equal('stripComments keeps length', stripped.length, text.length);
	equal('stripComments keeps newline count', stripped.split('\n').length, text.split('\n').length);
	contains('stripComments removes line comment body', stripped, 'a     \n');
	check('stripComments removes block comment', stripped.indexOf('d') < 0);
	contains('stripComments keeps include-style strings', stripped, '"g // h"');
	check('stripComments keeps code after a block comment', stripped.indexOf('f') >= 0);
})();

// ---------------------------------------------------------------- brace helpers

(function () {
	var text = 'void f() {\n\tif (x) {\n\t}\n}\n';
	var open = text.indexOf('{');
	equal('matchBrace finds the outer close', analysis.matchBrace(text, open), text.lastIndexOf('}'));
	equal('braceDepthAt inside nested block', analysis.braceDepthAt(text, text.indexOf('if')), 1);
	equal('braceDepthAt at top level', analysis.braceDepthAt(text, text.length), 0);
	equal('matchBrace on an unterminated block', analysis.matchBrace('{ x', 0), -1);
})();

// ---------------------------------------------------------------- scanDocument

var CANVAS_SOURCE = [
	'program Colorized;',
	'shader_type canvas_item;',
	'variant USE_PALETTE;',
	'render_mode blend_mix, unshaded;',
	'precision highp;',
	'batched BatchedColorized;',
	'',
	'uniform sampler2D uTexture : texture_unit(0);',
	'uniform vec4 uColor;',
	'',
	'layout (std140) uniform InstanceBlock',
	'{',
	'\tmat4 modelMatrix;',
	'\tvec4 color;',
	'\tvec2 spriteSize;',
	'};',
	'',
	'attribute vec2 aPosition;',
	'varying flat vec4 vTint;',
	'',
	'vec4 helper(vec4 c) {',
	'\treturn c;',
	'}',
	'',
	'void fragment() {',
	'\tCOLOR = helper(texture(TEXTURE, UV));',
	'}',
	'',
	'void fixed_function(pvr, psp) {',
	'\tpass p;',
	'\tp.color = COLOR;',
	'\tsubmit_quad(p);',
	'}',
	''
].join('\n');

(function () {
	var scan = analysis.scanDocument(CANVAS_SOURCE);
	equal('program name', scan.programs.length !== 0 ? scan.programs[0].name : null, 'Colorized');
	equal('program line', scan.programs[0].line, 0);
	equal('shader_type', scan.shaderType, 'canvas_item');
	equal('variant count', scan.variants.length, 1);
	equal('variant name', scan.variants[0].name, 'USE_PALETTE');
	equal('render mode count', scan.renderModes.length, 2);
	equal('render mode 0', scan.renderModes[0], 'blend_mix');
	equal('render mode 1', scan.renderModes[1], 'unshaded');
	equal('precision', scan.precision, 'highp');
	equal('batched name', scan.batched[0].name, 'BatchedColorized');
	equal('uniform count', scan.uniforms.length, 2);
	equal('sampler uniform name', scan.uniforms[0].name, 'uTexture');
	equal('sampler texture unit', scan.uniforms[0].unit, 0);
	equal('plain uniform has no unit', scan.uniforms[1].unit, null);
	equal('block count', scan.blocks.length, 1);
	equal('block name', scan.blocks[0].name, 'InstanceBlock');
	equal('block member count', scan.blocks[0].members.length, 3);
	equal('block member name', scan.blocks[0].members[1].name, 'color');
	equal('attribute name', scan.attributes[0].name, 'aPosition');
	equal('varying name', scan.varyings[0].name, 'vTint');
	equal('varying type skips qualifiers', scan.varyings[0].type, 'vec4');
	equal('helper function found', scan.functions.length, 1);
	equal('helper function name', scan.functions[0].name, 'helper');
	equal('entry point count', scan.entryPoints.length, 2);
	equal('fragment entry', scan.entryPoints[0].name, 'fragment');
	equal('fixed_function entry', scan.entryPoints[1].name, 'fixed_function');
	equal('fixed_function target count', scan.entryPoints[1].targets.length, 2);
	equal('fixed_function target 0', scan.entryPoints[1].targets[0], 'pvr');
	equal('fixed_function target 1', scan.entryPoints[1].targets[1], 'psp');
	equal('no includes', scan.hasIncludes, false);
})();

(function () {
	var source = 'program P;\n#include "Include/Frag.inc"\n';
	var scan = analysis.scanDocument(source);
	equal('include count', scan.includes.length, 1);
	equal('include path', scan.includes[0].path, 'Include/Frag.inc');
	equal('include line', scan.includes[0].line, 1);
	equal('hasIncludes', scan.hasIncludes, true);
	equal('include offsets bound the path',
		source.substring(scan.includes[0].startOffset, scan.includes[0].endOffset), 'Include/Frag.inc');
})();

(function () {
	// A generic fixed_function block has empty parentheses and therefore no targets
	var scan = analysis.scanDocument('program P;\nvoid fixed_function() {\n\tpass p;\n}\n');
	equal('generic fixed_function has no targets', scan.entryPoints[0].targets.length, 0);
})();

(function () {
	// The three-token GLSL precision statement is not the directive
	var scan = analysis.scanDocument('program P;\nprecision highp float;\n');
	equal('three-token precision is not the directive', scan.precision, null);
})();

(function () {
	// Declarations commented out must not be picked up
	var scan = analysis.scanDocument('program P;\n// uniform vec4 uDead;\n/* varying vec2 vDead; */\n');
	equal('commented uniform ignored', scan.uniforms.length, 0);
	equal('commented varying ignored', scan.varyings.length, 0);
})();

// ---------------------------------------------------------------- contextAt

function contextKindAt(source, marker) {
	var offset = source.indexOf(marker);
	check('marker present: ' + marker, offset >= 0);
	return analysis.contextAt(source, offset + marker.length).kind;
}

(function () {
	equal('top level context', contextKindAt('program P;\nvoid fragment() {\n}\nvar', 'var'), 'topLevel');
	equal('fragment body context',
		contextKindAt('program P;\nvoid fragment() {\n\tCO', 'CO'), 'fragmentBody');
	equal('vertex body context',
		contextKindAt('program P;\nvoid vertex() {\n\tgl_', 'gl_'), 'vertexBody');
	equal('fixed function body context',
		contextKindAt('program P;\nvoid fixed_function() {\n\tsub', 'sub'), 'fixedFunctionBody');
	equal('include path context', contextKindAt('#include "Inc', '"Inc'), 'includePath');
	equal('shader_type argument context', contextKindAt('shader_type can', 'can'), 'directiveArg');
	equal('render_mode argument context', contextKindAt('render_mode blend_', 'blend_'), 'directiveArg');
	equal('uniform hints context',
		contextKindAt('uniform sampler2D uTexture : tex', 'tex'), 'uniformHints');
	equal('fixed function targets context',
		contextKindAt('void fixed_function(pv', 'pv'), 'fixedFunctionTargets');

	var ctx = analysis.contextAt('shader_type can', 'shader_type can'.length);
	equal('directive name is reported', ctx.directive, 'shader_type');
	equal('word prefix is reported', ctx.prefix, 'can');

	var includeCtx = analysis.contextAt('#include "Include/Li', '#include "Include/Li'.length);
	equal('include prefix is the whole partial path', includeCtx.prefix, 'Include/Li');

	// A uniform declaration already terminated must not look like a hint list
	equal('terminated uniform is not a hint list',
		contextKindAt('uniform sampler2D u : texture_unit(0);\nva', 'va'), 'topLevel');
})();

// ---------------------------------------------------------------- diagnostics parsing

(function () {
	var withLine = analysis.parseDiagnosticLine('Sources/Shaders/Tinted.shader:42: error: unknown hint');
	equal('file with line: file', withLine.file, 'Sources/Shaders/Tinted.shader');
	equal('file with line: line', withLine.line, 42);
	equal('file with line: severity', withLine.severity, 'error');
	equal('file with line: message', withLine.message, 'unknown hint');

	var windows = analysis.parseDiagnosticLine('C:\\src\\jazz2\\Tinted.shader:7: error: bad thing');
	equal('windows path keeps the drive letter', windows.file, 'C:\\src\\jazz2\\Tinted.shader');
	equal('windows path line', windows.line, 7);

	var fileOnly = analysis.parseDiagnosticLine('Sources/Shaders/X.shader: error: cannot read input file');
	equal('file only: file', fileOnly.file, 'Sources/Shaders/X.shader');
	equal('file only: line', fileOnly.line, null);
	equal('file only: message', fileOnly.message, 'cannot read input file');

	var bare = analysis.parseDiagnosticLine('error: --glslang requires a path argument');
	equal('bare: file', bare.file, null);
	equal('bare: message', bare.message, '--glslang requires a path argument');

	equal('non-diagnostic line ignored', analysis.parseDiagnosticLine('[HlslCheck] 4/4 stages compiled'), null);
	equal('empty stderr yields nothing', analysis.parseDiagnostics('').length, 0);
	equal('null stderr yields nothing', analysis.parseDiagnostics(null).length, 0);

	var many = analysis.parseDiagnostics('noise line\nA.shader:1: error: one\nB.shader:2: warning: two\n');
	equal('multi-line count', many.length, 2);
	equal('multi-line severity', many[1].severity, 'warning');
})();

// ---------------------------------------------------------------- builtin checks

function messagesOf(findings) {
	var out = [];
	for (var i = 0; i < findings.length; i++) {
		out.push(findings[i].message);
	}
	return out.join(' | ');
}

(function () {
	var ok = analysis.builtinChecks(CANVAS_SOURCE);
	equal('a valid canvas shader has no builtin findings', ok.length, 0, messagesOf(ok));
})();

(function () {
	var custom = 'program P;\nvoid vertex() {\n\tgl_Position = vec4(0.0);\n}\nvoid fragment() {\n\tCOLOR = vec4(1.0);\n}\n';
	equal('a valid custom shader has no builtin findings', analysis.builtinChecks(custom).length, 0,
		messagesOf(analysis.builtinChecks(custom)));
})();

(function () {
	var findings = analysis.builtinChecks('program P;\n#version 330\nvoid vertex() {}\nvoid fragment() {}\n');
	contains('#version reported', messagesOf(findings), 'Do not put #version');
})();

(function () {
	var findings = analysis.builtinChecks('program P;\nvoid vertex() {}\nvoid fragment() {\n\tfragColor = vec4(1.0);\n}\n');
	contains('fragColor reported', messagesOf(findings), "'fragColor' is a parse error");
	// ...but not when it only appears in a comment
	var commented = analysis.builtinChecks('program P;\nvoid vertex() {}\nvoid fragment() {\n\t// fragColor is gone\n\tCOLOR = vec4(1.0);\n}\n');
	equal('fragColor in a comment is not reported', commented.length, 0, messagesOf(commented));
})();

(function () {
	var findings = analysis.builtinChecks('program P;\nbatched B;\nvoid vertex() {}\nvoid fragment() {}\n');
	contains('batched in custom mode reported', messagesOf(findings), "'batched' requires 'shader_type canvas_item;'");
})();

(function () {
	var findings = analysis.builtinChecks('shader_type canvas_item;\nprogram P;\nvoid fragment() {}\n');
	contains('directive order reported', messagesOf(findings), "'program' must precede 'shader_type'");
})();

(function () {
	var findings = analysis.builtinChecks('program P;\nprogram Q;\nvoid vertex() {}\nvoid fragment() {}\n');
	contains('duplicate program reported', messagesOf(findings), "Duplicate 'program' directive");
})();

(function () {
	var findings = analysis.builtinChecks('program P;\n#if defined(VERTEX_STAGE)\n#endif\nvoid vertex() {}\nvoid fragment() {}\n');
	contains('#if defined(VERTEX_STAGE) reported', messagesOf(findings), 'only supported in the #ifdef');
	var good = analysis.builtinChecks('program P;\n#ifdef VERTEX_STAGE\n#endif\nvoid vertex() {}\nvoid fragment() {}\n');
	equal('#ifdef VERTEX_STAGE is fine', good.length, 0, messagesOf(good));
})();

(function () {
	var source = 'program P;\nshader_type canvas_item;\nvoid vertex() {\n\tif (x) { return; }\n}\nvoid fragment() {}\n';
	contains('canvas vertex return reported', messagesOf(analysis.builtinChecks(source)), "'return;' inside a canvas-mode vertex()");
	// The same early return is legal in a custom-mode fragment()
	var customReturn = 'program P;\nvoid vertex() {}\nvoid fragment() {\n\tCOLOR = vec4(1.0);\n\treturn;\n}\n';
	equal('custom fragment return is fine', analysis.builtinChecks(customReturn).length, 0,
		messagesOf(analysis.builtinChecks(customReturn)));
})();

(function () {
	var source = 'program P;\nshader_type canvas_item;\nvoid fragment() {\n\tCOLOR = vec4(TIME);\n}\n';
	contains('unsupported canvas builtin reported', messagesOf(analysis.builtinChecks(source)), "'TIME' is reported as unsupported");
	// In custom mode TIME is an ordinary user identifier
	var custom = 'program P;\nvoid vertex() {}\nvoid fragment() {\n\tCOLOR = vec4(TIME);\n}\n';
	equal('TIME is ordinary in custom mode', analysis.builtinChecks(custom).length, 0,
		messagesOf(analysis.builtinChecks(custom)));
})();

(function () {
	var findings = analysis.builtinChecks('uniform vec4 uColor;\n');
	contains('missing program reported', messagesOf(findings), "Missing 'program");
	contains('missing fragment reported', messagesOf(findings), "Missing 'void fragment()'");
	contains('missing vertex reported', messagesOf(findings), "Missing 'void vertex()'");
})();

(function () {
	// Everything structural may arrive through an include, so those checks must stand down
	var findings = analysis.builtinChecks('program P;\n#include "Include/Everything.inc"\n');
	equal('includes suppress the structural checks', findings.length, 0, messagesOf(findings));
	// A real in-tree shape: vertex here, fragment from the include
	var lightingMesh = 'program LightingMesh;\nvarying vec4 vColor;\nvoid vertex() {\n\tgl_Position = vec4(0.0);\n}\n#include "Include/LightingFs.inc"\n';
	equal('a shader whose fragment comes from an include is clean', analysis.builtinChecks(lightingMesh).length, 0,
		messagesOf(analysis.builtinChecks(lightingMesh)));
})();

(function () {
	// A canvas shader legitimately omits vertex()
	var canvas = 'program P;\nshader_type canvas_item;\nvoid fragment() {\n\tCOLOR = vec4(1.0);\n}\n';
	equal('canvas mode may omit vertex()', analysis.builtinChecks(canvas).length, 0,
		messagesOf(analysis.builtinChecks(canvas)));
})();

// ---------------------------------------------------------------- language vocabulary sanity

(function () {
	function names(list) {
		var out = [];
		for (var i = 0; i < list.length; i++) {
			out.push(typeof list[i] === 'string' ? list[i] : list[i].name);
		}
		return out;
	}

	function noDuplicates(label, list) {
		var seen = {};
		var duplicate = null;
		var all = names(list);
		for (var i = 0; i < all.length; i++) {
			if (seen[all[i]] === true) {
				duplicate = all[i];
			}
			seen[all[i]] = true;
		}
		check(label + ' has no duplicates', duplicate === null, 'duplicate: ' + duplicate);
	}

	function allDocumented(label, list) {
		var missing = null;
		for (var i = 0; i < list.length; i++) {
			var entry = list[i];
			if (typeof entry !== 'string' && (typeof entry.doc !== 'string' || entry.doc.length === 0)) {
				missing = entry.name;
			}
		}
		check(label + ' is fully documented', missing === null, 'undocumented: ' + missing);
	}

	var tables = [
		['DIRECTIVES', language.DIRECTIVES],
		['ENTRY_POINTS', language.ENTRY_POINTS],
		['SHADER_TYPES', language.SHADER_TYPES],
		['RENDER_MODES', language.RENDER_MODES],
		['PRECISION_QUALIFIERS', language.PRECISION_QUALIFIERS],
		['UNIFORM_HINTS', language.UNIFORM_HINTS],
		['FIXED_FUNCTION_TARGETS', language.FIXED_FUNCTION_TARGETS],
		['BUILTINS', language.BUILTINS],
		['STAGE_MACROS', language.STAGE_MACROS],
		['GL_BUILTIN_VARIABLES', language.GL_BUILTIN_VARIABLES],
		['CANVAS_CONTRACT', language.CANVAS_CONTRACT],
		['GLSL_TYPES', language.GLSL_TYPES],
		['GLSL_FUNCTIONS', language.GLSL_FUNCTIONS],
		['GLSL_KEYWORDS', language.GLSL_KEYWORDS],
		['FIXED_FUNCTION.submits', language.FIXED_FUNCTION.submits],
		['FIXED_FUNCTION.context', language.FIXED_FUNCTION.context],
		['FIXED_FUNCTION.passFields', language.FIXED_FUNCTION.passFields],
		['FIXED_FUNCTION.pipelines', language.FIXED_FUNCTION.pipelines],
		['FIXED_FUNCTION.stripHelpers', language.FIXED_FUNCTION.stripHelpers]
	];
	for (var i = 0; i < tables.length; i++) {
		check(tables[i][0] + ' is a non-empty array',
			Object.prototype.toString.call(tables[i][1]) === '[object Array]' && tables[i][1].length !== 0);
		noDuplicates(tables[i][0], tables[i][1]);
		allDocumented(tables[i][0], tables[i][1]);
	}

	// The render modes offered must be exactly the six the parser accepts
	var modes = names(language.RENDER_MODES).sort().join(',');
	equal('render mode set', modes, 'blend_add,blend_mix,blend_mul,blend_premul_alpha,blend_sub,unshaded');
	equal('fixed function target set', names(language.FIXED_FUNCTION_TARGETS).sort().join(','), 'gs,gx,psp,pvr');
	equal('pass field set', names(language.FIXED_FUNCTION.passFields).sort().join(','),
		'blend,color,luma_gain,offset_color,screen_offset,tev');
	equal('blend mode set', language.FIXED_FUNCTION.blendModes.slice().sort().join(','), 'ADD,ALPHA,MATERIAL,OPAQUE');
	equal('tev preset set', language.FIXED_FUNCTION.tevPresets.slice().sort().join(','),
		'LUMA_RAMP,MODULATE,MODULATE_X2,MODULATE_X4,SILHOUETTE,TINT_MIX');
	equal('pipeline intrinsic set', names(language.FIXED_FUNCTION.pipelines).sort().join(','),
		'lighting_combine,line_strip_mesh,tile_map_mesh');

	// Anything with a snippet placeholder must be inserted as a snippet, so check the markers are sane
	var withPlaceholders = language.DIRECTIVES.concat(language.ENTRY_POINTS);
	var broken = null;
	for (var k = 0; k < withPlaceholders.length; k++) {
		var insert = withPlaceholders[k].insert;
		if (typeof insert === 'string' && insert.indexOf('${') >= 0) {
			// balanced braces in the placeholder syntax
			var opens = insert.split('${').length - 1;
			var closes = insert.split('}').length - 1;
			if (opens > closes) {
				broken = withPlaceholders[k].name;
			}
		}
	}
	check('snippet placeholders are balanced', broken === null, 'broken: ' + broken);
})();

(function () {
	// A ".inc" include fragment is pasted into some other file, so it legitimately has no program
	// directive, no vertex() and no shader_type of its own - this is the real shape of the in-tree
	// Sources/Shaders/Include/*.inc files
	var fragment = [
		'uniform sampler2D uTexture : texture_unit(0);',
		'',
		'float lightBlend(float t) {',
		'\treturn t * t * t;',
		'}',
		'',
		'void fragment() {',
		'\tCOLOR = vec4(lightBlend(0.5));',
		'}',
		''
	].join('\n');
	var asFragment = analysis.builtinChecks(fragment, null, { isIncludeFragment: true });
	equal('an include fragment is clean when flagged as one', asFragment.length, 0, messagesOf(asFragment));
	var asShader = analysis.builtinChecks(fragment, null);
	check('the same text WOULD be reported as a standalone .shader', asShader.length !== 0);
	contains('...specifically for the missing program', messagesOf(asShader), "Missing 'program");

	// 'batched' cannot be judged in a fragment either - the shader_type lives in the including file
	var batchedFragment = analysis.builtinChecks('batched B;\nvoid fragment() {}\n', null, { isIncludeFragment: true });
	equal('batched in a fragment is not judged', batchedFragment.length, 0, messagesOf(batchedFragment));

	// ...but the checks that do not depend on file structure still apply to a fragment
	var badFragment = analysis.builtinChecks('#version 330\nvoid fragment() {\n\tfragColor = vec4(1.0);\n}\n',
		null, { isIncludeFragment: true });
	contains('#version still reported in a fragment', messagesOf(badFragment), 'Do not put #version');
	contains('fragColor still reported in a fragment', messagesOf(badFragment), "'fragColor' is a parse error");

	// the option object is optional and a missing/false flag behaves as before
	equal('an explicit false flag behaves like no options',
		analysis.builtinChecks(fragment, null, { isIncludeFragment: false }).length, asShader.length);
})();

// ---------------------------------------------------------------- summary

log('');
log(passed + ' passed, ' + failed + ' failed');
if (failed !== 0 && typeof process !== 'undefined') {
	process.exitCode = 1;
}
if (failed !== 0 && isGjs) {
	imports.system.exit(1);
}
