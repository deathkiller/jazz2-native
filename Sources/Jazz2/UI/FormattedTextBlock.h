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
	/** @brief Initialization parameters for formatted text block */
	struct FormattedTextBlockParams
	{
		/** @brief Text to display */
		StringView Text;
		/** @brief Font used to render the text */
		Font* TextFont = nullptr;
		/** @brief Text alignment */
		Alignment Align = Alignment::Left;
		/** @brief Default text color */
		Colorf Color = Font::DefaultColor;
		/** @brief Text scale */
		float Scale = 1.0f;
		/** @brief Character spacing */
		float CharSpacing = 1.0f;
		/** @brief Line spacing */
		float LineSpacing = 1.0f;
	};

	/**
		@brief Formatted text block
		
		Lays out and renders a block of rich text to a canvas, splitting it into parts according to inline formatting,
		alignment, scale, and spacing. It supports multiline layout with optional word wrapping and ellipsizing to fit
		given bounds, and caches the computed layout for repeated drawing and measurement.
	*/
	class FormattedTextBlock
	{
	public:
		/** @brief Creates a new empty instance */
		FormattedTextBlock();
		/** @brief Creates a new instance from the specified parameters */
		FormattedTextBlock(const FormattedTextBlockParams& params);

		FormattedTextBlock(const FormattedTextBlock&) = delete;
		/** @brief Creates a new instance by moving the contents of another instance */
		FormattedTextBlock(FormattedTextBlock&& other) noexcept;
		FormattedTextBlock& operator=(const FormattedTextBlock&) = delete;
		FormattedTextBlock& operator=(FormattedTextBlock&& other) noexcept;

		/** @brief Returns a copy of the specified instance */
		static FormattedTextBlock From(const FormattedTextBlock& source);

		/** @brief Draws the text block to the specified canvas within the given bounds */
		void Draw(Canvas* canvas, Rectf bounds, std::uint16_t depth, std::int32_t& charOffset, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f, float speed = 0.4f);
		/** @brief Measures the size of the text block for the specified proposed size */
		Vector2f MeasureSize(Vector2f proposedSize);
		/** @brief Returns the cached width of the text block */
		float GetCachedWidth() const;
		/** @brief Returns the cached height of the text block */
		float GetCachedHeight() const;

		/** @brief Returns the text alignment */
		Alignment GetAlignment() const {
			return _alignment;
		}

		/** @brief Sets the text alignment */
		void SetAlignment(Alignment value);

		/** @brief Returns the default text color */
		Colorf GetDefaultColor() const {
			return _defaultColor;
		}

		/** @brief Sets the default text color */
		void SetDefaultColor(Colorf color);

		/** @brief Returns the font used to render the text */
		Font* GetFont() {
			return _font;
		}

		/** @brief Sets the font used to render the text */
		void SetFont(Font* value);

		/** @brief Returns the text scale */
		float GetScale() const {
			return _defaultScale;
		}

		/** @brief Sets the text scale */
		void SetScale(float value);

		/** @brief Returns the character spacing */
		float GetCharSpacing() const {
			return _defaultCharSpacing;
		}

		/** @brief Sets the character spacing */
		void SetCharSpacing(float value);

		/** @brief Returns the line spacing */
		float GetLineSpacing() const {
			return _defaultLineSpacing;
		}

		/** @brief Sets the line spacing */
		void SetLineSpacing(float value);

		/** @brief Returns the proposed width used for layout */
		constexpr float GetProposedWidth() const {
			return _proposedWidth;
		}

		/** @brief Sets the proposed width used for layout */
		void SetProposedWidth(float value);

		/** @brief Returns the displayed text */
		StringView GetText() const {
			return _text;
		}

		/** @brief Sets the displayed text */
		void SetText(StringView value);
		/** @overload */
		void SetText(String&& value);

		/** @brief Returns `true` if multiline text is enabled */
		constexpr bool IsMultiline() const {
			return (_flags & FormattedTextBlockFlags::Multiline) == FormattedTextBlockFlags::Multiline;
		}
		/** @brief Sets whether multiline text is enabled */
		void SetMultiline(bool value);

		/** @brief Returns `true` if word wrapping is enabled */
		constexpr bool GetWrapping() const {
			return (_flags & FormattedTextBlockFlags::Wrapping) == FormattedTextBlockFlags::Wrapping;
		}
		/** @brief Sets whether word wrapping is enabled */
		void SetWrapping(bool value);

		/** @brief Returns `true` if the text is ellipsized to fit the bounds */
		constexpr bool IsEllipsized() const {
			return (_flags & FormattedTextBlockFlags::Ellipsized) == FormattedTextBlockFlags::Ellipsized;
		}

	private:
		static constexpr std::uint32_t Ellipsis = UINT32_MAX;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct Part
		{
			std::uint32_t Begin;
			std::uint32_t Length;
			Vector2f Location;
			float Height;
			Colorf CurrentColor;
			float Scale;
			float CharSpacing;
			bool AllowVariance;

			Part(std::uint32_t begin, std::uint32_t length, Vector2f location, float height, Colorf color, float scale, float charSpacing, bool allowVariance) noexcept;
			
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
#endif

		enum class FormattedTextBlockFlags : std::uint16_t {
			None = 0,
			Multiline = 0x01,
			Wrapping = 0x02,

			Ellipsized = 0x10
		};

		DEATH_PRIVATE_ENUM_FLAGS(FormattedTextBlockFlags);

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