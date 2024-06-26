#pragma once

#include "Canvas.h"
#include "Font.h"
#include "../../nCine/Primitives/Rect.h"
#include "../../nCine/Primitives/Vector2.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::UI
{
	struct FormattedTextBlockParams
	{
		StringView Text;
		Font* TextFont = nullptr;
		Alignment Align = Alignment::Left;
		Colorf Color = Font::DefaultColor;
		float Scale = 1.0f;
		float CharSpacing = 1.0f;
		float LineSpacing = 1.0f;
	};

	class FormattedTextBlock
	{
	public:
		FormattedTextBlock();
		FormattedTextBlock(const FormattedTextBlockParams& params);

		FormattedTextBlock(const FormattedTextBlock&) = delete;
		FormattedTextBlock(FormattedTextBlock&& other) noexcept;
		FormattedTextBlock& operator=(const FormattedTextBlock&) = delete;
		FormattedTextBlock& operator=(FormattedTextBlock&& other) noexcept;

		static FormattedTextBlock From(const FormattedTextBlock& source);

		void Draw(Canvas* canvas, Rectf bounds, std::uint16_t depth, std::int32_t& charOffset, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f, float speed = 0.4f);
		Vector2f MeasureSize(Vector2f proposedSize);
		float GetCachedWidth() const;
		float GetCachedHeight() const;

		Alignment GetAlignment() const {
			return _alignment;
		}

		void SetAlignment(Alignment value);

		Colorf GetDefaultColor() const {
			return _defaultColor;
		}

		void SetDefaultColor(Colorf color);

		Font* GetFont() {
			return _font;
		}

		void SetFont(Font* value);

		float GetScale() const {
			return _defaultScale;
		}

		void SetScale(float value);

		float GetCharSpacing() const {
			return _defaultCharSpacing;
		}

		void SetCharSpacing(float value);

		float GetLineSpacing() const {
			return _defaultLineSpacing;
		}

		void SetLineSpacing(float value);

		constexpr float GetProposedWidth() const {
			return _proposedWidth;
		}

		void SetProposedWidth(float value);

		StringView GetText() const {
			return _text;
		}

		void SetText(StringView value);
		void SetText(String&& value);

		constexpr bool IsMultiline() const {
			return (_flags & FormattedTextBlockFlags::Multiline) == FormattedTextBlockFlags::Multiline;
		}
		void SetMultiline(bool value);

		constexpr bool GetWrapping() const {
			return (_flags & FormattedTextBlockFlags::Wrapping) == FormattedTextBlockFlags::Wrapping;
		}
		void SetWrapping(bool value);

		constexpr bool IsEllipsized() const {
			return (_flags & FormattedTextBlockFlags::Ellipsized) == FormattedTextBlockFlags::Ellipsized;
		}

	private:
		struct Part
		{
			const char* Value;
			std::uint32_t Length;
			Vector2f Location;
			float Height;
			Colorf CurrentColor;
			float Scale;
			float CharSpacing;
			bool AllowVariance;

			Part(const char* value, std::uint32_t length, Vector2f location, float height, Colorf color, float scale, float charSpacing, bool allowVariance) noexcept;
			
			Part(const Part& other) noexcept;
			Part(Part&& other) noexcept;
			Part& operator=(const Part& other) noexcept;
			Part& operator=(Part&& other) noexcept;
		};

		struct BackgroundPart
		{
			Rectf Bounds;
			Colorf CurrentColor;

			BackgroundPart(Colorf color);
		};

		enum class FormattedTextBlockFlags : std::uint16_t {
			None = 0,
			Multiline = 0x01,
			Wrapping = 0x02,

			Ellipsized = 0x10
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(FormattedTextBlockFlags);

		SmallVector<Part, 1> _parts;
		SmallVector<BackgroundPart, 0> _background;
		Font* _font;
		String _text;
		FormattedTextBlockFlags _flags;
		float _proposedWidth;
		float _cachedWidth;
		Colorf _defaultColor;
		float _defaultScale;
		float _defaultCharSpacing;
		float _defaultLineSpacing;
		Alignment _alignment;

		void RecreateCache();
		void HandleEndOfLine(Vector2f currentLocation, std::int32_t& lineBeginIndex, std::int32_t& lineAlignIndex, std::int32_t& backgroundIndex);
		void InsertEllipsis(Vector2f& currentLocation, Colorf currentColor, float scale, float charSpacing, float lineSpacing, bool allowVariance, float maxWidth, float* charFitWidths);
		void InsertDottedUnderline(Part& part, float width);

		static float PerformVerticalAlignment(SmallVectorImpl<Part>& processedParts, std::int32_t firstPartOfLine);
		static void FinalizeBackgroundPart(Vector2f currentLocation, SmallVectorImpl<Part>& parts, SmallVectorImpl<BackgroundPart>& backgroundParts);
		static char* FindFirstControlSequence(char* string, std::int32_t length);
		static Colorf Uint32ToColorf(std::uint32_t value);
	};
}