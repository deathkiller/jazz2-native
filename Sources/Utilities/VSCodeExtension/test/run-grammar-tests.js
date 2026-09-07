// Tests for the TextMate grammar (syntaxes/deathshader.tmLanguage.json).
//
// Two layers, because the grammar is data rather than code:
//
//   1. Structure and vocabulary. Reads the grammar as JSON and checks that it holds together (every
//      `include` resolves, every repository entry is reachable, every capture index exists in its
//      own pattern, every scope belongs to this language) and that it still spells the same language
//      src/language.js documents. Needs nothing at all:
//
//          node test/run-grammar-tests.js
//          gjs  test/run-grammar-tests.js
//
//   2. Real tokenization, which is the only way to prove what a rule actually does. Needs an
//      Oniguruma-backed TextMate engine, and there is no npm dependency in this tree, so it runs
//      against the copy inside an installed VS Code:
//
//          set ELECTRON_RUN_AS_NODE=1                     (Windows; export on Linux/macOS)
//          "%LOCALAPPDATA%\Programs\Microsoft VS Code\Code.exe" test/run-grammar-tests.js
//
//      Point DEATH_SHADER_VSCODE_APP at the `resources/app` directory if it is not found on its own.
//      With no engine around this layer prints SKIP and the run still passes - it is a stricter
//      check, not a required one.
//
// A non-zero exit code (or a printed FAIL) means something regressed.

'use strict';

var isGjs = (typeof imports !== 'undefined' && typeof imports.gi !== 'undefined');
var log = (typeof print === 'function' ? print : console.log);

var GRAMMAR_RELATIVE = 'syntaxes/deathshader.tmLanguage.json';

var grammarText;
var language;

if (isGjs) {
	var GLib = imports.gi.GLib;
	var here = GLib.path_get_dirname(new Error().fileName || 'test/run-grammar-tests.js');
	var readFile = function (relative) {
		var parts = GLib.file_get_contents(GLib.build_filenamev([here, '..'].concat(relative.split('/'))));
		var bytes = parts[1];
		return (typeof TextDecoder !== 'undefined' ? new TextDecoder().decode(bytes) : imports.byteArray.toString(bytes));
	};
	grammarText = readFile(GRAMMAR_RELATIVE);
	var box = { exports: {} };
	(new Function('module', 'exports', readFile('src/language.js')))(box, box.exports);
	language = box.exports;
} else {
	var fs = require('fs');
	var path = require('path');
	grammarText = fs.readFileSync(path.join(__dirname, '..', GRAMMAR_RELATIVE), 'utf8');
	language = require('../src/language.js');
}

var grammar = JSON.parse(grammarText);

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

// Walks every rule of the grammar, root patterns and repository alike
function eachRule(visit) {
	var seen = [];
	var walk = function (node, where) {
		if (!node || typeof node !== 'object' || seen.indexOf(node) >= 0) {
			return;
		}
		seen.push(node);
		if (node.match || node.begin || node.include || node.name) {
			visit(node, where);
		}
		var keys = ['patterns', 'captures', 'beginCaptures', 'endCaptures'];
		for (var i = 0; i < keys.length; i++) {
			var child = node[keys[i]];
			if (!child) {
				continue;
			}
			for (var key in child) {
				if (Object.prototype.hasOwnProperty.call(child, key)) {
					walk(child[key], where + '/' + keys[i] + '[' + key + ']');
				}
			}
		}
	};
	for (var i = 0; i < (grammar.patterns || []).length; i++) {
		walk(grammar.patterns[i], 'patterns[' + i + ']');
	}
	for (var key in grammar.repository) {
		if (Object.prototype.hasOwnProperty.call(grammar.repository, key)) {
			walk(grammar.repository[key], '#' + key);
		}
	}
}

// Number of capturing groups in an Oniguruma pattern: an unescaped '(' outside a character class
// that does not open a group extension '(?...'
function captureCount(pattern) {
	var count = 0;
	var inClass = false;
	for (var i = 0; i < pattern.length; i++) {
		var c = pattern.charAt(i);
		if (c === '\\') {
			i++;
		} else if (inClass) {
			if (c === ']') {
				inClass = false;
			}
		} else if (c === '[') {
			inClass = true;
		} else if (c === '(' && pattern.charAt(i + 1) !== '?') {
			count++;
		}
	}
	return count;
}

// ---------------------------------------------------------------- structure

(function () {
	equal('scope name', grammar.scopeName, 'source.deathshader');
	check('registers .shader and .inc', (grammar.fileTypes || []).join(',') === 'shader,inc', grammar.fileTypes);

	var missing = [];
	var referenced = {};
	eachRule(function (rule, where) {
		if (typeof rule.include !== 'string' || rule.include.charAt(0) !== '#') {
			return;
		}
		var key = rule.include.substring(1);
		referenced[key] = true;
		if (!Object.prototype.hasOwnProperty.call(grammar.repository, key)) {
			missing.push(where + ' -> ' + rule.include);
		}
	});
	equal('every include resolves to a repository entry', missing.length, 0, missing.join(', '));

	var unreachable = [];
	for (var key in grammar.repository) {
		if (Object.prototype.hasOwnProperty.call(grammar.repository, key) && !referenced[key]) {
			unreachable.push('#' + key);
		}
	}
	equal('no orphaned repository entry', unreachable.length, 0, unreachable.join(', '));

	var shapeErrors = [];
	var captureErrors = [];
	var scopeErrors = [];
	eachRule(function (rule, where) {
		if (rule.begin && !rule.end && !rule.while) {
			shapeErrors.push(where + ' has begin without end');
		}
		if (rule.end && !rule.begin) {
			shapeErrors.push(where + ' has end without begin');
		}
		if (rule.match && rule.begin) {
			shapeErrors.push(where + ' has both match and begin');
		}
		var pairs = [['match', 'captures'], ['begin', 'beginCaptures'], ['end', 'endCaptures']];
		for (var i = 0; i < pairs.length; i++) {
			var pattern = rule[pairs[i][0]];
			var captures = rule[pairs[i][1]];
			if (!pattern || !captures) {
				continue;
			}
			var groups = captureCount(pattern);
			for (var index in captures) {
				if (Object.prototype.hasOwnProperty.call(captures, index) && Number(index) > groups) {
					captureErrors.push(where + ' ' + pairs[i][1] + '[' + index + '] but ' + pairs[i][0] + ' has ' + groups + ' groups');
				}
			}
		}
		if (typeof rule.name === 'string' && rule.name.length && !/\.deathshader$/.test(rule.name) && rule.name.indexOf('meta.') !== 0) {
			scopeErrors.push(where + ' -> ' + rule.name);
		}
	});
	equal('every rule is a match or a begin/end pair', shapeErrors.length, 0, shapeErrors.join('; '));
	equal('every capture index exists in its pattern', captureErrors.length, 0, captureErrors.join('; '));
	equal('every scope belongs to this language', scopeErrors.length, 0, scopeErrors.join('; '));
})();

// ---------------------------------------------------------------- vocabulary parity with language.js

(function () {
	// The grammar and language.js are written separately but describe one language, so every name the
	// hover documents has to be a name the grammar colours. A miss here is the two drifting apart.
	function names(list) {
		var out = [];
		for (var i = 0; i < list.length; i++) {
			out.push(typeof list[i] === 'string' ? list[i] : list[i].name);
		}
		return out;
	}

	var groups = {
		directives: names(language.DIRECTIVES),
		'entry points': names(language.ENTRY_POINTS),
		'shader types': names(language.SHADER_TYPES),
		'render modes': names(language.RENDER_MODES),
		'precision qualifiers': names(language.PRECISION_QUALIFIERS),
		'uniform hints': names(language.UNIFORM_HINTS),
		'fixed-function targets': names(language.FIXED_FUNCTION_TARGETS),
		'canvas built-ins': names(language.BUILTINS),
		'unsupported built-ins': names(language.UNSUPPORTED_BUILTINS),
		'stage macros': names(language.STAGE_MACROS),
		'block statements': names(language.FIXED_FUNCTION.statements),
		submits: names(language.FIXED_FUNCTION.submits),
		'strip helpers': names(language.FIXED_FUNCTION.stripHelpers),
		'context facilities': names(language.FIXED_FUNCTION.context),
		'pass fields': names(language.FIXED_FUNCTION.passFields),
		'blend modes': names(language.FIXED_FUNCTION.blendModes),
		'tev presets': names(language.FIXED_FUNCTION.tevPresets),
		pipelines: names(language.FIXED_FUNCTION.pipelines),
		'GLSL types': names(language.GLSL_TYPES),
		'GLSL functions': names(language.GLSL_FUNCTIONS),
		'GLSL keywords': language.GLSL_KEYWORDS
	};

	for (var label in groups) {
		if (!Object.prototype.hasOwnProperty.call(groups, label)) {
			continue;
		}
		var absent = [];
		var list = groups[label];
		for (var i = 0; i < list.length; i++) {
			if (grammarText.indexOf(list[i]) < 0) {
				absent.push(list[i]);
			}
		}
		equal('the grammar knows every one of the ' + label, absent.length, 0, absent.join(', '));
	}
})();

// ---------------------------------------------------------------- tokenization

var FIXTURES = [
	{
		name: 'a statement after a nested block is still fixed-function',
		// The block's end pattern used to be `^\s*\}`, so the closing brace of the inner `if` ended it
		// and everything below was scoped as ordinary GLSL
		text: 'program P;\nvoid fixed_function() {\n\tif (COLOR.a > 0.0) {\n\t\tpass inner;\n\t}\n\tpass tail;\n\tsubmit_quad(tail);\n}\n',
		expect: [
			['pass', 6, 'storage.type.pass.deathshader'],
			['tail', 6, 'variable.other.pass.deathshader'],
			['submit_quad', 7, 'support.function.submit.deathshader']
		]
	},
	{
		name: 'a ternary is not a uniform hint list',
		// The hint rule used to begin at any ':' followed by a ';', which caught every ternary
		text: 'program P;\nvoid fragment() {\n\tCOLOR = vec4(UV.x > 0.5 ? 1.0 : 0.0);\n}\n',
		expect: [
			[':', 3, 'keyword.operator.deathshader'],
			['?', 3, 'keyword.operator.deathshader'],
			['0.0', 3, 'constant.numeric.deathshader']
		]
	},
	{
		name: 'a uniform hint list still reads as one',
		text: 'program P;\nuniform sampler2D uTexture : texture_unit(0), bogus;\n',
		expect: [
			['texture_unit', 2, 'support.function.hint.texture-unit.deathshader'],
			['bogus', 2, 'invalid.illegal.hint.deathshader']
		]
	},
	{
		name: 'precision with a type is plain GLSL, not the directive',
		text: 'program P;\nprecision highp float;\nprecision mediump;\n',
		expect: [
			['precision', 2, 'storage.modifier.deathshader'],
			['precision', 3, 'keyword.other.directive.precision.deathshader'],
			['mediump', 3, 'constant.language.precision.deathshader']
		]
	},
	{
		name: 'a block accepts only its own maths subset',
		// The body is transpiled to C++ once per draw - the GLSL built-ins are not its vocabulary
		text: 'program P;\nvoid fixed_function() {\n\tfloat a = clamp(0.0, 0.0, 1.0);\n\tfloat b = smoothstep(0.0, 1.0, a);\n}\n',
		expect: [
			['clamp', 3, 'support.function.fixed-function.deathshader'],
			['smoothstep', 4, 'entity.name.function.call.deathshader']
		]
	},
	{
		name: 'the two enum pass fields carry their value',
		text: 'program P;\nvoid fixed_function(gx) {\n\tpass p;\n\tp.tev = LUMA_RAMP;\n\tp.blend = ADD;\n\tp.tev = NOPE;\n}\n',
		expect: [
			['LUMA_RAMP', 4, 'constant.language.tev-preset.deathshader'],
			['ADD', 5, 'constant.language.blend-mode.deathshader'],
			['NOPE', 6, 'invalid.illegal.tev-preset.deathshader']
		]
	},
	{
		name: 'an unclosed brace recovers at the next top-level keyword',
		text: 'program P;\nvoid fixed_function() {\n\tif (true) {\n\t\tpass p;\n\nuniform vec4 uAfter;\n',
		expect: [
			['uniform', 6, 'storage.modifier.uniform.deathshader'],
			['vec4', 6, 'storage.type.deathshader']
		]
	},
	{
		name: 'the block vocabulary reads in an include fragment too',
		// An .inc holding a block body is pasted into one, so it has no block of its own to sit in
		text: 'pass fallback;\nfallback.color = COLOR;\nstrip_position(0, quad_origin());\nsubmit_strip_shaded(fallback, 4);\n',
		expect: [
			['pass', 1, 'storage.type.pass.deathshader'],
			['color', 2, 'variable.other.property.pass-field.deathshader'],
			['strip_position', 3, 'support.function.strip.deathshader'],
			['quad_origin', 3, 'support.function.context.deathshader'],
			['submit_strip_shaded', 4, 'support.function.submit.deathshader']
		]
	}
];

// Resolves vscode-textmate/vscode-oniguruma from an npm install if there is one, and otherwise from
// an installed VS Code - whose copies live inside node_modules.asar, readable only by the Electron
// binary running as Node (see the header). Returns null when neither is around.
function loadEngine() {
	if (isGjs || typeof require !== 'function') {
		return null;
	}
	var roots = [null];
	if (process.env.DEATH_SHADER_VSCODE_APP) {
		roots.push(path.join(process.env.DEATH_SHADER_VSCODE_APP, 'node_modules.asar'));
		roots.push(path.join(process.env.DEATH_SHADER_VSCODE_APP, 'node_modules'));
	}
	// When this file is run by Code.exe itself, the app directory is next to the executable - either
	// directly or under the versioned directory a staged update leaves behind
	var near = path.dirname(process.execPath);
	var candidates = [near];
	try {
		var entries = fs.readdirSync(near);
		for (var i = 0; i < entries.length; i++) {
			candidates.push(path.join(near, entries[i]));
		}
	} catch (e) { /* not a directory we may read - the npm path may still work */ }
	for (var c = 0; c < candidates.length; c++) {
		roots.push(path.join(candidates[c], 'resources', 'app', 'node_modules.asar'));
		roots.push(path.join(candidates[c], 'resources', 'app', 'node_modules'));
	}

	for (var r = 0; r < roots.length; r++) {
		try {
			var root = roots[r];
			var textmate = require(root === null ? 'vscode-textmate' : path.join(root, 'vscode-textmate'));
			var oniguruma = require(root === null ? 'vscode-oniguruma' : path.join(root, 'vscode-oniguruma'));
			var wasm = null;
			var wasmCandidates = root === null
				? [path.join(path.dirname(require.resolve('vscode-oniguruma')), 'onig.wasm')]
				: [path.join(root, 'vscode-oniguruma', 'release', 'onig.wasm'),
					path.join(root + '.unpacked', 'vscode-oniguruma', 'release', 'onig.wasm')];
			for (var w = 0; w < wasmCandidates.length; w++) {
				try {
					wasm = fs.readFileSync(wasmCandidates[w]);
					break;
				} catch (e) { /* try the next layout */ }
			}
			if (!wasm) {
				continue;
			}
			return { textmate: textmate, oniguruma: oniguruma, wasm: wasm };
		} catch (e) { /* try the next root */ }
	}
	return null;
}

// The strongest check there is, and the one that found the two bugs this file guards against: every
// shader the compiler accepts, tokenized whole. Nothing in them may scope as an error, and no line of
// a fixed_function body may fall out of the block. Skipped when the extension is not sitting in the
// tree (Sources/Utilities/VSCodeExtension), since there is nothing to read then.
function checkRealShaders(compiled) {
	var shaders = path.join(__dirname, '..', '..', '..', 'Shaders');
	var files = [];
	try {
		var roots = [shaders, path.join(shaders, 'Include')];
		for (var r = 0; r < roots.length; r++) {
			var entries = fs.readdirSync(roots[r]);
			for (var i = 0; i < entries.length; i++) {
				if (/\.(shader|inc)$/.test(entries[i])) {
					files.push(path.join(roots[r], entries[i]));
				}
			}
		}
	} catch (e) {
		log('SKIP: the shaders of the tree are not next to the extension');
		return;
	}

	var errors = [];
	var escaped = [];
	for (var f = 0; f < files.length; f++) {
		var lines = fs.readFileSync(files[f], 'utf8').split(/\r\n|\r|\n/);
		var stack = engine.textmate.INITIAL;
		var inBlock = false;
		for (var n = 0; n < lines.length; n++) {
			var result = compiled.tokenizeLine(lines[n], stack);
			var where = path.basename(files[f]) + ':' + (n + 1);
			var covered = true;
			for (var t = 0; t < result.tokens.length; t++) {
				var token = result.tokens[t];
				var text = lines[n].substring(token.startIndex, token.endIndex);
				if (!text.replace(/\s/g, '').length) {
					continue;
				}
				for (var s = 0; s < token.scopes.length; s++) {
					if (token.scopes[s].indexOf('invalid.') === 0) {
						errors.push(where + ' ' + JSON.stringify(text) + ' -> ' + token.scopes[s]);
					}
				}
				if (token.scopes.join(' ').indexOf('fixed-function') < 0) {
					covered = false;
				}
			}
			if (/^\s*void\s+fixed_function\s*\(/.test(lines[n])) {
				inBlock = true;
			} else if (inBlock) {
				if (/^\}/.test(lines[n])) {
					inBlock = false;
				} else if (!covered) {
					escaped.push(where + ' ' + lines[n].trim());
				}
			}
			stack = result.ruleStack;
		}
	}
	check('the ' + files.length + ' shaders of the tree hold nothing scoped as an error', errors.length === 0, errors.slice(0, 6).join('; '));
	check('no fixed_function body loses its block', escaped.length === 0, escaped.slice(0, 6).join('; '));
}

function finish() {
	log('');
	log(passed + ' passed, ' + failed + ' failed');
	if (failed !== 0 && typeof process !== 'undefined') {
		process.exitCode = 1;
	}
	if (failed !== 0 && isGjs) {
		imports.system.exit(1);
	}
}

var engine = loadEngine();
if (!engine) {
	log('SKIP: tokenization tests (no vscode-textmate around - see the header of this file)');
	finish();
} else {
	engine.oniguruma.loadWASM(engine.wasm.buffer.slice(engine.wasm.byteOffset, engine.wasm.byteOffset + engine.wasm.byteLength)).then(function () {
		var registry = new engine.textmate.Registry({
			onigLib: Promise.resolve({
				createOnigScanner: function (sources) { return new engine.oniguruma.OnigScanner(sources); },
				createOnigString: function (s) { return new engine.oniguruma.OnigString(s); }
			}),
			loadGrammar: function () {
				return Promise.resolve(engine.textmate.parseRawGrammar(grammarText, GRAMMAR_RELATIVE));
			}
		});
		return registry.loadGrammar('source.deathshader');
	}).then(function (compiled) {
		for (var f = 0; f < FIXTURES.length; f++) {
			var fixture = FIXTURES[f];
			var lines = fixture.text.split('\n');
			var stack = engine.textmate.INITIAL;
			var scopes = [];
			for (var i = 0; i < lines.length; i++) {
				var result = compiled.tokenizeLine(lines[i], stack);
				for (var t = 0; t < result.tokens.length; t++) {
					var token = result.tokens[t];
					scopes.push({
						line: i + 1,
						text: lines[i].substring(token.startIndex, token.endIndex),
						scope: token.scopes[token.scopes.length - 1]
					});
				}
				stack = result.ruleStack;
			}
			for (var e = 0; e < fixture.expect.length; e++) {
				var want = fixture.expect[e];
				var found = null;
				for (var s = 0; s < scopes.length; s++) {
					if (scopes[s].line === want[1] && scopes[s].text === want[0]) {
						found = scopes[s].scope;
						break;
					}
				}
				equal(fixture.name + ': ' + JSON.stringify(want[0]) + ' on line ' + want[1], found, want[2]);
			}
		}
		checkRealShaders(compiled);
		finish();
	}).catch(function (e) {
		check('tokenization tests ran', false, String(e && e.stack ? e.stack : e));
		finish();
	});
}
