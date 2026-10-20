#include "PlayTransition.h"

#if defined(WITH_EDITOR)

#include "../../Rendering/PolylineRenderer.h"
#include "../../../nCine/Graphics/Material.h"

#include <algorithm>
#include <cmath>

using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		// Segments of the circles, a few more than look smooth at the size of a screen
		constexpr std::int32_t Segments = 96;
	}

	PlayTransition::PlayTransition()
		: _outside(nullptr), _inside(nullptr), _color(UiTheme::Accent), _time(0.0f), _hold(false)
	{
	}

	PlayTransition::~PlayTransition()
	{
	}

	void PlayTransition::Start(Texture* outside, Texture* inside, const Colorf& color)
	{
		// With reduced motion, what's next is shown at once, without the ring
		if (UiTheme::ReducedMotion) {
			Stop();
			return;
		}
		_outside = outside;
		_inside = inside;
		_color = color;
		_time = 0.0f;
		_hold = false;
	}

	void PlayTransition::GetHole(Vector2f screenSize, float uiScale, Vector2f& center, float& radius, float& outer) const
	{
		// Slow at first, so the ring is seen appearing, then faster to the edges
		float t = std::clamp(_time / Duration, 0.0f, 1.0f);
		float eased = t * t * (3.0f - 2.0f * t) * 0.35f + t * t * 0.65f;
		center = screenSize * 0.5f;
		float coverRadius = screenSize.Length() * 0.5f + 2.0f;
		float thickness = std::max(48.0f, std::round(uiScale * 72.0f));
		outer = eased * (coverRadius + thickness);
		radius = std::max(0.0f, outer - thickness);
	}

	void PlayTransition::SetProgress(float progress, bool hold)
	{
		_time = std::clamp(progress, 0.0f, 1.0f) * Duration;
		_hold = hold;
	}
	void PlayTransition::Stop()
	{
		_outside = nullptr;
		_inside = nullptr;
	}

	void PlayTransition::Update(float timeMult)
	{
		if (_outside == nullptr || _hold) {
			return;
		}
		_time += timeMult;
		if (_time >= Duration) {
			Stop();
		}
	}

	void PlayTransition::Draw(UiPainter& painter, Vector2f screenSize)
	{
		DrawOutside(painter, screenSize);
		DrawRing(painter, screenSize);
	}

	void PlayTransition::DrawOutside(UiPainter& painter, Vector2f screenSize)
	{
		if (_outside == nullptr || screenSize.X <= 0.0f || screenSize.Y <= 0.0f) {
			return;
		}

		Vector2f center;
		float hole, outer;
		GetHole(screenSize, painter.GetScale(), center, hole, outer);
		float coverRadius = screenSize.Length() * 0.5f + 2.0f;

		// The last image around the hole, sampled where it was on the screen
		painter.Command(DrawBand(0, center, hole, coverRadius, *_outside, screenSize, 0.0f, 0.0f, painter.GetDepth(UiLayer::Back), Colorf::White));
	}

	void PlayTransition::DrawRing(UiPainter& painter, Vector2f screenSize)
	{
		if (_outside == nullptr || screenSize.X <= 0.0f || screenSize.Y <= 0.0f) {
			return;
		}

		Vector2f center;
		float hole, outer;
		GetHole(screenSize, painter.GetScale(), center, hole, outer);

		// What is shown now in the hole, over whatever was drawn before, a little into the ring so no gap is seen
		if (_inside != nullptr && hole > 0.0f) {
			painter.Command(DrawBand(6, center, 0.0f, std::min(hole + 2.0f, outer), *_inside, screenSize, 0.0f, 0.0f, painter.GetDepth(UiLayer::Back), Colorf::White));
		}

		// The ring over all of the slot, so it covers what the editor still shows outside of it, solid but for
		// a pixel or so at its edges that fades out, however thick it is
		if (_ring == nullptr) {
			_ring = std::make_unique<Rendering::PolylineRenderer>(256);
		}
		Texture& feather = _ring->GetFeatherTexture();
		std::uint16_t ringDepth = std::uint16_t(painter.GetDepth(UiLayer::Back) + 2 * UiPainter::SlotDepth);
		float fade = std::max(1.0f, painter.GetScale());
		constexpr float Opaque = 0.175f;
		painter.Command(DrawBand(1, center, std::max(hole - fade, 0.0f), hole, feather, Vector2f(), 0.0f, Opaque, ringDepth, _color));
		painter.Command(DrawBand(2, center, hole, outer, feather, Vector2f(), 0.5f, 0.5f, ringDepth, _color));
		painter.Command(DrawBand(3, center, outer, outer + fade, feather, Vector2f(), Opaque, 0.0f, ringDepth, _color));

		// A thin darker line inside, so the ring stands out against bright levels too, under the ring but in the hole
		if (hole > 1.0f) {
			float line = std::max(2.0f, painter.GetScale() * 2.0f);
			Colorf dark(_color.R * 0.2f, _color.G * 0.2f, _color.B * 0.25f, 0.9f);
			painter.Command(DrawBand(4, center, std::max(hole - line - fade, 0.0f), hole - line, feather, Vector2f(), 0.0f, Opaque, painter.GetDepth(UiLayer::OverlayText), dark));
			painter.Command(DrawBand(5, center, hole - line, hole, feather, Vector2f(), 0.5f, 0.5f, painter.GetDepth(UiLayer::OverlayText), dark));
		}
	}

	RenderCommand* PlayTransition::DrawBand(std::int32_t index, Vector2f center, float innerRadius, float outerRadius, Texture& texture,
		Vector2f screenSize, float innerU, float outerU, std::uint16_t depth, const Colorf& color)
	{
		if (outerRadius <= innerRadius) {
			return nullptr;
		}

		// Textured by where it is on the screen if it has a size, across its width otherwise
		Band& band = _bands[index];
		bool byScreen = (screenSize.X > 0.0f && screenSize.Y > 0.0f);
		band.Vertices.resize_for_overwrite(std::size_t(Segments + 1) * 2 * 4);
		float* v = band.Vertices.data();
		for (std::int32_t i = 0; i <= Segments; i++) {
			float angle = 6.2831853f * float(i) / float(Segments);
			Vector2f direction(std::cos(angle), std::sin(angle));
			Vector2f inner = center + direction * innerRadius;
			Vector2f outer = center + direction * outerRadius;
			*v++ = inner.X; *v++ = inner.Y; *v++ = (byScreen ? inner.X / screenSize.X : innerU); *v++ = (byScreen ? inner.Y / screenSize.Y : 0.0f);
			*v++ = outer.X; *v++ = outer.Y; *v++ = (byScreen ? outer.X / screenSize.X : outerU); *v++ = (byScreen ? outer.Y / screenSize.Y : 0.0f);
		}

		if (band.Command == nullptr) {
			band.Command = std::make_unique<RenderCommand>(RenderCommand::Type::MeshSprite);
			band.Command->GetMaterial().SetBlendingEnabled(true);
		}
		RenderCommand* command = band.Command.get();
		if (command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::MeshSprite)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}
		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
		command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, (Segments + 1) * 2);
		command->GetGeometry().SetElementsPerVertex(4);
		command->GetGeometry().SetHostVertexPointer(band.Vertices.data());
		auto instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(1.0f, 1.0f);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());
		command->SetTransformation(Matrix4x4f::Identity);
		command->SetLayer(depth);
		command->GetMaterial().SetTexture(0, texture);
		return command;
	}
}

#endif
