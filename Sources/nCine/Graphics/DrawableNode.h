#pragma once

#include "SceneNode.h"
#include "../Primitives/Rect.h"
#include "RenderCommand.h"

namespace nCine
{
	class RenderCommand;
	class RenderQueue;

	/**
		@brief Base class for scene nodes that can be drawn through the render queue
		
		Extends @ref SceneNode with everything needed to issue a draw: a node area (width and height), an
		anchor point for its transform, blending state and a @ref RenderCommand. On @ref OnDraw the node
		updates its render command and submits it to the render queue. Concrete drawables (sprites, particles)
		derive from this class.
	*/
	class DrawableNode : public SceneNode
	{
		friend class ShaderState;
		friend class Viewport;

	public:
		/** @{ @name Constants */

		// Notable anchor points for a drawable node
		static const Vector2f AnchorCenter;
		static const Vector2f AnchorBottomLeft;
		static const Vector2f AnchorTopLeft;
		static const Vector2f AnchorBottomRight;
		static const Vector2f AnchorTopRight;

		/** @} */

		/** @brief Blending factor of the source or destination color (alias of the backend-neutral @ref nCine::BlendingFactor) */
		using BlendingFactor = nCine::BlendingFactor;

		/**
		 * @brief Presets for source and destination blending factors
		 */
		enum class BlendingPreset
		{
			Disabled,					/**< Uses `BlendingFactor::One` and `BlendingFactor::Zero` */
			Alpha,						/**< Uses `BlendingFactor::SrcAlpha` and `BlendingFactor::OneMinusSrcAlpha` */
			PremultipliedAlpha,			/**< Uses `BlendingFactor::One` and `BlendingFactor::OneMinusSrcAlpha` */
			Additive,					/**< Uses `BlendingFactor::SrcAlpha` and `BlendingFactor::One` */
			Multiply					/**< Uses `BlendingFactor::DstColor` and `BlendingFactor::Zero` */
		};

		/** @brief Creates a node as a child of @p parent at the relative position (@p xx, @p yy) */
		DrawableNode(SceneNode* parent, float xx, float yy);
		/** @brief Creates a node as a child of @p parent at the relative position @p position */
		DrawableNode(SceneNode* parent, Vector2f position);
		/** @brief Creates a node as a child of @p parent, positioned at the relative origin */
		explicit DrawableNode(SceneNode* parent);
		/** @brief Creates a node with no parent, positioned at the origin */
		DrawableNode();
		~DrawableNode() override;

		DrawableNode& operator=(const DrawableNode&) = delete;
		DrawableNode(DrawableNode&&);
		DrawableNode& operator=(DrawableNode&&);

		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Returns the width of the node area */
		inline virtual float width() const {
			return _width * _scaleFactor.X;
		}
		/** @brief Returns the height of the node area */
		inline virtual float height() const {
			return _height * _scaleFactor.Y;
		}
		/** @brief Returns the size of the node area */
		inline Vector2f size() const {
			return Vector2f(width(), height());
		}

		/** @brief Returns the absolute width of the node area */
		inline virtual float absWidth() const {
			return _width * _absScaleFactor.X;
		}
		/** @brief Returns the absolute height of the node area */
		inline virtual float absHeight() const {
			return _height * _absScaleFactor.Y;
		}
		/** @brief Returns the absolute size of the node area */
		inline Vector2f absSize() const {
			return Vector2f(absWidth(), absHeight());
		}

		/** @brief Returns the transformation anchor point */
		inline Vector2f anchorPoint() const {
			return (_anchorPoint / size()) + 0.5f;
		}
		/** @brief Sets the transformation anchor point */
		void setAnchorPoint(float xx, float yy);
		/** @brief Sets the transformation anchor point from a `Vector2f` */
		inline void setAnchorPoint(Vector2f point) {
			setAnchorPoint(point.X, point.Y);
		}

		/** @brief Returns whether the node renders with blending enabled */
		bool isBlendingEnabled() const;
		/** @brief Sets the blending state for node rendering */
		void setBlendingEnabled(bool blendingEnabled);

		/** @brief Returns the source blending factor */
		BlendingFactor srcBlendingFactor() const;
		/** @brief Returns the destination blending factor */
		BlendingFactor destBlendingFactor() const;

		/** @brief Sets source and destination blending factors from a preset */
		void setBlendingPreset(BlendingPreset blendingPreset);
		/** @brief Sets specific source and destination blending factors */
		void setBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor);

		/** @brief Returns the last frame in which any viewport rendered this node (i.e. it was not culled) */
		inline std::uint32_t lastFrameRendered() const {
			return _lastFrameRendered;
		}
		/** @brief Returns the axis-aligned bounding box of the node area in the last frame */
		inline Rectf aabb() const {
			return _aabb;
		}

	protected:
		/** @brief Node width in pixels */
		float _width;
		/** @brief Node height in pixels */
		float _height;

		/** @brief The render command associated with this node */
		RenderCommand _renderCommand;

		/** @brief The last frame in which any viewport rendered this node */
		std::uint32_t _lastFrameRendered;
		/** @brief Axis-aligned bounding box of the node area */
		Rectf _aabb;
		/** @brief Recalculates the axis-aligned bounding box */
		virtual void updateAabb();
		/** @brief Called by each viewport update to refresh this node's culling state */
		void updateCulling();

		/** @brief Protected copy constructor used to clone objects */
		DrawableNode(const DrawableNode& other);

		/** @brief Performs the required tasks upon a change to the shader */
		virtual void shaderHasChanged() = 0;

		/** @brief Updates the render command before the node is drawn */
		virtual void updateRenderCommand() = 0;
	};

}
