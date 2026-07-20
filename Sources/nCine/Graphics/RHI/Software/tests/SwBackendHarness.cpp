// Standalone harness for the integrated software RHI backend.
//
// It drives the `RHI::Software::Sw*` types through the `nCine::RHI::` contract aliases exactly the way
// `RenderCommand::Issue()` drives them for a sprite: it builds a render-target framebuffer, a DefaultSprite
// program fed the offline reflection, a checker/solid texture, vertex/index buffers and a vertex array,
// fills the InstanceBlock (ortho projection, identity view, a placing model matrix, color, texRect,
// spriteSize), then `Use()`s the program, binds, commits and issues `Device::DrawArrays`/`DrawElements`.
// Finally it asserts known pixels and writes the framebuffer to a PNG.
//
// Build (from the scratchpad, MSVC): see the cl.exe command in the accompanying run; the backend needs
// only RhiTypes.h, Primitives and ShaderCompilerTypes.h besides its own sources.

// WITH_RHI_SOFTWARE is defined on the compiler command line so every translation unit sees it

#include "nCine/Graphics/RHI/Software/SwBackend.h"
#include "nCine/Graphics/RHI/Software/SwRaster.h"
#include "Shaders/Generated/DefaultSprite.h"
#include "Shaders/Generated/TexturedBackground.h"
#include "Shaders/Generated/TexturedBackgroundCircle.h"
#include "Shaders/Generated/Combine.h"
#include "Shaders/Generated/PaletteRemap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

using namespace nCine;

namespace
{
	// ---- Minimal RGBA8 PNG writer (stored/uncompressed DEFLATE, so no zlib dependency) ----

	void PutBE32(std::vector<std::uint8_t>& out, std::uint32_t v)
	{
		out.push_back(std::uint8_t(v >> 24));
		out.push_back(std::uint8_t(v >> 16));
		out.push_back(std::uint8_t(v >> 8));
		out.push_back(std::uint8_t(v));
	}

	std::uint32_t Crc32(const std::uint8_t* data, std::size_t length)
	{
		std::uint32_t crc = 0xFFFFFFFFu;
		for (std::size_t i = 0; i < length; i++) {
			crc ^= data[i];
			for (int b = 0; b < 8; b++) {
				crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
			}
		}
		return crc ^ 0xFFFFFFFFu;
	}

	std::uint32_t Adler32(const std::uint8_t* data, std::size_t length)
	{
		std::uint32_t a = 1, b = 0;
		for (std::size_t i = 0; i < length; i++) {
			a = (a + data[i]) % 65521u;
			b = (b + a) % 65521u;
		}
		return (b << 16) | a;
	}

	void WriteChunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data)
	{
		PutBE32(png, std::uint32_t(data.size()));
		std::vector<std::uint8_t> typeAndData;
		typeAndData.insert(typeAndData.end(), type, type + 4);
		typeAndData.insert(typeAndData.end(), data.begin(), data.end());
		png.insert(png.end(), typeAndData.begin(), typeAndData.end());
		PutBE32(png, Crc32(typeAndData.data(), typeAndData.size()));
	}

	bool WritePng(const char* path, const std::uint8_t* rgba, std::int32_t width, std::int32_t height, std::int32_t stride)
	{
		// Filtered raw data: one filter byte (0 = none) per scanline followed by the RGBA row
		std::vector<std::uint8_t> raw;
		raw.reserve(std::size_t(height) * (1 + std::size_t(width) * 4));
		for (std::int32_t y = 0; y < height; y++) {
			raw.push_back(0);
			const std::uint8_t* row = rgba + std::size_t(y) * stride;
			raw.insert(raw.end(), row, row + std::size_t(width) * 4);
		}

		// zlib stream wrapping stored DEFLATE blocks
		std::vector<std::uint8_t> zlib;
		zlib.push_back(0x78);
		zlib.push_back(0x01);
		std::size_t offset = 0;
		while (offset < raw.size()) {
			std::size_t blockLen = raw.size() - offset;
			if (blockLen > 65535) {
				blockLen = 65535;
			}
			const bool last = (offset + blockLen >= raw.size());
			zlib.push_back(last ? 1 : 0);
			zlib.push_back(std::uint8_t(blockLen & 0xFF));
			zlib.push_back(std::uint8_t((blockLen >> 8) & 0xFF));
			const std::uint16_t nlen = std::uint16_t(~std::uint16_t(blockLen));
			zlib.push_back(std::uint8_t(nlen & 0xFF));
			zlib.push_back(std::uint8_t((nlen >> 8) & 0xFF));
			zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + blockLen);
			offset += blockLen;
		}
		const std::uint32_t adler = Adler32(raw.data(), raw.size());
		PutBE32(zlib, adler);

		std::vector<std::uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
		std::vector<std::uint8_t> ihdr;
		PutBE32(ihdr, std::uint32_t(width));
		PutBE32(ihdr, std::uint32_t(height));
		ihdr.push_back(8);		// bit depth
		ihdr.push_back(6);		// color type: RGBA
		ihdr.push_back(0);		// compression
		ihdr.push_back(0);		// filter
		ihdr.push_back(0);		// interlace
		WriteChunk(png, "IHDR", ihdr);
		WriteChunk(png, "IDAT", zlib);
		WriteChunk(png, "IEND", std::vector<std::uint8_t>{});

		std::ofstream file(path, std::ios::binary);
		if (!file) {
			return false;
		}
		file.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
		return file.good();
	}

	// ---- Pixel assertions ----

	int g_checks = 0;
	int g_failures = 0;

	void MakeTranslation(float tx, float ty, float* m)
	{
		for (int i = 0; i < 16; i++) {
			m[i] = 0.0f;
		}
		m[0] = m[5] = m[10] = m[15] = 1.0f;
		m[12] = tx;
		m[13] = ty;
	}

	const std::uint8_t* PixelAt(const std::uint8_t* pixels, std::int32_t stride, std::int32_t x, std::int32_t y)
	{
		return pixels + std::size_t(y) * stride + std::size_t(x) * 4;
	}

	void CheckPixel(const std::uint8_t* pixels, std::int32_t stride, std::int32_t x, std::int32_t y,
		int r, int g, int b, int a, int tolerance, const char* label)
	{
		g_checks++;
		const std::uint8_t* p = PixelAt(pixels, stride, x, y);
		const bool ok = std::abs(int(p[0]) - r) <= tolerance && std::abs(int(p[1]) - g) <= tolerance &&
			std::abs(int(p[2]) - b) <= tolerance && std::abs(int(p[3]) - a) <= tolerance;
		if (!ok) {
			g_failures++;
			std::printf("  FAIL %-28s (%3d,%3d) = (%3d,%3d,%3d,%3d), expected (%3d,%3d,%3d,%3d)\n",
				label, x, y, p[0], p[1], p[2], p[3], r, g, b, a);
		} else {
			std::printf("  ok   %-28s (%3d,%3d) = (%3d,%3d,%3d,%3d)\n", label, x, y, p[0], p[1], p[2], p[3]);
		}
	}

	// ---- Uniform-buffer suballocator (registered with the backend, backed by one host buffer) ----

	RHI::Buffer* g_uniformBuffer = nullptr;
	std::uint32_t g_uniformBump = 0;

	RHI::BufferRange AllocUniformRange(std::uint32_t bytes)
	{
		RHI::BufferRange range;
		const std::uint32_t alignedOffset = (g_uniformBump + 15u) & ~15u;
		range.object = g_uniformBuffer;
		range.offset = alignedOffset;
		range.size = bytes;
		range.mapBase = g_uniformBuffer->HostData();
		g_uniformBump = alignedOffset + bytes;
		return range;
	}

	// ---- One sprite draw, replaying CommitAll() + Issue() ----

	enum class DrawKind { Arrays, Elements };

	void DrawSprite(RHI::ShaderProgram& program, RHI::ShaderUniforms& camera, RHI::ShaderUniformBlocks& blocks,
		RHI::Texture& texture, RHI::Buffer& vbo, RHI::Buffer& ibo,
		const float* model, const float* color, const float* texRect, const float* spriteSize,
		bool blend, BlendingFactor srcFactor, BlendingFactor dstFactor, const Recti* scissor, DrawKind kind)
	{
		// Fill the InstanceBlock members (the equivalent of the node transform + material uniforms)
		auto* instanceBlock = blocks.GetUniformBlock("InstanceBlock");
		instanceBlock->GetUniform("modelMatrix")->SetFloatVector(model);
		instanceBlock->GetUniform("color")->SetFloatVector(color);
		instanceBlock->GetUniform("texRect")->SetFloatVector(texRect);
		instanceBlock->GetUniform("spriteSize")->SetFloatVector(spriteSize);

		// CommitAll(): copy the block into the streaming uniform buffer
		blocks.CommitUniformBlocks();

		// Issue(): material.Bind() -> textures, program, uniform blocks
		texture.Bind(0);
		program.Use();
		blocks.Bind();
		// material.CommitUniforms() -> loose (camera) uniforms
		camera.CommitUniforms();

		// Blend + scissor state, as RenderCommand::Issue() sets around the draw
		RHI::Device::SetBlendingEnabled(blend);
		if (blend) {
			RHI::Device::SetBlendingFactors(srcFactor, dstFactor, BlendingFactor::One, dstFactor);
		}
		RHI::Device::ScissorState savedScissor = RHI::Device::GetScissorState();
		if (scissor != nullptr) {
			RHI::Device::SetScissor(*scissor);
		} else {
			RHI::Device::SetScissorTestEnabled(false);
		}

		// material.DefineVertexFormat() + geometry.Bind() + geometry.Draw()
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		if (kind == DrawKind::Arrays) {
			RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
		} else {
			ibo.Bind();
			RHI::Device::DrawElements(PrimitiveType::Triangles, 6, 0, 0);
		}

		RHI::Device::SetScissorState(savedScissor);
	}
}

// ---- Shared helpers for the effect tests ----

void MakePath(const char* baseDir, const char* name, char* out, std::size_t outSize)
{
	std::snprintf(out, outSize, "%s/%s", baseDir, name);
}

// Ortho projection with a top-left origin, mapping model [0,W]x[0,H] to the viewport (y flipped)
void BuildOrtho(std::int32_t width, std::int32_t height, float* m)
{
	const float near = -1.0f, far = 1.0f;
	const float proj[16] = {
		2.0f / width, 0.0f, 0.0f, 0.0f,
		0.0f, -2.0f / height, 0.0f, 0.0f,
		0.0f, 0.0f, -2.0f / (far - near), 0.0f,
		-1.0f, 1.0f, -(far + near) / (far - near), 1.0f
	};
	std::memcpy(m, proj, sizeof(proj));
}

// Writes one instance's std140 InstanceBlock bytes (112-byte layout shared by every sprite-family shader)
void FillInstanceRaw(std::uint8_t* dst, const float* model, const float* color, const float* texRect,
	const float* spriteSize, float palOffset)
{
	std::memset(dst, 0, 112);
	std::memcpy(dst + 0, model, 16 * sizeof(float));
	std::memcpy(dst + 64, color, 4 * sizeof(float));
	std::memcpy(dst + 80, texRect, 4 * sizeof(float));
	std::memcpy(dst + 96, spriteSize, 2 * sizeof(float));
	std::memcpy(dst + 104, &palOffset, sizeof(float));
}

const float kIdentityView[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};
const float kTexRectFull[4] = {1.0f, 0.0f, 1.0f, 0.0f};
const float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};

bool RunSpriteTest(const char* baseDir)
{
	char outputPath[512];
	MakePath(baseDir, "sw_sprite.png", outputPath, sizeof(outputPath));
	constexpr std::int32_t Width = 256;
	constexpr std::int32_t Height = 256;
	g_uniformBump = 0;

	std::printf("\n=== DefaultSprite (regression) ===\n");

	// --- Program from the offline DefaultSprite reflection ---
	RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&nCine::ShadersGen::DefaultSprite.Variants[0]);
	program.Link(RHI::ShaderProgram::Introspection::Enabled);
	program.SetObjectLabel("DefaultSprite");

	// --- Streaming uniform buffer + suballocator ---
	RHI::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	RHI::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	// --- Camera (loose) uniforms: an ortho projection (top-left origin) and an identity view ---
	std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
	RHI::ShaderUniforms camera(&program);
	camera.SetUniformsDataPointer(cameraBuffer.data());

	const float near = -1.0f, far = 1.0f;
	const float projection[16] = {
		2.0f / Width, 0.0f, 0.0f, 0.0f,
		0.0f, -2.0f / Height, 0.0f, 0.0f,
		0.0f, 0.0f, -2.0f / (far - near), 0.0f,
		-1.0f, 1.0f, -(far + near) / (far - near), 1.0f
	};
	const float view[16] = {
		1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
	};
	camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
	camera.GetUniform("uViewMatrix")->SetFloatVector(view);

	// --- Instance block storage ---
	std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
	RHI::ShaderUniformBlocks blocks(&program);
	blocks.SetUniformsDataPointer(blockBuffer.data());

	// --- Textures ---
	// 2x2 checker (RGBA): top-left red, top-right green, bottom-left blue, bottom-right yellow
	const std::uint8_t checker[16] = {
		255, 0, 0, 255,    0, 255, 0, 255,
		0, 0, 255, 255,    255, 255, 0, 255
	};
	RHI::Texture checkerTexture(TextureTarget::Texture2D);
	checkerTexture.TexImage2D(0, PixelFormat::RGBA8, false, 2, 2, checker);

	// 1x1 solid white (for color-modulation and blend tests)
	const std::uint8_t white[4] = {255, 255, 255, 255};
	RHI::Texture whiteTexture(TextureTarget::Texture2D);
	whiteTexture.TexImage2D(0, PixelFormat::RGBA8, false, 1, 1, white);

	// --- Vertex/index buffers + vertex array (mirroring the sprite draw protocol) ---
	RHI::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	RHI::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices), indices, BufferUsage::StaticDraw);
	RHI::VertexArray vao;
	vao.SetObjectLabel("SpriteQuad");

	// --- Render target ---
	RHI::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, Width, Height, nullptr);
	RHI::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	RHI::Device::SetupInitialState();
	RHI::Device::SetViewport(Recti(0, 0, Width, Height));
	RHI::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
	RHI::Device::Clear(ClearFlags::Color);

	const float texRectFull[4] = {1.0f, 0.0f, 1.0f, 0.0f};
	const float sizeSprite[2] = {64.0f, 48.0f};
	const float whiteColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float model[16];

	// Sprite 1: checker, opaque, white modulation -> textured path + clear-outside
	MakeTranslation(96.0f, 80.0f, model);
	DrawSprite(program, camera, blocks, checkerTexture, vbo, ibo, model, whiteColor, texRectFull, sizeSprite,
		false, BlendingFactor::One, BlendingFactor::Zero, nullptr, DrawKind::Arrays);

	// Sprite 2: white texture, opaque, color modulation (1, 0.5, 0.25, 1)
	const float modColor[4] = {1.0f, 0.5f, 0.25f, 1.0f};
	MakeTranslation(8.0f, 8.0f, model);
	DrawSprite(program, camera, blocks, whiteTexture, vbo, ibo, model, modColor, texRectFull, sizeSprite,
		false, BlendingFactor::One, BlendingFactor::Zero, nullptr, DrawKind::Arrays);

	// Sprite 3: white texture, alpha blend, half-alpha modulation -> blend over the gray clear
	const float halfAlpha[4] = {1.0f, 1.0f, 1.0f, 0.5f};
	MakeTranslation(184.0f, 8.0f, model);
	DrawSprite(program, camera, blocks, whiteTexture, vbo, ibo, model, halfAlpha, texRectFull, sizeSprite,
		true, BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, nullptr, DrawKind::Arrays);

	// Sprite 4: checker, opaque, drawn via DrawElements, with a scissor clipping its right half (px < 128)
	MakeTranslation(96.0f, 176.0f, model);
	const Recti scissor(0, 0, 128, Height);
	DrawSprite(program, camera, blocks, checkerTexture, vbo, ibo, model, whiteColor, texRectFull, sizeSprite,
		false, BlendingFactor::One, BlendingFactor::Zero, &scissor, DrawKind::Elements);

	// --- Read back and assert ---
	// Drain the tile renderer's deferred draws before touching the texel store directly. The engine's
	// present/readback paths flush implicitly; the harness bypasses them, and reading (or letting the
	// test's textures die) with commands still queued reads a stale target - or worse, a later flush
	// rasterizes commands whose textures are already destroyed.
	RHI::Software::SwRaster::Flush();
	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();

	// The render-target store is bottom-up (OpenGL framebuffer convention - see SwRaster::SetColorBuffer's
	// isFboTarget), so every assertion flips its logical top-down y into a store row
	auto fy = [&](std::int32_t y) { return Height - 1 - y; };

	std::printf("Software RHI backend harness (%dx%d)\n", Width, Height);
	std::printf("Sprite 1 - checker quadrants (opaque, white modulation):\n");
	CheckPixel(pixels, stride, 110, fy(90), 255, 0, 0, 255, 0, "checker top-left = red");
	CheckPixel(pixels, stride, 145, fy(90), 0, 255, 0, 255, 0, "checker top-right = green");
	CheckPixel(pixels, stride, 110, fy(116), 0, 0, 255, 255, 0, "checker bottom-left = blue");
	CheckPixel(pixels, stride, 145, fy(116), 255, 255, 0, 255, 0, "checker bottom-right = yellow");

	std::printf("Clear color outside the sprites:\n");
	CheckPixel(pixels, stride, 10, fy(130), 40, 40, 40, 255, 0, "clear (left edge)");
	CheckPixel(pixels, stride, 250, fy(200), 40, 40, 40, 255, 0, "clear (bottom-right)");

	std::printf("Sprite 2 - color modulation (white x (1, 0.5, 0.25)):\n");
	CheckPixel(pixels, stride, 40, fy(32), 255, 128, 64, 255, 2, "modulated white");

	std::printf("Sprite 3 - alpha blend (half-alpha white over gray 40):\n");
	CheckPixel(pixels, stride, 216, fy(32), 148, 148, 148, 191, 2, "alpha-blended white");

	std::printf("Sprite 4 - scissor (right half clipped):\n");
	CheckPixel(pixels, stride, 110, fy(186), 255, 0, 0, 255, 0, "scissor kept = red");
	CheckPixel(pixels, stride, 145, fy(186), 40, 40, 40, 255, 0, "scissor clipped = clear");

	const bool wrote = WritePng(outputPath, pixels, Width, Height, stride);
	std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
	return wrote;
}

// ============================================================================================
// Full-screen procedural effects (TexturedBackground / Combine) and the palette-remap path
// ============================================================================================

// Uploads a full RGBA8 image built by a per-texel callback (helps stage the combine/scene textures)
template<class Fn>
void UploadRgba(RHI::Texture& texture, std::int32_t w, std::int32_t h, Fn texel)
{
	std::vector<std::uint8_t> data(std::size_t(w) * std::size_t(h) * 4);
	for (std::int32_t y = 0; y < h; y++) {
		for (std::int32_t x = 0; x < w; x++) {
			std::uint8_t* p = data.data() + (std::size_t(y) * w + x) * 4;
			texel(x, y, p);
		}
	}
	texture.TexImage2D(0, PixelFormat::RGBA8, false, w, h, data.data());
}

// --- TexturedBackground / TexturedBackgroundCircle: solid source -> analytic horizon/edge asserts ---

bool RunBackgroundTest(const char* baseDir, bool circle)
{
	constexpr std::int32_t W = 256, H = 144;		// 16:9, so the shader's aspect correction is exactly 1.0
	g_uniformBump = 0;

	const ShaderCompiler::Program& prog = (circle ? Jazz2::ShadersGen::TexturedBackgroundCircle : Jazz2::ShadersGen::TexturedBackground);
	const char* label = (circle ? "TexturedBackgroundCircle" : "TexturedBackground");
	std::printf("\n=== %s (solid source) ===\n", label);

	RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&prog.Variants[0]);
	program.Link(RHI::ShaderProgram::Introspection::Enabled);
	program.SetObjectLabel(label);

	RHI::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	RHI::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
	RHI::ShaderUniforms camera(&program);
	camera.SetUniformsDataPointer(cameraBuffer.data());
	float projection[16];
	BuildOrtho(W, H, projection);
	camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
	camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);
	camera.GetUniform("uViewSize")->SetFloatValue(float(W), float(H));
	camera.GetUniform("uCameraPos")->SetFloatValue(0.0f, 0.0f);
	// horizonColor.w = 0 disables the star field, so the horizon band is exactly the horizon colour
	const float horizon[4] = {0.9f, 0.2f, 0.1f, 0.0f};
	camera.GetUniform("uHorizonColor")->SetFloatVector(horizon);
	camera.GetUniform("uShift")->SetFloatValue(0.0f, 0.0f);

	std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
	RHI::ShaderUniformBlocks blocks(&program);
	blocks.SetUniformsDataPointer(blockBuffer.data());

	const std::uint8_t solid[4] = {0, 180, 90, 255};
	RHI::Texture srcTexture(TextureTarget::Texture2D);
	srcTexture.TexImage2D(0, PixelFormat::RGBA8, false, 1, 1, solid);

	RHI::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	RHI::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices), indices, BufferUsage::StaticDraw);

	RHI::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, W, H, nullptr);
	RHI::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	RHI::Device::SetupInitialState();
	RHI::Device::SetViewport(Recti(0, 0, W, H));
	RHI::Device::SetClearColor(Colorf(0.0f, 0.0f, 0.0f, 1.0f));
	RHI::Device::Clear(ClearFlags::Color);

	float model[16];
	MakeTranslation(0.0f, 0.0f, model);
	auto ib = blocks.GetUniformBlock("InstanceBlock");
	ib->GetUniform("modelMatrix")->SetFloatVector(model);
	ib->GetUniform("color")->SetFloatVector(kWhite);
	ib->GetUniform("texRect")->SetFloatVector(kTexRectFull);
	ib->GetUniform("spriteSize")->SetFloatValue(float(W), float(H));
	blocks.CommitUniformBlocks();

	srcTexture.Bind(0);
	program.Use();
	blocks.Bind();
	camera.CommitUniforms();
	RHI::Device::SetBlendingEnabled(false);
	RHI::Device::SetScissorTestEnabled(false);
	program.DefineVertexFormat(&vbo, &ibo, 0);
	vbo.Bind();
	RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);

	// Drain the tile renderer's deferred draws before touching the texel store directly. The engine's
	// present/readback paths flush implicitly; the harness bypasses them, and reading (or letting the
	// test's textures die) with commands still queued reads a stale target - or worse, a later flush
	// rasterizes commands whose textures are already destroyed.
	RHI::Software::SwRaster::Flush();
	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();
	const int hr = 230, hg = 51, hb = 26;		// round(horizon.rgb * 255)

	if (!circle) {
		std::printf("Horizon band (vertical centre) = horizon colour:\n");
		CheckPixel(pixels, stride, 64, 68, hr, hg, hb, 255, 1, "band left");
		CheckPixel(pixels, stride, 128, 72, hr, hg, hb, 255, 1, "band centre");
		CheckPixel(pixels, stride, 200, 76, hr, hg, hb, 255, 1, "band right");
		std::printf("Top/bottom edges (opacity 0) = source texture colour:\n");
		CheckPixel(pixels, stride, 128, 0, 0, 180, 90, 255, 1, "top edge = texture");
		CheckPixel(pixels, stride, 128, H - 1, 0, 180, 90, 255, 1, "bottom edge = texture");
	} else {
		std::printf("Screen centre (opacity 1) = horizon colour:\n");
		CheckPixel(pixels, stride, 128, 72, hr, hg, hb, 255, 1, "centre = horizon");
		std::printf("Screen corner (opacity 0) = source texture colour:\n");
		CheckPixel(pixels, stride, 2, 2, 0, 180, 90, 255, 1, "corner = texture");
	}

	char outputPath[512];
	MakePath(baseDir, (circle ? "sw_background_circle.png" : "sw_background.png"), outputPath, sizeof(outputPath));
	const bool wrote = WritePng(outputPath, pixels, W, H, stride);
	std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
	return wrote;
}

// --- TexturedBackground with a patterned source: proves the warp varies + dither changes the output ---

bool RunBackgroundWarpTest(const char* baseDir)
{
	constexpr std::int32_t W = 256, H = 144;
	g_uniformBump = 0;
	std::printf("\n=== TexturedBackground / Dither (patterned source, warp) ===\n");

	RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&Jazz2::ShadersGen::TexturedBackground.Variants[0]);
	program.Link(RHI::ShaderProgram::Introspection::Enabled);

	RHI::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	RHI::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
	RHI::ShaderUniforms camera(&program);
	camera.SetUniformsDataPointer(cameraBuffer.data());
	float projection[16];
	BuildOrtho(W, H, projection);
	camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
	camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);
	camera.GetUniform("uViewSize")->SetFloatValue(float(W), float(H));
	camera.GetUniform("uCameraPos")->SetFloatValue(0.0f, 0.0f);
	const float horizon[4] = {0.9f, 0.2f, 0.1f, 0.0f};
	camera.GetUniform("uHorizonColor")->SetFloatVector(horizon);
	camera.GetUniform("uShift")->SetFloatValue(0.0f, 0.0f);

	std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
	RHI::ShaderUniformBlocks blocks(&program);
	blocks.SetUniformsDataPointer(blockBuffer.data());

	// A distinct per-texel pattern so the warp maps different screen positions to different texels
	RHI::Texture srcTexture(TextureTarget::Texture2D);
	UploadRgba(srcTexture, 8, 8, [](std::int32_t x, std::int32_t y, std::uint8_t* p) {
		p[0] = std::uint8_t((x * 32) & 0xFF);
		p[1] = std::uint8_t((y * 32) & 0xFF);
		p[2] = std::uint8_t(((x + y) * 16) & 0xFF);
		p[3] = 255;
	});

	RHI::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	RHI::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices), indices, BufferUsage::StaticDraw);

	RHI::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, W, H, nullptr);
	RHI::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	RHI::Device::SetupInitialState();
	RHI::Device::SetViewport(Recti(0, 0, W, H));
	RHI::Device::SetClearColor(Colorf(0.0f, 0.0f, 0.0f, 1.0f));

	float model[16];
	MakeTranslation(0.0f, 0.0f, model);
	auto ib = blocks.GetUniformBlock("InstanceBlock");
	ib->GetUniform("modelMatrix")->SetFloatVector(model);
	ib->GetUniform("color")->SetFloatVector(kWhite);
	ib->GetUniform("texRect")->SetFloatVector(kTexRectFull);
	ib->GetUniform("spriteSize")->SetFloatValue(float(W), float(H));

	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();

	// Every pass drains the tile renderer's deferred draws before the store is read. The engine's
	// present/readback paths flush implicitly; the harness bypasses them, and reading (or letting the
	// test's textures die) with commands still queued reads a stale target - or worse, a later flush
	// (SetTargetBuffer flushes the OLD queue when the next test switches targets) rasterizes commands
	// whose source texture and destination store are already destroyed - a use-after-free that
	// corrupted the heap and crashed the run one test later.
	auto issueDraw = [&](const char* lbl) {
		program.SetObjectLabel(lbl);
		blocks.CommitUniformBlocks();
		srcTexture.Bind(0);
		program.Use();
		blocks.Bind();
		camera.CommitUniforms();
		RHI::Device::SetBlendingEnabled(false);
		RHI::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
		RHI::Software::SwRaster::Flush();
	};

	// Non-dither pass, captured for comparison
	RHI::Device::Clear(ClearFlags::Color);
	issueDraw("TexturedBackground");
	std::vector<std::uint8_t> nonDither(pixels, pixels + std::size_t(H) * stride);

	// Dither pass (the PNG)
	RHI::Device::Clear(ClearFlags::Color);
	issueDraw("TexturedBackgroundDither");

	std::printf("Horizon band still resolves to the horizon colour:\n");
	CheckPixel(pixels, stride, 128, H - 1 - 72, 230, 51, 26, 255, 1, "band centre");

	// The warp must sample different texels along a scanline (top row, opacity ~ 0 -> pure texture)
	int minR = 255, maxR = 0;
	for (std::int32_t x = 0; x < W; x++) {
		const std::uint8_t* p = nonDither.data() + std::size_t(4) * stride + std::size_t(x) * 4;
		if (p[0] < minR) { minR = p[0]; }
		if (p[0] > maxR) { maxR = p[0]; }
	}
	g_checks++;
	if (maxR - minR > 8) {
		std::printf("  ok   warp varies across scanline (R range %d..%d)\n", minR, maxR);
	} else {
		g_failures++;
		std::printf("  FAIL warp scanline too flat (R range %d..%d)\n", minR, maxR);
	}

	// The software renderer intentionally DROPS the dither sample (TexturedBackground.shader guards it
	// with `#ifdef DITHER #ifndef SOFTWARE_RENDERER` - the extra per-pixel texture sample is too costly
	// on the CPU), so the DITHER variant must resolve to the same generated fragment math and produce a
	// byte-identical image. This inverts the original "dither changes pixels" assertion, which described
	// the GPU backends.
	g_checks++;
	if (std::memcmp(nonDither.data(), pixels, std::size_t(H) * stride) == 0) {
		std::printf("  ok   dither variant == non-dither (SW drops the dither term by design)\n");
	} else {
		int changed = 0;
		for (std::int32_t y = 0; y < H; y++) {
			for (std::int32_t x = 0; x < W; x++) {
				const std::uint8_t* a = nonDither.data() + std::size_t(y) * stride + std::size_t(x) * 4;
				const std::uint8_t* b = pixels + std::size_t(y) * stride + std::size_t(x) * 4;
				if (std::memcmp(a, b, 4) != 0) {
					changed++;
				}
			}
		}
		g_failures++;
		std::printf("  FAIL dither variant differs from non-dither in %d pixels (SW should drop dithering)\n", changed);
	}

	char outputPath[512];
	MakePath(baseDir, "sw_background_dither.png", outputPath, sizeof(outputPath));
	const bool wrote = WritePng(outputPath, pixels, W, H, stride);
	std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
	return wrote;
}

// --- Combine (viewport compositor) ---
//
// The software backend does NOT run a Combine fragment: SwDevice::Dispatch intercepts every draw of a
// Combine-labeled program and applies the CPU dynamic-lighting combine the compositor queued for the
// viewport (SetPendingSoftwareLighting) in place over the SCREEN back-buffer - with nothing queued the
// draw is a no-op. The original shader-compositing assertions (scene x lighting x blur textures) became
// stale with that design; this now asserts the device-intercept semantics against a verbatim scalar
// reference of the combine loop.

// Scalar reference of SwDevice::ApplyPendingSoftwareLighting's per-pixel combine (kept verbatim so the
// SIMD kernel is checked bit-exactly)
static void RefCombineLighting(std::uint8_t* pixels, std::int32_t fbWidth, std::int32_t fbHeight, std::int32_t fbStride,
	const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
	std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH,
	float ambR, float ambG, float ambB)
{
	vpX = std::max(0, vpX);
	vpY = std::max(0, vpY);
	vpW = std::min(vpW, fbWidth - vpX);
	vpH = std::min(vpH, fbHeight - vpY);
	if (vpW <= 0 || vpH <= 0) return;
	auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
	for (std::int32_t y = 0; y < vpH; y++) {
		const std::int32_t lmY = std::min(y / scale, lmH - 1);
		const float* texelBase = lightmap + (std::size_t)lmY * lmW * 2;
		std::uint8_t* px = pixels + (std::size_t)(vpY + y) * fbStride + (std::size_t)vpX * 4;
		for (std::int32_t x = 0; x < vpW; x++, px += 4) {
			const std::int32_t lmX = std::min(x / scale, lmW - 1);
			const float r = clampf(texelBase[lmX * 2], 0.0f, 1.0f);
			const float g = clampf(texelBase[lmX * 2 + 1], 0.0f, 1.0f);
			const float darkT = clampf(1.0f - r, 0.0f, 1.0f);
			if (darkT <= 0.0f && g <= 0.0f) {
				continue;
			}
			const float lit = 1.0f + g;
			const float core = (g > 0.7f ? g - 0.7f : 0.0f);
			float cr = (px[0] / 255.0f) * lit + core;
			float cg = (px[1] / 255.0f) * lit + core;
			float cb = (px[2] / 255.0f) * lit + core;
			cr += (ambR - cr) * darkT;
			cg += (ambG - cg) * darkT;
			cb += (ambB - cb) * darkT;
			px[0] = (std::uint8_t)clampf(cr * 255.0f + 0.5f, 0.0f, 255.0f);
			px[1] = (std::uint8_t)clampf(cg * 255.0f + 0.5f, 0.0f, 255.0f);
			px[2] = (std::uint8_t)clampf(cb * 255.0f + 0.5f, 0.0f, 255.0f);
		}
	}
}

bool RunCombineTest(const char* baseDir)
{
	constexpr std::int32_t W = 64, H = 64;
	g_uniformBump = 0;
	std::printf("\n=== Combine (device-intercepted CPU lighting combine) ===\n");

	RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&Jazz2::ShadersGen::Combine.Variants[0]);
	program.Link(RHI::ShaderProgram::Introspection::Enabled);
	program.SetObjectLabel("Combine");

	RHI::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	RHI::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	// The intercept composites over the SCREEN back-buffer, not a render target
	RHI::Software::SwDevice::SetRenderTarget(nullptr);
	RHI::Software::SwDevice::ResizeScreenFramebuffer(W, H);
	RHI::Software::Framebuffer fb = RHI::Software::SwDevice::GetScreenFramebuffer();
	g_checks++;
	if (fb.pixels == nullptr) {
		g_failures++;
		std::printf("  FAIL no screen framebuffer\n");
		return false;
	}
	std::printf("  ok   screen framebuffer %dx%d\n", fb.width, fb.height);

	// Deterministic scene in the back-buffer
	for (std::int32_t y = 0; y < H; y++) {
		for (std::int32_t x = 0; x < W; x++) {
			std::uint8_t* p = fb.pixels + std::size_t(y) * fb.strideBytes + std::size_t(x) * 4;
			p[0] = std::uint8_t((x * 4) & 0xFF);
			p[1] = std::uint8_t((y * 4) & 0xFF);
			p[2] = std::uint8_t(((x + y) * 2) & 0xFF);
			p[3] = 255;
		}
	}
	std::vector<std::uint8_t> expected(fb.pixels, fb.pixels + std::size_t(H) * fb.strideBytes);

	RHI::Device::SetupInitialState();
	RHI::Device::SetViewport(Recti(0, 0, W, H));
	RHI::Device::SetBlendingEnabled(false);
	RHI::Device::SetScissorTestEnabled(false);
	program.Use();

	// 1: a Combine draw with NO queued lighting must leave the buffer untouched (no-op intercept)
	RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
	g_checks++;
	if (std::memcmp(fb.pixels, expected.data(), expected.size()) == 0) {
		std::printf("  ok   no queued lighting -> buffer untouched\n");
	} else {
		g_failures++;
		std::printf("  FAIL no queued lighting modified the buffer\n");
	}

	// 2: queue a lightmap covering lit / clamped / half+glow / dark / negative regimes, draw, and
	// compare the whole buffer against the scalar reference
	constexpr std::int32_t lmW = 32, lmH = 32, scale = 2;
	std::vector<float> lightmap(std::size_t(lmW) * lmH * 2);
	for (std::int32_t ty = 0; ty < lmH; ty++) {
		for (std::int32_t tx = 0; tx < lmW; tx++) {
			float* t = lightmap.data() + (std::size_t(ty) * lmW + tx) * 2;
			if (tx < 6) { t[0] = 1.0f; t[1] = 0.0f; }			// fully lit -> untouched
			else if (tx < 12) { t[0] = 1.4f; t[1] = -0.3f; }	// out-of-range, clamps to lit
			else if (tx < 18) { t[0] = 0.5f; t[1] = 0.9f; }		// half lit + glow core (g > 0.7)
			else if (tx < 24) { t[0] = 0.5f; t[1] = 0.7f; }		// half lit, g == 0.7 (no core)
			else { t[0] = 0.0f; t[1] = 0.0f; }					// dark -> ambient
		}
	}
	const float ambR = 0.15f, ambG = 0.10f, ambB = 0.30f;
	RefCombineLighting(expected.data(), fb.width, fb.height, fb.strideBytes, lightmap.data(), lmW, lmH, scale, 0, 0, W, H, ambR, ambG, ambB);
	RHI::Software::SwDevice::SetPendingSoftwareLighting(lightmap.data(), lmW, lmH, scale, 0, 0, W, H, ambR, ambG, ambB);
	RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);

	std::size_t diffs = 0;
	std::int32_t firstX = -1, firstY = -1;
	for (std::int32_t y = 0; y < H; y++) {
		for (std::int32_t i = 0; i < W * 4; i++) {
			if (fb.pixels[std::size_t(y) * fb.strideBytes + i] != expected[std::size_t(y) * fb.strideBytes + i]) {
				if (diffs == 0) { firstX = i / 4; firstY = y; }
				diffs++;
			}
		}
	}
	g_checks++;
	if (diffs == 0) {
		std::printf("  ok   queued lighting combine == scalar reference (%dx%d, scale %d)\n", W, H, scale);
	} else {
		g_failures++;
		std::printf("  FAIL combine differs from reference: %zu bytes (first at %d,%d)\n", diffs, firstX, firstY);
	}

	// 3: the queue must be consumed - a further draw with nothing queued is a no-op again
	std::vector<std::uint8_t> afterCombine(fb.pixels, fb.pixels + std::size_t(H) * fb.strideBytes);
	RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
	g_checks++;
	if (std::memcmp(fb.pixels, afterCombine.data(), afterCombine.size()) == 0) {
		std::printf("  ok   queue consumed - next Combine draw is a no-op\n");
	} else {
		g_failures++;
		std::printf("  FAIL lighting queue not consumed by the Combine draw\n");
	}

	char outputPath[512];
	MakePath(baseDir, "sw_combine.png", outputPath, sizeof(outputPath));
	const bool wrote = WritePng(outputPath, fb.pixels, W, H, fb.strideBytes);
	std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
	return wrote;
}

// --- PaletteRemap / BatchedPaletteRemap (the gameplay-critical index -> palette path) ---

bool RunPaletteTest(const char* baseDir)
{
	constexpr std::int32_t W = 64, H = 64;
	g_uniformBump = 0;
	std::printf("\n=== PaletteRemap / BatchedPaletteRemap ===\n");

	RHI::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	RHI::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	// 256x256 palette texture, matching the engine's shared palette store (the fragment maps the flat
	// palette index into a 256x256 texture: palY = floor(palIndex / 256) / 256, so a shorter texture
	// would fold every row back onto row 0). Row 0 and row 1 hold two distinct 256-colour palettes.
	RHI::Texture paletteTexture(TextureTarget::Texture2D);
	UploadRgba(paletteTexture, 256, 256, [](std::int32_t x, std::int32_t y, std::uint8_t* p) {
		if (y == 0) {
			if (x == 0) { p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0; }	// index 0 = transparent
			else { p[0] = std::uint8_t(x); p[1] = std::uint8_t(255 - x); p[2] = 128; p[3] = 255; }
		} else {
			p[0] = std::uint8_t(255 - x); p[1] = std::uint8_t(x); p[2] = 64; p[3] = 255;
		}
	});

	// R8 index sprite: index = column * 16 (0, 16, ... 240), uniform down each column
	RHI::Texture indexTexture(TextureTarget::Texture2D);
	{
		std::vector<std::uint8_t> data(16 * 16);
		for (std::int32_t y = 0; y < 16; y++) {
			for (std::int32_t x = 0; x < 16; x++) {
				data[std::size_t(y) * 16 + x] = std::uint8_t((x * 16) & 0xFF);
			}
		}
		indexTexture.TexImage2D(0, PixelFormat::R8, false, 16, 16, data.data());
	}

	RHI::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	RHI::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices6[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices6), indices6, BufferUsage::StaticDraw);

	RHI::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, W, H, nullptr);
	RHI::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	// Drain the tile renderer's deferred draws before touching the texel store directly. The engine's
	// present/readback paths flush implicitly; the harness bypasses them, and reading (or letting the
	// test's textures die) with commands still queued reads a stale target - or worse, a later flush
	// rasterizes commands whose textures are already destroyed.
	RHI::Software::SwRaster::Flush();
	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();
	bool wroteAll = true;

	// ---- Draw 1: single PaletteRemap, palette row 0, opaque, white modulation ----
	{
		RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
		program.SetReflection(&Jazz2::ShadersGen::PaletteRemap.Variants[0]);
		program.Link(RHI::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("PaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		RHI::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		RHI::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		RHI::Device::SetupInitialState();
		RHI::Device::SetViewport(Recti(0, 0, W, H));
		RHI::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		RHI::Device::Clear(ClearFlags::Color);

		float model[16];
		MakeTranslation(0.0f, 0.0f, model);
		const float size[2] = {float(W), float(H)};
		auto ib = blocks.GetUniformBlock("InstanceBlock");
		ib->GetUniform("modelMatrix")->SetFloatVector(model);
		ib->GetUniform("color")->SetFloatVector(kWhite);
		ib->GetUniform("texRect")->SetFloatVector(kTexRectFull);
		ib->GetUniform("spriteSize")->SetFloatVector(size);
		ib->GetUniform("palOffset")->SetFloatValue(0.0f);
		blocks.CommitUniformBlocks();

		indexTexture.Bind(0);
		paletteTexture.Bind(1);
		program.Use();
		blocks.Bind();
		camera.CommitUniforms();
		RHI::Device::SetBlendingEnabled(false);
		RHI::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
		RHI::Software::SwRaster::Flush();	// Drain the deferred tiles before asserting texels

		std::printf("Row 0 lookups (opaque, white):\n");
		CheckPixel(pixels, stride, 2, 10, 0, 0, 0, 0, 0, "index 0 = transparent entry");
		CheckPixel(pixels, stride, 6, 10, 16, 239, 128, 255, 1, "index 16");
		CheckPixel(pixels, stride, 34, 10, 128, 127, 128, 255, 1, "index 128");
		CheckPixel(pixels, stride, 62, 10, 240, 15, 128, 255, 1, "index 240");

		char outputPath[512];
		MakePath(baseDir, "sw_palette.png", outputPath, sizeof(outputPath));
		const bool wrote = WritePng(outputPath, pixels, W, H, stride);
		std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
		wroteAll = wroteAll && wrote;
	}

	// ---- Draw 2: single PaletteRemap, palette row 1 (offset 256), colour modulation ----
	{
		RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
		program.SetReflection(&Jazz2::ShadersGen::PaletteRemap.Variants[0]);
		program.Link(RHI::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("PaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		RHI::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		RHI::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		RHI::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		RHI::Device::Clear(ClearFlags::Color);

		float model[16];
		MakeTranslation(0.0f, 0.0f, model);
		const float size[2] = {float(W), float(H)};
		const float modColor[4] = {1.0f, 0.5f, 1.0f, 1.0f};
		auto ib = blocks.GetUniformBlock("InstanceBlock");
		ib->GetUniform("modelMatrix")->SetFloatVector(model);
		ib->GetUniform("color")->SetFloatVector(modColor);
		ib->GetUniform("texRect")->SetFloatVector(kTexRectFull);
		ib->GetUniform("spriteSize")->SetFloatVector(size);
		ib->GetUniform("palOffset")->SetFloatValue(256.0f);
		blocks.CommitUniformBlocks();

		indexTexture.Bind(0);
		paletteTexture.Bind(1);
		program.Use();
		blocks.Bind();
		camera.CommitUniforms();
		RHI::Device::SetBlendingEnabled(false);
		RHI::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
		RHI::Software::SwRaster::Flush();	// Drain the deferred tiles before asserting texels

		std::printf("Row 1 lookups (offset 256) with colour modulation (1, 0.5, 1):\n");
		CheckPixel(pixels, stride, 6, 10, 239, 8, 64, 255, 1, "row 1 index 16 x mod");
		CheckPixel(pixels, stride, 34, 10, 127, 64, 64, 255, 1, "row 1 index 128 x mod");
	}

	// ---- Draw 3: BatchedPaletteRemap, two instances (row 0 left half, row 1 right half) ----
	{
		RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
		program.SetBatchSize(2);
		program.SetReflection(&Jazz2::ShadersGen::BatchedPaletteRemap.Variants[0]);
		program.Link(RHI::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("BatchedPaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		RHI::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		RHI::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		RHI::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		RHI::Device::Clear(ClearFlags::Color);

		// Instance 0: left half [0,32) using palette row 0; instance 1: right half [32,64) using row 1
		float model0[16], model1[16];
		MakeTranslation(0.0f, 0.0f, model0);
		MakeTranslation(32.0f, 0.0f, model1);
		const float sizeHalf[2] = {32.0f, float(H)};
		FillInstanceRaw(blockBuffer.data() + 0, model0, kWhite, kTexRectFull, sizeHalf, 0.0f);
		FillInstanceRaw(blockBuffer.data() + 112, model1, kWhite, kTexRectFull, sizeHalf, 256.0f);
		blocks.CommitUniformBlocks();

		indexTexture.Bind(0);
		paletteTexture.Bind(1);
		program.Use();
		blocks.Bind();
		camera.CommitUniforms();
		RHI::Device::SetBlendingEnabled(false);
		RHI::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		RHI::Buffer ibo12(BufferTarget::Index);
		const std::uint16_t indices12[12] = {0, 1, 2, 2, 1, 3, 0, 1, 2, 2, 1, 3};
		ibo12.BufferData(sizeof(indices12), indices12, BufferUsage::StaticDraw);
		ibo12.Bind();
		RHI::Device::DrawElements(PrimitiveType::Triangles, 12, 0, 0);
		RHI::Software::SwRaster::Flush();	// Drain the deferred tiles before asserting texels

		std::printf("Batched: left half = row 0, right half = row 1 (index 48 in each):\n");
		CheckPixel(pixels, stride, 6, 10, 48, 207, 128, 255, 1, "instance 0 row 0 index 48");
		CheckPixel(pixels, stride, 38, 10, 207, 48, 64, 255, 1, "instance 1 row 1 index 48");

		char outputPath[512];
		MakePath(baseDir, "sw_palette_batched.png", outputPath, sizeof(outputPath));
		const bool wrote = WritePng(outputPath, pixels, W, H, stride);
		std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
		wroteAll = wroteAll && wrote;
	}

	// ---- Draw 4: PaletteRemap with an RG8 index (alpha carried in G), alpha-blended over the clear ----
	{
		RHI::Texture rgTexture(TextureTarget::Texture2D);
		{
			std::vector<std::uint8_t> data(std::size_t(16) * 16 * 2);
			for (std::int32_t y = 0; y < 16; y++) {
				for (std::int32_t x = 0; x < 16; x++) {
					std::uint8_t* p = data.data() + (std::size_t(y) * 16 + x) * 2;
					p[0] = std::uint8_t((x * 16) & 0xFF);						// index
					p[1] = std::uint8_t(x == 0 ? 0 : (x % 2 ? 128 : 255));		// alpha in G
				}
			}
			rgTexture.TexImage2D(0, PixelFormat::RG8, false, 16, 16, data.data());
			// The engine samples RG8 index textures with `.a` mapped to green (ContentResolver sets this
			// swizzle on every RG8 index upload); without it the promoted store's opaque byte 3 is read
			rgTexture.SetSwizzle(SwizzleChannel::Red, SwizzleChannel::Green, SwizzleChannel::Blue, SwizzleChannel::Green);
		}

		RHI::ShaderProgram program(RHI::ShaderProgram::QueryPhase::Immediate);
		program.SetReflection(&Jazz2::ShadersGen::PaletteRemap.Variants[0]);
		program.Link(RHI::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("PaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		RHI::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		RHI::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		RHI::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		RHI::Device::Clear(ClearFlags::Color);

		float model[16];
		MakeTranslation(0.0f, 0.0f, model);
		const float size[2] = {float(W), float(H)};
		auto ib = blocks.GetUniformBlock("InstanceBlock");
		ib->GetUniform("modelMatrix")->SetFloatVector(model);
		ib->GetUniform("color")->SetFloatVector(kWhite);
		ib->GetUniform("texRect")->SetFloatVector(kTexRectFull);
		ib->GetUniform("spriteSize")->SetFloatVector(size);
		ib->GetUniform("palOffset")->SetFloatValue(0.0f);
		blocks.CommitUniformBlocks();

		rgTexture.Bind(0);
		paletteTexture.Bind(1);
		program.Use();
		blocks.Bind();
		camera.CommitUniforms();
		RHI::Device::SetBlendingEnabled(true);
		RHI::Device::SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
		RHI::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		RHI::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
		RHI::Software::SwRaster::Flush();	// Drain the deferred tiles before asserting texels

		// index 16 with G = 128 alpha, palette[0][16] = (16,239,128), blended over the (40,40,40) clear
		const float sa = 128.0f / 255.0f;
		const int er = int(16 * sa + 40 * (1 - sa) + 0.5f);
		const int eg = int(239 * sa + 40 * (1 - sa) + 0.5f);
		const int eb = int(128 * sa + 40 * (1 - sa) + 0.5f);
		const int ea = int(128 * sa + 255 * (1 - sa) + 0.5f);
		std::printf("RG8 index (alpha in G), alpha-blended over the clear:\n");
		CheckPixel(pixels, stride, 2, 10, 40, 40, 40, 255, 1, "index 0 (G=0) = clear shows through");
		CheckPixel(pixels, stride, 6, 10, er, eg, eb, ea, 2, "index 16 (G=128) blended");
	}

	return wroteAll;
}

int main(int argc, char** argv)
{
	// Unbuffered stdout so a crash in a later test cannot swallow the log of the earlier ones
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const char* baseDir = (argc > 1 ? argv[1] : ".");

	std::printf("Software RHI backend harness\n");
	std::printf("Output directory: %s\n", baseDir);

	bool wroteAll = true;
	wroteAll = RunSpriteTest(baseDir) && wroteAll;
	wroteAll = RunBackgroundTest(baseDir, false) && wroteAll;
	wroteAll = RunBackgroundTest(baseDir, true) && wroteAll;
	wroteAll = RunBackgroundWarpTest(baseDir) && wroteAll;
	wroteAll = RunCombineTest(baseDir) && wroteAll;
	wroteAll = RunPaletteTest(baseDir) && wroteAll;

	std::printf("\n=====================================\n");
	std::printf("Total checks: %d, failures: %d, all PNGs written: %s\n", g_checks, g_failures, wroteAll ? "yes" : "no");

	return (g_failures == 0 && wroteAll) ? 0 : 1;
}
