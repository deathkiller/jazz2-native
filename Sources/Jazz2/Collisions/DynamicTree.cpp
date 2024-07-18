// MIT License
//
// Copyright (c) 2019 Erin Catto
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "DynamicTree.h"

#include <float.h>

namespace Jazz2::Collisions
{
	DynamicTree::DynamicTree()
	{
		_root = NullNode;

		_nodeCapacity = DefaultNodeCapacity;
		_nodeCount = 0;
		_nodes = new TreeNode[_nodeCapacity];

		// Build a linked list for the free list.
		for (std::int32_t i = 0; i < _nodeCapacity - 1; ++i) {
			_nodes[i].Next = i + 1;
			_nodes[i].Height = -1;
		}
		_nodes[_nodeCapacity - 1].Next = NullNode;
		_nodes[_nodeCapacity - 1].Height = -1;
		_freeList = 0;

		_insertionCount = 0;
	}

	DynamicTree::~DynamicTree()
	{
		delete[] _nodes;
	}

	// Allocate a node from the pool. Grow the pool if necessary.
	std::int32_t DynamicTree::AllocateNode()
	{
		// Expand the node pool as needed.
		if (_freeList == NullNode) {
			//b2Assert(m_nodeCount == m_nodeCapacity);

			// The free list is empty. Rebuild a bigger pool.
			TreeNode* oldNodes = _nodes;
			_nodeCapacity *= 2;
			_nodes = new TreeNode[_nodeCapacity];
			std::memcpy(_nodes, oldNodes, _nodeCount * sizeof(TreeNode));
			delete[] oldNodes;

			// Build a linked list for the free list. The parent
			// pointer becomes the "next" pointer.
			for (std::int32_t i = _nodeCount; i < _nodeCapacity - 1; ++i) {
				_nodes[i].Next = i + 1;
				_nodes[i].Height = -1;
			}
			_nodes[_nodeCapacity - 1].Next = NullNode;
			_nodes[_nodeCapacity - 1].Height = -1;
			_freeList = _nodeCount;
		}

		// Peel a node off the free list.
		std::int32_t nodeId = _freeList;
		_freeList = _nodes[nodeId].Next;
		_nodes[nodeId].Parent = NullNode;
		_nodes[nodeId].Child1 = NullNode;
		_nodes[nodeId].Child2 = NullNode;
		_nodes[nodeId].Height = 0;
		_nodes[nodeId].UserData = nullptr;
		_nodes[nodeId].Moved = false;
		++_nodeCount;
		return nodeId;
	}

	// Return a node to the pool.
	void DynamicTree::FreeNode(std::int32_t nodeId)
	{
		//b2Assert(0 <= nodeId && nodeId < m_nodeCapacity);
		//b2Assert(0 < m_nodeCount);
		_nodes[nodeId].Next = _freeList;
		_nodes[nodeId].Height = -1;
		_freeList = nodeId;
		--_nodeCount;
	}

	// Create a proxy in the tree as a leaf node. We return the index
	// of the node instead of a pointer so that we can grow
	// the node pool.
	std::int32_t DynamicTree::CreateProxy(const AABBf& aabb, void* userData)
	{
		std::int32_t proxyId = AllocateNode();

		// Fatten the aabb
		_nodes[proxyId].Aabb.L = aabb.L - AabbExtension;
		_nodes[proxyId].Aabb.T = aabb.T - AabbExtension;
		_nodes[proxyId].Aabb.R = aabb.R + AabbExtension;
		_nodes[proxyId].Aabb.B = aabb.B + AabbExtension;
		_nodes[proxyId].UserData = userData;
		_nodes[proxyId].Height = 0;
		_nodes[proxyId].Moved = true;

		InsertLeaf(proxyId);

		return proxyId;
	}

	void DynamicTree::DestroyProxy(std::int32_t proxyId)
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);
		//b2Assert(m_nodes[proxyId].IsLeaf());

		RemoveLeaf(proxyId);
		FreeNode(proxyId);
	}

	bool DynamicTree::MoveProxy(std::int32_t proxyId, const AABBf& aabb, const Vector2f& displacement)
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);

		//b2Assert(m_nodes[proxyId].IsLeaf());

		// Extend AABB
		AABBf fatAABB;
		fatAABB.L = aabb.L - AabbExtension;
		fatAABB.T = aabb.T - AabbExtension;
		fatAABB.R = aabb.R + AabbExtension;
		fatAABB.B = aabb.B + AabbExtension;

		// Predict AABB movement
		Vector2f d = AabbMultiplier * displacement;

		if (d.X < 0.0f) {
			fatAABB.L += d.X;
		} else {
			fatAABB.R += d.X;
		}

		if (d.Y < 0.0f) {
			fatAABB.T += d.Y;
		} else {
			fatAABB.B += d.Y;
		}

		const AABBf& treeAABB = _nodes[proxyId].Aabb;
		if (treeAABB.Contains(aabb)) {
			// The tree AABB still contains the object, but it might be too large.
			// Perhaps the object was moving fast but has since gone to sleep.
			// The huge AABB is larger than the new fat AABB.
			AABBf hugeAABB;
			hugeAABB.L = fatAABB.L - 4.0f * AabbExtension;
			hugeAABB.T = fatAABB.T - 4.0f * AabbExtension;
			hugeAABB.R = fatAABB.R + 4.0f * AabbExtension;
			hugeAABB.B = fatAABB.B + 4.0f * AabbExtension;

			if (hugeAABB.Contains(treeAABB)) {
				// The tree AABB contains the object AABB and the tree AABB is
				// not too large. No tree update needed.
				return false;
			}

			// Otherwise the tree AABB is huge and needs to be shrunk
		}

		RemoveLeaf(proxyId);

		_nodes[proxyId].Aabb = fatAABB;

		InsertLeaf(proxyId);

		_nodes[proxyId].Moved = true;

		return true;
	}

	void DynamicTree::InsertLeaf(std::int32_t leaf)
	{
		++_insertionCount;

		if (_root == NullNode) {
			_root = leaf;
			_nodes[_root].Parent = NullNode;
			return;
		}

		// Find the best sibling for this node
		AABBf leafAABB = _nodes[leaf].Aabb;
		std::int32_t index = _root;
		while (!_nodes[index].IsLeaf()) {
			std::int32_t child1 = _nodes[index].Child1;
			std::int32_t child2 = _nodes[index].Child2;

			float area = _nodes[index].Aabb.GetPerimeter();

			AABBf combinedAABB = AABBf::Combine(_nodes[index].Aabb, leafAABB);
			float combinedArea = combinedAABB.GetPerimeter();

			// Cost of creating a new parent for this node and the new leaf
			float cost = 2.0f * combinedArea;

			// Minimum cost of pushing the leaf further down the tree
			float inheritanceCost = 2.0f * (combinedArea - area);

			// Cost of descending into child1
			float cost1;
			if (_nodes[child1].IsLeaf()) {
				AABBf aabb = AABBf::Combine(leafAABB, _nodes[child1].Aabb);
				cost1 = aabb.GetPerimeter() + inheritanceCost;
			} else {
				AABBf aabb = AABBf::Combine(leafAABB, _nodes[child1].Aabb);
				float oldArea = _nodes[child1].Aabb.GetPerimeter();
				float newArea = aabb.GetPerimeter();
				cost1 = (newArea - oldArea) + inheritanceCost;
			}

			// Cost of descending into child2
			float cost2;
			if (_nodes[child2].IsLeaf()) {
				AABBf aabb = AABBf::Combine(leafAABB, _nodes[child2].Aabb);
				cost2 = aabb.GetPerimeter() + inheritanceCost;
			} else {
				AABBf aabb = AABBf::Combine(leafAABB, _nodes[child2].Aabb);
				float oldArea = _nodes[child2].Aabb.GetPerimeter();
				float newArea = aabb.GetPerimeter();
				cost2 = newArea - oldArea + inheritanceCost;
			}

			// Descend according to the minimum cost.
			if (cost < cost1 && cost < cost2) {
				break;
			}

			// Descend
			if (cost1 < cost2) {
				index = child1;
			} else {
				index = child2;
			}
		}

		std::int32_t sibling = index;

		// Create a new parent.
		std::int32_t oldParent = _nodes[sibling].Parent;
		std::int32_t newParent = AllocateNode();
		_nodes[newParent].Parent = oldParent;
		_nodes[newParent].UserData = nullptr;
		_nodes[newParent].Aabb = AABBf::Combine(leafAABB, _nodes[sibling].Aabb);
		_nodes[newParent].Height = _nodes[sibling].Height + 1;

		if (oldParent != NullNode) {
			// The sibling was not the root.
			if (_nodes[oldParent].Child1 == sibling) {
				_nodes[oldParent].Child1 = newParent;
			} else {
				_nodes[oldParent].Child2 = newParent;
			}

			_nodes[newParent].Child1 = sibling;
			_nodes[newParent].Child2 = leaf;
			_nodes[sibling].Parent = newParent;
			_nodes[leaf].Parent = newParent;
		} else {
			// The sibling was the root.
			_nodes[newParent].Child1 = sibling;
			_nodes[newParent].Child2 = leaf;
			_nodes[sibling].Parent = newParent;
			_nodes[leaf].Parent = newParent;
			_root = newParent;
		}

		// Walk back up the tree fixing heights and AABBs
		index = _nodes[leaf].Parent;
		while (index != NullNode) {
			index = Balance(index);

			std::int32_t child1 = _nodes[index].Child1;
			std::int32_t child2 = _nodes[index].Child2;

			//b2Assert(child1 != NullNode);
			//b2Assert(child2 != NullNode);

			_nodes[index].Height = 1 + std::max(_nodes[child1].Height, _nodes[child2].Height);
			_nodes[index].Aabb = AABBf::Combine(_nodes[child1].Aabb, _nodes[child2].Aabb);

			index = _nodes[index].Parent;
		}

		//Validate();
	}

	void DynamicTree::RemoveLeaf(std::int32_t leaf)
	{
		if (leaf == _root) {
			_root = NullNode;
			return;
		}

		std::int32_t parent = _nodes[leaf].Parent;
		std::int32_t grandParent = _nodes[parent].Parent;
		std::int32_t sibling;
		if (_nodes[parent].Child1 == leaf) {
			sibling = _nodes[parent].Child2;
		} else {
			sibling = _nodes[parent].Child1;
		}

		if (grandParent != NullNode) {
			// Destroy parent and connect sibling to grandParent.
			if (_nodes[grandParent].Child1 == parent) {
				_nodes[grandParent].Child1 = sibling;
			} else {
				_nodes[grandParent].Child2 = sibling;
			}
			_nodes[sibling].Parent = grandParent;
			FreeNode(parent);

			// Adjust ancestor bounds.
			std::int32_t index = grandParent;
			while (index != NullNode) {
				index = Balance(index);

				std::int32_t child1 = _nodes[index].Child1;
				std::int32_t child2 = _nodes[index].Child2;

				_nodes[index].Aabb = AABBf::Combine(_nodes[child1].Aabb, _nodes[child2].Aabb);
				_nodes[index].Height = 1 + std::max(_nodes[child1].Height, _nodes[child2].Height);

				index = _nodes[index].Parent;
			}
		} else {
			_root = sibling;
			_nodes[sibling].Parent = NullNode;
			FreeNode(parent);
		}

		//Validate();
	}

	// Perform a left or right rotation if node A is imbalanced.
	// Returns the new root index.
	std::int32_t DynamicTree::Balance(std::int32_t iA)
	{
		//b2Assert(iA != NullNode);

		TreeNode* A = &_nodes[iA];
		if (A->IsLeaf() || A->Height < 2) {
			return iA;
		}

		std::int32_t iB = A->Child1;
		std::int32_t iC = A->Child2;
		//b2Assert(0 <= iB && iB < m_nodeCapacity);
		//b2Assert(0 <= iC && iC < m_nodeCapacity);

		TreeNode* B = &_nodes[iB];
		TreeNode* C = &_nodes[iC];

		std::int32_t balance = C->Height - B->Height;

		// Rotate C up
		if (balance > 1) {
			std::int32_t iF = C->Child1;
			std::int32_t iG = C->Child2;
			TreeNode* F = &_nodes[iF];
			TreeNode* G = &_nodes[iG];
			//b2Assert(0 <= iF && iF < m_nodeCapacity);
			//b2Assert(0 <= iG && iG < m_nodeCapacity);

			// Swap A and C
			C->Child1 = iA;
			C->Parent = A->Parent;
			A->Parent = iC;

			// A's old parent should point to C
			if (C->Parent != NullNode) {
				if (_nodes[C->Parent].Child1 == iA) {
					_nodes[C->Parent].Child1 = iC;
				} else {
					//b2Assert(m_nodes[C->parent].child2 == iA);
					_nodes[C->Parent].Child2 = iC;
				}
			} else {
				_root = iC;
			}

			// Rotate
			if (F->Height > G->Height) {
				C->Child2 = iF;
				A->Child2 = iG;
				G->Parent = iA;
				A->Aabb = AABBf::Combine(B->Aabb, G->Aabb);
				C->Aabb = AABBf::Combine(A->Aabb, F->Aabb);

				A->Height = 1 + std::max(B->Height, G->Height);
				C->Height = 1 + std::max(A->Height, F->Height);
			} else {
				C->Child2 = iG;
				A->Child2 = iF;
				F->Parent = iA;
				A->Aabb = AABBf::Combine(B->Aabb, F->Aabb);
				C->Aabb = AABBf::Combine(A->Aabb, G->Aabb);

				A->Height = 1 + std::max(B->Height, F->Height);
				C->Height = 1 + std::max(A->Height, G->Height);
			}

			return iC;
		}

		// Rotate B up
		if (balance < -1) {
			std::int32_t iD = B->Child1;
			std::int32_t iE = B->Child2;
			TreeNode* D = &_nodes[iD];
			TreeNode* E = &_nodes[iE];
			//b2Assert(0 <= iD && iD < m_nodeCapacity);
			//b2Assert(0 <= iE && iE < m_nodeCapacity);

			// Swap A and B
			B->Child1 = iA;
			B->Parent = A->Parent;
			A->Parent = iB;

			// A's old parent should point to B
			if (B->Parent != NullNode) {
				if (_nodes[B->Parent].Child1 == iA) {
					_nodes[B->Parent].Child1 = iB;
				} else {
					//b2Assert(m_nodes[B->parent].child2 == iA);
					_nodes[B->Parent].Child2 = iB;
				}
			} else {
				_root = iB;
			}

			// Rotate
			if (D->Height > E->Height) {
				B->Child2 = iD;
				A->Child1 = iE;
				E->Parent = iA;
				A->Aabb = AABBf::Combine(C->Aabb, E->Aabb);
				B->Aabb = AABBf::Combine(A->Aabb, D->Aabb);

				A->Height = 1 + std::max(C->Height, E->Height);
				B->Height = 1 + std::max(A->Height, D->Height);
			} else {
				B->Child2 = iE;
				A->Child1 = iD;
				D->Parent = iA;
				A->Aabb = AABBf::Combine(C->Aabb, D->Aabb);
				B->Aabb = AABBf::Combine(A->Aabb, E->Aabb);

				A->Height = 1 + std::max(C->Height, D->Height);
				B->Height = 1 + std::max(A->Height, E->Height);
			}

			return iB;
		}

		return iA;
	}

	std::int32_t DynamicTree::GetHeight() const
	{
		if (_root == NullNode) {
			return 0;
		}

		return _nodes[_root].Height;
	}

	//
	float DynamicTree::GetAreaRatio() const
	{
		if (_root == NullNode) {
			return 0.0f;
		}

		const TreeNode* root = &_nodes[_root];
		float rootArea = root->Aabb.GetPerimeter();

		float totalArea = 0.0f;
		for (std::int32_t i = 0; i < _nodeCapacity; ++i) {
			const TreeNode* node = _nodes + i;
			if (node->Height < 0) {
				// Free node in pool
				continue;
			}

			totalArea += node->Aabb.GetPerimeter();
		}

		return totalArea / rootArea;
	}

	// Compute the height of a sub-tree.
	std::int32_t DynamicTree::ComputeHeight(std::int32_t nodeId) const
	{
		//b2Assert(0 <= nodeId && nodeId < m_nodeCapacity);
		TreeNode* node = &_nodes[nodeId];

		if (node->IsLeaf()) {
			return 0;
		}

		std::int32_t height1 = ComputeHeight(node->Child1);
		std::int32_t height2 = ComputeHeight(node->Child2);
		return 1 + std::max(height1, height2);
	}

	std::int32_t DynamicTree::ComputeHeight() const
	{
		std::int32_t height = ComputeHeight(_root);
		return height;
	}

	void DynamicTree::ValidateStructure(std::int32_t index) const
	{
		/*
		if (index == NullNode) {
			return;
		}

		if (index == _root) {
			//b2Assert(_nodes[index].Parent == NullNode);
		}

		const TreeNode* node = &_nodes[index];

		std::int32_t child1 = node->Child1;
		std::int32_t child2 = node->Child2;

		if (node->IsLeaf()) {
			//b2Assert(child1 == NullNode);
			//b2Assert(child2 == NullNode);
			//b2Assert(node->Height == 0);
			return;
		}

		//b2Assert(0 <= child1 && child1 < _nodeCapacity);
		//b2Assert(0 <= child2 && child2 < _nodeCapacity);

		//b2Assert(_nodes[child1].Parent == index);
		//b2Assert(_nodes[child2].Parent == index);

		ValidateStructure(child1);
		ValidateStructure(child2);
		*/
	}

	/*void DynamicTree::ValidateMetrics(std::int32_t index) const
	{
		if (index == NullNode) {
			return;
		}

		const TreeNode* node = &_nodes[index];

		std::int32_t child1 = node->Child1;
		std::int32_t child2 = node->Child2;

		if (node->IsLeaf()) {
			//b2Assert(child1 == NullNode);
			//b2Assert(child2 == NullNode);
			//b2Assert(node->Height == 0);
			return;
		}

		//b2Assert(0 <= child1 && child1 < _nodeCapacity);
		//b2Assert(0 <= child2 && child2 < _nodeCapacity);

		std::int32_t height1 = _nodes[child1].Height;
		std::int32_t height2 = _nodes[child2].Height;
		std::int32_t height = 1 + std::max(height1, height2);
		//b2Assert(node->Height == height);

		//AABBf aabb = AABBf::Combine(_nodes[child1].Aabb, _nodes[child2].Aabb);
		//b2Assert(aabb.lowerBound == node->Aabb.lowerBound);
		//b2Assert(aabb.upperBound == node->Aabb.upperBound);

		ValidateMetrics(child1);
		ValidateMetrics(child2);
	}*/

	void DynamicTree::Validate() const
	{
#if defined(b2DEBUG)
		ValidateStructure(_root);
		//ValidateMetrics(_root);

		std::int32_t freeCount = 0;
		std::int32_t freeIndex = _freeList;
		while (freeIndex != NullNode) {
			//b2Assert(0 <= freeIndex && freeIndex < _nodeCapacity);
			freeIndex = _nodes[freeIndex].next;
			++freeCount;
		}

		//b2Assert(GetHeight() == ComputeHeight());
		//b2Assert(_nodeCount + freeCount == _nodeCapacity);
#endif
	}

	std::int32_t DynamicTree::GetMaxBalance() const
	{
		std::int32_t maxBalance = 0;
		for (std::int32_t i = 0; i < _nodeCapacity; ++i) {
			const TreeNode* node = &_nodes[i];
			if (node->Height <= 1) {
				continue;
			}

			//b2Assert(!node->IsLeaf());

			std::int32_t child1 = node->Child1;
			std::int32_t child2 = node->Child2;
			std::int32_t balance = std::abs(_nodes[child2].Height - _nodes[child1].Height);
			maxBalance = std::max(maxBalance, balance);
		}

		return maxBalance;
	}

	void DynamicTree::RebuildBottomUp()
	{
		std::unique_ptr<std::int32_t[]> nodes = std::make_unique<std::int32_t[]>(_nodeCount);
		std::int32_t count = 0;

		// Build array of leaves. Free the rest.
		for (std::int32_t i = 0; i < _nodeCapacity; ++i) {
			if (_nodes[i].Height < 0) {
				// free node in pool
				continue;
			}

			if (_nodes[i].IsLeaf()) {
				_nodes[i].Parent = NullNode;
				nodes[count] = i;
				++count;
			} else {
				FreeNode(i);
			}
		}

		while (count > 1) {
			float minCost = FLT_MAX;
			std::int32_t iMin = -1, jMin = -1;
			for (std::int32_t i = 0; i < count; ++i) {
				AABBf aabbi = _nodes[nodes[i]].Aabb;

				for (std::int32_t j = i + 1; j < count; ++j) {
					AABBf aabbj = _nodes[nodes[j]].Aabb;
					AABBf b = AABBf::Combine(aabbi, aabbj);
					float cost = b.GetPerimeter();
					if (cost < minCost) {
						iMin = i;
						jMin = j;
						minCost = cost;
					}
				}
			}

			std::int32_t index1 = nodes[iMin];
			std::int32_t index2 = nodes[jMin];
			TreeNode* child1 = &_nodes[index1];
			TreeNode* child2 = &_nodes[index2];

			std::int32_t parentIndex = AllocateNode();
			TreeNode* parent = &_nodes[parentIndex];
			parent->Child1 = index1;
			parent->Child2 = index2;
			parent->Height = 1 + std::max(child1->Height, child2->Height);
			parent->Aabb = AABBf::Combine(child1->Aabb, child2->Aabb);
			parent->Parent = NullNode;

			child1->Parent = parentIndex;
			child2->Parent = parentIndex;

			nodes[jMin] = nodes[count - 1];
			nodes[iMin] = parentIndex;
			--count;
		}

		_root = nodes[0];
		//std::free(nodes);

		Validate();
	}

	void DynamicTree::ShiftOrigin(Vector2f newOrigin)
	{
		// Build array of leaves. Free the rest.
		for (std::int32_t i = 0; i < _nodeCapacity; ++i) {
			_nodes[i].Aabb.L -= newOrigin.X;
			_nodes[i].Aabb.T -= newOrigin.Y;
			_nodes[i].Aabb.R -= newOrigin.X;
			_nodes[i].Aabb.B -= newOrigin.Y;
		}
	}
}