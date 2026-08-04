#include "SceneNode.h"
#include "../Application.h"
#include "../../Main.h"
#include "../tracy.h"

namespace nCine
{
	/** @param parent The parent can be `nullptr` */
	SceneNode::SceneNode(SceneNode* parent, float x, float y)
		: Object(ObjectType::SceneNode),
		_updateEnabled(true), _drawEnabled(true), _parent(nullptr),
		_childOrderIndex(0), _withVisitOrder(true),
		_visitOrderState(VisitOrderState::SameAsParent), _visitOrderIndex(0),
		_position(x, y), _anchorPoint(0.0f, 0.0f), _scaleFactor(1.0f, 1.0f), _rotation(0.0f),
		_color(Colorf::White), _layer(0), _absPosition(0.0f, 0.0f), _absScaleFactor(1.0f, 1.0f),
		_absRotation(0.0f), _absColor(Colorf::White), _absLayer(0),
		_worldMatrix(Matrix4x4f::Identity), _localMatrix(Matrix4x4f::Identity),
		_shouldDeleteChildrenOnDestruction(true), _dirtyBits(0xFF), _lastFrameUpdated(0)
	{
		setParent(parent);
	}

	/** @param parent The parent can be `nullptr` */
	SceneNode::SceneNode(SceneNode* parent, Vector2f position)
		: SceneNode(parent, position.X, position.Y)
	{
	}

	/** @param parent The parent can be `nullptr` */
	SceneNode::SceneNode(SceneNode* parent)
		: SceneNode(parent, 0.0f, 0.0f)
	{
	}

	SceneNode::SceneNode()
		: SceneNode(nullptr, 0.0f, 0.0f)
	{
	}

	SceneNode::~SceneNode()
	{
		if (_shouldDeleteChildrenOnDestruction) {
			for (SceneNode* child : _children) {
				delete child;
			}
		} else {
			for (SceneNode* child : _children) {
				child->_parent = nullptr;
			}
		}

		setParent(nullptr);
	}

	SceneNode::SceneNode(SceneNode&& other) noexcept
		: Object(std::move(other)), _updateEnabled(other._updateEnabled), _drawEnabled(other._drawEnabled), _parent(other._parent),
			_children(std::move(other._children)), _visitOrderState(other._visitOrderState), _position(other._position), _anchorPoint(other._anchorPoint),
			_scaleFactor(other._scaleFactor), _rotation(other._rotation), _color(other._color), _layer(other._layer),
			_shouldDeleteChildrenOnDestruction(other._shouldDeleteChildrenOnDestruction), _dirtyBits(other._dirtyBits), _lastFrameUpdated(other._lastFrameUpdated)
	{
		swapChildPointer(this, &other);
		for (SceneNode* child : _children) {
			child->_parent = this;
		}
	}

	SceneNode& SceneNode::operator=(SceneNode&& other) noexcept
	{
		Object::operator=(std::move(other));

		_updateEnabled = other._updateEnabled;
		_drawEnabled = other._drawEnabled;
		_parent = other._parent;
		_children = std::move(other._children);
		_visitOrderState = other._visitOrderState;
		_position = other._position;
		_anchorPoint = other._anchorPoint;
		_scaleFactor = other._scaleFactor;
		_rotation = other._rotation;
		_color = other._color;
		_layer = other._layer;
		_shouldDeleteChildrenOnDestruction = other._shouldDeleteChildrenOnDestruction;
		_dirtyBits = other._dirtyBits;
		_lastFrameUpdated = other._lastFrameUpdated;

		swapChildPointer(this, &other);
		for (SceneNode* child : _children) {
			child->_parent = this;
		}
		return *this;
	}

	/** @return `true` if the parent has been set */
	bool SceneNode::setParent(SceneNode* parentNode)
	{
		// Can't set yourself or your parent as parent
		if (parentNode == this || parentNode == _parent) {
			return false;
		}

		if (_parent != nullptr) {
			_parent->removeChildNode(this);
		}
		if (parentNode != nullptr) {
			parentNode->_children.push_back(this);
			_childOrderIndex = (unsigned int)parentNode->_children.size() - 1;
		}
		_parent = parentNode;

		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);

		return true;
	}

	/** @return `true` if the node has been added */
	bool SceneNode::addChildNode(SceneNode* childNode)
	{
		// Can't add yourself or one of your children as a child
		if (childNode == this || (childNode != nullptr && childNode->_parent == this)) {
			return false;
		}

		if (childNode->_parent != nullptr) {
			childNode->_parent->removeChildNode(childNode);
		}
		_children.push_back(childNode);
		childNode->_childOrderIndex = (unsigned int)_children.size() - 1;
		childNode->_parent = this;

		return true;
	}

	/** @return `true` if the node has been removed */
	bool SceneNode::removeChildNode(SceneNode* childNode)
	{
		// Can't remove yourself or a `nullptr` from your children
		if (childNode == this || childNode == nullptr) {
			return false;
		}

		bool hasBeenRemoved = false;
		if (!_children.empty() &&			// Avoid checking if this node has no children
			childNode->_parent == this)		// Avoid checking if the child doesn't belong to this node
		{
			for (unsigned int i = 0; i < _children.size(); i++) {
				if (_children[i] == childNode) {
					hasBeenRemoved = removeChildNodeAt(i);
					break;
				}
			}
		}

		return hasBeenRemoved;
	}

	/** @return `true` if the node has been removed */
	bool SceneNode::removeChildNodeAt(std::uint32_t index)
	{
		// Can't remove at an index past the number of children
		if (_children.empty() || index > _children.size() - 1) {
			return false;
		}

		_children[index]->_parent = nullptr;
		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
		_children.eraseUnordered(&_children[index]);
		// The last child has been moved to this index position
		if (_children.size() > index)
			_children[index]->_childOrderIndex = index;
		return true;
	}

	/** @return `true` if there was at least one node to remove */
	bool SceneNode::removeAllChildrenNodes()
	{
		if (_children.empty()) {
			return false;
		}

		for (unsigned int i = 0; i < _children.size(); i++) {
			_children[i]->_parent = nullptr;
			_dirtyBits.set(DirtyBitPositions::TransformationBit);
			_dirtyBits.set(DirtyBitPositions::AabbBit);
		}
		_children.clear();

		return true;
	}

	/** @return `true` if the node has been unlinked */
	bool SceneNode::unlinkChildNode(SceneNode* childNode)
	{
		// Can't unlink yourself or a `nullptr` from your children
		if (childNode == this || childNode == nullptr) {
			return false;
		}

		bool hasBeenUnlinked = false;

		if (!_children.empty() &&			// Avoid checking if this node has no children
			childNode->_parent == this)		// Avoid checking if the child doesn't belong to this node
		{
			removeChildNode(childNode);

			// Nephews reparenting
			for (SceneNode* child : childNode->_children) {
				addChildNode(child);
			}
			hasBeenUnlinked = true;
		}

		return hasBeenUnlinked;
	}

	/** @return The order index among the siblings, or 0 if the node has no parent */
	std::uint32_t SceneNode::childOrderIndex() const
	{
		std::uint32_t index = 0;
		if (_parent != nullptr) {
			DEATH_ASSERT(_parent->_children[_childOrderIndex] == this);
			index = _childOrderIndex;
		}

		return index;
	}

	/** @return `true` if the two nodes have been swapped */
	bool SceneNode::swapChildrenNodes(std::uint32_t firstIndex, std::uint32_t secondIndex)
	{
		// Check if there are at least two children and if the indices are different and valid
		const std::uint32_t numChildren = std::uint32_t(_children.size());
		if (numChildren < 2 || firstIndex == secondIndex ||
			firstIndex > numChildren - 1 || secondIndex > numChildren - 1) {
			return false;
		}

		std::swap(_children[firstIndex], _children[secondIndex]);
		std::swap(_children[firstIndex]->_childOrderIndex, _children[secondIndex]->_childOrderIndex);
		return true;
	}

	/** @return `true` if the node has been moved one position forward */
	bool SceneNode::swapNodeForward()
	{
		if (_parent == nullptr) {
			return false;
		}

		return _parent->swapChildrenNodes(_childOrderIndex, _childOrderIndex + 1);
	}

	/** @return `true` if the node has been moved one position back */
	bool SceneNode::swapNodeBack()
	{
		if (_parent == nullptr || _childOrderIndex == 0) {
			return false;
		}

		return _parent->swapChildrenNodes(_childOrderIndex, _childOrderIndex - 1);
	}

	void SceneNode::OnUpdate(float timeMult)
	{
		// Early return not needed, the first call to this method is on the root node

		if (_updateEnabled) {
			transform();

			for (unsigned int i = 0; i < (unsigned int)_children.size(); i++) {
				_children[i]->OnUpdate(timeMult);
			}

			_dirtyBits.reset(DirtyBitPositions::TransformationBit);
			_dirtyBits.reset(DirtyBitPositions::ColorBit);

			// A non-drawable scenenode does not have the `updateRenderCommand()` method to reset the flags
			if (_type == ObjectType::SceneNode || _type == ObjectType::ParticleSystem) {
				_dirtyBits.reset(DirtyBitPositions::TransformationUploadBit);
				_dirtyBits.reset(DirtyBitPositions::ColorUploadBit);
			}

			_lastFrameUpdated = theApplication().GetFrameCount();
		}
	}

	void SceneNode::OnVisit(RenderQueue& renderQueue, std::uint32_t& visitOrderIndex)
	{
		// Early return not needed, the first call to this method is on the root node

		if (_drawEnabled) {
			// Increment the index without knowing if the node is going to be rendered or not.
			// It avoids both a one frame delay when the value changes and calling `DrawableNode::setVisitOrder()` from this function.
			_visitOrderIndex = (_type != ObjectType::Particle ? visitOrderIndex + 1 : visitOrderIndex);
			const bool rendered = OnDraw(renderQueue);

			_visitOrderIndex = visitOrderIndex;
			// Visit order index only incremented for rendered nodes
			// Particles get their index incremented only once by their parent particle system
			const bool incrementIndex = ((rendered && _type != ObjectType::Particle) || _type == ObjectType::ParticleSystem);
			_visitOrderIndex = incrementIndex ? visitOrderIndex++ : visitOrderIndex;

			for (SceneNode* child : _children) {
				child->OnVisit(renderQueue, visitOrderIndex);
			}
		}
	}

	SceneNode::SceneNode(const SceneNode& other)
		: Object(other), _updateEnabled(other._updateEnabled), _drawEnabled(other._drawEnabled), _parent(nullptr), _childOrderIndex(0),
			_withVisitOrder(true), _visitOrderState(other._visitOrderState), _visitOrderIndex(0), _position(other._position),
			_anchorPoint(other._anchorPoint), _scaleFactor(other._scaleFactor), _rotation(other._rotation), _color(other._color),
			_layer(other._layer), _absPosition(0.0f, 0.0f), _absScaleFactor(1.0f, 1.0f), _absRotation(0.0f), _absColor(Colorf::White),
			_absLayer(0), _worldMatrix(Matrix4x4f::Identity), _localMatrix(Matrix4x4f::Identity),
			_shouldDeleteChildrenOnDestruction(other._shouldDeleteChildrenOnDestruction), _dirtyBits(0xFF)
	{
		setParent(other._parent);
	}

	/** @note Faster than calling `setParent()` on the first child and `removeChildNode()` on the second one */
	void SceneNode::swapChildPointer(SceneNode* first, SceneNode* second)
	{
		DEATH_ASSERT(first->_parent == second->_parent);

		SceneNode* parent = first->_parent;
		if (parent != nullptr) {
			for (unsigned int i = 0; i < parent->_children.size(); i++) {
				if (parent->_children[i] == second) {
					parent->_children[i] = this;
					_childOrderIndex = i;
					second->_parent = nullptr;
					break;
				}
			}
		}
	}

	void SceneNode::transform()
	{
		ZoneScopedC(0x81A861);

		if (_parent != nullptr && _layer == 0) {
			_absLayer = _parent->_absLayer;
		} else {
			_absLayer = _layer;
		}

		switch (_visitOrderState) {
			case VisitOrderState::Enabled: _withVisitOrder = true; break;
			case VisitOrderState::SameAsParent: _withVisitOrder = (_parent == nullptr || _parent->_withVisitOrder); break;
			default: _withVisitOrder = false; break;
		}

		const bool parentHasDirtyColor = (_parent != nullptr && _parent->_dirtyBits.test(DirtyBitPositions::ColorBit));
		if (parentHasDirtyColor) {
			_dirtyBits.set(DirtyBitPositions::ColorBit);
		}
		if (_dirtyBits.test(DirtyBitPositions::ColorBit)) {
			_absColor = (_parent != nullptr ? _color * _parent->_absColor : _color);
			_dirtyBits.set(DirtyBitPositions::ColorUploadBit);
		}
		const bool parentHasDirtyTransformation = _parent && _parent->_dirtyBits.test(DirtyBitPositions::TransformationBit);
		if (parentHasDirtyTransformation) {
			_dirtyBits.set(DirtyBitPositions::TransformationBit);
			_dirtyBits.set(DirtyBitPositions::AabbBit);
		}

		if (_dirtyBits.test(DirtyBitPositions::TransformationBit)) {
			// Calculating world and local matrices, the local matrix is equivalent to
			// Translation(position) * RotateZ(rotation) * Scale(scale) * Translation(-anchor)
			float c = 1.0f, s = 0.0f;
			if (_rotation != 0.0f) {
				c = cosf(_rotation);
				s = sinf(_rotation);
			}
			const float m00 = c * _scaleFactor.X;
			const float m01 = s * _scaleFactor.X;
			const float m10 = -s * _scaleFactor.Y;
			const float m11 = c * _scaleFactor.Y;
			const float tx = _position.X - _anchorPoint.X * m00 - _anchorPoint.Y * m10;
			const float ty = _position.Y - _anchorPoint.X * m01 - _anchorPoint.Y * m11;

			_localMatrix[0].Set(m00, m01, 0.0f, 0.0f);
			_localMatrix[1].Set(m10, m11, 0.0f, 0.0f);
			_localMatrix[2].Set(0.0f, 0.0f, 1.0f, 0.0f);
			_localMatrix[3].Set(tx, ty, 0.0f, 1.0f);

			_absScaleFactor = _scaleFactor;
			_absRotation = _rotation;

			if (_parent != nullptr) {
				// Equivalent to _parent->_worldMatrix * _localMatrix, but the two zero columns of the local matrix are skipped
				const Matrix4x4f& pm = _parent->_worldMatrix;
				_worldMatrix[0] = pm[0] * m00 + pm[1] * m01;
				_worldMatrix[1] = pm[0] * m10 + pm[1] * m11;
				_worldMatrix[2] = pm[2];
				_worldMatrix[3] = pm[0] * tx + pm[1] * ty + pm[3];

				_absScaleFactor *= _parent->_absScaleFactor;
				_absRotation += _parent->_absRotation;
			} else {
				_worldMatrix = _localMatrix;
			}
			_absPosition.X = _worldMatrix[3][0];
			_absPosition.Y = _worldMatrix[3][1];

			_dirtyBits.set(DirtyBitPositions::TransformationUploadBit);
		}
	}
}
