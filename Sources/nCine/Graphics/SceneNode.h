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
		inline SceneNode Clone() const {
			return SceneNode(*this);
		}

		inline static ObjectType sType() {
			return ObjectType::SceneNode;
		}

		/** @brief Returns the parent node as constant, or `nullptr` if there is none */
		inline const SceneNode* GetParent() const {
			return _parent;
		}
		/** @brief Returns the parent node, or `nullptr` if there is none */
		inline SceneNode* GetParent() {
			return _parent;
		}
		/** @brief Sets the parent node */
		bool SetParent(SceneNode* parentNode);
		/** @brief Returns the array of child nodes */
		inline const SmallVectorImpl<SceneNode*>& GetChildren() {
			return _children;
		}
		/** @brief Returns the array of child nodes as constant */
		const SmallVectorImpl<const SceneNode*>& GetChildren() const;
		/** @brief Adds a node as a child of this one */
		bool AddChildNode(SceneNode* childNode);
		/** @brief Removes a child of this node without reparenting its children */
		bool RemoveChildNode(SceneNode* childNode);
		/** @brief Removes the child at the specified index without reparenting its children */
		bool RemoveChildNodeAt(std::uint32_t index);
		/** @brief Removes all children without reparenting their children */
		bool RemoveAllChildrenNodes();
		/** @brief Removes a child of this node, reparenting its children to this node */
		bool UnlinkChildNode(SceneNode* childNode);

		/** @brief Returns the order index of this node among its siblings, or zero if it has no parent */
		std::uint32_t GetChildOrderIndex() const;
		/** @brief Swaps two children at the specified indices */
		bool SwapChildrenNodes(std::uint32_t firstIndex, std::uint32_t secondIndex);
		/** @brief Moves this node one position forward in the parent's list of children */
		bool SwapNodeForward();
		/** @brief Moves this node one position back in the parent's list of children */
		bool SwapNodeBack();

		/** @brief Returns the node visit order state */
		inline enum VisitOrderState GetVisitOrderState() const {
			return _visitOrderState;
		}
		/** @brief Sets the node visit order state */
		inline void SetVisitOrderState(enum VisitOrderState visitOrderState) {
			_visitOrderState = visitOrderState;
		}
		/** @brief Returns the visit drawing order index of the node */
		inline std::uint16_t GetVisitOrderIndex() const {
			return _visitOrderIndex;
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
		inline bool IsUpdateEnabled() const {
			return _updateEnabled;
		}
		/** @brief Enables or disables node updating */
		inline void SetUpdateEnabled(bool updateEnabled) {
			_updateEnabled = updateEnabled;
		}
		/** @brief Returns `true` if node drawing is enabled */
		inline bool IsDrawEnabled() const {
			return _drawEnabled;
		}
		/** @brief Enables or disables node drawing */
		inline void SetDrawEnabled(bool drawEnabled) {
			_drawEnabled = drawEnabled;
		}
		/** @brief Returns `true` if the node is both updating and drawing */
		inline bool IsEnabled() const {
			return (_updateEnabled == true && _drawEnabled == true);
		}
		/** @brief Enables or disables both node updating and drawing */
		void SetEnabled(bool isEnabled);

		/** @brief Returns the node position relative to its parent */
		inline Vector2f GetPosition() const {
			return _position;
		}
		/** @brief Returns the absolute node position */
		inline Vector2f GetAbsPosition() const {
			return _absPosition;
		}
		/** @brief Sets the node position through two coordinates */
		void SetPosition(float x, float y);
		/** @brief Sets the node position through a vector */
		void SetPosition(Vector2f position);
		/** @brief Sets the X coordinate of the node position */
		void SetPositionX(float x);
		/** @brief Sets the Y coordinate of the node position */
		void SetPositionY(float y);
		/** @brief Moves the node by two offsets */
		void Move(float x, float y);
		/** @brief Moves the node by an offset vector */
		void Move(Vector2f position);
		/** @brief Moves the node by an offset on the X axis */
		void MoveX(float x);
		/** @brief Moves the node by an offset on the Y axis */
		void MoveY(float y);

		/** @brief Returns the transformation anchor point in pixels */
		inline Vector2f GetAbsAnchorPoint() const {
			return _anchorPoint;
		}
		/** @brief Sets the transformation anchor point in pixels through two coordinates */
		void SetAbsAnchorPoint(float x, float y);
		/** @brief Sets the transformation anchor point in pixels through a `Vector2f` */
		void SetAbsAnchorPoint(Vector2f point);

		/** @brief Returns the node scale factors */
		inline const Vector2f& GetScale() const {
			return _scaleFactor;
		}
		/** @brief Returns the absolute node scale factors */
		inline const Vector2f& GetAbsScale() const {
			return _absScaleFactor;
		}
		/** @brief Sets a uniform scale factor for both axes */
		void SetScale(float scaleFactor);
		/** @brief Sets the horizontal and vertical scale factors separately */
		void SetScale(float scaleFactorX, float scaleFactorY);
		/** @brief Sets the horizontal and vertical scale factors through a `Vector2f` */
		void SetScale(Vector2f scaleFactor);

		/** @brief Returns the node rotation in radians */
		inline float GetRotation() const {
			return _rotation;
		}
		/** @brief Returns the absolute node rotation in radians */
		inline float GetAbsRotation() const {
			return _absRotation;
		}
		/** @brief Sets the node rotation in radians */
		void SetRotation(float rotation);

		/** @brief Returns the node color */
		inline Colorf GetColor() const {
			return _color;
		}
		/** @brief Returns the absolute node color */
		inline Colorf GetAbsColor() const {
			return _absColor;
		}
		/** @brief Sets the node color through a `Color` object */
		void SetColor(const Color& color);
		/** @brief Sets the node color through a `Colorf` object */
		void SetColor(const Colorf& color);
		/** @brief Returns the node alpha */
		inline float GetAlpha() const {
			return _color.A;
		}
		/** @brief Returns the absolute node alpha */
		inline float GetAbsAlpha() const {
			return _absColor.A;
		}
		/** @brief Sets the node alpha through an unsigned char component */
		void SetAlpha(std::uint8_t alpha);
		/** @brief Sets the node alpha through a float component */
		void SetAlphaF(float alpha);

		/** @brief Returns the node rendering layer */
		inline std::uint16_t GetLayer() const {
			return _layer;
		}
		/**
		 * @brief Returns the absolute node rendering layer
		 *
		 * @note The final layer value is inherited from the parent when the node layer is 0.
		 */
		inline std::uint16_t GetAbsLayer() const {
			return _absLayer;
		}
		/**
		 * @brief Sets the node rendering layer
		 *
		 * @note The lowest value (bottom) is 0 and the highest one (top) is 65535. When the value
		 * is 0, the final layer value is inherited from the parent.
		 */
		void SetLayer(std::uint16_t layer) {
			_layer = layer;
		}

		/** @brief Returns the node world matrix */
		inline const Matrix4x4f& GetWorldMatrix() const {
			return _worldMatrix;
		}
		/** @brief Sets the node world matrix (only useful when called inside `OnPostUpdate()`) */
		void SetWorldMatrix(const Matrix4x4f& worldMatrix);

		/** @brief Returns the node local matrix */
		inline const Matrix4x4f& GetLocalMatrix() const {
			return _localMatrix;
		}
		/** @brief Sets the node local matrix */
		void SetLocalMatrix(const Matrix4x4f& localMatrix);

		/**
		 * @brief Returns the delete children on destruction flag
		 *
		 * When the flag is `true` the children are deleted upon node destruction.
		 */
		inline bool DeleteChildrenOnDestruction() const {
			return _shouldDeleteChildrenOnDestruction;
		}
		/** @brief Sets the delete children on destruction flag */
		inline void SetDeleteChildrenOnDestruction(bool shouldDeleteChildrenOnDestruction) {
			_shouldDeleteChildrenOnDestruction = shouldDeleteChildrenOnDestruction;
		}

		/** @brief Returns the last frame in which any viewport updated this node */
		inline std::uint32_t GetLastFrameUpdated() const {
			return _lastFrameUpdated;
		}

	protected:
		/** @brief Bit positions inside the dirty bitset */
		enum DirtyBitPositions
		{
			// Reset by `SceneNode::Transform()`
			TransformationBit = 0,
			ColorBit = 1,
			// Reset by `BaseSprite::UpdateRenderCommand()`
			SizeBit = 2,
			TextureBit = 3,
			// Reset by `DrawableNode::UpdateCulling()`
			AabbBit = 4,
			// Reset by `DrawableNode::draw()` (for non culled nodes) and by `SceneNode::Update()`
			// They are both used for OpenGL GPU uploading.
			TransformationUploadBit = 5,
			ColorUploadBit = 6,
		};

		/** @brief Pointer to the parent node */
		SceneNode* _parent;
		/** @brief Array of child nodes */
		SmallVector<SceneNode*, 0> _children;
		/**
		 * @brief Order index of this node among its siblings
		 *
		 * @note The index is cached here to make sibling reordering methods faster.
		 */
		std::uint32_t _childOrderIndex;

		bool _updateEnabled;
		bool _drawEnabled;
		/**
		 * @brief Whether the visit order resolves the drawing order of same-layer nodes
		 *
		 * @note This flag cannot be changed by the user; it is derived from the parent and this
		 * node's states.
		 */
		bool _withVisitOrder;

		/** @brief Whether the destructor should also delete all children */
		bool _shouldDeleteChildrenOnDestruction;

		/** @brief Visit order state of this node */
		enum VisitOrderState _visitOrderState;
		/** @brief Visit order index of this node */
		std::uint16_t _visitOrderIndex;

		/**
		 * @brief Node rendering layer
		 *
		 * Even if the base scene node is not always drawable, it carries layer information to
		 * easily pass it down to its children.
		 */
		std::uint16_t _layer;

		/** @brief Node position relative to its parent */
		Vector2f _position;
		/**
		 * @brief Anchor point for transformations, in pixels
		 *
		 * @note The default point is the center.
		 */
		Vector2f _anchorPoint;
		/** @brief Horizontal and vertical scale factors for the node size */
		Vector2f _scaleFactor;
		/** @brief Clockwise node rotation in radians */
		float _rotation;

		/**
		 * @brief Node color for transparency and translucency
		 *
		 * Even if the base scene node is not always drawable, it carries color information to
		 * easily pass it down to its children.
		 */
		Colorf _color;

		/** @brief Absolute position as calculated by the `Transform()` function */
		Vector2f _absPosition;
		/** @brief Absolute horizontal and vertical scale factors as calculated by the `Transform()` function */
		Vector2f _absScaleFactor;
		/** @brief Absolute node rotation as calculated by the `Transform()` function */
		float _absRotation;

		/** @brief Absolute node color as calculated by the `Transform()` function */
		Colorf _absColor;

		/** @brief Absolute node rendering layer as calculated by the `Transform()` function */
		std::uint16_t _absLayer;

		/** @brief Bitset that stores the various dirty state bits */
		BitSet<std::uint8_t> _dirtyBits;

		/** @brief World transformation matrix (calculated from the local and the parent's world matrix) */
		Matrix4x4f _worldMatrix;
		/** @brief Local transformation matrix */
		Matrix4x4f _localMatrix;

		/** @brief Last frame any viewport updated this node */
		std::uint32_t _lastFrameUpdated;

		/** @brief Protected copy constructor used to clone objects */
		SceneNode(const SceneNode& other);

		/** @brief Swaps the child pointer of a parent when moving an object */
		void SwapChildPointer(SceneNode* first, SceneNode* second);

		virtual void Transform();
	};

	inline const SmallVectorImpl<const SceneNode*>& SceneNode::GetChildren() const
	{
		return reinterpret_cast<const SmallVectorImpl<const SceneNode*>&>(_children);
	}

	inline void SceneNode::SetEnabled(bool enabled)
	{
		_updateEnabled = enabled;
		_drawEnabled = enabled;
	}

	inline void SceneNode::SetPosition(float x, float y)
	{
		_position.Set(x, y);
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetPosition(Vector2f position)
	{
		_position = position;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetPositionX(float x)
	{
		_position.X = x;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetPositionY(float y)
	{
		_position.Y = y;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::Move(float x, float y)
	{
		_position.X += x;
		_position.Y += y;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::Move(Vector2f position)
	{
		_position += position;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::MoveX(float x)
	{
		_position.X += x;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::MoveY(float y)
	{
		_position.Y += y;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetAbsAnchorPoint(float x, float y)
	{
		_anchorPoint.Set(x, y);
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetAbsAnchorPoint(Vector2f point)
	{
		_anchorPoint = point;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetScale(float scaleFactor)
	{
		_scaleFactor.Set(scaleFactor, scaleFactor);
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetScale(float scaleFactorX, float scaleFactorY)
	{
		_scaleFactor.Set(scaleFactorX, scaleFactorY);
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetScale(Vector2f scaleFactor)
	{
		_scaleFactor = scaleFactor;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetRotation(float rotation)
	{
		// Kept within one turn with the sign of the input, exactly what `fmodf(rotation, fRadAngle360)` gave,
		// without the call: newlib's fmodf is a 170-instruction bit-by-bit division loop on every console,
		// and this runs for every rotating shot and piece of debris every frame. Almost every value is
		// already inside the turn, so the common path is the two compares.
		if (rotation >= fRadAngle360 || rotation <= -fRadAngle360) {
			rotation -= fRadAngle360 * (float)(std::int32_t)(rotation * (1.0f / fRadAngle360));
			// The quotient above is rounded, so for a large input the result can land a hair outside the turn
			if (rotation >= fRadAngle360) {
				rotation -= fRadAngle360;
			} else if (rotation <= -fRadAngle360) {
				rotation += fRadAngle360;
			}
		}
		_rotation = rotation;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetColor(const Color& color)
	{
		_color = color;
		_dirtyBits.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::SetColor(const Colorf& color)
	{
		_color = color;
		_dirtyBits.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::SetAlpha(std::uint8_t alpha)
	{
		_color.SetAlpha(alpha / 255.0f);
		_dirtyBits.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::SetAlphaF(float alpha)
	{
		_color.SetAlpha(alpha);
		_dirtyBits.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::SetWorldMatrix(const Matrix4x4f& worldMatrix)
	{
		_worldMatrix = worldMatrix;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::SetLocalMatrix(const Matrix4x4f& localMatrix)
	{
		_localMatrix = localMatrix;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}
}
