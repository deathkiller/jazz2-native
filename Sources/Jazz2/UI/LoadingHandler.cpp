#include "LoadingHandler.h"
#include "Menu/Tweening.h"

#include "../../nCine/Application.h"
#include "../../nCine/CommonConstants.h"

#include <cmath>

namespace Jazz2::UI
{
	namespace
	{
		// Loading that is still going after this many seconds is a long wait
		constexpr float LongWaitDelay = 60.0f;
		// Seconds it takes the light theme to turn dark once a long wait begins
		constexpr float DarkThemeDuration = 10.0f;
		// Seconds it takes the aura to fade in, much longer than the theme takes to turn dark, so most of it blooms on
		// an already dark background
		constexpr float AuraFadeInDuration = 45.0f;
		// Seconds a mood of the aura holds, and then takes to blend into the next one
		constexpr float AuraMoodHold = 30.0f;
		constexpr float AuraMoodBlend = 25.0f;
		// How much later a blob starts blending, in seconds per radian of its phase, so the blobs don't all change at once
		constexpr float AuraMoodStagger = 1.5f;
		// How far the hue of each blob sways on its own, in degrees, and how long one sway takes, in seconds
		constexpr float AuraHueSway = 12.0f;
		constexpr float AuraHueSwayPeriod = 23.0f;
		// How much the blobs squash and stretch
		constexpr float AuraWobble = 0.12f;
		// Opacity of the dark patch behind the indicator, which keeps it readable when a bright blob passes behind
		constexpr float AuraScrimOpacity = 0.4f;
		// A small power of two, which every backend can sample, some of the consoles don't take anything else
		constexpr std::int32_t AuraTextureSize = 128;

		constexpr std::uint16_t BackgroundLayer = 900;
		// Every blob gets a layer of its own, because alpha blending depends on the order they are painted in, and
		// commands that share both a layer and a material are drawn in no particular order
		constexpr std::uint16_t AuraLayer = 901;
		constexpr std::uint16_t AuraScrimLayer = 950;
		constexpr std::uint16_t ProgressTrackLayer = 955;
		constexpr std::uint16_t ProgressBarLayer = 956;
		constexpr std::uint16_t IndicatorLayer = 960;

		struct AuraColor
		{
			// In degrees
			float Hue;
			float Saturation;
			float Value;
		};

		// Every mood is a handful of neighboring hues, and a blob keeps the same slot in all of them. No slot moves by
		// more than 110 degrees from one mood to the next, and the blend goes through the hues in between rather than
		// straight through RGB, so the colors never turn grey on the way.
		constexpr std::int32_t AuraMoodSlots = 5;
		constexpr AuraColor AuraMoods[][AuraMoodSlots] = {
			// Twilight - blue and violet
			{ { 200.0f, 1.00f, 0.85f }, { 228.0f, 0.90f, 1.00f }, { 262.0f, 0.85f, 1.00f }, { 300.0f, 0.85f, 0.85f }, { 335.0f, 0.80f, 0.95f } },
			// Aurora - green, teal and cyan, a bit darker, because these hues look much brighter than blue at the same value
			{ { 120.0f, 0.85f, 0.75f }, { 150.0f, 0.90f, 0.75f }, { 175.0f, 1.00f, 0.75f }, { 195.0f, 1.00f, 0.85f }, { 225.0f, 0.90f, 1.00f } },
			// Sunset - magenta, red and orange
			{ { 290.0f, 0.85f, 0.85f }, { 320.0f, 0.85f, 0.90f }, { 350.0f, 0.85f, 1.00f }, { 15.0f, 0.90f, 1.00f }, { 32.0f, 0.90f, 1.00f } }
		};
		// Twilight comes back between the other two, going from orange to green directly would pass through yellow
		constexpr std::int32_t AuraMoodSequence[] = { 0, 1, 0, 2 };

		struct AuraBlob
		{
			// Center of the path, relative to the view size
			float X, Y;
			// How far the blob moves away from the center, relative to the view size
			float RangeX, RangeY;
			// Periods of the horizontal movement, the vertical one and the wobble, in seconds
			float PeriodX, PeriodY, PeriodWobble;
			float Phase;
			// Relative to the view unit, see DrawAura()
			float Radius;
			float Opacity;
			// Index into each of the moods
			std::int32_t Slot;
		};

		// Painted in this order
		constexpr AuraBlob AuraBlobs[] = {
			// Large clouds drifting slowly, which fill the background with color
			{ 0.15f, 0.25f, 0.20f, 0.22f, 53.0f, 41.0f, 11.0f, 0.0f, 1.00f, 0.50f, 1 },
			{ 0.80f, 0.35f, 0.20f, 0.22f, 47.0f, 59.0f, 13.0f, 2.1f, 1.05f, 0.50f, 2 },
			{ 0.60f, 0.10f, 0.30f, 0.12f, 67.0f, 37.0f, 9.0f, 4.0f, 0.90f, 0.45f, 3 },
			{ 0.30f, 0.90f, 0.25f, 0.12f, 61.0f, 43.0f, 12.0f, 5.2f, 0.90f, 0.45f, 0 },
			// Smaller blobs rising and sinking through the whole view like the wax of a lava lamp, each at its own
			// pace, so they keep meeting and parting
			{ 0.30f, 0.50f, 0.06f, 0.62f, 19.0f, 31.0f, 7.0f, 0.0f, 0.42f, 0.55f, 3 },
			{ 0.52f, 0.50f, 0.07f, 0.62f, 23.0f, 43.0f, 8.0f, 2.4f, 0.36f, 0.55f, 4 },
			{ 0.72f, 0.50f, 0.05f, 0.62f, 17.0f, 37.0f, 9.0f, 4.4f, 0.38f, 0.55f, 0 },
			{ 0.12f, 0.50f, 0.05f, 0.62f, 29.0f, 53.0f, 10.0f, 1.1f, 0.30f, 0.55f, 2 },
			{ 0.90f, 0.50f, 0.05f, 0.62f, 27.0f, 47.0f, 7.0f, 3.3f, 0.32f, 0.55f, 1 }
		};

		static_assert(AuraLayer + arraySize(AuraBlobs) <= AuraScrimLayer, "Blobs must be painted below the scrim");

		// Hermite ease of a progress that may run past both ends
		float EaseInOut(float progress)
		{
			return Menu::Easing::SmoothStep(std::clamp(progress, 0.0f, 1.0f));
		}

		// Sine of an oscillation with the given period at the given time, which is wrapped to the period first, so
		// the precision doesn't decay however long the loading takes
		float Oscillate(float time, float period, float phase)
		{
			return sinApprox(std::fmod(time, period) * (fTwoPi / period) + phase);
		}

		Colorf ColorFromHsv(float hue, float saturation, float value, float alpha)
		{
			hue = std::fmod(hue, 360.0f);
			if (hue < 0.0f) {
				hue += 360.0f;
			}
			float sector = hue / 60.0f;
			float sectorFloor = std::floor(sector);
			// A hue just below zero can round up to 360 when wrapped, which is red again, the same as 0
			std::int32_t index = std::int32_t(sectorFloor) % 6;
			float f = sector - sectorFloor;
			float p = value * (1.0f - saturation);
			float q = value * (1.0f - saturation * f);
			float t = value * (1.0f - saturation * (1.0f - f));
			switch (index) {
				case 0: return Colorf(value, t, p, alpha);
				case 1: return Colorf(q, value, p, alpha);
				case 2: return Colorf(p, value, t, alpha);
				case 3: return Colorf(p, q, value, alpha);
				case 4: return Colorf(t, p, value, alpha);
				default: return Colorf(value, p, q, alpha);
			}
		}

		// White, with the opacity falling off from the center as (1 - r^2)^2, which flattens out at both ends, so a blob
		// has neither a hard core nor a visible rim. Keeping it grey also keeps the soft alpha alive on backends that
		// store a texture with 16 bits per texel, the N64 one then picks a format with 8 bits of alpha instead of 1 (see
		// RdpTexture::ConvertToStore()).
		std::unique_ptr<Texture> CreateAuraTexture()
		{
			constexpr float HalfSize = AuraTextureSize * 0.5f;

			std::unique_ptr<std::uint8_t[]> texels = std::make_unique<std::uint8_t[]>(AuraTextureSize * AuraTextureSize * 4);
			for (std::int32_t y = 0; y < AuraTextureSize; y++) {
				float dy = (y + 0.5f - HalfSize) / HalfSize;
				for (std::int32_t x = 0; x < AuraTextureSize; x++) {
					float dx = (x + 0.5f - HalfSize) / HalfSize;
					float falloff = std::max(1.0f - (dx * dx + dy * dy), 0.0f);
					// Written byte by byte, the order of the channels must not depend on the endianness
					std::uint8_t* texel = &texels[(y * AuraTextureSize + x) * 4];
					texel[0] = 255;
					texel[1] = 255;
					texel[2] = 255;
					texel[3] = std::uint8_t(falloff * falloff * 255.0f + 0.5f);
				}
			}

			std::unique_ptr<Texture> texture = std::make_unique<Texture>("Aura", Texture::Format::RGBA8, AuraTextureSize, AuraTextureSize);
			texture->LoadFromTexels(texels.get(), 0, 0, AuraTextureSize, AuraTextureSize);
			texture->SetMinFiltering(SamplerFilter::Linear);
			texture->SetMagFiltering(SamplerFilter::Linear);
			texture->SetWrap(SamplerWrapping::ClampToEdge);
			return texture;
		}
	}

	LoadingHandler::LoadingHandler(IRootController* root, bool darkMode)
		: _root(root), _startTime(TimeStamp::now()), _time(0.0f), _transition(0.0f), _progress(-1.0f), _progressTransition(0.0f),
			_darkness(darkMode ? 1.0f : 0.0f), _aura(0.0f)
	{
		_canvasBackground = std::make_unique<BackgroundCanvas>(this);

		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/Loading"_s);
		DEATH_ASSERT(_metadata != nullptr, "Cannot load required metadata", );
	}

	LoadingHandler::LoadingHandler(IRootController* root, bool darkMode, Function<bool(IRootController*)>&& callback)
		: LoadingHandler(root, darkMode)
	{
		_callback = std::move(callback);
	}

	LoadingHandler::~LoadingHandler()
	{
		_canvasBackground->setParent(nullptr);
	}

	Vector2i LoadingHandler::GetViewSize() const
	{
		return _upscalePass.GetViewSize();
	}

	void LoadingHandler::OnBeginFrame()
	{
		float timeMult = theApplication().GetTimeMult();

		if (_callback && _callback(_root)) {
			_callback = nullptr;
		}

		if (_transition < 1.0f) {
			_transition = std::min(_transition + timeMult * 0.04f, 1.0f);
		}

		_time = _startTime.secondsSince();
		float longWait = _time - LongWaitDelay;
		if (longWait > 0.0f) {
			// The dark theme simply stays dark
			_darkness = std::max(_darkness, EaseInOut(longWait / DarkThemeDuration));
			_aura = EaseInOut(longWait / AuraFadeInDuration);
			if (_auraTexture == nullptr) {
				// Not created up front, because most loading is over long before it would be needed
				_auraTexture = CreateAuraTexture();
			}
		}

		// Only a conversion of the original game files reports anything, and it can be well under way by the
		// time this handler appears, so the bar starts at wherever the conversion has got to. It's also never
		// taken back down: the conversion stops reporting as soon as it's done, and the last value it reported
		// is the one that should stay on screen for the frames until the main menu takes over.
		float reported = _root->GetInitializationProgress();
		if (reported >= 0.0f) {
			if (_progress < 0.0f) {
				_progress = reported;
			} else if (reported > _progress) {
				// Whole assets are converted at a time, so what is reported arrives in jumps of uneven size
				_progress = std::min(_progress + (reported - _progress) * 0.12f * timeMult, reported);
			}
		}

		if (_progress >= 0.0f && _progressTransition < 1.0f) {
			_progressTransition = std::min(_progressTransition + timeMult * 0.04f, 1.0f);
		}
	}

	void LoadingHandler::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		Vector2i viewSize = Rendering::UpscaleRenderPass::CalculateViewSize(width, height, DefaultWidth, DefaultHeight);
		std::int32_t w = viewSize.X;
		std::int32_t h = viewSize.Y;

		_upscalePass.Initialize(w, h, width, height);

		// Viewports must be registered in reverse order
		_upscalePass.Register();

		_canvasBackground->setParent(_upscalePass.GetNode());
	}

	bool LoadingHandler::BackgroundCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_upscalePass.GetViewSize();

		float darkness = _owner->_darkness;
		float background = 1.0f - darkness;
		DrawSolid(Vector2f::Zero, BackgroundLayer, Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y)), Colorf(background, background, background));

		bool hasAura = (_owner->_aura > 0.0f && _owner->_auraTexture != nullptr);
		if (hasAura) {
			DrawAura();
		}

		auto* loadingRes = _owner->_metadata->FindAnimation(AnimState::Idle);
		if (loadingRes != nullptr) {
			std::int32_t frame = loadingRes->GetFrameForTime(AnimTime);

			GenericGraphicResource* base = loadingRes->Base;
			// The indicator's whole cell, which is also what the progress bar below is placed against: the
			// frames are trimmed to the pixels they ink, so a single frame's extent moves as it animates
			Vector2f cellSize = Vector2f(base->FrameDimensions.X, base->FrameDimensions.Y);
			Vector2f cellPos = Vector2f(ViewSize.X - cellSize.X - 50.0f, ViewSize.Y - cellSize.Y - 40.0f);

			Vector2i texSize = base->TextureDiffuse->GetSize();
			Recti frameRect = base->GetFrameRect(frame);
			// A trimmed frame covers less than its cell, so it shifts into place instead of being stretched
			Vector2i frameOffset = base->GetFrameOffset(frame);
			Vector2f size = Vector2f((float)frameRect.W, (float)frameRect.H);
			Vector2f pos = cellPos + Vector2f((float)frameOffset.X, (float)frameOffset.Y);
			Vector4f texCoords = Vector4f(
				float(frameRect.W) / float(texSize.X),
				float(frameRect.X) / float(texSize.X),
				float(frameRect.H) / float(texSize.Y),
				float(frameRect.Y) / float(texSize.Y)
			);

			Colorf color = Colorf(1.0f, 1.0f, 1.0f, lerp(1.0f, 0.8f, darkness) * _owner->_transition);
			std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? loadingRes->PaletteOffset : -1);
			DrawTexture(*base->TextureDiffuse.get(), pos, IndicatorLayer, size, texCoords, color, false, 0.0f, paletteOffset);

			// Where the dark patch behind the indicator starts, the bar extends it to the left
			float scrimLeft = cellPos.X;

			if (_owner->_progress >= 0.0f) {
				constexpr float BarWidth = 240.0f;
				constexpr float BarHeight = 3.0f;
				constexpr float BarSpacing = 18.0f;
				constexpr float BarMarginLeft = 20.0f;

				// Ends where the indicator's cell begins and is centered on it, so the two read as one element.
				// A view too narrow for the whole bar gets a shorter one instead of one running off the edge.
				float right = std::round(cellPos.X - BarSpacing);
				float left = std::max(right - BarWidth, BarMarginLeft);
				float top = std::round(cellPos.Y + (cellSize.Y - BarHeight) * 0.5f);
				float width = right - left;

				if (width > 0.0f) {
					float alpha = _owner->_progressTransition;
					// Black in the light theme and white in the dark one, switching over much faster than the
					// background turns dark, otherwise the two would pass through the same grey together and the
					// bar would disappear for a while
					float ink = EaseInOut((darkness - 0.35f) / 0.3f);

					// The track is barely visible on purpose --- it's only there to show how much is still left
					DrawSolid(Vector2f(left, top), ProgressTrackLayer, Vector2f(width, BarHeight), Colorf(ink, ink, ink, lerp(0.12f, 0.2f, ink) * alpha));
					DrawSolid(Vector2f(left, top), ProgressBarLayer, Vector2f(std::round(width * _owner->_progress), BarHeight), Colorf(ink, ink, ink, lerp(0.5f, 0.8f, ink) * alpha));
					scrimLeft = left;
				}
			}

			if (hasAura) {
				float scrimRight = cellPos.X + cellSize.X;
				float scrimHeight = cellSize.Y * 6.0f;
				Vector2f scrimSize = Vector2f(std::max((scrimRight - scrimLeft) * 1.6f, scrimHeight), scrimHeight);
				Vector2f scrimCenter = Vector2f((scrimLeft + scrimRight) * 0.5f, cellPos.Y + cellSize.Y * 0.5f);
				DrawTexture(*_owner->_auraTexture, scrimCenter - scrimSize * 0.5f, AuraScrimLayer, scrimSize,
					Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf(0.0f, 0.0f, 0.0f, AuraScrimOpacity * _owner->_aura));
			}
		}

		return true;
	}

	void LoadingHandler::BackgroundCanvas::DrawAura()
	{
		constexpr float MoodDuration = AuraMoodHold + AuraMoodBlend;
		constexpr std::int32_t MoodCount = std::int32_t(arraySize(AuraMoodSequence));

		float time = _owner->_time;
		// The moods count from the start of the long wait, so the aura always fades in with the first one
		float longWait = std::max(time - LongWaitDelay, 0.0f);
		Vector2f viewSize = Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y));
		// The height a 16:9 view would have, so the blobs cover an ultrawide or a portrait view just as well
		float unit = std::max(viewSize.X, viewSize.Y) * (9.0f / 16.0f);

		for (std::int32_t i = 0; i < std::int32_t(arraySize(AuraBlobs)); i++) {
			const AuraBlob& blob = AuraBlobs[i];

			// The vertical movement, the wobble and the color are offset by other multiples of the same phase, so no
			// two of them are in step, neither within a blob nor between blobs
			Vector2f center = Vector2f(
				(blob.X + blob.RangeX * Oscillate(time, blob.PeriodX, blob.Phase)) * viewSize.X,
				(blob.Y + blob.RangeY * Oscillate(time, blob.PeriodY, blob.Phase * 1.7f)) * viewSize.Y);
			// Squashed in one direction as much as it's stretched in the other
			float wobble = AuraWobble * Oscillate(time, blob.PeriodWobble, blob.Phase * 2.3f);
			float diameter = 2.0f * blob.Radius * unit;
			Vector2f size = Vector2f(diameter * (1.0f + wobble), diameter * (1.0f - wobble));

			float moodTime = longWait + blob.Phase * AuraMoodStagger;
			std::int32_t mood = std::int32_t(moodTime / MoodDuration);
			float blend = EaseInOut((moodTime - float(mood) * MoodDuration - AuraMoodHold) / AuraMoodBlend);
			const AuraColor& from = AuraMoods[AuraMoodSequence[mood % MoodCount]][blob.Slot];
			const AuraColor& to = AuraMoods[AuraMoodSequence[(mood + 1) % MoodCount]][blob.Slot];
			// The shorter way around the color wheel
			float hueDelta = to.Hue - from.Hue;
			if (hueDelta > 180.0f) {
				hueDelta -= 360.0f;
			} else if (hueDelta < -180.0f) {
				hueDelta += 360.0f;
			}
			float hue = from.Hue + hueDelta * blend + AuraHueSway * Oscillate(time, AuraHueSwayPeriod, blob.Phase * 3.1f);
			Colorf color = ColorFromHsv(hue, lerp(from.Saturation, to.Saturation, blend), lerp(from.Value, to.Value, blend),
				blob.Opacity * _owner->_aura);

			DrawTexture(*_owner->_auraTexture, center - size * 0.5f, std::uint16_t(AuraLayer + i), size,
				Vector4f(1.0f, 0.0f, 1.0f, 0.0f), color);
		}
	}
}
