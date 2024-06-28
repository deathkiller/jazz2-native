#include "FormattedTextBlock.h"

#include <Base/StackAlloc.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace Jazz2::UI
{
	FormattedTextBlock::Part::Part(const char* value, std::uint32_t length, Vector2f location, float height, Colorf color, float scale, float charSpacing, bool allowVariance) noexcept
		: Value(value), Length(length), Location(location), Height(height), CurrentColor(color), Scale(scale), CharSpacing(charSpacing), AllowVariance(allowVariance)
	{
	}

	FormattedTextBlock::Part::Part(const Part& other) noexcept
	{
		Value = other.Value;
		Length = other.Length;
		Location = other.Location;
		Height = other.Height;
		CurrentColor = other.CurrentColor;
		Scale = other.Scale;
		CharSpacing = other.CharSpacing;
		AllowVariance = other.AllowVariance;
	}

	FormattedTextBlock::Part::Part(Part&& other) noexcept
	{
		Value = other.Value;
		Length = other.Length;
		Location = other.Location;
		Height = other.Height;
		CurrentColor = std::move(other.CurrentColor);
		Scale = other.Scale;
		CharSpacing = other.CharSpacing;
		AllowVariance = other.AllowVariance;
	}

	FormattedTextBlock::Part& FormattedTextBlock::Part::operator=(const Part& other) noexcept
	{
		Value = other.Value;
		Length = other.Length;
		Location = other.Location;
		Height = other.Height;
		CurrentColor = other.CurrentColor;
		Scale = other.Scale;
		CharSpacing = other.CharSpacing;
		AllowVariance = other.AllowVariance;
		return *this;
	}

	FormattedTextBlock::Part& FormattedTextBlock::Part::operator=(Part&& other) noexcept
	{
		Value = other.Value;
		Length = other.Length;
		Location = other.Location;
		Height = other.Height;
		CurrentColor = std::move(other.CurrentColor);
		Scale = other.Scale;
		CharSpacing = other.CharSpacing;
		AllowVariance = other.AllowVariance;
		return *this;
	}

	FormattedTextBlock::BackgroundPart::BackgroundPart(Colorf color)
		: CurrentColor(color)
	{
	}

	FormattedTextBlock::FormattedTextBlock()
		: _font(nullptr), _flags(FormattedTextBlockFlags::None), _defaultColor(Font::DefaultColor),
			_proposedWidth(4096), _cachedWidth(0), _defaultScale(1.0f), _defaultCharSpacing(1.0f),
			_defaultLineSpacing(1.0f), _alignment(Alignment::Left)
	{
	}

	FormattedTextBlock::FormattedTextBlock(const FormattedTextBlockParams& params)
		: _font(params.TextFont), _text(params.Text), _flags(FormattedTextBlockFlags::None), _defaultColor(params.Color),
			_proposedWidth(4096), _cachedWidth(0), _defaultScale(params.Scale), _defaultCharSpacing(params.CharSpacing),
			_defaultLineSpacing(params.LineSpacing), _alignment(params.Align)
	{

	}

	FormattedTextBlock::FormattedTextBlock(FormattedTextBlock&& other) noexcept
	{
		_parts = std::move(other._parts);
		_background = std::move(other._background);
		_font = std::move(other._font);
		_text = std::move(other._text);
		_flags = other._flags;
		_proposedWidth = other._proposedWidth;
		_cachedWidth = other._cachedWidth;
		_defaultColor = other._defaultColor;
		_defaultScale = other._defaultScale;
		_defaultCharSpacing = other._defaultCharSpacing;
		_defaultLineSpacing = other._defaultLineSpacing;
		_alignment = other._alignment;
	}

	FormattedTextBlock& FormattedTextBlock::operator=(FormattedTextBlock&& other) noexcept
	{
		_parts = std::move(other._parts);
		_background = std::move(other._background);
		_font = std::move(other._font);
		_text = std::move(other._text);
		_flags = other._flags;
		_proposedWidth = other._proposedWidth;
		_cachedWidth = other._cachedWidth;
		_defaultColor = other._defaultColor;
		_defaultScale = other._defaultScale;
		_defaultCharSpacing = other._defaultCharSpacing;
		_defaultLineSpacing = other._defaultLineSpacing;
		_alignment = other._alignment;
		return *this;
	}

	FormattedTextBlock FormattedTextBlock::From(const FormattedTextBlock& source)
	{
		FormattedTextBlock dest;
		dest._flags = source._flags;
		dest._font = source._font;
		dest._text = source._text;
		dest._defaultColor = source._defaultColor;
		dest._defaultScale = source._defaultScale;
		dest._defaultCharSpacing = source._defaultCharSpacing;
		dest._defaultLineSpacing = source._defaultLineSpacing;
		dest._alignment = source._alignment;
		return dest;
	}

	void FormattedTextBlock::Draw(Canvas* canvas, Rectf bounds, std::uint16_t depth, std::int32_t& charOffset, float angleOffset, float varianceX, float varianceY, float speed)
	{
		if (_parts.empty() || _proposedWidth != bounds.W) {
			_proposedWidth = bounds.W;
			RecreateCache();
		}

		/*for (auto& part : _background) {
			if (part.Bounds.H > 5) {
				// TODO
				// Rounded rectangle
				Color outlineColor = (part.CurrentColor.GetR() + part.CurrentColor.GetG() + part.CurrentColor.GetB() < 3 * 128
					? Color::FromArgb(180, part.CurrentColor.GetR() + 60, part.CurrentColor.GetG() + 60, part.CurrentColor.GetB() + 60)
					: Color::FromArgb(110, part.CurrentColor.GetR() - 120, part.CurrentColor.GetG() - 120, part.CurrentColor.GetB() - 120));

				g.FillRoundedRectangle(part.CurrentColor, outlineColor, part.Bounds.Offset(Point(bounds.X, bounds.Y)));
			} else {
				// Dotted line
				for (std::int32_t y = 0; y < part.Bounds.H; y++) {
					for (std::int32_t x = 1; x < part.Bounds.W; x += part.Bounds.H * 2) {
						g.FillPixelsUnsafe(part.CurrentColor, part.Bounds.X + bounds.X + x, part.Bounds.Y + bounds.Y + y, part.Bounds.H);
					}
				}
			}
		}*/

		std::int32_t charOffsetShadow = charOffset;

		auto it = _parts.begin();
		while (it != _parts.end()) {
			if (it->Location.Y + it->Height > bounds.H) {
				break;
			}

			Vector2f p = it->Location;
			p.X += bounds.X;
			p.Y += bounds.Y;

			_font->DrawString(canvas, StringView(it->Value, it->Length), charOffsetShadow, p.X, p.Y + 2.8f * _defaultScale, depth - 80, Alignment::Left,
				Colorf(0.0f, 0.0f, 0.0f, 0.29f), it->Scale, it->AllowVariance ? angleOffset : 0.0f, varianceX, varianceY, speed, it->CharSpacing);

			++it;
		}

		it = _parts.begin();
		while (it != _parts.end()) {
			if (it->Location.Y + it->Height > bounds.H) {
				break;
			}

			Vector2f p = it->Location;
			p.X += bounds.X;
			p.Y += bounds.Y;

			_font->DrawString(canvas, StringView(it->Value, it->Length), charOffset, p.X, p.Y, depth, Alignment::Left,
				it->CurrentColor, it->Scale, it->AllowVariance ? angleOffset : 0.0f, varianceX, varianceY, speed, it->CharSpacing);

			++it;
		}
	}

	Vector2f FormattedTextBlock::MeasureSize(Vector2f proposedSize)
	{
		if (_parts.empty() || _proposedWidth != proposedSize.X) {
			_proposedWidth = proposedSize.X;
			RecreateCache();
		}

		std::int32_t cachedHeight = GetCachedHeight();
		if (cachedHeight <= 0) {
			return {};
		}

		return Vector2f(_cachedWidth, cachedHeight);
	}

	float FormattedTextBlock::GetCachedWidth() const
	{
		return _cachedWidth;
	}

	float FormattedTextBlock::GetCachedHeight() const
	{
		if (_parts.empty()) {
			return 0.0f;
		}

		const Part& lastPart = _parts[_parts.size() - 1];
		return lastPart.Location.Y + lastPart.Height;
	}

	void FormattedTextBlock::RecreateCache()
	{
		_flags &= ~FormattedTextBlockFlags::Ellipsized;
		_cachedWidth = 0;

		if (_text.empty()) {
			return;
		}

		_parts.clear();
		_background.clear();

		char* unprocessedText = _text.data();
		std::int32_t unprocessedLength = (std::int32_t)_text.size();
		auto charFitWidths = stack_alloc(float, unprocessedLength + 1);

		Colorf currentColor = _defaultColor;
		float scale = _defaultScale;
		float charSpacing = _defaultCharSpacing;
		float lineSpacing = _defaultLineSpacing;

		enum class SkipTill {
			None,
			EndOfLine,
			EndOfHighlight
		};

		enum class StyleIndex {
			Bold,
			Italic,
			Underline,
			DottedUnderline,
			Strikeout,
			AllowVariance,
			Count
		};

		Vector2f currentLocation;
		std::int32_t firstPartOfLine = 0;
		std::int32_t lineBeginIndex = 0;
		std::int32_t lineAlignIndex = -1;
		std::int32_t backgroundIndex = -1;
		SkipTill skipTill = SkipTill::None;
		std::int32_t styleCount[(std::int32_t)StyleIndex::Count] {};

		while (unprocessedLength > 0) {
			char* nextPtr = FindFirstControlSequence(unprocessedText, unprocessedLength);
			if (nextPtr == unprocessedText) {
				if (nextPtr[0] == '\f') {
					char* formatDescPtr = nextPtr + 2;
					char* formatEndPtr = strchr(formatDescPtr, ']');
					if (formatEndPtr == nullptr) {
						LOGD("Missing closing format bracket in formatted string");
						goto End;
					}

					std::int32_t formatLength = (std::int32_t)(formatEndPtr - formatDescPtr);
					if (formatLength > 0) {
						switch (formatDescPtr[0]) {
							case 'a': // Dotted underline
								if (formatLength == 1) {
									++styleCount[(std::int32_t)StyleIndex::DottedUnderline];
								}
								break;
							case 'c': // Color
								if ((formatLength == 9 || formatLength == 11) && formatDescPtr[1] == ':' && formatDescPtr[2] == '#') {
									formatDescPtr += 3;
									formatLength -= 3;

									wchar_t number[9];
									std::int32_t i = 0;
									for (; i < formatLength; i++) {
										number[i] = *formatDescPtr;
										formatDescPtr++;
									}
									number[i] = '\0';

									uint32_t color = wcstoul(number, nullptr, 16);
									if (formatLength == 6) {
										color |= 0xff000000;
									}

									// Swap red and blue channel, because Color stores it in 0xAABBGGRR format internally
									currentColor = Uint32ToColorf(color);
								}
								break;
							case 'u': // Underline
								if (formatLength == 1) {
									++styleCount[(std::int32_t)StyleIndex::Underline];
								}
								break;
							case 's': // Strikeout
								if (formatLength == 1) {
									++styleCount[(std::int32_t)StyleIndex::Strikeout];
								}
								break;
							case 'r': // Background color (highlight rectangle)
								if ((formatLength == 9 || formatLength == 11) && formatDescPtr[1] == ':' && formatDescPtr[2] == '#') {
									// TODO: Combination with align to right is not supported yet
									if (skipTill != SkipTill::EndOfLine && lineAlignIndex == -1) {
										formatDescPtr += 3;
										formatLength -= 3;

										wchar_t number[9];
										std::int32_t i = 0;
										for (; i < formatLength; i++) {
											number[i] = *formatDescPtr;
											formatDescPtr++;
										}
										number[i] = '\0';

										uint32_t color = wcstoul(number, nullptr, 16);
										if (formatLength == 6) {
											color |= 0xff000000;
										}

										if (backgroundIndex != -1) {
											// Finish previous unclosed highlight
											FinalizeBackgroundPart(currentLocation, _parts, _background);
										}

										backgroundIndex = (std::int32_t)_background.size();

										// Swap red and blue channel, because Color stores it in 0xAABBGGRR format internally
										auto& part = _background.emplace_back(Uint32ToColorf(color));
										part.Bounds.X = currentLocation.X - 3;
										part.Bounds.Y = currentLocation.Y;
									}
								}
								break;
							case '-': // Align to right
								if (formatLength == 1 && lineAlignIndex == -1) {
									lineAlignIndex = (std::int32_t)_parts.size();
								}
								break;
							case '^': // Show rest only when multiline is disabled
								if (formatLength == 1 && IsMultiline()) {
									skipTill = SkipTill::EndOfLine;
								}
								break;

							case 'h': // Scale
								if (formatLength >= 3 && formatDescPtr[1] == ':') {
									char* end = nullptr;
									std::uint32_t paramValue = strtoul(&formatDescPtr[2], &end, 10);
									if (formatDescPtr + formatLength == end) {
										scale = _defaultScale * paramValue * 0.01f;
									}
								}
								break;
							case 'w': // Character spacing
								if (formatLength >= 3 && formatDescPtr[1] == ':') {
									char* end = nullptr;
									std::uint32_t paramValue = strtoul(&formatDescPtr[2], &end, 10);
									if (formatDescPtr + formatLength == end) {
										charSpacing = _defaultCharSpacing * paramValue * 0.01f;
									}
								}
								break;
							case 'j': // Allow variance
								if (formatLength == 1) {
									++styleCount[(std::int32_t)StyleIndex::AllowVariance];
								}
								break;

							case '/': { // End tag
								if (formatLength == 2) {
									switch (formatDescPtr[1]) {
										case 'a': {
											styleCount[(std::int32_t)StyleIndex::DottedUnderline];
											break;
										}
										case 'u': {
											if (styleCount[(std::int32_t)StyleIndex::Underline] > 0) {
												--styleCount[(std::int32_t)StyleIndex::Underline];
											}
											break;
										}
										case 's': {
											if (styleCount[(std::int32_t)StyleIndex::Strikeout] > 0) {
												--styleCount[(std::int32_t)StyleIndex::Strikeout];
											}
											break;
										}
										case 'c': {
											currentColor = _defaultColor;
											break;
										}
										case 'r': {
											if (backgroundIndex != -1) {
												FinalizeBackgroundPart(currentLocation, _parts, _background);
												backgroundIndex = -1;
												if (skipTill == SkipTill::EndOfHighlight) {
													skipTill = SkipTill::None;
												}
											}
											break;
										}

										case 'h': {
											scale = _defaultScale;
											break;
										}
										case 'w': {
											charSpacing = _defaultCharSpacing;
											break;
										}
										case 'j': {
											if (styleCount[(std::int32_t)StyleIndex::AllowVariance] > 0) {
												--styleCount[(std::int32_t)StyleIndex::AllowVariance];
											}
											break;
										}
									}
								}
								break;
							}
						}
					}

					formatEndPtr++;

					unprocessedLength -= (std::int32_t)(formatEndPtr - unprocessedText);
					unprocessedText = formatEndPtr;
					continue;
				} else if (nextPtr[0] == L'\n') {
					// New line
					HandleEndOfLine(currentLocation, lineBeginIndex, lineAlignIndex, backgroundIndex);

					float lineHeight = PerformVerticalAlignment(_parts, firstPartOfLine);
					firstPartOfLine = (std::int32_t)_parts.size();
					if (lineHeight == -1) {
						Vector2f size = _font->MeasureStringEx("\n"_s, scale, charSpacing, _proposedWidth, nullptr, nullptr);
						lineHeight = size.Y;
					}

					if (!IsMultiline()) {
						goto End;
					}

					if (_cachedWidth < currentLocation.X) {
						_cachedWidth = currentLocation.X;
					}

					currentLocation.X = 0;
					currentLocation.Y += lineHeight;

					unprocessedLength--;
					unprocessedText++;

					skipTill = SkipTill::None;
					continue;
				} else if (nextPtr[0] == L'\t') {
					// Tabs
					std::int32_t tabCount = 0;
					do {
						unprocessedLength--;
						unprocessedText++;
						tabCount++;
					} while (unprocessedLength > 0 && unprocessedText[0] == L'\t');

					// This has to be adjusted for bitmap font
					//std::int32_t tabWidth = _font->GetSizeInPixels() * 5 / 2;
					std::int32_t tabWidth = _font->GetSizeInPixels() * 2;

					float remainder = fmodf(currentLocation.X, tabWidth);
					currentLocation.X += (tabWidth * tabCount) - remainder;

					if (currentLocation.X >= _proposedWidth) {
						currentLocation.X = _proposedWidth;

						if (!IsMultiline()) {
							goto End;
						}

						lineAlignIndex = -1;

						if ((_flags & (FormattedTextBlockFlags::Multiline | FormattedTextBlockFlags::Wrapping)) == (FormattedTextBlockFlags::Multiline | FormattedTextBlockFlags::Wrapping)) {
							std::int32_t lineHeight = PerformVerticalAlignment(_parts, firstPartOfLine);
							firstPartOfLine = (std::int32_t)_parts.size();

							if (_cachedWidth < currentLocation.X) {
								_cachedWidth = currentLocation.X;
							}

							currentLocation.X = 0;
							currentLocation.Y += lineHeight;

							skipTill = SkipTill::None;
						} else {
							skipTill = SkipTill::EndOfLine;
						}
					}
					continue;
				} else if (nextPtr[0] == '\0') {
					// Skip NULL terminators - they shouldn't be inside strings anyway
					unprocessedLength--;
					unprocessedText++;
					continue;
				}
			}

			if (skipTill != SkipTill::None) {
				// Wrapping is not enabled, so skip everything till end of line, but preserve formatting
				unprocessedLength -= (std::int32_t)(nextPtr - unprocessedText);
				unprocessedText = nextPtr;
				continue;
			}

			std::int32_t lengthToMeasure = (std::int32_t)(nextPtr - unprocessedText);
			float maxWidth = _proposedWidth - currentLocation.X;
			std::int32_t charFit;
			Vector2f size = _font->MeasureStringEx(StringView(unprocessedText, lengthToMeasure), scale, charSpacing, maxWidth, &charFit, charFitWidths);
			float charFitWidth = (charFit > 0 ? charFitWidths[charFit - 1] : 0.0f);

			if (charFit >= lengthToMeasure) {
				// All the characters fit
				char* toPtr = (nextPtr[0] == L'\n' && nextPtr[-1] == L'\r' ? nextPtr - 1 : nextPtr);
				std::int32_t partLength = (std::int32_t)(toPtr - unprocessedText);
				Part& part = _parts.emplace_back(unprocessedText, partLength, currentLocation,
					size.Y * lineSpacing, currentColor, scale, charSpacing,
					styleCount[(std::int32_t)StyleIndex::AllowVariance] > 0);

				if (nextPtr[0] == L'\n') {
					if (partLength > 0) {
						if (styleCount[(std::int32_t)StyleIndex::DottedUnderline] > 0) {
							InsertDottedUnderline(part, charFitWidths[partLength - 1]);
						}

						currentLocation.X += charFitWidths[partLength - 1];
					}

					HandleEndOfLine(currentLocation, lineBeginIndex, lineAlignIndex, backgroundIndex);

					float lineHeight = PerformVerticalAlignment(_parts, firstPartOfLine);
					firstPartOfLine = (std::int32_t)_parts.size();

					if (!IsMultiline()) {
						goto End;
					}

					if (_cachedWidth < currentLocation.X) {
						_cachedWidth = currentLocation.X;
					}

					currentLocation.X = 0;
					currentLocation.Y += lineHeight;

					// Skip the new line and also all spaces that follow
					do {
						nextPtr++;
					} while (nextPtr[0] == L' ');

					skipTill = SkipTill::None;
				} else {
					if (styleCount[(std::int32_t)StyleIndex::DottedUnderline] > 0 && charFitWidth > 0) {
						InsertDottedUnderline(part, charFitWidth);
					}

					currentLocation.X += charFitWidth;
				}

				unprocessedLength -= (std::int32_t)(nextPtr - unprocessedText);
				unprocessedText = nextPtr;
			} else {
				// There's not enough room for all the characters
				if ((_flags & (FormattedTextBlockFlags::Multiline | FormattedTextBlockFlags::Wrapping)) == (FormattedTextBlockFlags::Multiline | FormattedTextBlockFlags::Wrapping)) {
					// The smallest part doesn't fit (and clipping is not supported), so it can't continue
					if (charFit == 0 && firstPartOfLine == _parts.size()) {
						goto End;
					}

					if (backgroundIndex != -1) {
						// If highlighting is active, trim it only, don't wrap it
						if (charFit > 2) {
							charFit -= 2;
							Part& part = _parts.emplace_back(unprocessedText, charFit, currentLocation,
								size.Y * lineSpacing, currentColor, scale, charSpacing,
								styleCount[(std::int32_t)StyleIndex::AllowVariance] > 0);

							if (styleCount[(std::int32_t)StyleIndex::DottedUnderline] > 0) {
								InsertDottedUnderline(part, charFitWidths[charFit - 1]);
							}

							currentLocation.X += charFitWidths[charFit - 1];
							maxWidth -= charFitWidths[charFit - 1];
						}

						InsertEllipsis(currentLocation, currentColor, scale, charSpacing, lineSpacing,
							styleCount[(std::int32_t)StyleIndex::AllowVariance] > 0, maxWidth, charFitWidths);
						skipTill = SkipTill::EndOfHighlight;
						continue;
					} else {
						char* lastWhitespacePtr;
						bool wasWhitespace = true;
						if (charFit > 0) {
							// Go backwards and try to find whitespace (or splittable character if it's not the first one)
							lastWhitespacePtr = unprocessedText + charFit;
							if (lastWhitespacePtr[0] != L' ' && lastWhitespacePtr[0] != L'\t') {
								lastWhitespacePtr--;
							}

							while (unprocessedText <= lastWhitespacePtr) {
								if (lastWhitespacePtr[0] == L' ' || lastWhitespacePtr[0] == L'\t' || lastWhitespacePtr[0] == L'/' || lastWhitespacePtr[0] == L'\\') {
									break;
								}

								lastWhitespacePtr--;
							}

							// Check if there is only whitespace in the current part, then don't split it here
							if (unprocessedText < lastWhitespacePtr) {
								bool onlyWhitespace = true;
								char* ptr = unprocessedText;
								do {
									if (ptr[0] != L' ' && ptr[0] != L'\t') {
										onlyWhitespace = false;
										break;
									}
									ptr++;
								} while (ptr < lastWhitespacePtr);

								if (onlyWhitespace) {
									lastWhitespacePtr = nullptr;
								}
							} else {
								lastWhitespacePtr = nullptr;
							}

							// If split point was not found yet, or there is a lot of space left, or it's the first path on current line, create largest possible part instead
							if (lastWhitespacePtr == nullptr && (maxWidth > 40 || firstPartOfLine == _parts.size())) {
								lastWhitespacePtr = unprocessedText + charFit;
								wasWhitespace = false;
							} else if (lastWhitespacePtr != nullptr && (lastWhitespacePtr[0] == L'/' || lastWhitespacePtr[0] == L'\\')) {
								// If it's splittable character, keep it on the current line
								lastWhitespacePtr++;
								wasWhitespace = false;
							}
						} else {
							// No character can fit
							lastWhitespacePtr = nullptr;
						}

						if (lastWhitespacePtr != nullptr) {
							std::int32_t partLength = (std::int32_t)(lastWhitespacePtr - unprocessedText);
							Part& part = _parts.emplace_back(unprocessedText, partLength, currentLocation,
								size.Y * lineSpacing, currentColor, scale, charSpacing,
								styleCount[(std::int32_t)StyleIndex::AllowVariance] > 0);

							if (styleCount[(std::int32_t)StyleIndex::DottedUnderline] > 0) {
								InsertDottedUnderline(part, charFitWidths[partLength - 1]);
							}

							currentLocation.X += charFitWidths[partLength - 1];

							if (wasWhitespace) {
								lastWhitespacePtr++;
								partLength++;
							}

							unprocessedLength -= partLength;
							unprocessedText = lastWhitespacePtr;
						} else {
							// If no characters can fit, at least trim all leading whitespace from the next line
							while (unprocessedLength > 0 && unprocessedText[0] == ' ') {
								unprocessedLength--;
								unprocessedText++;
							}
						}
					}

					// Split alignment is not allowed on wrapped line
					lineAlignIndex = -1;
					HandleEndOfLine(currentLocation, lineBeginIndex, lineAlignIndex, backgroundIndex);

					float lineHeight = PerformVerticalAlignment(_parts, firstPartOfLine);

					if (_cachedWidth < currentLocation.X) {
						_cachedWidth = currentLocation.X;
					}

					currentLocation.X = 0;
					currentLocation.Y += lineHeight;
				} else {
					if (charFit > 2) {
						charFit -= 2;
						Part& part = _parts.emplace_back(unprocessedText, charFit, currentLocation,
							size.Y * lineSpacing, currentColor, scale, charSpacing,
							styleCount[(std::int32_t)StyleIndex::AllowVariance] > 0);

						if (styleCount[(std::int32_t)StyleIndex::DottedUnderline] > 0) {
							InsertDottedUnderline(part, charFitWidths[charFit - 1]);
						}

						currentLocation.X += charFitWidths[charFit - 1];
						maxWidth -= charFitWidths[charFit - 1];
					}

					InsertEllipsis(currentLocation, currentColor, scale, charSpacing, lineSpacing,
						styleCount[(std::int32_t)StyleIndex::AllowVariance] > 0, maxWidth, charFitWidths);
					_flags |= FormattedTextBlockFlags::Ellipsized;

					if (!IsMultiline()) {
						goto End;
					}

					if (nextPtr[0] == L'\n') {
						nextPtr++;

						unprocessedLength -= (std::int32_t)(nextPtr - unprocessedText);
						unprocessedText = nextPtr;

						float lineHeight = PerformVerticalAlignment(_parts, firstPartOfLine);

						if (_cachedWidth < currentLocation.X) {
							_cachedWidth = currentLocation.X;
						}

						currentLocation.X = 0;
						currentLocation.Y += lineHeight;
					} else {
						unprocessedLength -= (std::int32_t)(nextPtr - unprocessedText);
						unprocessedText = nextPtr;

						lineAlignIndex = -1;
						skipTill = SkipTill::EndOfLine;
						continue;
					}
				}

				lineAlignIndex = -1;
				firstPartOfLine = (std::int32_t)_parts.size();
				skipTill = SkipTill::None;
			}
		}

	End:
		HandleEndOfLine(currentLocation, lineBeginIndex, lineAlignIndex, backgroundIndex);
		PerformVerticalAlignment(_parts, firstPartOfLine);

		if (_cachedWidth < currentLocation.X) {
			_cachedWidth = currentLocation.X;
		}
	}

	void FormattedTextBlock::HandleEndOfLine(Vector2f currentLocation, std::int32_t& lineBeginIndex, std::int32_t& lineAlignIndex, std::int32_t& backgroundIndex)
	{
		if (backgroundIndex != -1) {
			auto& part = _background[backgroundIndex];
			if (part.Bounds.X == currentLocation.X - 3) {
				_background.pop_back();
			} else {
				part.Bounds.W = currentLocation.X - part.Bounds.X + 3;
				auto& lastPart = _parts[_parts.size() - 1];
				part.Bounds.H = lastPart.Height + 1;
			}
			backgroundIndex = -1;
		}

		if (lineAlignIndex != -1) {
			// Perform split alignment
			float offset = (_proposedWidth - currentLocation.X);
			for (std::int32_t i = lineAlignIndex; i < (std::int32_t)_parts.size(); i++) {
				Part& partRef = _parts[i];
				partRef.Location.X += offset;
			}

			lineAlignIndex = -1;
		} else {
			// Perform standard alignment
			Alignment horizontalAlign = (_alignment & Alignment::HorizontalMask);
			if (horizontalAlign == Alignment::Center || horizontalAlign == Alignment::Right) {
				float offset = (_proposedWidth - currentLocation.X);
				if (horizontalAlign == Alignment::Center) {
					offset *= 0.5f;
				}
				for (std::int32_t i = lineBeginIndex; i < (std::int32_t)_parts.size(); i++) {
					Part& partRef = _parts[i];
					partRef.Location.X += offset;
				}
			}
		}

		lineBeginIndex = (std::int32_t)_parts.size();
	}

	void FormattedTextBlock::InsertEllipsis(Vector2f& currentLocation, Colorf currentColor, float scale, float charSpacing, float lineSpacing, bool allowVariance, float maxWidth, float* charFitWidths)
	{
		std::int32_t charFit;
		Vector2f size = _font->MeasureStringEx("..."_s, scale, charSpacing, maxWidth, &charFit, charFitWidths);
		if (charFit > 0) {
			_parts.emplace_back("...", charFit, currentLocation,
				size.Y * lineSpacing, currentColor, scale, charSpacing, allowVariance);
			currentLocation.X += charFitWidths[charFit - 1];
		}
	}

	void FormattedTextBlock::InsertDottedUnderline(Part& part, float width)
	{
		std::int32_t ascent = _font->GetAscentInPixels();
		// Max. thickness is 5px, because it's shared also with rounded rectangles (with height >= 6px)
		std::int32_t thickness = std::clamp(ascent / 16, 1, 5);

		BackgroundPart& underline = _background.emplace_back(part.CurrentColor);
		underline.Bounds.X = part.Location.X;
		underline.Bounds.Y = part.Location.Y + ((part.Height + ascent + 1) / 2) - thickness; // Perfectly aligned with Segoe UI font
		underline.Bounds.W = width;
		underline.Bounds.H = thickness;
	}

	float FormattedTextBlock::PerformVerticalAlignment(SmallVectorImpl<Part>& processedParts, std::int32_t firstPartOfLine)
	{
		float maxHeight = -1.0f;

		for (std::size_t i = firstPartOfLine; i < processedParts.size(); i++) {
			maxHeight = std::max(maxHeight, processedParts[i].Height);
		}

		for (std::size_t i = firstPartOfLine; i < processedParts.size(); i++) {
			Part& lastPart = processedParts[i];

			if (lastPart.Height != maxHeight) {
				// Round up division, it should be better aligned at least for 16px images
				//float diff = ceilf((maxHeight - lastPart.Height) * 0.5f);
				float diff = roundf((maxHeight - lastPart.Height) * 0.2f);
				lastPart.Location.Y += diff;
				lastPart.Height = maxHeight - diff;
			}
		}

		return maxHeight;
	}

	void FormattedTextBlock::FinalizeBackgroundPart(Vector2f currentLocation, SmallVectorImpl<Part>& parts, SmallVectorImpl<BackgroundPart>& backgroundParts)
	{
		auto& background = backgroundParts[backgroundParts.size() - 1];

		if (background.Bounds.X == currentLocation.X - 3) {
			backgroundParts.erase(&background);
		} else {
			background.Bounds.W = currentLocation.X - background.Bounds.X + 3;

			auto* lastPart = &parts[parts.size() - 1];
			background.Bounds.H = lastPart->Height + 1;
		}
	}

	char* FormattedTextBlock::FindFirstControlSequence(char* string, std::int32_t length)
	{
		for (std::int32_t i = 0; i < length; i++) {
			if (string[i] == '\0' || string[i] == '\n' || string[i] == '\t' || (string[i] == '\f' && string[i + 1] == '[')) {
				return &string[i];
			}
		}
		return string + length;
	}

	void FormattedTextBlock::SetAlignment(Alignment value)
	{
		if (_alignment == value) {
			return;
		}

		_alignment = value;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetDefaultColor(Colorf color)
	{
		if (_defaultColor == color) {
			return;
		}

		_defaultColor = color;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetFont(Font* value)
	{
		if (_font == value) {
			return;
		}

		_font = value;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetScale(float value)
	{
		if (_defaultScale == value) {
			return;
		}

		_defaultScale = value;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetCharSpacing(float value)
	{
		if (_defaultCharSpacing == value) {
			return;
		}

		_defaultCharSpacing = value;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetLineSpacing(float value)
	{
		if (_defaultLineSpacing == value) {
			return;
		}

		_defaultLineSpacing = value;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetProposedWidth(float value) {
		if (_proposedWidth == value) {
			return;
		}

		_proposedWidth = value;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetText(StringView value)
	{
		if (_text == value) {
			return;
		}
		_text = value;
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetText(String&& value)
	{
		if (_text == value) {
			return;
		}
		_text = std::move(value);
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetMultiline(bool value)
	{
		if (((_flags & FormattedTextBlockFlags::Multiline) == FormattedTextBlockFlags::Multiline) == value) {
			return;
		}

		if (value) {
			_flags |= FormattedTextBlockFlags::Multiline;
		} else {
			_flags &= ~FormattedTextBlockFlags::Multiline;
		}
		_parts.clear();
		_background.clear();
	}

	void FormattedTextBlock::SetWrapping(bool value)
	{
		if (((_flags & FormattedTextBlockFlags::Wrapping) == FormattedTextBlockFlags::Wrapping) == value) {
			return;
		}

		if (value) {
			_flags |= FormattedTextBlockFlags::Wrapping;
		} else {
			_flags &= ~FormattedTextBlockFlags::Wrapping;
		}
		_parts.clear();
		_background.clear();
	}

	Colorf FormattedTextBlock::Uint32ToColorf(std::uint32_t value)
	{
		return Colorf((float)((value & 0x00ff0000) >> 16) / 255.0f, (float)((value & 0x0000ff00) >> 8) / 255.0f,
					  (float)(value & 0x000000ff) / 255.0f, (float)((value & 0xff000000) >> 24) / 255.0f);
	}
}