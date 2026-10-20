#include "Canvas.h"
#include "../ContentResolver.h"

#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/RenderResources.h"
#include "../../nCine/Base/Random.h"

namespace Jazz2::UI
{
	Canvas::Canvas()
		: AnimTime(0.0f), _renderCommandsCount(0), _nextVisitOrder(0), _currentRenderQueue(nullptr)
	{
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

	void Canvas::OnUpdate(float timeMult)
	{
		SceneNode::OnUpdate(timeMult);

		AnimTime += timeMult * AnimTimeMultiplier;
	}

	bool Canvas::OnDraw(RenderQueue& renderQueue)
	{
		SceneNode::OnDraw(renderQueue);

		_renderCommandsCount = 0;
		_nextVisitOrder = 0;
		_currentRenderQueue = &renderQueue;

		// The glyph batches start over too, each record remembering how many glyphs it took (see GlyphBatch)
		for (GlyphBatch& glyphBatch : _glyphBatches) {
			glyphBatch.LastCount = glyphBatch.Count;
			glyphBatch.Count = 0;
			glyphBatch.Active = false;
			glyphBatch.StandIn = nullptr;
			glyphBatch.Batch = RenderBatcher::DirectBatch();
			glyphBatch.BatchCommandsUsed = 0;
		}
		// The runs of rectangles too, each record keeps the commands of its batches
		for (QuadBatch& quadBatch : _quadBatches) {
			quadBatch.Open = false;
			quadBatch.Direct = true;
			quadBatch.StandIn = nullptr;
			quadBatch.Batch = RenderBatcher::DirectBatch();
			quadBatch.BatchCommandsUsed = 0;
		}
		_openQuadRuns = 0;

		return false;
	}

	void Canvas::DrawRenderCommand(RenderCommand* command)
	{
		EndQuadRun(command->GetLayer());
		_currentRenderQueue->AddCommand(command);
	}

	void Canvas::DrawTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, bool additiveBlending, float angle, std::int32_t paletteOffset)
	{
		// Apply the optional per-layer draw transform (menu section transitions; identity by default)
		pos = pos * LayerScale + LayerOffset;
		size = size * LayerScale;
		const Colorf finalColor = color * LayerColor;

		Vector4f clippedTexCoords = texCoords;
		if (std::abs(angle) > 0.01f) {
			// A rotated rectangle can't be cut down, it's only left out if all of it is outside. It turns about its
			// center, so whatever the angle, all of it stays within the square around the circle through its corners.
			if (ClipRect.W > 0.0f && ClipRect.H > 0.0f) {
				const float radius = 0.5f * std::sqrt(size.X * size.X + size.Y * size.Y);
				Vector2f boundsPos = pos + size * 0.5f - Vector2f(radius, radius);
				Vector2f boundsSize = Vector2f(radius * 2.0f, radius * 2.0f);
				if (!ClipQuad(boundsPos, boundsSize, nullptr)) {
					return;
				}
			}
		} else if (!ClipQuad(pos, size, &clippedTexCoords)) {
			return;
		}

		DrawTransformedTexture(texture, pos, z, size, clippedTexCoords, finalColor, additiveBlending, angle, paletteOffset);
	}

	void Canvas::DrawTextureBatched(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color)
	{
		// Apply the optional per-layer draw transform (menu section transitions; identity by default)
		pos = pos * LayerScale + LayerOffset;
		size = size * LayerScale;
		const Colorf finalColor = color * LayerColor;

		Vector4f clippedTexCoords = texCoords;
		if (!ClipQuad(pos, size, &clippedTexCoords)) {
			return;
		}

		QuadBatch* record = GetQuadRun(texture, z);
		if (record != nullptr) {
			RenderBatcher::DirectBatch& batch = record->Batch;
			if (batch.Count == batch.Capacity) {
				// Full, the run goes on in another batch, drawn right after this one
				RenderResources::GetRenderBatcher().EndDirectBatch(batch);
				record->RunCapacity = std::min<std::uint32_t>(record->RunCapacity * 2, 1024);
				record->StandIn->SetVisitOrder(TakeVisitOrder());
				if (!BeginQuadBatch(*record)) {
					record = nullptr;
				}
			}
		}
		if (record == nullptr) {
			DrawTransformedTexture(texture, pos, z, size, clippedTexCoords, finalColor, false, 0.0f, -1);
			return;
		}

		// Exactly what the command of its own would carry once committed, as a glyph in Font::DrawString()
		RenderBatcher::DirectBatch& batch = record->Batch;
		std::uint8_t* instance = batch.Instances + batch.Count * batch.Stride;
		float* modelMatrix = static_cast<float*>(static_cast<void*>(instance + record->ModelMatrixOffset));
		modelMatrix[0] = 1.0f; modelMatrix[1] = 0.0f; modelMatrix[2] = 0.0f; modelMatrix[3] = 0.0f;
		modelMatrix[4] = 0.0f; modelMatrix[5] = 1.0f; modelMatrix[6] = 0.0f; modelMatrix[7] = 0.0f;
		modelMatrix[8] = 0.0f; modelMatrix[9] = 0.0f; modelMatrix[10] = 1.0f; modelMatrix[11] = 0.0f;
		modelMatrix[12] = pos.X; modelMatrix[13] = pos.Y; modelMatrix[14] = record->Depth; modelMatrix[15] = 1.0f;
		float* instanceColor = static_cast<float*>(static_cast<void*>(instance + record->ColorOffset));
		instanceColor[0] = finalColor.R; instanceColor[1] = finalColor.G; instanceColor[2] = finalColor.B; instanceColor[3] = finalColor.A;
		float* texRect = static_cast<float*>(static_cast<void*>(instance + record->TexRectOffset));
		texRect[0] = clippedTexCoords.X; texRect[1] = clippedTexCoords.Y; texRect[2] = clippedTexCoords.Z; texRect[3] = clippedTexCoords.W;
		float* spriteSize = static_cast<float*>(static_cast<void*>(instance + record->SpriteSizeOffset));
		spriteSize[0] = size.X;
		spriteSize[1] = size.Y;
		if (record->PaletteOffsetOffset >= 0) {
			*static_cast<float*>(static_cast<void*>(instance + record->PaletteOffsetOffset)) = 0.0f;
		}
		batch.Count++;
		// The batch is finished for what it holds after every rectangle, so it's drawn whole whenever the frame ends
		RenderResources::GetRenderBatcher().EndDirectBatch(batch);
	}

	Canvas::QuadBatch* Canvas::GetQuadRun(const Texture& texture, std::uint16_t layer)
	{
		std::int32_t index = -1;
		if (_lastQuadBatch >= 0 && _lastQuadBatch < std::int32_t(_quadBatches.size()) && _quadBatches[_lastQuadBatch].Layer == layer) {
			index = _lastQuadBatch;
		} else {
			for (std::int32_t i = 0; i < std::int32_t(_quadBatches.size()); i++) {
				if (_quadBatches[i].Layer == layer) {
					index = i;
					break;
				}
			}
			if (index < 0) {
				index = std::int32_t(_quadBatches.size());
				_quadBatches.emplace_back();
				_quadBatches[index].Layer = layer;
			}
		}
		_lastQuadBatch = index;

		QuadBatch& record = _quadBatches[index];
		if (!record.Direct) {
			return nullptr;
		}
		if (record.Open && record.QuadTexture == &texture) {
			return &record;
		}
		if (record.Open) {
			// Another texture, the rectangles of the run before are drawn under it
			record.Open = false;
			_openQuadRuns--;
		}

		bool textureChanged = (record.QuadTexture != &texture);
		if (record.StandIn == nullptr) {
			// The first run of the record in this frame: the stand-in, and everything its rectangles share
			textureChanged = true;
			record.StandIn = RentRenderCommand();
			Material& material = record.StandIn->GetMaterial();
			if (material.SetShaderProgramType(Material::ShaderProgramType::Sprite)) {
				material.ReserveUniformsDataMemory();
				record.StandIn->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
				auto* textureUniform = material.Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
			}
			material.SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
			record.StandIn->SetLayer(layer);

			// Rectangles are written straight into the instances, member by member, at the offsets the stand-in's own
			// instance block has them, as glyphs are (see GetGlyphBatch())
			const RenderCommand::InstanceUniforms* instanceUniforms = record.StandIn->GetInstanceUniforms();
			RHI::UniformBlockCache* instanceBlock = record.StandIn->GetInstanceBlock();
			RHI::UniformCache* modelMatrix = (instanceBlock != nullptr ? instanceBlock->GetUniform(Material::ModelMatrixUniformName) : nullptr);
			if (instanceUniforms == nullptr || modelMatrix == nullptr || instanceUniforms->Color == nullptr ||
				instanceUniforms->TexRect == nullptr || instanceUniforms->SpriteSize == nullptr) {
				record.Direct = false;
				return nullptr;
			}
			const std::uint8_t* base = instanceBlock->GetDataPointer();
			record.ModelMatrixOffset = std::uint32_t(modelMatrix->GetDataPointer() - base);
			record.ColorOffset = std::uint32_t(instanceUniforms->Color->GetDataPointer() - base);
			record.TexRectOffset = std::uint32_t(instanceUniforms->TexRect->GetDataPointer() - base);
			record.SpriteSizeOffset = std::uint32_t(instanceUniforms->SpriteSize->GetDataPointer() - base);
			record.PaletteOffsetOffset = (instanceUniforms->PaletteOffset != nullptr
				? std::int32_t(instanceUniforms->PaletteOffset->GetDataPointer() - base) : -1);
			const Camera::ProjectionValues& projection = RenderResources::GetCurrentCamera()->GetProjectionValues();
			record.Depth = RenderCommand::CalculateDepth(layer, projection.nearClip, projection.farClip);
		} else {
			// A run after something else drawn on the layer is drawn after that
			record.StandIn->SetVisitOrder(TakeVisitOrder());
		}
		if (textureChanged) {
			record.StandIn->GetMaterial().SetTexture(0, texture);
		}
		record.QuadTexture = &texture;

		// Most runs are a few shapes, the room grows when it's filled
		record.RunCapacity = 16;
		if (!BeginQuadBatch(record)) {
			return nullptr;
		}
		record.Open = true;
		_openQuadRuns++;
		return &record;
	}

	bool Canvas::BeginQuadBatch(QuadBatch& record)
	{
		if (record.BatchCommandsUsed >= record.BatchCommands.size()) {
			record.BatchCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Sprite));
		}
		RenderCommand* batchCommand = record.BatchCommands[record.BatchCommandsUsed].get();
		if (!RenderResources::GetRenderBatcher().BeginDirectBatch(*record.StandIn, record.RunCapacity, record.Batch, batchCommand)) {
			// The shader can't be batched this way, each rectangle gets a command of its own for the rest of the frame
			if (record.Open) {
				record.Open = false;
				_openQuadRuns--;
			}
			record.Direct = false;
			return false;
		}
		record.BatchCommandsUsed++;
		_currentRenderQueue->AddCommand(record.Batch.Command);
		return true;
	}

	void Canvas::EndQuadRunOnLayer(std::uint16_t layer)
	{
		for (QuadBatch& record : _quadBatches) {
			if (record.Open && record.Layer == layer) {
				record.Open = false;
				_openQuadRuns--;
				break;
			}
		}
	}

	std::uint16_t Canvas::TakeVisitOrder()
	{
		// The render queue orders commands of the same layer by their visit order before anything else, so a number
		// that grows with every command keeps them in the order they were drawn in (see OrderedDraws)
		if (!OrderedDraws) {
			return 0;
		}
		std::uint16_t visitOrder = _nextVisitOrder;
		if (_nextVisitOrder < UINT16_MAX) {
			_nextVisitOrder++;
		}
		return visitOrder;
	}

	void Canvas::DrawTransformedTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& clippedTexCoords,
		const Colorf& finalColor, bool additiveBlending, float angle, std::int32_t paletteOffset)
	{
		EndQuadRun(z);

		// An indexed texture (palette index in the red channel) is recolored at draw time through the shared palette
		// texture; paletteOffset selects the palette region (0 = default sprite palette). -1 = a plain RGBA texture.
		bool indexed = (paletteOffset >= 0);
		auto command = RentRenderCommand();
		bool shaderChanged = (indexed
			? command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::PaletteRemap))
			: command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite));
		if (shaderChanged) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
			if (paletteUniform != nullptr) {
				paletteUniform->SetIntValue(1); // GL_TEXTURE1
			}
		}

		// Use a separate alpha blend so the alpha channel of an RGBA render target accumulates correct "over" coverage,
		// otherwise drawing semi-transparent content over an opaque background would erode its alpha and let the
		// scene behind show through. Harmless for opaque/RGB targets (alpha ignored).
		if (additiveBlending) {
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One, BlendingFactor::Zero, BlendingFactor::One);
		} else {
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
		}

		// Resolved once per shader change instead of looked up in the block by name on every draw
		auto* instanceUniforms = command->GetInstanceUniforms();
		instanceUniforms->TexRect->SetFloatVector(clippedTexCoords.Data());
		instanceUniforms->SpriteSize->SetFloatVector(size.Data());
		instanceUniforms->Color->SetFloatVector(finalColor.Data());
		if (indexed && instanceUniforms->PaletteOffset != nullptr) {
			instanceUniforms->PaletteOffset->SetFloatValue((float)paletteOffset);
		}

		Matrix4x4f worldMatrix = Matrix4x4f::Translation(pos.X, pos.Y, 0.0f);
		if (std::abs(angle) > 0.01f) {
			worldMatrix.Translate(size.X * 0.5f, size.Y * 0.5f, 0.0f);
			worldMatrix.RotateZ(angle);
			worldMatrix.Translate(size.X * -0.5f, size.Y * -0.5f, 0.0f);
		}
		command->SetTransformation(worldMatrix);
		command->SetLayer(z);
		command->GetMaterial().SetTexture(0, texture);
		if (indexed) {
			Texture* palette = ContentResolver::Get().GetPaletteTexture();
			if (palette != nullptr) {
				command->GetMaterial().SetTexture(1, *palette);
			}
		}

		_currentRenderQueue->AddCommand(command);
	}

	void Canvas::DrawTextureWithPalette(const Texture& texture, const Texture& palette, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, float paletteOffset)
	{
		auto* shader = ContentResolver::Get().GetShader(PrecompiledShader::PaletteRemap);
		if (shader == nullptr) {
			// No palette shader - fall back to a plain textured draw (which applies the layer transform itself)
			DrawTexture(texture, pos, z, size, texCoords, color);
			return;
		}

		// Apply the optional per-layer draw transform (menu section transitions; identity by default)
		pos = pos * LayerScale + LayerOffset;
		size = size * LayerScale;
		const Colorf finalColor = color * LayerColor;

		Vector4f clippedTexCoords = texCoords;
		if (!ClipQuad(pos, size, &clippedTexCoords)) {
			return;
		}

		EndQuadRun(z);
		auto command = RentRenderCommand();
		if (command->GetMaterial().SetShader(shader)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
			if (paletteUniform != nullptr) {
				paletteUniform->SetIntValue(1); // GL_TEXTURE1
			}
		}

		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);

		// Resolved once per shader change instead of looked up in the block by name on every draw
		auto* instanceUniforms = command->GetInstanceUniforms();
		instanceUniforms->TexRect->SetFloatVector(clippedTexCoords.Data());
		instanceUniforms->SpriteSize->SetFloatVector(size.Data());
		instanceUniforms->Color->SetFloatVector(finalColor.Data());
		if (instanceUniforms->PaletteOffset != nullptr) {
			instanceUniforms->PaletteOffset->SetFloatValue(paletteOffset);
		}

		command->SetTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
		command->SetLayer(z);
		command->GetMaterial().SetTexture(0, texture);
		command->GetMaterial().SetTexture(1, palette);

		_currentRenderQueue->AddCommand(command);
	}

	void Canvas::DrawSolid(Vector2f pos, std::uint16_t z, Vector2f size, const Colorf& color, bool additiveBlending)
	{
		// Apply the optional per-layer draw transform (menu section transitions; identity by default)
		pos = pos * LayerScale + LayerOffset;
		size = size * LayerScale;
		const Colorf finalColor = color * LayerColor;

		if (!ClipQuad(pos, size, nullptr)) {
			return;
		}

		EndQuadRun(z);
		auto command = RentRenderCommand();
		if (command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::SpriteNoTexture)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			// Required to reset render command properly
			//command->SetTransformation(command->transformation());
		}

		if (additiveBlending) {
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One, BlendingFactor::Zero, BlendingFactor::One);
		} else {
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
		}

		// Resolved once per shader change instead of looked up in the block by name on every draw
		auto* instanceUniforms = command->GetInstanceUniforms();
		instanceUniforms->SpriteSize->SetFloatVector(size.Data());
		instanceUniforms->Color->SetFloatVector(finalColor.Data());

		command->SetTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
		command->SetLayer(z);
		command->GetMaterial().SetTexture(0, nullptr);

		_currentRenderQueue->AddCommand(command);
	}

	RenderCommand* Canvas::RentGlyphCommand(const Texture& fontTexture, Shader* colorizeShader)
	{
		RenderCommand* command = RentRenderCommand();
		command->SetType(RenderCommand::Type::Text);
		const bool shaderChanged = (colorizeShader != nullptr
			? command->GetMaterial().SetShader(colorizeShader)
			: command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite));
		if (shaderChanged) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}

		// Separate alpha blend so text (e.g. semi-transparent shadows) accumulates correct alpha coverage
		// when drawn into an RGBA render target, harmless for opaque/RGB targets
		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
		command->GetMaterial().SetTexture(fontTexture);
		return command;
	}

	std::int32_t Canvas::GetGlyphBatch(const Texture& fontTexture, Shader* colorizeShader, std::uint16_t layer)
	{
		// A text drawn over rectangles of its layer comes after them
		EndQuadRun(layer);

		const bool colorized = (colorizeShader != nullptr);
		std::int32_t index = -1, unusedIndex = -1;
		for (std::int32_t i = 0; i < std::int32_t(_glyphBatches.size()); i++) {
			const GlyphBatch& glyphBatch = _glyphBatches[i];
			if (glyphBatch.FontTexture == &fontTexture && glyphBatch.Colorized == colorized && glyphBatch.Layer == layer) {
				index = i;
				break;
			}
			// A record used neither in this frame nor in the previous one can take the new key
			if (unusedIndex < 0 && !glyphBatch.Active && glyphBatch.LastCount == 0) {
				unusedIndex = i;
			}
		}
		if (index < 0) {
			if (unusedIndex >= 0) {
				index = unusedIndex;
			} else {
				index = std::int32_t(_glyphBatches.size());
				_glyphBatches.emplace_back();
			}
			GlyphBatch& glyphBatch = _glyphBatches[index];
			glyphBatch = GlyphBatch();
			glyphBatch.FontTexture = &fontTexture;
			glyphBatch.Colorized = colorized;
			glyphBatch.Layer = layer;
		}

		GlyphBatch& glyphBatch = _glyphBatches[index];
		if (!glyphBatch.Active) {
			// The first glyph of the record in this frame: the stand-in, and everything its glyphs share
			glyphBatch.Active = true;
			glyphBatch.Direct = false;
			glyphBatch.StandIn = RentGlyphCommand(fontTexture, colorizeShader);
			glyphBatch.StandIn->SetLayer(layer);

			// Glyphs are written straight into the instances, member by member, at the offsets the stand-in's own
			// instance block has them - the batched shader's instances are laid out exactly like that block
			const RenderCommand::InstanceUniforms* instanceUniforms = glyphBatch.StandIn->GetInstanceUniforms();
			RHI::UniformBlockCache* instanceBlock = glyphBatch.StandIn->GetInstanceBlock();
			RHI::UniformCache* modelMatrix = (instanceBlock != nullptr ? instanceBlock->GetUniform(Material::ModelMatrixUniformName) : nullptr);
			if (instanceUniforms != nullptr && modelMatrix != nullptr && instanceUniforms->Color != nullptr &&
				instanceUniforms->TexRect != nullptr && instanceUniforms->SpriteSize != nullptr) {
				const std::uint8_t* base = instanceBlock->GetDataPointer();
				glyphBatch.ModelMatrixOffset = std::uint32_t(modelMatrix->GetDataPointer() - base);
				glyphBatch.ColorOffset = std::uint32_t(instanceUniforms->Color->GetDataPointer() - base);
				glyphBatch.TexRectOffset = std::uint32_t(instanceUniforms->TexRect->GetDataPointer() - base);
				glyphBatch.SpriteSizeOffset = std::uint32_t(instanceUniforms->SpriteSize->GetDataPointer() - base);
				glyphBatch.PaletteOffsetOffset = (instanceUniforms->PaletteOffset != nullptr
					? std::int32_t(instanceUniforms->PaletteOffset->GetDataPointer() - base) : -1);
				// What RenderCommand::CommitNodeTransformation() puts in the z translation of a command on this layer
				const Camera::ProjectionValues& projection = RenderResources::GetCurrentCamera()->GetProjectionValues();
				glyphBatch.Depth = RenderCommand::CalculateDepth(layer, projection.nearClip, projection.farClip);
				glyphBatch.Direct = true;
			}
		}
		return index;
	}

	RenderCommand* Canvas::NextGlyphBatchCommand(GlyphBatch& glyphBatch)
	{
		if (glyphBatch.BatchCommandsUsed >= glyphBatch.BatchCommands.size()) {
			glyphBatch.BatchCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Text));
		}
		return glyphBatch.BatchCommands[glyphBatch.BatchCommandsUsed++].get();
	}

	void Canvas::FinishGlyphBatches()
	{
		RenderBatcher& batcher = RenderResources::GetRenderBatcher();
		for (GlyphBatch& glyphBatch : _glyphBatches) {
			if (glyphBatch.Active) {
				batcher.EndDirectBatch(glyphBatch.Batch);
			}
		}
	}

	Vector2f Canvas::ApplyAlignment(Alignment align, Vector2f vec, Vector2f size)
	{
		Vector2f result = vec;

		// Sprites are aligned to center by default
		switch (align & Alignment::HorizontalMask) {
			case Alignment::Center: result.X -= size.X * 0.5f; break;
			case Alignment::Right: result.X -= size.X; break;
		}
		switch (align & Alignment::VerticalMask) {
			case Alignment::Center: result.Y -= size.Y * 0.5f; break;
			case Alignment::Bottom: result.Y -= size.Y; break;
		}

		return result;
	}

	RenderCommand* Canvas::RentRenderCommand()
	{
		RenderCommand* command;
		if (_renderCommandsCount < _renderCommands.size()) {
			command = _renderCommands[_renderCommandsCount].get();
			command->SetType(RenderCommand::Type::Sprite);
			// A pooled command may still reference a palette texture at unit 1 from a previous frame; that texture
			// can have been freed since (e.g., a menu section's preview palette that was destroyed on navigating
			// back), so clear it to avoid binding a dangling pointer. Palette draws re-bind unit 1 after renting.
			command->GetMaterial().SetTexture(1, nullptr);
		} else {
			command = _renderCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Sprite)).get();
			command->GetMaterial().SetBlendingEnabled(true);
		}
		_renderCommandsCount++;

		command->SetVisitOrder(TakeVisitOrder());
		return command;
	}

	bool Canvas::ClipQuadToRect(Vector2f& pos, Vector2f& size, Vector4f* texCoords) const
	{
		const float x0 = pos.X, y0 = pos.Y;
		const float x1 = pos.X + size.X, y1 = pos.Y + size.Y;
		const float cx0 = std::max(x0, ClipRect.X);
		const float cy0 = std::max(y0, ClipRect.Y);
		const float cx1 = std::min(x1, ClipRect.X + ClipRect.W);
		const float cy1 = std::min(y1, ClipRect.Y + ClipRect.H);
		if (cx1 <= cx0 || cy1 <= cy0) {
			return false;
		}
		if (cx0 == x0 && cy0 == y0 && cx1 == x1 && cy1 == y1) {
			// Entirely inside
			return true;
		}

		if (texCoords != nullptr && size.X > 0.0f && size.Y > 0.0f) {
			// A texture coordinate is `t * scale + offset` for `t` running from 0 to 1 across the rectangle, so the
			// part that is left runs from `left` to `right` of the original `t`
			const float left = (cx0 - x0) / size.X, right = (cx1 - x0) / size.X;
			const float top = (cy0 - y0) / size.Y, bottom = (cy1 - y0) / size.Y;
			const Vector4f original = *texCoords;
			texCoords->X = original.X * (right - left);
			texCoords->Y = original.Y + original.X * left;
			texCoords->Z = original.Z * (bottom - top);
			texCoords->W = original.W + original.Z * top;
		}

		pos = Vector2f(cx0, cy0);
		size = Vector2f(cx1 - cx0, cy1 - cy0);
		return true;
	}
}