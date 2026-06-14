#pragma once

#include "../Base/Object.h"
#include "../Primitives/Vector2.h"
#include "../Primitives/Matrix4x4.h"
#include "../Primitives/Color.h"
#include "../Primitives/Colorf.h"
#include "../Base/BitSet.h"
#include "../CommonConstants.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class RenderQueue;
	class Viewport;

	/**
		@brief Base node of the scene graph transformation hierarchy
		
		Every drawable and grouping object derives from this class. A node owns a relative
		transform (position, anchor, scale, rotation), a color and a rendering layer, and keeps a
		list of child nodes. During a viewport visit the absolute transform is computed from the
		parent's, and the node and its children are drawn in layer and visit order.
	*/
	class SceneNode : public Object
	{
	public:
		/** @brief Whether a node uses its visit order to break ties between same-layer siblings */
		enum class VisitOrderState
		{
			Disabled,
			Enabled,
			SameAsParent
		};

		/** @{ @name Constants */

		/** @brief Minimum amount of rotation that triggers a sine and cosine recalculation */
		static constexpr float MinRotation = 0.5f * fDegToRad;

		/** @} */

		/** @brief Constructs a node with a parent and a relative position given as two coordinates */
		SceneNode(SceneNode* parent, float x, float y);
		/** @brief Constructs a node with a parent and a relative position given as a vector */
		SceneNode(SceneNode* parent, Vector2f position);
		/** @brief Constructs a node with a parent, positioned at the relative origin */
		explicit SceneNode(SceneNode* parent);
		/** @brief Constructs a node with no parent, positioned at the origin */
		SceneNode();
		~SceneNode() override;

		SceneNode& operator=(const SceneNode&) = delete;
		SceneNode(SceneNode&& other) noexcept;
		SceneNode& operator=(SceneNode&& other) noexcept;

		/** @brief Returns a copy of this object */
		inline SceneNode clone() const {
			return SceneNode(*this);
		}

		inline static ObjectType sType() {
			return ObjectType::SceneNode;
		}

		/** @brief Returns the parent node as constant, or `nullptr` if there is none */
		inline const SceneNode* parent() const {
			return parent_;
		}
		/** @brief Returns the parent node, or `nullptr` if there is none */
		inline SceneNode* parent() {
			return parent_;
		}
		/** @brief Sets the parent node */
		bool setParent(SceneNode* parentNode);
		/** @brief Returns the array of child nodes */
		inline const SmallVectorImpl<SceneNode*>& children() {
			return children_;
		}
		/** @brief Returns the array of child nodes as constant */
		const SmallVectorImpl<const SceneNode*>& children() const;
		/** @brief Adds a node as a child of this one */
		bool addChildNode(SceneNode* childNode);
		/** @brief Removes a child of this node without reparenting its children */
		bool removeChildNode(SceneNode* childNode);
		/** @brief Removes the child at the specified index without reparenting its children */
		bool removeChildNodeAt(std::uint32_t index);
		/** @brief Removes all children without reparenting their children */
		bool removeAllChildrenNodes();
		/** @brief Removes a child of this node, reparenting its children to this node */
		bool unlinkChildNode(SceneNode* childNode);

		/** @brief Returns the order index of this node among its siblings, or zero if it has no parent */
		std::uint32_t childOrderIndex() const;
		/** @brief Swaps two children at the specified indices */
		bool swapChildrenNodes(std::uint32_t firstIndex, std::uint32_t secondIndex);
		/** @brief Moves this node one position forward in the parent's list of children */
		bool swapNodeForward();
		/** @brief Moves this node one position back in the parent's list of children */
		bool swapNodeBack();

		/** @brief Returns the node visit order state */
		inline enum VisitOrderState visitOrderState() const {
			return visitOrderState_;
		}
		/** @brief Sets the node visit order state */
		inline void setVisitOrderState(enum VisitOrderState visitOrderState) {
			visitOrderState_ = visitOrderState;
		}
		/** @brief Returns the visit drawing order index of the node */
		inline std::uint16_t visitOrderIndex() const {
			return visitOrderIndex_;
		}

		/** @brief Called every frame to update the node state */
		virtual void OnUpdate(float timeMult);
		/** @brief Updates the absolute transform, draws the node and visits its children */
		virtual void OnVisit(RenderQueue& renderQueue, std::uint32_t& visitOrderIndex);
		/** @brief Called when the node needs to be drawn, returning `true` if a command was added */
		virtual bool OnDraw(RenderQueue& renderQueue) {
			return false;
		}

		/** @brief Returns `true` if node updating is enabled */
		inline bool isUpdateEnabled() const {
			return updateEnabled_;
		}
		/** @brief Enables or disables node updating */
		inline void setUpdateEnabled(bool updateEnabled) {
			updateEnabled_ = updateEnabled;
		}
		/** @brief Returns `true` if node drawing is enabled */
		inline bool isDrawEnabled() const {
			return drawEnabled_;
		}
		/** @brief Enables or disables node drawing */
		inline void setDrawEnabled(bool drawEnabled) {
			drawEnabled_ = drawEnabled;
		}
		/** @brief Returns `true` if the node is both updating and drawing */
		inline bool isEnabled() const {
			return (updateEnabled_ == true && drawEnabled_ == true);
		}
		/** @brief Enables or disables both node updating and drawing */
		void setEnabled(bool isEnabled);

		/** @brief Returns the node position relative to its parent */
		inline Vector2f position() const {
			return position_;
		}
		/** @brief Returns the absolute node position */
		inline Vector2f absPosition() const {
			return absPosition_;
		}
		/** @brief Sets the node position through two coordinates */
		void setPosition(float x, float y);
		/** @brief Sets the node position through a vector */
		void setPosition(Vector2f position);
		/** @brief Sets the X coordinate of the node position */
		void setPositionX(float x);
		/** @brief Sets the Y coordinate of the node position */
		void setPositionY(float y);
		/** @brief Moves the node by two offsets */
		void move(float x, float y);
		/** @brief Moves the node by an offset vector */
		void move(Vector2f position);
		/** @brief Moves the node by an offset on the X axis */
		void moveX(float x);
		/** @brief Moves the node by an offset on the Y axis */
		void moveY(float y);

		/** @brief Returns the transformation anchor point in pixels */
		inline Vector2f absAnchorPoint() const {
			return anchorPoint_;
		}
		/** @brief Sets the transformation anchor point in pixels through two coordinates */
		void setAbsAnchorPoint(float x, float y);
		/** @brief Sets the transformation anchor point in pixels through a `Vector2f` */
		void setAbsAnchorPoint(Vector2f point);

		/** @brief Returns the node scale factors */
		inline const Vector2f& scale() const {
			return scaleFactor_;
		}
		/** @brief Returns the absolute node scale factors */
		inline const Vector2f& absScale() const {
			return absScaleFactor_;
		}
		/** @brief Sets a uniform scale factor for both axes */
		void setScale(float scaleFactor);
		/** @brief Sets the horizontal and vertical scale factors separately */
		void setScale(float scaleFactorX, float scaleFactorY);
		/** @brief Sets the horizontal and vertical scale factors through a `Vector2f` */
		void setScale(Vector2f scaleFactor);

		/** @brief Returns the node rotation in radians */
		inline float rotation() const {
			return rotation_;
		}
		/** @brief Returns the absolute node rotation in radians */
		inline float absRotation() const {
			return absRotation_;
		}
		/** @brief Sets the node rotation in radians */
		void setRotation(float rotation);

		/** @brief Returns the node color */
		inline Colorf color() const {
			return color_;
		}
		/** @brief Returns the absolute node color */
		inline Colorf absColor() const {
			return absColor_;
		}
		/** @brief Sets the node color through a `Color` object */
		void setColor(const Color& color);
		/** @brief Sets the node color through a `Colorf` object */
		void setColor(const Colorf& color);
		/** @brief Returns the node alpha */
		inline float alpha() const {
			return color_.A;
		}
		/** @brief Returns the absolute node alpha */
		inline float absAlpha() const {
			return absColor_.A;
		}
		/** @brief Sets the node alpha through an unsigned char component */
		void setAlpha(std::uint8_t alpha);
		/** @brief Sets the node alpha through a float component */
		void setAlphaF(float alpha);

		/** @brief Returns the node rendering layer */
		inline std::uint16_t layer() const {
			return layer_;
		}
		/**
		 * @brief Returns the absolute node rendering layer
		 *
		 * @note The final layer value is inherited from the parent when the node layer is 0.
		 */
		inline std::uint16_t absLayer() const {
			return absLayer_;
		}
		/**
		 * @brief Sets the node rendering layer
		 *
		 * @note The lowest value (bottom) is 0 and the highest one (top) is 65535. When the value
		 * is 0, the final layer value is inherited from the parent.
		 */
		void setLayer(std::uint16_t layer) {
			layer_ = layer;
		}

		/** @brief Returns the node world matrix */
		inline const Matrix4x4f& worldMatrix() const {
			return worldMatrix_;
		}
		/** @brief Sets the node world matrix (only useful when called inside `OnPostUpdate()`) */
		void setWorldMatrix(const Matrix4x4f& worldMatrix);

		/** @brief Returns the node local matrix */
		inline const Matrix4x4f& localMatrix() const {
			return localMatrix_;
		}
		/** @brief Sets the node local matrix */
		void setLocalMatrix(const Matrix4x4f& localMatrix);

		/**
		 * @brief Returns the delete children on destruction flag
		 *
		 * When the flag is `true` the children are deleted upon node destruction.
		 */
		inline bool deleteChildrenOnDestruction() const {
			return shouldDeleteChildrenOnDestruction_;
		}
		/** @brief Sets the delete children on destruction flag */
		inline void setDeleteChildrenOnDestruction(bool shouldDeleteChildrenOnDestruction) {
			shouldDeleteChildrenOnDestruction_ = shouldDeleteChildrenOnDestruction;
		}

		/** @brief Returns the last frame in which any viewport updated this node */
		inline std::uint32_t lastFrameUpdated() const {
			return lastFrameUpdated_;
		}

	protected:
		/** @brief Bit positions inside the dirty bitset */
		enum DirtyBitPositions
		{
			// Reset by `SceneNode::transform()`
			TransformationBit = 0,
			ColorBit = 1,
			// Reset by `BaseSprite::updateRenderCommand()`
			SizeBit = 2,
			TextureBit = 3,
			// Reset by `DrawableNode::updateCulling()`
			AabbBit = 4,
			// Reset by `DrawableNode::draw()` (for non culled nodes) and by `SceneNode::update()`
			// They are both used for OpenGL GPU uploading.
			TransformationUploadBit = 5,
			ColorUploadBit = 6,
		};

		/** @brief Pointer to the parent node */
		SceneNode* parent_;
		/** @brief Array of child nodes */
		SmallVector<SceneNode*, 0> children_;
		/**
		 * @brief Order index of this node among its siblings
		 *
		 * @note The index is cached here to make sibling reordering methods faster.
		 */
		std::uint32_t childOrderIndex_;

		bool updateEnabled_;
		bool drawEnabled_;
		/**
		 * @brief Whether the visit order resolves the drawing order of same-layer nodes
		 *
		 * @note This flag cannot be changed by the user; it is derived from the parent and this
		 * node's states.
		 */
		bool withVisitOrder_;

		/** @brief Whether the destructor should also delete all children */
		bool shouldDeleteChildrenOnDestruction_;

		/** @brief Visit order state of this node */
		enum VisitOrderState visitOrderState_;
		/** @brief Visit order index of this node */
		std::uint16_t visitOrderIndex_;

		/**
		 * @brief Node rendering layer
		 *
		 * Even if the base scene node is not always drawable, it carries layer information to
		 * easily pass it down to its children.
		 */
		std::uint16_t layer_;

		/** @brief Node position relative to its parent */
		Vector2f position_;
		/**
		 * @brief Anchor point for transformations, in pixels
		 *
		 * @note The default point is the center.
		 */
		Vector2f anchorPoint_;
		/** @brief Horizontal and vertical scale factors for the node size */
		Vector2f scaleFactor_;
		/** @brief Clockwise node rotation in radians */
		float rotation_;

		/**
		 * @brief Node color for transparency and translucency
		 *
		 * Even if the base scene node is not always drawable, it carries color information to
		 * easily pass it down to its children.
		 */
		Colorf color_;

		/** @brief Absolute position as calculated by the `transform()` function */
		Vector2f absPosition_;
		/** @brief Absolute horizontal and vertical scale factors as calculated by the `transform()` function */
		Vector2f absScaleFactor_;
		/** @brief Absolute node rotation as calculated by the `transform()` function */
		float absRotation_;

		/** @brief Absolute node color as calculated by the `transform()` function */
		Colorf absColor_;

		/** @brief Absolute node rendering layer as calculated by the `transform()` function */
		std::uint16_t absLayer_;

		/** @brief Bitset that stores the various dirty state bits */
		BitSet<std::uint8_t> dirtyBits_;

		/** @brief World transformation matrix (calculated from the local and the parent's world matrix) */
		Matrix4x4f worldMatrix_;
		/** @brief Local transformation matrix */
		Matrix4x4f localMatrix_;

		/** @brief Last frame any viewport updated this node */
		std::uint32_t lastFrameUpdated_;

		/** @brief Protected copy constructor used to clone objects */
		SceneNode(const SceneNode& other);

		/** @brief Swaps the child pointer of a parent when moving an object */
		void swapChildPointer(SceneNode* first, SceneNode* second);

		virtual void transform();
	};

	inline const SmallVectorImpl<const SceneNode*>& SceneNode::children() const
	{
		return reinterpret_cast<const SmallVectorImpl<const SceneNode*>&>(children_);
	}

	inline void SceneNode::setEnabled(bool enabled)
	{
		updateEnabled_ = enabled;
		drawEnabled_ = enabled;
	}

	inline void SceneNode::setPosition(float x, float y)
	{
		position_.Set(x, y);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setPosition(Vector2f position)
	{
		position_ = position;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setPositionX(float x)
	{
		position_.X = x;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setPositionY(float y)
	{
		position_.Y = y;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::move(float x, float y)
	{
		position_.X += x;
		position_.Y += y;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::move(Vector2f position)
	{
		position_ += position;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::moveX(float x)
	{
		position_.X += x;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::moveY(float y)
	{
		position_.Y += y;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setAbsAnchorPoint(float x, float y)
	{
		anchorPoint_.Set(x, y);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setAbsAnchorPoint(Vector2f point)
	{
		anchorPoint_ = point;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setScale(float scaleFactor)
	{
		scaleFactor_.Set(scaleFactor, scaleFactor);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setScale(float scaleFactorX, float scaleFactorY)
	{
		scaleFactor_.Set(scaleFactorX, scaleFactorY);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setScale(Vector2f scaleFactor)
	{
		scaleFactor_ = scaleFactor;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setRotation(float rotation)
	{
		rotation_ = fmodf(rotation, fRadAngle360);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setColor(const Color& color)
	{
		color_ = color;
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setColor(const Colorf& color)
	{
		color_ = color;
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setAlpha(std::uint8_t alpha)
	{
		color_.SetAlpha(alpha / 255.0f);
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setAlphaF(float alpha)
	{
		color_.SetAlpha(alpha);
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setWorldMatrix(const Matrix4x4f& worldMatrix)
	{
		worldMatrix_ = worldMatrix;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setLocalMatrix(const Matrix4x4f& localMatrix)
	{
		localMatrix_ = localMatrix;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}
}
