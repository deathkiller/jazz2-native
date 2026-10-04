// Runs the WebAssembly build of AssetPacker off the page's thread. The page posts the console image and the files of
// the original game, and gets back the log as it is written and the new image at the end (see index.html).
//
// Nothing is copied into memory up front: the user's files are mounted read-only (WORKERFS reads them from disk as
// the tool asks), the game's own content is part of the module (AssetPacker.data, at "/Content"), and the converted
// tree lives in memory only while the image is being written. The image itself is written to a device that collects
// it into blobs, which the browser may keep on disk - a Dreamcast image is over 700 MB.

/* global createAssetPacker */
importScripts('AssetPacker.js');

// Blobs of up to this many bytes are formed while the image is written, so the pieces do not pile up in memory
const BlobPartSize = 64 * 1024 * 1024;

function post(type, data) {
	self.postMessage(Object.assign({ type }, data));
}

/**
	Makes a device that takes the bytes written to it and keeps them as blobs. Writing an image of hundreds of
	megabytes to the module's in-memory file system would keep reallocating one contiguous buffer as it grows.
*/
function createImageSink(FS, path) {
	let pending = [];
	let pendingSize = 0;
	const blobs = [];
	let total = 0;

	const flush = () => {
		if (pending.length > 0) {
			blobs.push(new Blob(pending));
			pending = [];
			pendingSize = 0;
		}
	};

	const device = FS.makedev(64, 0);
	FS.registerDevice(device, {
		open() {},
		close() {},
		read() {
			throw new FS.ErrnoError(28 /* EINVAL */);
		},
		write(stream, buffer, offset, length) {
			// The buffer is a view into the module's memory, so it is copied out
			pending.push(buffer.slice(offset, offset + length));
			pendingSize += length;
			total += length;
			if (pendingSize >= BlobPartSize) {
				flush();
			}
			return length;
		}
	});
	FS.mkdev(path, 0o666, device);

	return {
		finish() {
			flush();
			return new Blob(blobs, { type: 'application/octet-stream' });
		},
		get size() {
			return total;
		}
	};
}

self.onmessage = async (e) => {
	const { image, sources, extraArgs } = e.data;
	const startTime = Date.now();

	let sink = null;
	try {
		const module = await createAssetPacker({
			print: (text) => post('log', { text, level: 'info' }),
			printErr: (text) => post('log', { text, level: 'error' }),
			preRun: [(m) => {
				const FS = m.FS;
				// The image and the original files, read straight from the user's disk
				FS.mkdir('/input');
				FS.mount(FS.filesystems.WORKERFS, {
					blobs: [{ name: 'image/' + image.name, data: image }]
						.concat(sources.map((s) => ({ name: 'source/' + s.path, data: s.file })))
				}, '/input');
				FS.mkdir('/output');
				sink = createImageSink(FS, '/output/' + image.name);
			}]
		});

		post('status', { text: 'Converting...' });
		const args = ['swap-content', '/input/image/' + image.name, '/output/' + image.name,
			'--source=/input/source', '--content=/Content'].concat(extraArgs || []);
		const exitCode = module.callMain(args);
		if (exitCode !== 0) {
			post('failed', { text: 'AssetPacker failed (exit code ' + exitCode + ')', elapsed: Date.now() - startTime });
			return;
		}

		const blob = sink.finish();
		post('done', { blob, name: image.name, size: blob.size, elapsed: Date.now() - startTime });
	} catch (error) {
		post('failed', { text: String(error && error.message ? error.message : error), elapsed: Date.now() - startTime });
	}
};
