#include "FxTileGenerator.h"

#if defined(WITH_EDITOR)

#include "TileSetsDialog.h"
#include "../EditorSession.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr std::int32_t TileSize = 32;

		// As the generator of the test level of the effects, Tests/LayerEffects/LevelGen.Core.cs
		std::int32_t Clamp(double v)
		{
			return (v < 0.0 ? 0 : (v > 255.0 ? 255 : std::int32_t(std::round(v))));
		}

		std::uint32_t Rgba(double r, double g, double b, double a)
		{
			return std::uint32_t(Clamp(r)) | (std::uint32_t(Clamp(g)) << 8) | (std::uint32_t(Clamp(b)) << 16) | (std::uint32_t(Clamp(a)) << 24);
		}

		// A texel of a blur behind layer: blur and reflection from 0 to 255, the displacement in pixels
		std::uint32_t Fx(double blur, double reflection, double dx = 0.0, double dy = 0.0)
		{
			return Rgba(128.0 + dx * 4.0, 128.0 + dy * 4.0, reflection, 1.0 + std::max(0.0, std::min(255.0, blur)) * 254.0 / 255.0);
		}

		double Mix(double a, double b, double t)
		{
			return a + (b - a) * t;
		}

		double Smooth(double e0, double e1, double x)
		{
			double t = std::max(0.0, std::min(1.0, (x - e0) / (e1 - e0)));
			return t * t * (3.0 - 2.0 * t);
		}

		std::int32_t Mod(std::int32_t a, std::int32_t m)
		{
			std::int32_t r = a % m;
			return (r < 0 ? r + m : r);
		}

		double Dither(std::int32_t x, std::int32_t y)
		{
			static const std::int32_t Bayer[] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
			return (Bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5;
		}

		double Hash(std::int32_t x, std::int32_t y, std::int32_t seed)
		{
			std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u + std::uint32_t(seed) * 2147483647u;
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= h >> 16;
			return (h & 0xFFFFFF) / double(0x1000000);
		}

		// Value noise, repeating every `period` pixels
		double Noise(double x, double y, std::int32_t cell, std::int32_t period, std::int32_t seed)
		{
			double fx = x / cell, fy = y / cell;
			std::int32_t ix = std::int32_t(std::floor(fx)), iy = std::int32_t(std::floor(fy));
			double tx = fx - ix, ty = fy - iy;
			tx = tx * tx * (3.0 - 2.0 * tx);
			ty = ty * ty * (3.0 - 2.0 * ty);
			std::int32_t p = std::max(1, period / cell);
			auto h = [p, seed](std::int32_t a, std::int32_t b) { return Hash(Mod(a, p), Mod(b, p), seed); };
			return Mix(Mix(h(ix, iy), h(ix + 1, iy), tx), Mix(h(ix, iy + 1), h(ix + 1, iy + 1), tx), ty);
		}

		// The effect at a pixel of a rectangle of w × h pixels, its strength scaled by k
		std::uint32_t FxAt(FxTileKind kind, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, double k)
		{
			switch (kind) {
				case FxTileKind::ClearGlass: {
					// A slight lens over the whole pane, stronger to its edges
					double u = (x + 0.5) / w * 2.0 - 1.0, v = (y + 0.5) / h * 2.0 - 1.0;
					return Fx(8.0 * k, 0.0, -3.0 * k * u * std::abs(u), -3.0 * k * v * std::abs(v));
				}
				case FxTileKind::GlassBlocks: {
					constexpr std::int32_t Block = 16;
					std::int32_t u = Mod(x, Block), v = Mod(y, Block);
					if (u < 2 || v < 2) {
						return 0;
					}
					double cu = u - (Block + 1) / 2.0, cv = v - (Block + 1) / 2.0;
					return Fx(70.0 * k, 0.0, -0.8 * k * cu, -0.8 * k * cv);
				}
				case FxTileKind::FrostedGlass:
					return Fx(235.0 * k + Dither(x, y) * 2.0, 0.0);
				case FxTileKind::Water: {
					double d = y + 0.5;
					double f = std::min(1.0, d / std::max(h, 1));
					double phase = d * 0.45 + std::sin(x * 2.0 * Pi / TileSize) * 0.9;
					double band = 0.5 + 0.5 * std::sin(phase);
					double reflection = 235.0 * (1.0 - 0.7 * f) * (0.72 + 0.28 * band) * k;
					double blur = (20.0 + 170.0 * f + 40.0 * (1.0 - band) * (1.0 - f)) * k;
					double strength = Smooth(0.0, 10.0, d) * k;
					return Fx(blur + Dither(x, y) * 2.0, reflection + Dither(x + 1, y) * 2.0, 1.6 * strength * std::cos(phase), 2.4 * strength * std::sin(phase));
				}
				case FxTileKind::HeatHaze: {
					double n = Noise(x, y, 8, 64, 171);
					double wobble = Noise(x, y, 16, 64, 181) - 0.5;
					double amount = Smooth(0.0, 14.0, x + 0.5) * Smooth(0.0, 14.0, w - x - 0.5) * Smooth(0.0, h * 0.8, h - y) * k;
					if (amount < 0.01) {
						return 0;
					}
					double blur = 120.0 * amount * (0.3 + 0.7 * Smooth(0.3, 0.7, n));
					return Fx(blur + Dither(x, y) * 2.0, 0.0, 14.0 * amount * wobble, 4.0 * amount * (n - 0.5));
				}
				case FxTileKind::Magnifier: {
					double cx = w * 0.5, cy = h * 0.5, radius = std::min(w, h) * 0.5;
					double px = x + 0.5 - cx, py = y + 0.5 - cy;
					if (px * px + py * py > (radius - 2.0) * (radius - 2.0)) {
						return 0;
					}
					return Fx(0.0, 0.0, -0.42 * k * px, -0.42 * k * py);
				}
				case FxTileKind::Puddle: {
					double u = (x + 0.5) / w * 2.0 - 1.0;
					double edge = 1.0 - u * u;
					if (edge <= 0.0) {
						return 0;
					}
					double depth = std::min(7.0 * std::sqrt(edge) * std::max(1, h / TileSize), double(h));
					if (y + 0.5 > depth) {
						return 0;
					}
					double strength = Smooth(0.0, 0.35, edge) * (1.0 - Smooth(depth * 0.5, depth, y + 0.5) * 0.6) * k;
					return Fx(30.0 * k, 220.0 * strength, 0.0, 1.2 * std::sin(x * 0.4) * strength);
				}
				default:
					return 0;
			}
		}

		String GetKindName(FxTileKind kind)
		{
			switch (kind) {
				case FxTileKind::ClearGlass: return _("Clear Glass");
				case FxTileKind::GlassBlocks: return _("Glass Blocks");
				case FxTileKind::FrostedGlass: return _("Frosted Glass");
				case FxTileKind::Water: return _("Water");
				case FxTileKind::HeatHaze: return _("Heat Haze");
				case FxTileKind::Magnifier: return _("Magnifier");
				case FxTileKind::Puddle: return _("Puddle");
				default: return String();
			}
		}

		struct GeneratorState
		{
			FxTileKind Kind = FxTileKind::FrostedGlass;
			std::int32_t Width = 2;
			std::int32_t Height = 2;
			float Strength = 100.0f;
		};
	}

	void GenerateFxTiles(FxTileKind kind, std::int32_t widthTiles, std::int32_t heightTiles, float strength, Formats::TileSetData& tiles)
	{
		widthTiles = std::max(widthTiles, 1);
		heightTiles = std::max(heightTiles, 1);
		std::int32_t w = widthTiles * TileSize, h = heightTiles * TileSize;
		tiles.Resize(widthTiles * heightTiles);
		for (std::int32_t ty = 0; ty < heightTiles; ty++) {
			for (std::int32_t tx = 0; tx < widthTiles; tx++) {
				std::int32_t tile = ty * widthTiles + tx;
				tiles.TrueColor[tile] = 1;
				// Without collision, an effect is only seen
				std::memset(&tiles.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile], 0, Formats::TileSetData::MaskBytesPerTile);
				std::uint8_t* pixels = &tiles.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
				for (std::int32_t y = 0; y < TileSize; y++) {
					for (std::int32_t x = 0; x < TileSize; x++) {
						std::uint32_t texel = FxAt(kind, tx * TileSize + x, ty * TileSize + y, w, h, strength);
						std::uint8_t* out = &pixels[(y * TileSize + x) * 4];
						out[0] = std::uint8_t(texel);
						out[1] = std::uint8_t(texel >> 8);
						out[2] = std::uint8_t(texel >> 16);
						out[3] = std::uint8_t(texel >> 24);
					}
				}
			}
		}
	}

	void ShowFxTileGeneratorDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto state = std::make_shared<GeneratorState>();
		auto dialog = std::make_unique<UiDialog>(_("Generate Effect Tiles"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		content->Add<UiLabel>(_("Tiles for a layer drawn as Blur Behind"), UiTheme::TextDim);

		auto addRow = [content](StringView label) {
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(label)->Width = 100.0f;
			return row;
		};
		{
			auto* row = addRow(_("Effect"));
			row->Add<UiDropdown>([state]() { return GetKindName(state->Kind); }, [state](SmallVectorImpl<UiMenuEntry>& entries) {
				for (std::int32_t i = 0; i < std::int32_t(FxTileKind::Count); i++) {
					FxTileKind kind = FxTileKind(i);
					UiMenuEntry entry;
					entry.Label = GetKindName(kind);
					entry.IsChecked = [state, kind]() { return state->Kind == kind; };
					entry.Execute = [state, kind]() { state->Kind = kind; };
					entries.push_back(std::move(entry));
				}
			})->Flex = 1.0f;
		}
		{
			auto* row = addRow(_("Size (tiles)"));
			row->Add<UiNumberBox>([state]() { return float(state->Width); }, [state](float value, bool final) {
				state->Width = std::clamp<std::int32_t>(std::int32_t(std::round(value)), 1, 16);
			}, 1.0f, 16.0f, 1.0f, 0)->Width = 48.0f;
			row->Add<UiLabel>("x"_s, UiTheme::TextDim);
			row->Add<UiNumberBox>([state]() { return float(state->Height); }, [state](float value, bool final) {
				state->Height = std::clamp<std::int32_t>(std::int32_t(std::round(value)), 1, 16);
			}, 1.0f, 16.0f, 1.0f, 0)->Width = 48.0f;
		}
		{
			auto* row = addRow(_("Strength (%)"));
			row->Add<UiNumberBox>([state]() { return state->Strength; }, [state](float value, bool final) {
				state->Strength = std::clamp(value, 0.0f, 200.0f);
			}, 0.0f, 200.0f, 5.0f, 0)->Width = 60.0f;
		}
		// They're made for this level, so the level keeps them by default
		auto target = std::make_shared<TileTarget>();
		target->Type = TileTarget::Kind::Level;
		AddTileTargetRow(session, addRow(_("Add to")), target);
		content->Add<UiLabel>(_("The tiles are added to the end of the tile set"), UiTheme::TextDim);
		AddTileTargetNote(session, content, target);

		dialog->AddButton(_("Generate"), [s, state, target, dialogPtr]() {
			Formats::TileSetData tiles;
			GenerateFxTiles(state->Kind, state->Width, state->Height, state->Strength / 100.0f, tiles);
			TileRef first;
			if (!AppendTiles(*s, *target, tiles, first)) {
				return;
			}
			// The brush paints the whole effect at once
			TileStamp stamp;
			stamp.Width = state->Width;
			stamp.Height = state->Height;
			for (std::int32_t i = 0; i < state->Width * state->Height; i++) {
				stamp.Tiles.push_back(TileRef{ std::uint16_t(first.Index + i), first.Source, 0 });
				stamp.Present.push_back(true);
			}
			s->SetStamp(std::move(stamp));
			s->ShowMessage(_("The tiles are the brush now, paint them on a layer drawn as Blur Behind"));
			dialogPtr->Close();
		}, true);
		dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
