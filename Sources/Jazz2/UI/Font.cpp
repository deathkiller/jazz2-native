#include "Font.h"

#include "../ContentResolver.h"
#include "../Compatibility/JJ2Anims.h"

#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Base/Random.h"

#include <IO/Compression/DeflateStream.h>

#include <Utf8.h>

using namespace Death;
using namespace Death::IO::Compression;

namespace Jazz2::UI
{
	Font::Font(const std::unique_ptr<Stream>& s, StringView path, const std::uint32_t* palette)
		: _asciiChars{}, _lineHeight(0), _baseSpacing(0)
	{
		if (!s->IsValid()) {
			// A font that can't be opened at all used to fail silently here, which then looked like a
			// rendering problem instead of a missing file: every string measures to nothing and draws
			// nothing, with not a word in the log. That happens whenever the content tree next to the
			// executable is stale or incomplete - a common state on consoles, where it lives on a
			// removable card that is updated by hand.
			LOGE("\"{}\" cannot be opened", path);
			return;
		}

		auto fileSize = s->GetSize();
		if (fileSize < 24 || fileSize > 8 * 1024 * 1024) {
			// 8 MB file size limit
			LOGE("\"{}\" has an implausible size of {} bytes", path, fileSize);
			return;
		}

		std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		std::uint8_t version = s->ReadValue<std::uint8_t>();
		/*std::uint8_t flags =*/ s->ReadValue<std::uint8_t>();
		if (signature != FontFormat::Signature || fileType != ContentFileType::Font || version != FontFormat::CurrentVersion) {
			LOGE("\"{}\" is not a supported font file", path);
			return;
		}

		// Everything past the identifying bytes is deflated, the glyph metrics and the atlas together
		std::int32_t compressedSize = s->ReadValueAsLE<std::int32_t>();
		DeflateStream uc(*s, compressedSize);

		std::int32_t w = uc.ReadValueAsLE<std::uint16_t>();
		std::int32_t h = uc.ReadValueAsLE<std::uint16_t>();
		_lineHeight = uc.ReadValueAsLE<std::uint16_t>();
		_baseSpacing = uc.ReadValueAsLE<std::int16_t>();
		std::uint8_t asciiFirst = uc.ReadValue<std::uint8_t>();
		std::uint8_t asciiCount = uc.ReadValue<std::uint8_t>();
		std::uint16_t unicodeCount = uc.ReadValueAsLE<std::uint16_t>();

		if (w <= 0 || h <= 0 || _lineHeight <= 0) {
			LOGE("\"{}\" is corrupted", path);
			_lineHeight = 0;
			return;
		}

		auto readGlyph = [&uc]() {
			FontFormat::Glyph glyph;
			glyph.X = uc.ReadValueAsLE<std::uint16_t>();
			glyph.Y = uc.ReadValueAsLE<std::uint16_t>();
			glyph.Width = uc.ReadValue<std::uint8_t>();
			glyph.Height = uc.ReadValue<std::uint8_t>();
			glyph.BearingX = uc.ReadValue<std::int8_t>();
			glyph.BearingY = uc.ReadValue<std::int8_t>();
			glyph.Advance = uc.ReadValue<std::uint8_t>();
			return glyph;
		};

		for (std::int32_t i = 0; i < asciiCount; i++) {
			const std::int32_t c = asciiFirst + i;
			FontFormat::Glyph glyph = readGlyph();
			if (c < std::int32_t(arraySize(_asciiChars))) {
				_asciiChars[c] = glyph;
			}
		}

		for (std::int32_t i = 0; i < unicodeCount; i++) {
			const std::uint32_t codepoint = uc.ReadValueAsLE<std::uint32_t>();
			FontFormat::Glyph glyph = readGlyph();
			if (codepoint == FontFormat::FallbackCodepoint) {
				// The character drawn in place of anything the font doesn't have, kept where the lookup below
				// can reach it without a second branch
				_asciiChars[0] = glyph;
			} else {
				_unicodeChars[codepoint] = glyph;
			}
		}

		if (!uc.IsValid()) {
			LOGE("\"{}\" is corrupted", path);
			_lineHeight = 0;
			return;
		}

		// The atlas holds one palette index per pixel, with index 0 standing for no pixel at all. The PowerVR
		// samples such an image directly - a paletted texture is one of its native formats - so there it is
		// uploaded as it is and the hardware resolves the colors, which costs a quarter of the video memory
		// and none of the main memory an expanded copy would. That only works while the palette's first entry
		// is the transparent one, since transparency has nowhere else to live.
#if defined(RHI_CAP_PALETTED_TEXTURES)
		const bool keepIndexed = (((palette[0] >> 24) & 0xFF) == 0);
#else
		constexpr bool keepIndexed = false;
#endif

		// The decoder stores four bytes per pixel whatever it reads, so an expanded copy needs no more room
		// than the indices plus the slack of that last write
		const std::size_t bufferSize = (keepIndexed
			? std::size_t(w) * h + ContentResolver::PixelSize
			: std::size_t(w) * h * ContentResolver::PixelSize);
		auto pixels = std::make_unique<std::uint8_t[]>(bufferSize);
		Compatibility::JJ2Anims::ReadImageContent(uc, pixels.get(), w, h, 1);

		if (keepIndexed) {
			_texture = std::make_unique<Texture>(path.data(), Texture::Format::R8, w, h);
		} else {
			// Expanded from the back, so a pixel is always read before anything can be written over it
			for (std::uint32_t i = std::uint32_t(w) * h; i-- > 0; ) {
				const std::uint32_t dstIdx = i * ContentResolver::PixelSize;
				const std::uint8_t index = pixels[i];
				const std::uint32_t color = palette[index];

				pixels[dstIdx + 0] = (color >> 0) & 0xFF;
				pixels[dstIdx + 1] = (color >> 8) & 0xFF;
				pixels[dstIdx + 2] = (color >> 16) & 0xFF;
				pixels[dstIdx + 3] = (index != 0 ? (color >> 24) & 0xFF : 0);
			}

			_texture = std::make_unique<Texture>(path.data(), Texture::Format::RGBA8, w, h);
		}

		_texture->LoadFromTexels(pixels.get(), 0, 0, w, h);
		_texture->SetMinFiltering(SamplerFilter::Linear);
		_texture->SetMagFiltering(SamplerFilter::Linear);
	}

	std::int32_t Font::GetSizeInPixels() const
	{
		// TODO
		return _lineHeight;
	}

	std::int32_t Font::GetAscentInPixels() const
	{
		// TODO
		return (_lineHeight * 4 / 5);
	}

	const FontFormat::Glyph& Font::GetGlyph(char32_t c) const
	{
		if (c < arraySize(_asciiChars)) {
			return _asciiChars[c];
		}

		auto it = _unicodeChars.find(std::uint32_t(c));
		return (it != _unicodeChars.end() ? it->second : _asciiChars[0]);
	}

	Vector2f Font::MeasureChar(char32_t c) const
	{
		// A character is as wide as the pen moves for it, not as wide as its pixels - two adjacent glyphs are
		// meant to overlap slightly, which is what the negative base spacing of these fonts arranges
		const FontFormat::Glyph& glyph = GetGlyph(c);
		return Vector2f(float(glyph.Advance), float(_lineHeight));
	}

	Vector2f Font::MeasureString(StringView text, float scale, float charSpacing, float lineSpacing)
	{
		std::size_t textLength = text.size();
		if (textLength == 0 || _lineHeight <= 0) {
			return Vector2f::Zero;
		}

		float totalWidth = 0.0f, lastWidth = 0.0f, totalHeight = 0.0f;
		float charSpacingPre = charSpacing;
		float scalePre = scale;

		std::int32_t idx = 0;
		do {
			Pair<char32_t, std::size_t> cursor = Utf8::NextChar(text, idx);

			if (cursor.first() == '\n') {
				// New line
				if (totalWidth < lastWidth) {
					totalWidth = lastWidth;
				}
				lastWidth = 0.0f;
				totalHeight += (_lineHeight * scale * lineSpacing);
			} else if (cursor.first() == '\f') {
				// Formatting
				cursor = Utf8::NextChar(text, cursor.second());
				if (cursor.first() == '[') {
					idx = std::int32_t(cursor.second());
					do {
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first() == ']') {
							break;
						}
						idx = std::int32_t(cursor.second());
					} while (idx < textLength);
				}
			} else {
				Vector2f charSize = MeasureChar(cursor.first());
				if (charSize.X > 0 && charSize.Y > 0) {
					lastWidth += (charSize.X + _baseSpacing) * charSpacingPre * scalePre;
				}
			}

			idx = std::int32_t(cursor.second());
		} while (idx < textLength);

		if (totalWidth < lastWidth) {
			totalWidth = lastWidth;
		}
		totalHeight += (_lineHeight * scale * lineSpacing);

		return Vector2f(ceilf(totalWidth), ceilf(totalHeight));
	}

	Vector2f Font::MeasureStringEx(StringView text, float scale, float charSpacing, float maxWidth, std::int32_t* charFit, float* charFitWidths)
	{
		if (charFit != nullptr) {
			*charFit = 0;
		}

		std::size_t textLength = text.size();
		if (textLength == 0 || _lineHeight <= 0) {
			return Vector2f::Zero;
		}

		float totalWidth = 0.0f, totalHeight = 0.0f;
		std::size_t idx = 0;;
		std::size_t lastCharFit = 0;

		do {
			auto [c, next] = Utf8::NextChar(text, idx);

			Vector2f charSize = MeasureChar(c);
			if (charSize.X > 0 && charSize.Y > 0) {
				float totalWidthNew = totalWidth + (charSize.X + _baseSpacing) * charSpacing * scale;
				if (charFitWidths != nullptr) {
					do {
						charFitWidths[lastCharFit++] = totalWidthNew;
					} while (lastCharFit < next);
				}
				if (totalWidthNew > maxWidth) {
					break;
				}
				totalWidth = totalWidthNew;
			}

			idx = next;
		} while (idx < textLength);

		if (charFit != nullptr) {
			*charFit = static_cast<std::int32_t>(idx);
		}

		totalHeight += (_lineHeight * scale);

		return Vector2f(ceilf(totalWidth), ceilf(totalHeight));
	}

	void Font::DrawString(Canvas* canvas, StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align, Colorf color, float scale, float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		std::size_t textLength = text.size();
		if (textLength == 0 || _lineHeight <= 0) {
			return;
		}

		// TODO: Revise this
		float phase = canvas->AnimTime * speed * 16.0f;

		// Maximum number of lines - center and right alignment starts to glitch if text has more lines, but it should be enough in most cases
		constexpr std::int32_t MaxLines = 16;

		// Preprocessing
		float totalWidth = 0.0f, lastWidth = 0.0f, totalHeight = 0.0f;
		float lineWidths[MaxLines];
		float charSpacingPre = charSpacing;
		float scalePre = scale;

		std::int32_t idx = 0;
		std::int32_t line = 0;
		do {
			Pair<char32_t, std::size_t> cursor = Utf8::NextChar(text, idx);

			if (cursor.first() == '\n') {
				// New line
				if (totalWidth < lastWidth) {
					totalWidth = lastWidth;
				}
				lineWidths[line & (MaxLines - 1)] = lastWidth;
				line++;
				lastWidth = 0.0f;
				totalHeight += (_lineHeight * scale * lineSpacing);
			} else if (cursor.first() == '\f') {
				// Formatting
				cursor = Utf8::NextChar(text, cursor.second());
				if (cursor.first() == '[') {
					idx = std::int32_t(cursor.second());
					cursor = Utf8::NextChar(text, idx);

					if (cursor.first() == 'w') {
						idx = std::int32_t(cursor.second());
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first() == ':') {
							idx = std::int32_t(cursor.second());
							std::int32_t paramLength = 0;
							char param[9];
							do {
								cursor = Utf8::NextChar(text, idx);
								if (cursor.first() == ']') {
									break;
								}
								if (paramLength < std::int32_t(arraySize(param)) - 1) {
									param[paramLength++] = (char)cursor.first();
								}
								idx = std::int32_t(cursor.second());
							} while (idx < textLength);

							if (paramLength > 0) {
								param[paramLength] = '\0';
								char* end = &param[paramLength];
								unsigned long paramValue = strtoul(param, &end, 10);
								if (param != end) {
									charSpacing = paramValue * 0.01f;
								}
							}
						} else if (cursor.first() == ']') {
							// Reset char spacing
							charSpacing = charSpacingPre;
						}
					} else {
						do {
							if (cursor.first() == ']') {
								break;
							}
							idx = std::int32_t(cursor.second());
							cursor = Utf8::NextChar(text, idx);
						} while (idx < textLength);
					}
				}
			} else {
				const FontFormat::Glyph& glyph = GetGlyph(cursor.first());
				if (glyph.Advance > 0) {
					lastWidth += (glyph.Advance + _baseSpacing) * charSpacing * scalePre;
				}
			}

			idx = std::int32_t(cursor.second());
		} while (idx < textLength);

		if (totalWidth < lastWidth) {
			totalWidth = lastWidth;
		}
		lineWidths[line & (MaxLines - 1)] = lastWidth;
		totalHeight += (_lineHeight * scale * lineSpacing);

		charSpacing = charSpacingPre;

		// Rendering
		Vector2f originPos = Vector2f(x, y);
		switch (align & Alignment::HorizontalMask) {
			case Alignment::Center: originPos.X -= totalWidth * 0.5f; break;
			case Alignment::Right: originPos.X -= totalWidth; break;
		}
		switch (align & Alignment::VerticalMask) {
			case Alignment::Center: originPos.Y -= totalHeight * 0.5f; break;
			case Alignment::Bottom: originPos.Y -= totalHeight; break;
		}

		float lineStart = originPos.X;

		switch (align & Alignment::HorizontalMask) {
			case Alignment::Center: originPos.X += (totalWidth - lineWidths[0]) * 0.5f; break;
			case Alignment::Right: originPos.X += (totalWidth - lineWidths[0]); break;
		}

		Vector2i texSize = _texture->GetSize();
		Shader* colorizeShader;
		bool useRandomColor, isShadow;
		float alpha;
		if (color.R == DefaultColor.R && color.G == DefaultColor.G && color.B == DefaultColor.B) {
			colorizeShader = nullptr;
			useRandomColor = false;
			isShadow = false;
			alpha = color.A;
			color = Colorf(1.0f, 1.0f, 1.0f, alpha);
		} else {
			colorizeShader = ContentResolver::Get().GetShader(PrecompiledShader::Colorized);
			useRandomColor = (color.R == RandomColor.R && color.G == RandomColor.G && color.B == RandomColor.B);
			isShadow = (color.R == 0.0f && color.G == 0.0f && color.B == 0.0f);
			alpha = std::min(color.A * 2.0f, 1.0f);
		}

		idx = 0;
		line = 0;
		do {
			Pair<char32_t, std::size_t> cursor = Utf8::NextChar(text, idx);

			if (cursor.first() == '\n') {
				// New line
				line++;
				originPos.X = lineStart;
				switch (align & Alignment::HorizontalMask) {
					case Alignment::Center: originPos.X += (totalWidth - lineWidths[line & (MaxLines - 1)]) * 0.5f; break;
					case Alignment::Right: originPos.X += (totalWidth - lineWidths[line & (MaxLines - 1)]); break;
				}
				originPos.Y += (_lineHeight * scale * lineSpacing);
			} else if (cursor.first() == '\f') {
				// Formatting
				cursor = Utf8::NextChar(text, cursor.second());
				if (cursor.first() == '[') {
					idx = std::int32_t(cursor.second());
					cursor = Utf8::NextChar(text, idx);
					if (cursor.first() == 'c') {
						idx = std::int32_t(cursor.second());
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first() == ':') {
							// Set custom color
							idx = std::int32_t(cursor.second());
							cursor = Utf8::NextChar(text, idx);
							if (cursor.first() == '#') {
								idx = std::int32_t(cursor.second());
								std::int32_t paramLength = 0;
								char param[9];
								do {
									cursor = Utf8::NextChar(text, idx);
									if (cursor.first() == ']') {
										break;
									}
									if (paramLength < std::int32_t(arraySize(param)) - 1) {
										param[paramLength++] = (char)cursor.first();
									}
									idx = std::int32_t(cursor.second());
								} while (idx < textLength);

								if (paramLength > 0 && !useRandomColor && !isShadow) {
									param[paramLength] = '\0';
									char* end = &param[paramLength];
									unsigned long paramValue = strtoul(param, &end, 16);
									if (param != end) {
										color = Color(paramValue);
										color.SetAlpha(0.5f * alpha);
										if (colorizeShader == nullptr) {
											colorizeShader = ContentResolver::Get().GetShader(PrecompiledShader::Colorized);
										}
									}
								}
							}
						}
					} else if (cursor.first() == 'w') {
						idx = std::int32_t(cursor.second());
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first() == ':') {
							idx = std::int32_t(cursor.second());
							std::int32_t paramLength = 0;
							char param[9];
							do {
								cursor = Utf8::NextChar(text, idx);
								if (cursor.first() == ']') {
									break;
								}
								if (paramLength < std::int32_t(arraySize(param)) - 1) {
									param[paramLength++] = (char)cursor.first();
								}
								idx = std::int32_t(cursor.second());
							} while (idx < textLength);

							if (paramLength > 0) {
								param[paramLength] = '\0';
								char* end = &param[paramLength];
								unsigned long paramValue = strtoul(param, &end, 10);
								if (param != end) {
									charSpacing = paramValue * 0.01f;
								}
							}
						}
					} else if (cursor.first() == '/') {
						idx = std::int32_t(cursor.second());
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first() == 'c') {
							// Reset color
							if (!useRandomColor && !isShadow) {
								color = Colorf(1.0f, 1.0f, 1.0f, alpha);
								colorizeShader = nullptr;
							}
						} else if (cursor.first() == 'w') {
							// Reset char spacing
							charSpacing = charSpacingPre;
						}
					}
				}

				while (cursor.first() != ']' && cursor.second() < text.size()) {
					cursor = Utf8::NextChar(text, cursor.second());
				}
			} else {
				const FontFormat::Glyph& glyph = GetGlyph(cursor.first());

				if (glyph.Advance > 0) {
					// A glyph is stored trimmed to the pixels it inks, so it draws at its bearing from the pen
					// rather than at the pen itself. One with no pixels at all - a space - only moves the pen.
					if (glyph.Width > 0 && glyph.Height > 0) {
						if (useRandomColor) {
							const Colorf& newColor = RandomColors[charOffset % std::int32_t(arraySize(RandomColors))];
							color = Colorf(newColor.R, newColor.G, newColor.B, color.A);
						}

						Vector2f pos = Vector2f(originPos.X + glyph.BearingX * scale, originPos.Y + glyph.BearingY * scale);

						if (angleOffset > 0.0f) {
							float currentPhase = (phase + charOffset) * angleOffset * fPi;
							if (speed > 0.0f && (charOffset % 2) == 1) {
								currentPhase = -currentPhase;
							}

							pos.X += cosf(currentPhase) * varianceX * scale;
							pos.Y += sinf(currentPhase) * varianceY * scale;
						}

						// Apply the canvas-wide draw transform (menu section transitions; identity by default)
						pos = pos * canvas->LayerScale + canvas->LayerOffset;
						float glyphScale = scale * canvas->LayerScale;
						Colorf glyphColor = color * canvas->LayerColor;

						pos.X = std::round(pos.X);
						pos.Y = std::round(pos.Y);

						Vector4f texCoords = Vector4f(
							glyph.Width / float(texSize.X),
							glyph.X / float(texSize.X),
							glyph.Height / float(texSize.Y),
							glyph.Y / float(texSize.Y)
						);

						auto command = canvas->RentRenderCommand();
						command->SetType(RenderCommand::Type::Text);
						bool shaderChanged = (colorizeShader
							? command->GetMaterial().SetShader(colorizeShader)
							: command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite));
						if (shaderChanged) {
							command->GetMaterial().ReserveUniformsDataMemory();
							command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
							// Required to reset render command properly
							//command->SetTransformation(command->transformation());

							auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->GetIntValue(0) != 0) {
								textureUniform->SetIntValue(0); // GL_TEXTURE0
							}
						}

						// Separate alpha blend so text (e.g. semi-transparent shadows) accumulates correct alpha coverage
						// when drawn into an RGBA render target, harmless for opaque/RGB targets
						command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha, BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);

						auto* instanceBlock = command->GetInstanceBlock();
						instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(texCoords.Data());
						instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(glyph.Width * glyphScale, glyph.Height * glyphScale);
						instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(glyphColor.Data());

						command->SetTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
						command->SetLayer(z - (charOffset & 1));
						command->GetMaterial().SetTexture(*_texture.get());

						canvas->_currentRenderQueue->AddCommand(command);
					}

					originPos.X += ((glyph.Advance + _baseSpacing) * scale * charSpacing);
					charOffset++;
				}
			}

			idx = std::int32_t(cursor.second());
		} while (idx < textLength);
		charOffset++;
	}

	String Font::StripFormatting(StringView text)
	{
		if (text.empty()) {
			return {};
		}

		SmallVector<char, 4000> tempBuffer;

		for (std::size_t i = 0; i < text.size(); i++) {
			while (i + 1 < text.size() && text[i] == '\f' && text[i + 1] == '[') {
				i += 2;

				while (text[i] != L']' && text[i] != L'\0') {
					i++;
				}
				i++;
			}

			if (text[i] != L'\0') {
				tempBuffer.push_back(text[i]);
			}
		}

		return String(tempBuffer.data(), tempBuffer.size());
	}
}