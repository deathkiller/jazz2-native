#include "PerformanceOverlay.h"
#include "Font.h"

#include "../../nCine/Base/FrameStatistics.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/Texture.h"
#include "../../nCine/Graphics/Viewport.h"
#include "../../nCine/Graphics/RHI/RhiFwd.h"	// RHI_CAP_POSTPROCESSING (a header macro, not a build define)

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Death::Containers::Literals;

namespace Jazz2::UI
{
	namespace
	{
		constexpr float TextScale = 0.7f;
		constexpr float CharSpacing = 0.9f;
		constexpr float LineSpacing = 0.85f;
		constexpr float Padding = 3.0f;
		constexpr float ColumnGap = 8.0f;
		constexpr float BlockGap = 4.0f;
		constexpr std::int32_t MaxRowsPerBlock = 16;
		// Builds a column remembers its widest text for, at two a second - a column narrows between five and ten
		// seconds after its text did
		constexpr std::int32_t WidthWindowBuilds = 10;
		// The texture grows in steps of this many pixels, so the table growing by a few does not reallocate it
		constexpr std::int32_t TargetGranularity = 16;
		// Layer of the text in the texture; a glyph goes on this layer or on the one below it (see Font::DrawString())
		constexpr std::uint16_t TextLayer = 2;

		// Tuned for the colorizing shader the font is drawn with, where (0.5, 0.5, 0.5) is the font's own grey
		constexpr Colorf LabelColor = Colorf(0.44f, 0.44f, 0.44f, 0.5f);
		constexpr float BackdropAlpha = 0.4f;

#if defined(RHI_CAP_POSTPROCESSING)
		// With an alpha channel, the backdrop stays see-through. The text is drawn over it with the separate alpha
		// blend every canvas uses, which leaves the texture premultiplied, and a black backdrop cleared with its
		// alpha is premultiplied already - so a premultiplied composite comes out exactly as drawing it all onto
		// the screen directly did.
		constexpr Texture::Format TargetFormat = Texture::Format::RGBA8;
		constexpr Colorf TargetClearColor = Colorf(0.0f, 0.0f, 0.0f, BackdropAlpha);
#else
		// The direct tier renders into the portable opaque format - several of its backends have no alpha in a
		// render target at all (the PowerVR renders into RGB565, the RDP keeps one bit of it, the GE shares it
		// with the stencil) - so the backdrop is opaque in the texture and the whole table is faded as it is drawn
		constexpr Texture::Format TargetFormat = Texture::ColorTargetFormat;
		constexpr Colorf TargetClearColor = Colorf(0.0f, 0.0f, 0.0f, 1.0f);
		constexpr float OpaqueTableAlpha = 0.75f;
#endif

		// Taken as arrays, because an array converts to a string view through strlen()
		template<std::size_t Size>
		StringView FormatMilliseconds(char(&buffer)[Size], float value)
		{
			return StringView(buffer, formatInto(buffer, "{:.2f} ms", value));
		}

		template<std::size_t Size>
		StringView FormatBytes(char(&buffer)[Size], float value, float limit)
		{
			constexpr float Megabyte = 1024.0f * 1024.0f;
			if (limit > 0.0f) {
				return StringView(buffer, formatInto(buffer, "{:.1f} / {:.1f} MB", value / Megabyte, limit / Megabyte));
			}
			if (value < Megabyte) {
				return StringView(buffer, formatInto(buffer, "{} KB", std::int32_t(value / 1024.0f + 0.5f)));
			}
			return StringView(buffer, formatInto(buffer, "{:.1f} MB", value / Megabyte));
		}

		template<std::size_t Size>
		StringView FormatCounter(char(&buffer)[Size], const FrameStatistics::Counter& counter)
		{
			switch (counter.Type) {
				case FrameStatistics::Unit::Milliseconds:
					return FormatMilliseconds(buffer, counter.Value);
				case FrameStatistics::Unit::Percent:
					return StringView(buffer, formatInto(buffer, "{}%", std::int32_t(counter.Value + 0.5f)));
				case FrameStatistics::Unit::Bytes:
					return FormatBytes(buffer, counter.Value, counter.Limit);
				default:
					return StringView(buffer, formatInto(buffer, "{}", std::int32_t(counter.Value + 0.5f)));
			}
		}

		std::int32_t RoundUpToGranularity(std::int32_t value)
		{
			return (value + TargetGranularity - 1) / TargetGranularity * TargetGranularity;
		}
	}

	PerformanceOverlay::PerformanceOverlay()
		: _rowCount(0), _snapshotSequence(0), _snapshotSeen(false), _font(nullptr), _lineHeight(0.0f), _blockCount(0),
			_rowsPerBlock(0), _labelWidths{}, _valueWidths{}, _buildsInWindow(0), _tableSize(Vector2i::Zero),
			_contentSize(Vector2i::Zero), _renderPending(false), _useTexture(true)
	{
	}

	PerformanceOverlay::~PerformanceOverlay()
	{
	}

	bool PerformanceOverlay::Update()
	{
		// A render the chain lost before it ran is scheduled again - a changed graphics option rebuilds the chain
		if (_renderPending && _view != nullptr) {
			ScheduleRender();
		}

		const FrameStatistics::Snapshot& snapshot = FrameStatistics::GetSnapshot();
		if (_snapshotSeen && snapshot.Sequence == _snapshotSequence) {
			return false;
		}

		_snapshotSequence = snapshot.Sequence;
		_snapshotSeen = true;
		_rowCount = 0;

		if (snapshot.FrameCount == 0) {
			// Nothing measured yet - the first interval has not completed - so there is no table either
			_tableSize = Vector2i::Zero;
			_contentSize = Vector2i::Zero;
			_renderPending = false;
			return false;
		}

		AddFrameStatistics();
		return true;
	}

	void PerformanceOverlay::AddRow(StringView label, StringView value)
	{
		if (_rowCount >= MaxRows) {
			return;
		}

		Row& row = _rows[_rowCount++];
		row.LabelLength = std::uint8_t(std::min(label.size(), MaxLabelLength));
		std::memcpy(row.Label, label.data(), row.LabelLength);
		row.ValueLength = std::uint8_t(std::min(value.size(), MaxValueLength));
		std::memcpy(row.Value, value.data(), row.ValueLength);
	}

	void PerformanceOverlay::Build(Font* font, float maxHeight)
	{
		if (_rowCount == 0 || font == nullptr || font->GetSizeInPixels() <= 0) {
			_tableSize = Vector2i::Zero;
			_contentSize = Vector2i::Zero;
			_renderPending = false;
			return;
		}

		// As many rows in a block as fit the height, then as many blocks as the rows need, and the rows spread
		// evenly over them so the last block is not left with a stub
		const float lineHeight = float(font->GetSizeInPixels()) * TextScale * LineSpacing;
		const std::int32_t rowsThatFit = std::clamp<std::int32_t>(maxHeight / lineHeight, 4, MaxRowsPerBlock);
		const std::int32_t blockCount = std::min((_rowCount + rowsThatFit - 1) / rowsThatFit, MaxBlocks);

		// A column is as wide as the widest text it held over the last window of builds and the one before it, so
		// the table does not jump sideways whenever a value gains or loses a digit, yet a one-off - the frame time
		// of the hitch that loaded the level - does not keep it wide for good. The window moves on before the
		// measuring rather than after it, as the rows are drawn with the widths this leaves until the next build.
		if (font != _font || blockCount != _blockCount) {
			// Dealt out over a different number of blocks, the rows of a column are not the same ones any more
			_font = font;
			_blockCount = blockCount;
			for (std::int32_t i = 0; i < MaxBlocks; i++) {
				_labelWidths[i] = {};
				_valueWidths[i] = {};
			}
			_buildsInWindow = 0;
		} else if (_buildsInWindow >= WidthWindowBuilds) {
			for (std::int32_t i = 0; i < MaxBlocks; i++) {
				_labelWidths[i] = { _labelWidths[i].Current, 0.0f };
				_valueWidths[i] = { _valueWidths[i].Current, 0.0f };
			}
			_buildsInWindow = 0;
		}
		_buildsInWindow++;
		_lineHeight = lineHeight;
		_rowsPerBlock = (_rowCount + blockCount - 1) / blockCount;

		// Measured here, twice a second at most, and not every time the table is drawn
		for (std::int32_t i = 0; i < _rowCount; i++) {
			const Row& row = _rows[i];
			const std::int32_t block = i / _rowsPerBlock;
			_labelWidths[block].Current = std::max(_labelWidths[block].Current,
				font->MeasureString({ row.Label, row.LabelLength }, TextScale, CharSpacing, LineSpacing).X);
			_valueWidths[block].Current = std::max(_valueWidths[block].Current,
				font->MeasureString({ row.Value, row.ValueLength }, TextScale, CharSpacing, LineSpacing).X);
		}

		float width = BlockGap * float(blockCount - 1);
		for (std::int32_t i = 0; i < blockCount; i++) {
			width += GetBlockWidth(i);
		}
		_tableSize = Vector2i(std::int32_t(std::ceil(width)), std::int32_t(std::ceil(float(_rowsPerBlock) * lineHeight + Padding * 2.0f)));

		if (_useTexture && EnsureTarget(_tableSize)) {
			_renderPending = true;
			ScheduleRender();
		} else {
			// Drawn directly, from the rows as they are now
			_contentSize = _tableSize;
		}
	}

	void PerformanceOverlay::Draw(Canvas* canvas, float right, float top, std::uint16_t z)
	{
		if (_contentSize.X <= 0 || _contentSize.Y <= 0) {
			return;
		}

		// On whole pixels, so the texture maps onto the view one to one
		const Vector2f size = Vector2f(float(_contentSize.X), float(_contentSize.Y));
		const Vector2f pos = Vector2f(std::round(right - size.X), std::round(top));

		if (_view == nullptr) {
			canvas->DrawSolid(pos, z, size, Colorf(0.0f, 0.0f, 0.0f, BackdropAlpha));
			DrawRows(canvas, pos, z + 4);
			return;
		}

		// Only the part of the texture the table covers, which is anchored at its top left corner
		const Vector2i targetSize = _target->GetSize();
		const Vector4f texCoords = Vector4f(size.X / float(targetSize.X), 0.0f, size.Y / float(targetSize.Y), 0.0f);

#if defined(RHI_CAP_POSTPROCESSING)
		// Canvas::DrawTexture() has no premultiplied blend, so this is that function with one. It applies the
		// canvas' own draw transform (a menu section transition) the same way, except that a premultiplied
		// texture fades by all four of its channels rather than by its alpha alone.
		const Vector2f layerPos = pos * canvas->LayerScale + canvas->LayerOffset;
		const Vector2f layerSize = size * canvas->LayerScale;
		const Colorf& tint = canvas->LayerColor;
		const Colorf color = Colorf(tint.R * tint.A, tint.G * tint.A, tint.B * tint.A, tint.A);

		RenderCommand* command = canvas->RentRenderCommand();
		Material& material = command->GetMaterial();
		if (material.SetShaderProgramType(Material::ShaderProgramType::Sprite)) {
			material.ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

			auto* textureUniform = material.Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}
		// The same factors for the alpha, so the table also accumulates correct coverage in an RGBA render target
		// (the HUD overlay layer of a supersampled scene)
		material.SetBlendingFactors(BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);

		auto instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(texCoords.Data());
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(layerSize.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

		command->SetTransformation(Matrix4x4f::Translation(layerPos.X, layerPos.Y, 0.0f));
		command->SetLayer(z);
		material.SetTexture(0, *_target);

		canvas->DrawRenderCommand(command);
#else
		canvas->DrawTexture(*_target, pos, z, size, texCoords, Colorf(1.0f, 1.0f, 1.0f, OpaqueTableAlpha));
#endif
	}

	void PerformanceOverlay::Release()
	{
		if (!_snapshotSeen && _canvas == nullptr) {
			return;
		}

		// The viewport first, it refers to the others (and leaves the chain on its own)
		_view = nullptr;
		_target = nullptr;
		_canvas = nullptr;

		_rowCount = 0;
		_snapshotSeen = false;
		_font = nullptr;
		_blockCount = 0;
		_tableSize = Vector2i::Zero;
		_contentSize = Vector2i::Zero;
		_renderPending = false;
	}

	void PerformanceOverlay::AddFrameStatistics()
	{
		const FrameStatistics::Snapshot& snapshot = FrameStatistics::GetSnapshot();

		using Phase = FrameStatistics::Phase;
		const auto phaseTime = [&snapshot](Phase phase) {
			return snapshot.PhaseTimes[(std::size_t)phase];
		};

		char value[MaxValueLength];
		AddRow("Frame"_s, FormatMilliseconds(value, snapshot.FrameTime));
		AddRow("Max"_s, FormatMilliseconds(value, snapshot.MaxFrameTime));
		// The engine's phases as they read in the game: its logic (input and events, the actors, then collisions and
		// the cameras), preparing what to draw, drawing it, and the audio streams
		AddRow("Logic"_s, FormatMilliseconds(value, phaseTime(Phase::BeginFrame) + phaseTime(Phase::Update) +
			phaseTime(Phase::PostUpdate) + phaseTime(Phase::EndFrame)));
		AddRow("Visit"_s, FormatMilliseconds(value, phaseTime(Phase::Visit)));
		AddRow("Render"_s, FormatMilliseconds(value, phaseTime(Phase::Draw)));
		AddRow("Audio"_s, FormatMilliseconds(value, phaseTime(Phase::Audio)));
		AddRow("Present"_s, FormatMilliseconds(value, phaseTime(Phase::Present)));
		// Exactly zero only when there is no frame rate limit (the display paces the frame inside Present instead)
		if (phaseTime(Phase::Wait) > 0.0f) {
			AddRow("Wait"_s, FormatMilliseconds(value, phaseTime(Phase::Wait)));
		}
		// Whatever the frame spends outside all of the above - the event pump between two frames and, in a build with
		// the ImGui overlay, its own frame and windows. Shown once it is large enough to be why the rows do not add up.
		float accountedTime = 0.0f;
		for (std::size_t i = 0; i < (std::size_t)Phase::Count; i++) {
			accountedTime += snapshot.PhaseTimes[i];
		}
		if (snapshot.FrameTime - accountedTime >= 0.5f) {
			AddRow("Other"_s, FormatMilliseconds(value, snapshot.FrameTime - accountedTime));
		}

		for (std::uint32_t i = 0; i < snapshot.CounterCount; i++) {
			const FrameStatistics::Counter& counter = snapshot.Counters[i];
			AddRow(counter.Name, FormatCounter(value, counter));
		}

		// Draw calls after batching, and what they were batched from
		AddRow("Draws"_s, StringView(value, formatInto(value, "{} / {}", std::int32_t(snapshot.DrawCalls + 0.5f),
			std::int32_t(snapshot.RenderCommands + 0.5f))));

		if (snapshot.MemoryUsed > 0) {
			AddRow("RAM"_s, FormatBytes(value, float(snapshot.MemoryUsed), float(snapshot.MemoryTotal)));
		}
	}

	bool PerformanceOverlay::EnsureTarget(Vector2i size)
	{
		const Vector2i currentSize = (_target != nullptr ? _target->GetSize() : Vector2i::Zero);
		if (_view != nullptr && size.X <= currentSize.X && size.Y <= currentSize.Y) {
			return true;
		}

		// Never smaller than it was, so a table that shrinks and grows back does not reallocate every time. Not a
		// power of two either: the backends that need one pad the store themselves, and the Nintendo 64 would
		// otherwise keep a texture of up to four times the table in its few megabytes of memory.
		const Vector2i targetSize = Vector2i(std::max(currentSize.X, RoundUpToGranularity(size.X)),
			std::max(currentSize.Y, RoundUpToGranularity(size.Y)));

		if (_canvas == nullptr) {
			_canvas = std::make_unique<TableCanvas>(this);
		}
		if (_target == nullptr) {
			_target = std::make_unique<Texture>(nullptr, TargetFormat, targetSize);
		} else {
			// Detached first, the viewport accepts a texture only of the size it already has
			if (_view != nullptr) {
				_view->RemoveAllTextures();
			}
			_target->Init(nullptr, TargetFormat, targetSize);
		}
		_target->SetMinFiltering(SamplerFilter::Nearest);
		_target->SetMagFiltering(SamplerFilter::Nearest);
		_target->SetWrap(SamplerWrapping::ClampToEdge);

		if (_view == nullptr) {
			_view = std::make_unique<Viewport>("PerformanceOverlay", _target.get(), Viewport::DepthStencilFormat::None);
			_view->SetRootNode(_canvas.get());
			_view->SetCamera(&_camera);
			_view->SetClearColor(TargetClearColor);
		} else {
			_view->SetTexture(_target.get());
		}

		if (_view->GetType() != Viewport::Type::WithTexture) {
			LOGW("Performance metrics cannot be rendered into a texture, they are drawn directly instead");
			DisableTexture();
			return false;
		}

		// Flipped like every other render target of the game (see UpscaleRenderPass), so the texture is sampled the
		// same way as any other and the table does not come out upside down
		_camera.SetOrthoProjection(0.0f, float(targetSize.X), float(targetSize.Y), 0.0f);
		_camera.SetView(0.0f, 0.0f, 0.0f, 1.0f);
		_canvas->ViewSize = targetSize;

		// A new store holds nothing, so there is nothing to show until the pass has rendered into it
		_contentSize = Vector2i::Zero;
		return true;
	}

	void PerformanceOverlay::DisableTexture()
	{
		_useTexture = false;
		_renderPending = false;
		_view = nullptr;
		_target = nullptr;
		_canvas = nullptr;
	}

	void PerformanceOverlay::ScheduleRender()
	{
		auto& chain = Viewport::GetChain();
		for (Viewport* viewport : chain) {
			if (viewport == _view.get()) {
				return;
			}
		}

		// Last, and the chain is drawn from its end, so the pass renders before anything else in the frame and the
		// table can be shown from its texture in the same frame. It is not rendered again until the table changes,
		// so it takes itself out of the chain once it has rendered (see TableCanvas::OnUpdate()).
		chain.push_back(_view.get());
	}

	void PerformanceOverlay::RemoveFromChain()
	{
		auto& chain = Viewport::GetChain();
		for (std::size_t i = chain.size(); i-- > 0; ) {
			if (chain[i] == _view.get()) {
				chain.erase(chain.begin() + i);
				break;
			}
		}
	}

	void PerformanceOverlay::DrawRows(Canvas* canvas, Vector2f origin, std::uint16_t z) const
	{
		if (_font == nullptr) {
			return;
		}

		std::int32_t charOffset = 0;
		float blockLeft = origin.X;
		for (std::int32_t b = 0; b < _blockCount; b++) {
			const float blockWidth = GetBlockWidth(b);
			const std::int32_t first = b * _rowsPerBlock;
			const std::int32_t last = std::min(first + _rowsPerBlock, _rowCount);
			for (std::int32_t i = first; i < last; i++) {
				// Row by row rather than a block as one string of lines, as a string gets aligned only up to 16 lines
				const Row& row = _rows[i];
				const float y = origin.Y + Padding + float(i - first) * _lineHeight;
				_font->DrawString(canvas, { row.Label, row.LabelLength }, charOffset, blockLeft + Padding, y, z,
					Alignment::TopLeft, LabelColor, TextScale, 0.0f, 0.0f, 0.0f, 0.0f, CharSpacing, LineSpacing);
				_font->DrawString(canvas, { row.Value, row.ValueLength }, charOffset, blockLeft + blockWidth - Padding, y, z,
					Alignment::TopRight, Font::DefaultColor, TextScale, 0.0f, 0.0f, 0.0f, 0.0f, CharSpacing, LineSpacing);
			}
			blockLeft += blockWidth + BlockGap;
		}
	}

	float PerformanceOverlay::GetBlockWidth(std::int32_t block) const
	{
		return _labelWidths[block].Get() + ColumnGap + _valueWidths[block].Get() + Padding * 2.0f;
	}

	PerformanceOverlay::TableCanvas::TableCanvas(PerformanceOverlay* owner)
		: _owner(owner)
	{
	}

	void PerformanceOverlay::TableCanvas::OnUpdate(float timeMult)
	{
		Canvas::OnUpdate(timeMult);

		// The texture was rendered in an earlier frame and keeps the table until it changes, so the pass leaves the
		// chain instead of rendering the same thing again. This runs inside the chain's update loop, from the pass'
		// own entry - the loop walks from the end and tests every entry it reaches, so taking this one out is safe.
		if (!_owner->_renderPending) {
			_owner->RemoveFromChain();
		}
	}

	bool PerformanceOverlay::TableCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		_owner->DrawRows(this, Vector2f::Zero, TextLayer);

		// Visited first and drawn first, so the texture holds the table before anything that shows it is drawn
		_owner->_contentSize = _owner->_tableSize;
		_owner->_renderPending = false;
		return false;
	}
}
