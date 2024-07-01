#include "SceneNode.h"
#include "../Application.h"
#include "../../Common.h"
#include "../tracy.h"

namespace nCine
{
	/*! \param parent The parent can be `nullptr` */
	SceneNode::SceneNode(SceneNode* parent, float x, float y)
		: Object(ObjectType::SceneNode),
		updateEnabled_(true), drawEnabled_(true), parent_(nullptr),
		childOrderIndex_(0), withVisitOrder_(true),
		visitOrderState_(VisitOrderState::SameAsParent), visitOrderIndex_(0),
		position_(x, y), anchorPoint_(0.0f, 0.0f), scaleFactor_(1.0f, 1.0f), rotation_(0.0f),
		color_(Colorf::White), layer_(0), absPosition_(0.0f, 0.0f), absScaleFactor_(1.0f, 1.0f),
		absRotation_(0.0f), absColor_(Colorf::White), absLayer_(0),
		worldMatrix_(Matrix4x4f::Identity), localMatrix_(Matrix4x4f::Identity),
		shouldDeleteChildrenOnDestruction_(true), dirtyBits_(0xFF), lastFrameUpdated_(0)
	{
		setParent(parent);
	}

	/*! \param parent The parent can be `nullptr` */
	SceneNode::SceneNode(SceneNode* parent, const Vector2f& position)
		: SceneNode(parent, position.X, position.Y)
	{
	}

	/*! \param parent The parent can be `nullptr` */
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
		if (shouldDeleteChildrenOnDestruction_) {
			for (SceneNode* child : children_) {
				delete child;
			}
		} else {
			for (SceneNode* child : children_) {
				child->parent_ = nullptr;
			}
		}

		setParent(nullptr);
	}

	SceneNode::SceneNode(SceneNode&& other) noexcept
		: Object(std::move(other)), updateEnabled_(other.updateEnabled_), drawEnabled_(other.drawEnabled_), parent_(other.parent_),
			children_(std::move(other.children_)), visitOrderState_(other.visitOrderState_), position_(other.position_), anchorPoint_(other.anchorPoint_),
			scaleFactor_(other.scaleFactor_), rotation_(other.rotation_), color_(other.color_), layer_(other.layer_),
			shouldDeleteChildrenOnDestruction_(other.shouldDeleteChildrenOnDestruction_), dirtyBits_(other.dirtyBits_), lastFrameUpdated_(other.lastFrameUpdated_)
	{
		swapChildPointer(this, &other);
		for (SceneNode* child : children_) {
			child->parent_ = this;
		}
	}

	SceneNode& SceneNode::operator=(SceneNode&& other) noexcept
	{
		Object::operator=(std::move(other));

		updateEnabled_ = other.updateEnabled_;
		drawEnabled_ = other.drawEnabled_;
		parent_ = other.parent_;
		children_ = std::move(other.children_);
		visitOrderState_ = other.visitOrderState_;
		position_ = other.position_;
		anchorPoint_ = other.anchorPoint_;
		scaleFactor_ = other.scaleFactor_;
		rotation_ = other.rotation_;
		color_ = other.color_;
		layer_ = other.layer_;
		shouldDeleteChildrenOnDestruction_ = other.shouldDeleteChildrenOnDestruction_;
		dirtyBits_ = other.dirtyBits_;
		lastFrameUpdated_ = other.lastFrameUpdated_;

		swapChildPointer(this, &other);
		for (SceneNode* child : children_) {
			child->parent_ = this;
		}
		return *this;
	}

	/*! \return True if the parent has been set */
	bool SceneNode::setParent(SceneNode* parentNode)
	{
		// Can't set yourself or your parent as parent
		if (parentNode == this || parentNode == parent_) {
			return false;
		}

		if (parent_ != nullptr) {
			parent_->removeChildNode(this);
		}
		if (parentNode != nullptr) {
			parentNode->children_.push_back(this);
			childOrderIndex_ = (unsigned int)parentNode->children_.size() - 1;
		}
		parent_ = parentNode;

		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);

		return true;
	}

	/*! \return True if the node has been added */
	bool SceneNode::addChildNode(SceneNode* childNode)
	{
		// Can't add yourself or one of your children as a child
		if (childNode == this || (childNode != nullptr && childNode->parent_ == this)) {
			return false;
		}

		if (childNode->parent_ != nullptr) {
			childNode->parent_->removeChildNode(childNode);
		}
		children_.push_back(childNode);
		childNode->childOrderIndex_ = (unsigned int)children_.size() - 1;
		childNode->parent_ = this;

		return true;
	}

	/*! \return True if the node has been removed */
	bool SceneNode::removeChildNode(SceneNode* childNode)
	{
		// Can't remove yourself or a `nullptr` from your children
		if (childNode == this || childNode == nullptr) {
			return false;
		}

		bool hasBeenRemoved = false;
		if (!children_.empty() &&			// Avoid checking if this node has no children
			childNode->parent_ == this)		// Avoid checking if the child doesn't belong to this node
		{
			for (unsigned int i = 0; i < children_.size(); i++) {
				if (children_[i] == childNode) {
					hasBeenRemoved = removeChildNodeAt(i);
					break;
				}
			}
		}

		return hasBeenRemoved;
	}

	/*! \return True if the node has been removed */
	bool SceneNode::removeChildNodeAt(unsigned int index)
	{
		// Can't remove at an index past the number of children
		if (children_.empty() || index > children_.size() - 1) {
			return false;
		}

		children_[index]->parent_ = nullptr;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
		children_.eraseUnordered(&children_[index]);
		// The last child has been moved to this index position
		if (children_.size() > index)
			children_[index]->childOrderIndex_ = index;
		return true;
	}

	/*! \return True if there were at least one node to remove */
	bool SceneNode::removeAllChildrenNodes()
	{
		if (children_.empty()) {
			return false;
		}

		for (unsigned int i = 0; i < children_.size(); i++) {
			children_[i]->parent_ = nullptr;
			dirtyBits_.set(DirtyBitPositions::TransformationBit);
			dirtyBits_.set(DirtyBitPositions::AabbBit);
		}
		children_.clear();

		return true;
	}

	/*!	\return True if the node has been unlinked */
	bool SceneNode::unlinkChildNode(SceneNode* childNode)
	{
		// Can't unlink yourself or a `nullptr` from your children
		if (childNode == this || childNode == nullptr) {
			return false;
		}

		bool hasBeenUnlinked = false;

		if (!children_.empty() &&			// Avoid checking if this node has no children
			childNode->parent_ == this)		// Avoid checking if the child doesn't belong to this node
		{
			removeChildNode(childNode);

			// Nephews reparenting
			for (SceneNode* child : childNode->children_) {
				addChildNode(child);
			}
			hasBeenUnlinked = true;
		}

		return hasBeenUnlinked;
	}

	/*! \return If the node has no parent then 0 is returned */
	unsigned int SceneNode::childOrderIndex() const
	{
		unsigned int index = 0;
		if (parent_ != nullptr) {
			ASSERT(parent_->children_[childOrderIndex_] == this);
			index = childOrderIndex_;
		}

		return index;
	}

	/*!	\return True if the two nodes have been swapped  */
	bool SceneNode::swapChildrenNodes(unsigned int firstIndex, unsigned int secondIndex)
	{
		// Check if there are at least two children and if the indices are different and valid
		const unsigned int numChildren = (unsigned int)children_.size();
		if (numChildren < 2 || firstIndex == secondIndex ||
			firstIndex > numChildren - 1 || secondIndex > numChildren - 1) {
			return false;
		}

		std::swap(children_[firstIndex], children_[secondIndex]);
		std::swap(children_[firstIndex]->childOrderIndex_, children_[secondIndex]->childOrderIndex_);
		return true;
	}

	/*!	\return True if the node has been brought one position forward */
	bool SceneNode::swapNodeForward()
	{
		if (parent_ == nullptr) {
			return false;
		}

		return parent_->swapChildrenNodes(childOrderIndex_, childOrderIndex_ + 1);
	}

	/*!	\return True if the node has been brought one position back */
	bool SceneNode::swapNodeBack()
	{
		if (parent_ == nullptr || childOrderIndex_ == 0) {
			return false;
		}

		return parent_->swapChildrenNodes(childOrderIndex_, childOrderIndex_ - 1);
	}

	void SceneNode::OnUpdate(float timeMult)
	{
		// Early return not needed, the first call to this method is on the root node

		if (updateEnabled_) {
			transform();

			for (unsigned int i = 0; i < (unsigned int)children_.size(); i++) {
				children_[i]->OnUpdate(timeMult);
			}

			// A non-drawable scenenode does not have the `updateRenderCommand()` method to reset the flags
			if (_type == ObjectType::SceneNode) {
				dirtyBits_.reset(DirtyBitPositions::TransformationBit);
				dirtyBits_.reset(DirtyBitPositions::ColorBit);
			}

			lastFrameUpdated_ = theApplication().GetFrameCount();
		}
	}

	void SceneNode::OnVisit(RenderQueue& renderQueue, unsigned int& visitOrderIndex)
	{
		// Early return not needed, the first call to this method is on the root node

		if (drawEnabled_) {
			// Increment the index without knowing if the node is going to be rendered or not.
			// It avoids both a one frame delay when the value changes and calling `DrawableNode::setVisitOrder()` from this function.
			visitOrderIndex_ = (_type != ObjectType::Particle ? visitOrderIndex + 1 : visitOrderIndex);
			const bool rendered = OnDraw(renderQueue);

			visitOrderIndex_ = visitOrderIndex;
			// Visit order index only incremented for rendered nodes
			// Particles get their index incremented only once by their parent particle system
			const bool incrementIndex = ((rendered && _type != ObjectType::Particle) || _type == ObjectType::ParticleSystem);
			visitOrderIndex_ = incrementIndex ? visitOrderIndex++ : visitOrderIndex;

			for (SceneNode* child : children_) {
				child->OnVisit(renderQueue, visitOrderIndex);
			}
		}
	}

	SceneNode::SceneNode(const SceneNode& other)
		: Object(other), updateEnabled_(other.updateEnabled_), drawEnabled_(other.drawEnabled_), parent_(nullptr), childOrderIndex_(0),
			withVisitOrder_(true), visitOrderState_(other.visitOrderState_), visitOrderIndex_(0), position_(other.position_),
			anchorPoint_(other.anchorPoint_), scaleFactor_(other.scaleFactor_), rotation_(other.rotation_), color_(other.color_),
			layer_(other.layer_), absPosition_(0.0f, 0.0f), absScaleFactor_(1.0f, 1.0f), absRotation_(0.0f), absColor_(Colorf::White),
			absLayer_(0), worldMatrix_(Matrix4x4f::Identity), localMatrix_(Matrix4x4f::Identity),
			shouldDeleteChildrenOnDestruction_(other.shouldDeleteChildrenOnDestruction_), dirtyBits_(0xFF)
	{
		setParent(other.parent_);
	}

	/*! \note It is faster than calling `setParent()` on the first child and `removeChildNode()` on the second one */
	void SceneNode::swapChildPointer(SceneNode* first, SceneNode* second)
	{
		ASSERT(first->parent_ == second->parent_);

		SceneNode* parent = first->parent_;
		if (parent != nullptr) {
			for (unsigned int i = 0; i < parent->children_.size(); i++) {
				if (parent->children_[i] == second) {
					parent->children_[i] = this;
					childOrderIndex_ = i;
					second->parent_ = nullptr;
					break;
				}
			}
		}
	}

	void SceneNode::transform()
	{
		ZoneScopedC(0x81A861);

		if (parent_ != nullptr && layer_ == 0) {
			absLayer_ = parent_->absLayer_;
		} else {
			absLayer_ = layer_;
		}

		switch (visitOrderState_) {
			case VisitOrderState::Enabled: withVisitOrder_ = true; break;
			case VisitOrderState::SameAsParent: withVisitOrder_ = (parent_ == nullptr || parent_->withVisitOrder_); break;
			default: withVisitOrder_ = false; break;
		}

		const bool parentHasDirtyColor = (parent_ != nullptr && parent_->dirtyBits_.test(DirtyBitPositions::ColorBit));
		if (parentHasDirtyColor) {
			dirtyBits_.set(DirtyBitPositions::ColorBit);
		}
		if (dirtyBits_.test(DirtyBitPositions::ColorBit)) {
			absColor_ = (parent_ != nullptr ? color_ * parent_->absColor_ : color_);
		}
		const bool parentHasDirtyTransformation = parent_ && parent_->dirtyBits_.test(DirtyBitPositions::TransformationBit);
		if (parentHasDirtyTransformation) {
			dirtyBits_.set(DirtyBitPositions::TransformationBit);
			dirtyBits_.set(DirtyBitPositions::AabbBit);
		}

		if (!dirtyBits_.test(DirtyBitPositions::TransformationBit)) {
			return;
		}

		// Calculating world and local matrices
		localMatrix_ = Matrix4x4f::Translation(position_.X, position_.Y, 0.0f);
		localMatrix_.RotateZ(rotation_);
		localMatrix_.Scale(scaleFactor_.X, scaleFactor_.Y, 1.0f);
		localMatrix_.Translate(-anchorPoint_.X, -anchorPoint_.Y, 0.0f);

		absScaleFactor_ = scaleFactor_;
		absRotation_ = rotation_;

		if (parent_ != nullptr) {
			worldMatrix_ = parent_->worldMatrix_ * localMatrix_;

			absScaleFactor_ *= parent_->absScaleFactor_;
			absRotation_ += parent_->absRotation_;
		} else {
			worldMatrix_ = localMatrix_;
		}
		absPosition_.X = worldMatrix_[3][0];
		absPosition_.Y = worldMatrix_[3][1];
	}
}
