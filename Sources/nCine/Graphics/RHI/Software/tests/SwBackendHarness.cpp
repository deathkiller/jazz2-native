// Standalone harness for the integrated software RHI backend.
//
// It drives the `RhiSoftware::Sw*` types through the `nCine::Rhi::` contract aliases exactly the way
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
#include "Shaders/Generated/DefaultSprite.h"
#include "Shaders/Generated/TexturedBackground.h"
#include "Shaders/Generated/TexturedBackgroundCircle.h"
#include "Shaders/Generated/Combine.h"
#include "Shaders/Generated/PaletteRemap.h"

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

	Rhi::Buffer* g_uniformBuffer = nullptr;
	std::uint32_t g_uniformBump = 0;

	Rhi::BufferRange AllocUniformRange(std::uint32_t bytes)
	{
		Rhi::BufferRange range;
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

	void DrawSprite(Rhi::ShaderProgram& program, Rhi::ShaderUniforms& camera, Rhi::ShaderUniformBlocks& blocks,
		Rhi::Texture& texture, Rhi::Buffer& vbo, Rhi::Buffer& ibo,
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
		Rhi::Device::SetBlendingEnabled(blend);
		if (blend) {
			Rhi::Device::SetBlendingFactors(srcFactor, dstFactor, BlendingFactor::One, dstFactor);
		}
		Rhi::Device::ScissorState savedScissor = Rhi::Device::GetScissorState();
		if (scissor != nullptr) {
			Rhi::Device::SetScissor(*scissor);
		} else {
			Rhi::Device::SetScissorTestEnabled(false);
		}

		// material.DefineVertexFormat() + geometry.Bind() + geometry.Draw()
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		if (kind == DrawKind::Arrays) {
			Rhi::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
		} else {
			ibo.Bind();
			Rhi::Device::DrawElements(PrimitiveType::Triangles, 6, 0, 0);
		}

		Rhi::Device::SetScissorState(savedScissor);
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
	Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&nCine::ShadersGen::DefaultSprite.Variants[0]);
	program.Link(Rhi::ShaderProgram::Introspection::Enabled);
	program.SetObjectLabel("DefaultSprite");

	// --- Streaming uniform buffer + suballocator ---
	Rhi::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	Rhi::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	// --- Camera (loose) uniforms: an ortho projection (top-left origin) and an identity view ---
	std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
	Rhi::ShaderUniforms camera(&program);
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
	Rhi::ShaderUniformBlocks blocks(&program);
	blocks.SetUniformsDataPointer(blockBuffer.data());

	// --- Textures ---
	// 2x2 checker (RGBA): top-left red, top-right green, bottom-left blue, bottom-right yellow
	const std::uint8_t checker[16] = {
		255, 0, 0, 255,    0, 255, 0, 255,
		0, 0, 255, 255,    255, 255, 0, 255
	};
	Rhi::Texture checkerTexture(TextureTarget::Texture2D);
	checkerTexture.TexImage2D(0, PixelFormat::RGBA8, false, 2, 2, checker);

	// 1x1 solid white (for color-modulation and blend tests)
	const std::uint8_t white[4] = {255, 255, 255, 255};
	Rhi::Texture whiteTexture(TextureTarget::Texture2D);
	whiteTexture.TexImage2D(0, PixelFormat::RGBA8, false, 1, 1, white);

	// --- Vertex/index buffers + vertex array (mirroring the sprite draw protocol) ---
	Rhi::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	Rhi::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices), indices, BufferUsage::StaticDraw);
	Rhi::VertexArray vao;
	vao.SetObjectLabel("SpriteQuad");

	// --- Render target ---
	Rhi::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, Width, Height, nullptr);
	Rhi::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	Rhi::Device::SetupInitialState();
	Rhi::Device::SetViewport(Recti(0, 0, Width, Height));
	Rhi::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
	Rhi::Device::Clear(ClearFlags::Color);

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
	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();

	std::printf("Software RHI backend harness (%dx%d)\n", Width, Height);
	std::printf("Sprite 1 - checker quadrants (opaque, white modulation):\n");
	CheckPixel(pixels, stride, 110, 90, 255, 0, 0, 255, 0, "checker top-left = red");
	CheckPixel(pixels, stride, 145, 90, 0, 255, 0, 255, 0, "checker top-right = green");
	CheckPixel(pixels, stride, 110, 116, 0, 0, 255, 255, 0, "checker bottom-left = blue");
	CheckPixel(pixels, stride, 145, 116, 255, 255, 0, 255, 0, "checker bottom-right = yellow");

	std::printf("Clear color outside the sprites:\n");
	CheckPixel(pixels, stride, 10, 130, 40, 40, 40, 255, 0, "clear (left edge)");
	CheckPixel(pixels, stride, 250, 200, 40, 40, 40, 255, 0, "clear (bottom-right)");

	std::printf("Sprite 2 - color modulation (white x (1, 0.5, 0.25)):\n");
	CheckPixel(pixels, stride, 40, 32, 255, 128, 64, 255, 2, "modulated white");

	std::printf("Sprite 3 - alpha blend (half-alpha white over gray 40):\n");
	CheckPixel(pixels, stride, 216, 32, 148, 148, 148, 191, 2, "alpha-blended white");

	std::printf("Sprite 4 - scissor (right half clipped):\n");
	CheckPixel(pixels, stride, 110, 186, 255, 0, 0, 255, 0, "scissor kept = red");
	CheckPixel(pixels, stride, 145, 186, 40, 40, 40, 255, 0, "scissor clipped = clear");

	const bool wrote = WritePng(outputPath, pixels, Width, Height, stride);
	std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
	return wrote;
}

// ============================================================================================
// Full-screen procedural effects (TexturedBackground / Combine) and the palette-remap path
// ============================================================================================

// Uploads a full RGBA8 image built by a per-texel callback (helps stage the combine/scene textures)
template<class Fn>
void UploadRgba(Rhi::Texture& texture, std::int32_t w, std::int32_t h, Fn texel)
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

	Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&prog.Variants[0]);
	program.Link(Rhi::ShaderProgram::Introspection::Enabled);
	program.SetObjectLabel(label);

	Rhi::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	Rhi::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
	Rhi::ShaderUniforms camera(&program);
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
	Rhi::ShaderUniformBlocks blocks(&program);
	blocks.SetUniformsDataPointer(blockBuffer.data());

	const std::uint8_t solid[4] = {0, 180, 90, 255};
	Rhi::Texture srcTexture(TextureTarget::Texture2D);
	srcTexture.TexImage2D(0, PixelFormat::RGBA8, false, 1, 1, solid);

	Rhi::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	Rhi::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices), indices, BufferUsage::StaticDraw);

	Rhi::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, W, H, nullptr);
	Rhi::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	Rhi::Device::SetupInitialState();
	Rhi::Device::SetViewport(Recti(0, 0, W, H));
	Rhi::Device::SetClearColor(Colorf(0.0f, 0.0f, 0.0f, 1.0f));
	Rhi::Device::Clear(ClearFlags::Color);

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
	Rhi::Device::SetBlendingEnabled(false);
	Rhi::Device::SetScissorTestEnabled(false);
	program.DefineVertexFormat(&vbo, &ibo, 0);
	vbo.Bind();
	Rhi::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);

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

	Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&Jazz2::ShadersGen::TexturedBackground.Variants[0]);
	program.Link(Rhi::ShaderProgram::Introspection::Enabled);

	Rhi::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	Rhi::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
	Rhi::ShaderUniforms camera(&program);
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
	Rhi::ShaderUniformBlocks blocks(&program);
	blocks.SetUniformsDataPointer(blockBuffer.data());

	// A distinct per-texel pattern so the warp maps different screen positions to different texels
	Rhi::Texture srcTexture(TextureTarget::Texture2D);
	UploadRgba(srcTexture, 8, 8, [](std::int32_t x, std::int32_t y, std::uint8_t* p) {
		p[0] = std::uint8_t((x * 32) & 0xFF);
		p[1] = std::uint8_t((y * 32) & 0xFF);
		p[2] = std::uint8_t(((x + y) * 16) & 0xFF);
		p[3] = 255;
	});

	Rhi::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	Rhi::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices), indices, BufferUsage::StaticDraw);

	Rhi::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, W, H, nullptr);
	Rhi::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	Rhi::Device::SetupInitialState();
	Rhi::Device::SetViewport(Recti(0, 0, W, H));
	Rhi::Device::SetClearColor(Colorf(0.0f, 0.0f, 0.0f, 1.0f));

	float model[16];
	MakeTranslation(0.0f, 0.0f, model);
	auto ib = blocks.GetUniformBlock("InstanceBlock");
	ib->GetUniform("modelMatrix")->SetFloatVector(model);
	ib->GetUniform("color")->SetFloatVector(kWhite);
	ib->GetUniform("texRect")->SetFloatVector(kTexRectFull);
	ib->GetUniform("spriteSize")->SetFloatValue(float(W), float(H));

	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();

	auto issueDraw = [&](const char* lbl) {
		program.SetObjectLabel(lbl);
		blocks.CommitUniformBlocks();
		srcTexture.Bind(0);
		program.Use();
		blocks.Bind();
		camera.CommitUniforms();
		Rhi::Device::SetBlendingEnabled(false);
		Rhi::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		Rhi::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);
	};

	// Non-dither pass, captured for comparison
	Rhi::Device::Clear(ClearFlags::Color);
	issueDraw("TexturedBackground");
	std::vector<std::uint8_t> nonDither(pixels, pixels + std::size_t(H) * stride);

	// Dither pass (the PNG)
	Rhi::Device::Clear(ClearFlags::Color);
	issueDraw("TexturedBackgroundDither");

	std::printf("Horizon band still resolves to the horizon colour:\n");
	CheckPixel(pixels, stride, 128, 72, 230, 51, 26, 255, 1, "band centre");

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

	// Dithering must change a meaningful number of pixels versus the non-dither pass
	int changed = 0;
	for (std::int32_t y = 0; y < H; y++) {
		for (std::int32_t x = 0; x < W; x++) {
			const std::uint8_t* a = nonDither.data() + std::size_t(y) * stride + std::size_t(x) * 4;
			const std::uint8_t* b = pixels + std::size_t(y) * stride + std::size_t(x) * 4;
			if (std::abs(int(a[0]) - int(b[0])) > 1 || std::abs(int(a[1]) - int(b[1])) > 1 || std::abs(int(a[2]) - int(b[2])) > 1) {
				changed++;
			}
		}
	}
	g_checks++;
	if (changed > 200) {
		std::printf("  ok   dither changed %d pixels vs. non-dither\n", changed);
	} else {
		g_failures++;
		std::printf("  FAIL dither changed only %d pixels\n", changed);
	}

	char outputPath[512];
	MakePath(baseDir, "sw_background_dither.png", outputPath, sizeof(outputPath));
	const bool wrote = WritePng(outputPath, pixels, W, H, stride);
	std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
	return wrote;
}

// --- Combine (viewport compositor) ---

bool RunCombineTest(const char* baseDir)
{
	constexpr std::int32_t W = 64, H = 64;
	g_uniformBump = 0;
	std::printf("\n=== Combine (scene + lighting + blur + ambient) ===\n");

	Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
	program.SetReflection(&Jazz2::ShadersGen::Combine.Variants[0]);
	program.Link(Rhi::ShaderProgram::Introspection::Enabled);
	program.SetObjectLabel("Combine");

	Rhi::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	Rhi::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
	Rhi::ShaderUniforms camera(&program);
	camera.SetUniformsDataPointer(cameraBuffer.data());
	float projection[16];
	BuildOrtho(W, H, projection);
	camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
	camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);
	const float ambient[4] = {0.15f, 0.10f, 0.30f, 1.0f};	// -> (38, 26, 76) in the dark regions
	camera.GetUniform("uAmbientColor")->SetFloatVector(ambient);
	camera.GetUniform("uTime")->SetFloatValue(0.0f);

	std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
	Rhi::ShaderUniformBlocks blocks(&program);
	blocks.SetUniformsDataPointer(blockBuffer.data());

	// Scene: four solid quadrants (so a +-1 texel sampling wobble stays in-colour)
	Rhi::Texture sceneTexture(TextureTarget::Texture2D);
	UploadRgba(sceneTexture, W, H, [](std::int32_t x, std::int32_t y, std::uint8_t* p) {
		const bool right = (x >= 32), bottom = (y >= 32);
		if (!right && !bottom) { p[0] = 200; p[1] = 40; p[2] = 40; }		// TL red
		else if (right && !bottom) { p[0] = 40; p[1] = 200; p[2] = 40; }	// TR green
		else if (!right && bottom) { p[0] = 40; p[1] = 40; p[2] = 200; }	// BL blue
		else { p[0] = 200; p[1] = 200; p[2] = 40; }							// BR yellow
		p[3] = 255;
	});
	// Lighting: three vertical bands (lit / half+glow / dark), uniform per column
	Rhi::Texture lightingTexture(TextureTarget::Texture2D);
	UploadRgba(lightingTexture, W, H, [](std::int32_t x, std::int32_t y, std::uint8_t* p) {
		static_cast<void>(y);
		if (x <= 20) { p[0] = 255; p[1] = 0; p[2] = 0; }		// fully lit
		else if (x <= 42) { p[0] = 128; p[1] = 200; p[2] = 0; }	// half lit + glow (g > 0.7)
		else { p[0] = 0; p[1] = 0; p[2] = 0; }					// dark -> ambient
		p[3] = 255;
	});
	Rhi::Texture blurHalfTexture(TextureTarget::Texture2D);
	UploadRgba(blurHalfTexture, W, H, [](std::int32_t, std::int32_t, std::uint8_t* p) { p[0] = 80; p[1] = 80; p[2] = 80; p[3] = 255; });
	Rhi::Texture blurQuarterTexture(TextureTarget::Texture2D);
	UploadRgba(blurQuarterTexture, W, H, [](std::int32_t, std::int32_t, std::uint8_t* p) { p[0] = 120; p[1] = 120; p[2] = 120; p[3] = 255; });

	Rhi::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	Rhi::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices), indices, BufferUsage::StaticDraw);

	Rhi::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, W, H, nullptr);
	Rhi::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	Rhi::Device::SetupInitialState();
	Rhi::Device::SetViewport(Recti(0, 0, W, H));
	Rhi::Device::SetClearColor(Colorf(0.0f, 0.0f, 0.0f, 1.0f));
	Rhi::Device::Clear(ClearFlags::Color);

	float model[16];
	MakeTranslation(0.0f, 0.0f, model);
	auto ib = blocks.GetUniformBlock("InstanceBlock");
	ib->GetUniform("modelMatrix")->SetFloatVector(model);
	ib->GetUniform("color")->SetFloatVector(kWhite);
	ib->GetUniform("texRect")->SetFloatVector(kTexRectFull);
	ib->GetUniform("spriteSize")->SetFloatValue(float(W), float(H));
	blocks.CommitUniformBlocks();

	sceneTexture.Bind(0);
	lightingTexture.Bind(1);
	blurHalfTexture.Bind(2);
	blurQuarterTexture.Bind(3);
	program.Use();
	blocks.Bind();
	camera.CommitUniforms();
	Rhi::Device::SetBlendingEnabled(false);
	Rhi::Device::SetScissorTestEnabled(false);
	program.DefineVertexFormat(&vbo, &ibo, 0);
	vbo.Bind();
	Rhi::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);

	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();

	std::printf("Fully lit (light.r = 1, g = 0) passes the scene through unchanged:\n");
	CheckPixel(pixels, stride, 10, 16, 200, 40, 40, 255, 2, "lit -> scene TL");
	CheckPixel(pixels, stride, 10, 48, 40, 40, 200, 255, 2, "lit -> scene BL");
	std::printf("Dark (light.r = 0) resolves to the ambient colour:\n");
	CheckPixel(pixels, stride, 54, 16, 38, 26, 76, 255, 2, "dark -> ambient");
	CheckPixel(pixels, stride, 54, 48, 38, 26, 76, 255, 2, "dark -> ambient");

	// Half-lit band: reproduce the composite independently and assert the pixel matches
	{
		const float sceneRgb[4] = {40.0f / 255.0f, 200.0f / 255.0f, 40.0f / 255.0f, 1.0f};	// scene TR (x=33, y=20)
		const float lr = 128.0f / 255.0f, lg = 200.0f / 255.0f;
		const float blurGray = ((80.0f + 120.0f) * 0.5f) / 255.0f;
		const float glow = std::max(lg - 0.7f, 0.0f);
		const float t1 = std::min(std::max((1.0f - lr) / std::sqrt(std::max(ambient[3], 0.35f)), 0.0f), 1.0f);
		const float t2 = 1.0f - lr;
		int expected[4];
		for (int i = 0; i < 4; i++) {
			const float blurI = (i < 3 ? blurGray : blurGray);
			const float lit = sceneRgb[i] * (1.0f + lg) + glow;
			const float mid = lit + (blurI - lit) * t1;
			const float outF = (mid + (ambient[i] - mid) * t2) * 255.0f;
			expected[i] = int(outF + 0.5f);
		}
		expected[3] = 255;
		std::printf("Half-lit band matches an independent composite:\n");
		CheckPixel(pixels, stride, 33, 20, expected[0], expected[1], expected[2], expected[3], 2, "half -> composite");
	}

	char outputPath[512];
	MakePath(baseDir, "sw_combine.png", outputPath, sizeof(outputPath));
	const bool wrote = WritePng(outputPath, pixels, W, H, stride);
	std::printf("PNG: %s (%s)\n", outputPath, wrote ? "ok" : "FAILED");
	return wrote;
}

// --- PaletteRemap / BatchedPaletteRemap (the gameplay-critical index -> palette path) ---

bool RunPaletteTest(const char* baseDir)
{
	constexpr std::int32_t W = 64, H = 64;
	g_uniformBump = 0;
	std::printf("\n=== PaletteRemap / BatchedPaletteRemap ===\n");

	Rhi::Buffer uniformBuffer(BufferTarget::Uniform);
	uniformBuffer.BufferData(64 * 1024, nullptr, BufferUsage::StreamDraw);
	g_uniformBuffer = &uniformBuffer;
	Rhi::ShaderUniformBlocks::SetUniformRangeAllocator(&AllocUniformRange);

	// 256-wide, 2-row palette texture (row 0 and row 1 hold two distinct 256-colour palettes)
	Rhi::Texture paletteTexture(TextureTarget::Texture2D);
	UploadRgba(paletteTexture, 256, 2, [](std::int32_t x, std::int32_t y, std::uint8_t* p) {
		if (y == 0) {
			if (x == 0) { p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0; }	// index 0 = transparent
			else { p[0] = std::uint8_t(x); p[1] = std::uint8_t(255 - x); p[2] = 128; p[3] = 255; }
		} else {
			p[0] = std::uint8_t(255 - x); p[1] = std::uint8_t(x); p[2] = 64; p[3] = 255;
		}
	});

	// R8 index sprite: index = column * 16 (0, 16, ... 240), uniform down each column
	Rhi::Texture indexTexture(TextureTarget::Texture2D);
	{
		std::vector<std::uint8_t> data(16 * 16);
		for (std::int32_t y = 0; y < 16; y++) {
			for (std::int32_t x = 0; x < 16; x++) {
				data[std::size_t(y) * 16 + x] = std::uint8_t((x * 16) & 0xFF);
			}
		}
		indexTexture.TexImage2D(0, PixelFormat::R8, false, 16, 16, data.data());
	}

	Rhi::Buffer vbo(BufferTarget::Vertex);
	vbo.BufferData(4 * 4 * sizeof(float), nullptr, BufferUsage::StaticDraw);
	Rhi::Buffer ibo(BufferTarget::Index);
	const std::uint16_t indices6[6] = {0, 1, 2, 2, 1, 3};
	ibo.BufferData(sizeof(indices6), indices6, BufferUsage::StaticDraw);

	Rhi::Texture colorTexture(TextureTarget::Texture2D);
	colorTexture.TexImage2D(0, PixelFormat::RGBA8, false, W, H, nullptr);
	Rhi::RenderTarget renderTarget;
	renderTarget.AttachColorTexture(colorTexture, 0);
	renderTarget.SetDrawBuffers(1);
	renderTarget.BindDraw();

	const std::uint8_t* pixels = colorTexture.GetPixels();
	const std::int32_t stride = colorTexture.GetStrideBytes();
	bool wroteAll = true;

	// ---- Draw 1: single PaletteRemap, palette row 0, opaque, white modulation ----
	{
		Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
		program.SetReflection(&Jazz2::ShadersGen::PaletteRemap.Variants[0]);
		program.Link(Rhi::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("PaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		Rhi::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		Rhi::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		Rhi::Device::SetupInitialState();
		Rhi::Device::SetViewport(Recti(0, 0, W, H));
		Rhi::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		Rhi::Device::Clear(ClearFlags::Color);

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
		Rhi::Device::SetBlendingEnabled(false);
		Rhi::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		Rhi::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);

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
		Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
		program.SetReflection(&Jazz2::ShadersGen::PaletteRemap.Variants[0]);
		program.Link(Rhi::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("PaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		Rhi::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		Rhi::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		Rhi::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		Rhi::Device::Clear(ClearFlags::Color);

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
		Rhi::Device::SetBlendingEnabled(false);
		Rhi::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		Rhi::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);

		std::printf("Row 1 lookups (offset 256) with colour modulation (1, 0.5, 1):\n");
		CheckPixel(pixels, stride, 6, 10, 239, 8, 64, 255, 1, "row 1 index 16 x mod");
		CheckPixel(pixels, stride, 34, 10, 127, 64, 64, 255, 1, "row 1 index 128 x mod");
	}

	// ---- Draw 3: BatchedPaletteRemap, two instances (row 0 left half, row 1 right half) ----
	{
		Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
		program.SetBatchSize(2);
		program.SetReflection(&Jazz2::ShadersGen::BatchedPaletteRemap.Variants[0]);
		program.Link(Rhi::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("BatchedPaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		Rhi::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		Rhi::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		Rhi::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		Rhi::Device::Clear(ClearFlags::Color);

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
		Rhi::Device::SetBlendingEnabled(false);
		Rhi::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		Rhi::Buffer ibo12(BufferTarget::Index);
		const std::uint16_t indices12[12] = {0, 1, 2, 2, 1, 3, 0, 1, 2, 2, 1, 3};
		ibo12.BufferData(sizeof(indices12), indices12, BufferUsage::StaticDraw);
		ibo12.Bind();
		Rhi::Device::DrawElements(PrimitiveType::Triangles, 12, 0, 0);

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
		Rhi::Texture rgTexture(TextureTarget::Texture2D);
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
		}

		Rhi::ShaderProgram program(Rhi::ShaderProgram::QueryPhase::Immediate);
		program.SetReflection(&Jazz2::ShadersGen::PaletteRemap.Variants[0]);
		program.Link(Rhi::ShaderProgram::Introspection::Enabled);
		program.SetObjectLabel("PaletteRemap");

		std::vector<std::uint8_t> cameraBuffer(program.GetUniformsSize() + 16, 0);
		Rhi::ShaderUniforms camera(&program);
		camera.SetUniformsDataPointer(cameraBuffer.data());
		float projection[16];
		BuildOrtho(W, H, projection);
		camera.GetUniform("uProjectionMatrix")->SetFloatVector(projection);
		camera.GetUniform("uViewMatrix")->SetFloatVector(kIdentityView);

		std::vector<std::uint8_t> blockBuffer(program.GetUniformBlocksSize() + 16, 0);
		Rhi::ShaderUniformBlocks blocks(&program);
		blocks.SetUniformsDataPointer(blockBuffer.data());

		Rhi::Device::SetClearColor(Colorf(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f));
		Rhi::Device::Clear(ClearFlags::Color);

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
		Rhi::Device::SetBlendingEnabled(true);
		Rhi::Device::SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
		Rhi::Device::SetScissorTestEnabled(false);
		program.DefineVertexFormat(&vbo, &ibo, 0);
		vbo.Bind();
		Rhi::Device::DrawArrays(PrimitiveType::TriangleStrip, 0, 4);

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
