#include "Font.h"

#include "../ContentResolver.h"

#include "../../nCine/Graphics/ITextureLoader.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Base/Random.h"

#include <Containers/StringConcatenable.h>
#include <Utf8.h>

using namespace Death;

namespace Jazz2::UI
{
	Font::Font(const StringView path, const std::uint32_t* palette)
		: _baseSpacing(0)
	{
		auto s = fs::Open(path + ".font"_s, FileAccess::Read);
		auto fileSize = s->GetSize();
		if (fileSize < 4 || fileSize > 8 * 1024 * 1024) {
			// 8 MB file size limit
			return;
		}

		std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(path);
		if (texLoader->hasLoaded()) {
			auto texFormat = texLoader->texFormat().internalFormat();
			if (texFormat != GL_RGBA8 && texFormat != GL_RGB8) {
				return;
			}

			std::int32_t w = texLoader->width();
			std::int32_t h = texLoader->height();
			auto pixels = (std::uint32_t*)texLoader->pixels();

			/*std::uint8_t flags =*/ s->ReadValue<std::uint8_t>();
			std::uint16_t width = s->ReadValue<std::uint16_t>();
			std::uint16_t height = s->ReadValue<std::uint16_t>();
			std::uint8_t cols = s->ReadValue<std::uint8_t>();
			std::int32_t rows = h / height;
			std::int16_t spacing = s->ReadValue<std::int16_t>();
			std::uint8_t asciiFirst = s->ReadValue<std::uint8_t>();
			std::uint8_t asciiCount = s->ReadValue<std::uint8_t>();

			std::uint8_t widths[128];
			s->Read(widths, asciiCount);

			std::int32_t i = 0;
			for (; i < asciiCount; i++) {
				_asciiChars[i + asciiFirst] = Rectf(
					(float)(i % cols) / cols,
					(float)(i / cols) / rows,
					widths[i],
					height - 1
				);
			}

			std::int32_t unicodeCharCount = asciiCount + s->ReadValue<std::int32_t>();
			for (; i < unicodeCharCount; i++) {
				char c[4] {};
				s->Read(c, 1);

				std::int32_t remainingBytes =
					((c[0] & 240) == 240) ? 3 : (
					((c[0] & 224) == 224) ? 2 : (
					((c[0] & 192) == 192) ? 1 : 0
				));
				if (remainingBytes == 0) {
					// Placeholder for unknown characters
					std::uint8_t charWidth = s->ReadValue<std::uint8_t>();
					if (c[0] == 0) {
						_asciiChars[0] = Rectf(
							(float)(i % cols) / cols,
							(float)(i / cols) / rows,
							charWidth,
							height - 1
						);
					}
					continue;
				}

				s->Read(c + 1, remainingBytes);
				std::uint8_t charWidth = s->ReadValue<std::uint8_t>();

				std::pair<char32_t, std::size_t> cursor = Utf8::NextChar(c, 0);
				_unicodeChars[cursor.first] = Rectf(
					(float)(i % cols) / cols,
					(float)(i / cols) / rows,
					charWidth,
					height - 1
				);
			}

			_charSize = Vector2i(width, height);
			_baseSpacing = spacing;

			for (std::int32_t i = 0; i < w * h; i++) {
				std::uint32_t color = palette[pixels[i] & 0xff];
				pixels[i] = (color & 0xffffff) | ((((color >> 24) & 0xff) * ((pixels[i] >> 24) & 0xff) / 255) << 24);
			}

			_texture = std::make_unique<Texture>(path.data(), Texture::Format::RGBA8, w, h);
			_texture->loadFromTexels((unsigned char*)pixels, 0, 0, w, h);
			_texture->setMinFiltering(SamplerFilter::Linear);
			_texture->setMagFiltering(SamplerFilter::Linear);
		}
	}

	std::int32_t Font::GetSizeInPixels() const
	{
		// TODO
		return _charSize.Y;
	}

	std::int32_t Font::GetAscentInPixels() const
	{
		// TODO
		return (_charSize.Y * 4 / 5);
	}

	Vector2f Font::MeasureChar(char32_t c) const
	{
		Rectf uvRect;
		if (c < 128) {
			uvRect = _asciiChars[c];
		} else {
			auto it = _unicodeChars.find(c);
			if (it != _unicodeChars.end()) {
				uvRect = it->second;
			} else {
				uvRect = _asciiChars[0];
			}
		}
		return Vector2f(uvRect.W, uvRect.H);
	}

	Vector2f Font::MeasureString(StringView text, float scale, float charSpacing, float lineSpacing)
	{
		std::size_t textLength = text.size();
		if (textLength == 0 || _charSize.Y <= 0) {
			return Vector2f::Zero;
		}

		float totalWidth = 0.0f, lastWidth = 0.0f, totalHeight = 0.0f;
		float charSpacingPre = charSpacing;
		float scalePre = scale;

		std::int32_t idx = 0;
		do {
			std::pair<char32_t, std::size_t> cursor = Utf8::NextChar(text, idx);

			if (cursor.first == '\n') {
				// New line
				if (totalWidth < lastWidth) {
					totalWidth = lastWidth;
				}
				lastWidth = 0.0f;
				totalHeight += (_charSize.Y * scale * lineSpacing);
			} else if (cursor.first == '\f') {
				// Formatting
				cursor = Utf8::NextChar(text, cursor.second);
				if (cursor.first == '[') {
					idx = cursor.second;
					do {
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first == ']') {
							break;
						}
						idx = cursor.second;
					} while (idx < textLength);
				}
			} else {
				Vector2f charSize = MeasureChar(cursor.first);
				if (charSize.X > 0 && charSize.Y > 0) {
					lastWidth += (charSize.X + _baseSpacing) * charSpacingPre * scalePre;
				}
			}

			idx = cursor.second;
		} while (idx < textLength);

		if (totalWidth < lastWidth) {
			totalWidth = lastWidth;
		}
		totalHeight += (_charSize.Y * scale * lineSpacing);

		return Vector2f(ceilf(totalWidth), ceilf(totalHeight));
	}

	Vector2f Font::MeasureStringEx(StringView text, float scale, float charSpacing, float maxWidth, std::int32_t* charFit, float* charFitWidths)
	{
		if (charFit != nullptr) {
			*charFit = 0;
		}

		std::size_t textLength = text.size();
		if (textLength == 0 || _charSize.Y <= 0) {
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

		totalHeight += (_charSize.Y * scale);

		return Vector2f(ceilf(totalWidth), ceilf(totalHeight));
	}

	void Font::DrawString(Canvas* canvas, StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align, Colorf color, float scale, float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		std::size_t textLength = text.size();
		if (textLength == 0 || _charSize.Y <= 0) {
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
			std::pair<char32_t, std::size_t> cursor = Utf8::NextChar(text, idx);

			if (cursor.first == '\n') {
				// New line
				if (totalWidth < lastWidth) {
					totalWidth = lastWidth;
				}
				lineWidths[line & (MaxLines - 1)] = lastWidth;
				line++;
				lastWidth = 0.0f;
				totalHeight += (_charSize.Y * scale * lineSpacing);
			} else if (cursor.first == '\f') {
				// Formatting
				cursor = Utf8::NextChar(text, cursor.second);
				if (cursor.first == '[') {
					idx = cursor.second;
					cursor = Utf8::NextChar(text, idx);

					if (cursor.first == 'w') {
						idx = cursor.second;
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first == ':') {
							idx = cursor.second;
							std::int32_t paramLength = 0;
							char param[9];
							do {
								cursor = Utf8::NextChar(text, idx);
								if (cursor.first == ']') {
									break;
								}
								if (paramLength < static_cast<std::int32_t>(arraySize(param)) - 1) {
									param[paramLength++] = (char)cursor.first;
								}
								idx = cursor.second;
							} while (idx < textLength);

							if (paramLength > 0) {
								param[paramLength] = '\0';
								char* end = &param[paramLength];
								unsigned long paramValue = strtoul(param, &end, 10);
								if (param != end) {
									charSpacing = paramValue * 0.01f;
								}
							}
						} else if (cursor.first == ']') {
							// Reset char spacing
							charSpacing = charSpacingPre;
						}
					} else {
						do {
							if (cursor.first == ']') {
								break;
							}
							idx = cursor.second;
							cursor = Utf8::NextChar(text, idx);
						} while (idx < textLength);
					}
				}
			} else {
				Rectf uvRect;
				if (cursor.first < 128) {
					uvRect = _asciiChars[cursor.first];
				} else {
					auto it = _unicodeChars.find(cursor.first);
					if (it != _unicodeChars.end()) {
						uvRect = it->second;
					} else {
						uvRect = _asciiChars[0];
					}
				}

				if (uvRect.W > 0 && uvRect.H > 0) {
					lastWidth += (uvRect.W + _baseSpacing) * charSpacing * scalePre;
				}
			}

			idx = cursor.second;
		} while (idx < textLength);

		if (totalWidth < lastWidth) {
			totalWidth = lastWidth;
		}
		lineWidths[line & (MaxLines - 1)] = lastWidth;
		totalHeight += (_charSize.Y * scale * lineSpacing);

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

		Vector2i texSize = _texture->size();
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
			std::pair<char32_t, std::size_t> cursor = Utf8::NextChar(text, idx);

			if (cursor.first == '\n') {
				// New line
				line++;
				originPos.X = lineStart;
				switch (align & Alignment::HorizontalMask) {
					case Alignment::Center: originPos.X += (totalWidth - lineWidths[line & (MaxLines - 1)]) * 0.5f; break;
					case Alignment::Right: originPos.X += (totalWidth - lineWidths[line & (MaxLines - 1)]); break;
				}
				originPos.Y += (_charSize.Y * scale * lineSpacing);
			} else if (cursor.first == '\f') {
				// Formatting
				cursor = Utf8::NextChar(text, cursor.second);
				if (cursor.first == '[') {
					idx = cursor.second;
					cursor = Utf8::NextChar(text, idx);
					if (cursor.first == 'c') {
						idx = cursor.second;
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first == ':') {
							// Set custom color
							idx = cursor.second;
							cursor = Utf8::NextChar(text, idx);
							if (cursor.first == '#') {
								idx = cursor.second;
								std::int32_t paramLength = 0;
								char param[9];
								do {
									cursor = Utf8::NextChar(text, idx);
									if (cursor.first == ']') {
										break;
									}
									if (paramLength < static_cast<std::int32_t>(arraySize(param)) - 1) {
										param[paramLength++] = (char)cursor.first;
									}
									idx = cursor.second;
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
					} else if (cursor.first == 'w') {
						idx = cursor.second;
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first == ':') {
							idx = cursor.second;
							std::int32_t paramLength = 0;
							char param[9];
							do {
								cursor = Utf8::NextChar(text, idx);
								if (cursor.first == ']') {
									break;
								}
								if (paramLength < static_cast<std::int32_t>(arraySize(param)) - 1) {
									param[paramLength++] = (char)cursor.first;
								}
								idx = cursor.second;
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
					} else if (cursor.first == '/') {
						idx = cursor.second;
						cursor = Utf8::NextChar(text, idx);
						if (cursor.first == 'c') {
							// Reset color
							if (!useRandomColor && !isShadow) {
								color = Colorf(1.0f, 1.0f, 1.0f, alpha);
								colorizeShader = nullptr;
							}
						} else if (cursor.first == 'w') {
							// Reset char spacing
							charSpacing = charSpacingPre;
						}
					}
				}

				while (cursor.first != ']') {
					cursor = Utf8::NextChar(text, cursor.second);
				}
			} else {
				Rectf uvRect;
				if (cursor.first < 128) {
					uvRect = _asciiChars[cursor.first];
				} else {
					auto it = _unicodeChars.find(cursor.first);
					if (it != _unicodeChars.end()) {
						uvRect = it->second;
					} else {
						uvRect = _asciiChars[0];
					}
				}

				if (uvRect.W > 0 && uvRect.H > 0) {
					if (useRandomColor) {
						const Colorf& newColor = RandomColors[charOffset % static_cast<std::int32_t>(arraySize(RandomColors))];
						color = Colorf(newColor.R, newColor.G, newColor.B, color.A);
					}

					Vector2f pos = Vector2f(originPos);

					if (angleOffset > 0.0f) {
						float currentPhase = (phase + charOffset) * angleOffset * fPi;
						if (speed > 0.0f && (charOffset % 2) == 1) {
							currentPhase = -currentPhase;
						}

						pos.X += cosf(currentPhase) * varianceX * scale;
						pos.Y += sinf(currentPhase) * varianceY * scale;
					}

					pos.X = std::round(pos.X);
					pos.Y = std::round(pos.Y);

					std::int32_t charWidth = _charSize.X;
					if (charWidth > uvRect.W) {
						charWidth--;
					}

					Vector4f texCoords = Vector4f(
						charWidth / float(texSize.X),
						uvRect.X,
						uvRect.H / float(texSize.Y),
						uvRect.Y
					);

					auto command = canvas->RentRenderCommand();
					command->setType(RenderCommand::Type::Text);
					bool shaderChanged = (colorizeShader
						? command->material().setShader(colorizeShader)
						: command->material().setShaderProgramType(Material::ShaderProgramType::Sprite));
					if (shaderChanged) {
						command->material().reserveUniformsDataMemory();
						command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
						// Required to reset render command properly
						//command->setTransformation(command->transformation());

						GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
						if (textureUniform && textureUniform->intValue(0) != 0) {
							textureUniform->setIntValue(0); // GL_TEXTURE0
						}
					}

					command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

					auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
					instanceBlock->uniform(Material::TexRectUniformName)->setFloatVector(texCoords.Data());
					instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(charWidth * scale, uvRect.H * scale);
					instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(color.Data());

					command->setTransformation(Matrix4x4f::Translation(pos.X, pos.Y, 0.0f));
					command->setLayer(z - (charOffset & 1));
					command->material().setTexture(*_texture.get());

					canvas->_currentRenderQueue->addCommand(command);

					originPos.X += ((uvRect.W + _baseSpacing) * scale * charSpacing);
					charOffset++;
				}
			}

			idx = cursor.second;
		} while (idx < textLength);
		charOffset++;
	}
}