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
		m_root = NullNode;

		m_nodeCapacity = 16;
		m_nodeCount = 0;
		m_nodes = (TreeNode*)malloc(m_nodeCapacity * sizeof(TreeNode));
		memset(m_nodes, 0, m_nodeCapacity * sizeof(TreeNode));

		// Build a linked list for the free list.
		for (int32_t i = 0; i < m_nodeCapacity - 1; ++i) {
			m_nodes[i].next = i + 1;
			m_nodes[i].height = -1;
		}
		m_nodes[m_nodeCapacity - 1].next = NullNode;
		m_nodes[m_nodeCapacity - 1].height = -1;
		m_freeList = 0;

		m_insertionCount = 0;
	}

	DynamicTree::~DynamicTree()
	{
		// This frees the entire tree in one shot.
		free(m_nodes);
	}

	// Allocate a node from the pool. Grow the pool if necessary.
	int32_t DynamicTree::AllocateNode()
	{
		// Expand the node pool as needed.
		if (m_freeList == NullNode) {
			//b2Assert(m_nodeCount == m_nodeCapacity);

			// The free list is empty. Rebuild a bigger pool.
			TreeNode* oldNodes = m_nodes;
			m_nodeCapacity *= 2;
			m_nodes = (TreeNode*)malloc(m_nodeCapacity * sizeof(TreeNode));
			memcpy(m_nodes, oldNodes, m_nodeCount * sizeof(TreeNode));
			free(oldNodes);

			// Build a linked list for the free list. The parent
			// pointer becomes the "next" pointer.
			for (int32_t i = m_nodeCount; i < m_nodeCapacity - 1; ++i) {
				m_nodes[i].next = i + 1;
				m_nodes[i].height = -1;
			}
			m_nodes[m_nodeCapacity - 1].next = NullNode;
			m_nodes[m_nodeCapacity - 1].height = -1;
			m_freeList = m_nodeCount;
		}

		// Peel a node off the free list.
		int32_t nodeId = m_freeList;
		m_freeList = m_nodes[nodeId].next;
		m_nodes[nodeId].parent = NullNode;
		m_nodes[nodeId].child1 = NullNode;
		m_nodes[nodeId].child2 = NullNode;
		m_nodes[nodeId].height = 0;
		m_nodes[nodeId].userData = nullptr;
		m_nodes[nodeId].moved = false;
		++m_nodeCount;
		return nodeId;
	}

	// Return a node to the pool.
	void DynamicTree::FreeNode(int32_t nodeId)
	{
		//b2Assert(0 <= nodeId && nodeId < m_nodeCapacity);
		//b2Assert(0 < m_nodeCount);
		m_nodes[nodeId].next = m_freeList;
		m_nodes[nodeId].height = -1;
		m_freeList = nodeId;
		--m_nodeCount;
	}

	// Create a proxy in the tree as a leaf node. We return the index
	// of the node instead of a pointer so that we can grow
	// the node pool.
	int32_t DynamicTree::CreateProxy(const AABBf& aabb, void* userData)
	{
		int32_t proxyId = AllocateNode();

		// Fatten the aabb.
		Vector2f r(AabbExtension, AabbExtension);
		m_nodes[proxyId].aabb.L = aabb.L - r.X;
		m_nodes[proxyId].aabb.T = aabb.T - r.Y;
		m_nodes[proxyId].aabb.R = aabb.R + r.X;
		m_nodes[proxyId].aabb.B = aabb.B + r.Y;
		m_nodes[proxyId].userData = userData;
		m_nodes[proxyId].height = 0;
		m_nodes[proxyId].moved = true;

		InsertLeaf(proxyId);

		return proxyId;
	}

	void DynamicTree::DestroyProxy(int32_t proxyId)
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);
		//b2Assert(m_nodes[proxyId].IsLeaf());

		RemoveLeaf(proxyId);
		FreeNode(proxyId);
	}

	bool DynamicTree::MoveProxy(int32_t proxyId, const AABBf& aabb, const Vector2f& displacement)
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);

		//b2Assert(m_nodes[proxyId].IsLeaf());

		// Extend AABB
		AABBf fatAABB;
		Vector2f r(AabbExtension, AabbExtension);
		fatAABB.L = aabb.L - r.X;
		fatAABB.T = aabb.T - r.Y;
		fatAABB.R = aabb.R + r.X;
		fatAABB.B = aabb.B + r.Y;

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

		const AABBf& treeAABB = m_nodes[proxyId].aabb;
		if (treeAABB.Contains(aabb)) {
			// The tree AABB still contains the object, but it might be too large.
			// Perhaps the object was moving fast but has since gone to sleep.
			// The huge AABB is larger than the new fat AABB.
			AABBf hugeAABB;
			hugeAABB.L = fatAABB.L - 4.0f * r.X;
			hugeAABB.T = fatAABB.T - 4.0f * r.Y;
			hugeAABB.R = fatAABB.R + 4.0f * r.X;
			hugeAABB.B = fatAABB.B + 4.0f * r.Y;

			if (hugeAABB.Contains(treeAABB)) {
				// The tree AABB contains the object AABB and the tree AABB is
				// not too large. No tree update needed.
				return false;
			}

			// Otherwise the tree AABB is huge and needs to be shrunk
		}

		RemoveLeaf(proxyId);

		m_nodes[proxyId].aabb = fatAABB;

		InsertLeaf(proxyId);

		m_nodes[proxyId].moved = true;

		return true;
	}

	void DynamicTree::InsertLeaf(int32_t leaf)
	{
		++m_insertionCount;

		if (m_root == NullNode) {
			m_root = leaf;
			m_nodes[m_root].parent = NullNode;
			return;
		}

		// Find the best sibling for this node
		AABBf leafAABB = m_nodes[leaf].aabb;
		int32_t index = m_root;
		while (m_nodes[index].IsLeaf() == false) {
			int32_t child1 = m_nodes[index].child1;
			int32_t child2 = m_nodes[index].child2;

			float area = m_nodes[index].aabb.GetPerimeter();

			AABBf combinedAABB = AABBf::Combine(m_nodes[index].aabb, leafAABB);
			float combinedArea = combinedAABB.GetPerimeter();

			// Cost of creating a new parent for this node and the new leaf
			float cost = 2.0f * combinedArea;

			// Minimum cost of pushing the leaf further down the tree
			float inheritanceCost = 2.0f * (combinedArea - area);

			// Cost of descending into child1
			float cost1;
			if (m_nodes[child1].IsLeaf()) {
				AABBf aabb = AABBf::Combine(leafAABB, m_nodes[child1].aabb);
				cost1 = aabb.GetPerimeter() + inheritanceCost;
			} else {
				AABBf aabb = AABBf::Combine(leafAABB, m_nodes[child1].aabb);
				float oldArea = m_nodes[child1].aabb.GetPerimeter();
				float newArea = aabb.GetPerimeter();
				cost1 = (newArea - oldArea) + inheritanceCost;
			}

			// Cost of descending into child2
			float cost2;
			if (m_nodes[child2].IsLeaf()) {
				AABBf aabb = AABBf::Combine(leafAABB, m_nodes[child2].aabb);
				cost2 = aabb.GetPerimeter() + inheritanceCost;
			} else {
				AABBf aabb = AABBf::Combine(leafAABB, m_nodes[child2].aabb);
				float oldArea = m_nodes[child2].aabb.GetPerimeter();
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

		int32_t sibling = index;

		// Create a new parent.
		int32_t oldParent = m_nodes[sibling].parent;
		int32_t newParent = AllocateNode();
		m_nodes[newParent].parent = oldParent;
		m_nodes[newParent].userData = nullptr;
		m_nodes[newParent].aabb = AABBf::Combine(leafAABB, m_nodes[sibling].aabb);
		m_nodes[newParent].height = m_nodes[sibling].height + 1;

		if (oldParent != NullNode) {
			// The sibling was not the root.
			if (m_nodes[oldParent].child1 == sibling) {
				m_nodes[oldParent].child1 = newParent;
			} else {
				m_nodes[oldParent].child2 = newParent;
			}

			m_nodes[newParent].child1 = sibling;
			m_nodes[newParent].child2 = leaf;
			m_nodes[sibling].parent = newParent;
			m_nodes[leaf].parent = newParent;
		} else {
			// The sibling was the root.
			m_nodes[newParent].child1 = sibling;
			m_nodes[newParent].child2 = leaf;
			m_nodes[sibling].parent = newParent;
			m_nodes[leaf].parent = newParent;
			m_root = newParent;
		}

		// Walk back up the tree fixing heights and AABBs
		index = m_nodes[leaf].parent;
		while (index != NullNode) {
			index = Balance(index);

			int32_t child1 = m_nodes[index].child1;
			int32_t child2 = m_nodes[index].child2;

			//b2Assert(child1 != NullNode);
			//b2Assert(child2 != NullNode);

			m_nodes[index].height = 1 + std::max(m_nodes[child1].height, m_nodes[child2].height);
			m_nodes[index].aabb = AABBf::Combine(m_nodes[child1].aabb, m_nodes[child2].aabb);

			index = m_nodes[index].parent;
		}

		//Validate();
	}

	void DynamicTree::RemoveLeaf(int32_t leaf)
	{
		if (leaf == m_root) {
			m_root = NullNode;
			return;
		}

		int32_t parent = m_nodes[leaf].parent;
		int32_t grandParent = m_nodes[parent].parent;
		int32_t sibling;
		if (m_nodes[parent].child1 == leaf) {
			sibling = m_nodes[parent].child2;
		} else {
			sibling = m_nodes[parent].child1;
		}

		if (grandParent != NullNode) {
			// Destroy parent and connect sibling to grandParent.
			if (m_nodes[grandParent].child1 == parent) {
				m_nodes[grandParent].child1 = sibling;
			} else {
				m_nodes[grandParent].child2 = sibling;
			}
			m_nodes[sibling].parent = grandParent;
			FreeNode(parent);

			// Adjust ancestor bounds.
			int32_t index = grandParent;
			while (index != NullNode) {
				index = Balance(index);

				int32_t child1 = m_nodes[index].child1;
				int32_t child2 = m_nodes[index].child2;

				m_nodes[index].aabb = AABBf::Combine(m_nodes[child1].aabb, m_nodes[child2].aabb);
				m_nodes[index].height = 1 + std::max(m_nodes[child1].height, m_nodes[child2].height);

				index = m_nodes[index].parent;
			}
		} else {
			m_root = sibling;
			m_nodes[sibling].parent = NullNode;
			FreeNode(parent);
		}

		//Validate();
	}

	// Perform a left or right rotation if node A is imbalanced.
	// Returns the new root index.
	int32_t DynamicTree::Balance(int32_t iA)
	{
		//b2Assert(iA != NullNode);

		TreeNode* A = m_nodes + iA;
		if (A->IsLeaf() || A->height < 2) {
			return iA;
		}

		int32_t iB = A->child1;
		int32_t iC = A->child2;
		//b2Assert(0 <= iB && iB < m_nodeCapacity);
		//b2Assert(0 <= iC && iC < m_nodeCapacity);

		TreeNode* B = m_nodes + iB;
		TreeNode* C = m_nodes + iC;

		int32_t balance = C->height - B->height;

		// Rotate C up
		if (balance > 1) {
			int32_t iF = C->child1;
			int32_t iG = C->child2;
			TreeNode* F = m_nodes + iF;
			TreeNode* G = m_nodes + iG;
			//b2Assert(0 <= iF && iF < m_nodeCapacity);
			//b2Assert(0 <= iG && iG < m_nodeCapacity);

			// Swap A and C
			C->child1 = iA;
			C->parent = A->parent;
			A->parent = iC;

			// A's old parent should point to C
			if (C->parent != NullNode) {
				if (m_nodes[C->parent].child1 == iA) {
					m_nodes[C->parent].child1 = iC;
				} else {
					//b2Assert(m_nodes[C->parent].child2 == iA);
					m_nodes[C->parent].child2 = iC;
				}
			} else {
				m_root = iC;
			}

			// Rotate
			if (F->height > G->height) {
				C->child2 = iF;
				A->child2 = iG;
				G->parent = iA;
				A->aabb = AABBf::Combine(B->aabb, G->aabb);
				C->aabb = AABBf::Combine(A->aabb, F->aabb);

				A->height = 1 + std::max(B->height, G->height);
				C->height = 1 + std::max(A->height, F->height);
			} else {
				C->child2 = iG;
				A->child2 = iF;
				F->parent = iA;
				A->aabb = AABBf::Combine(B->aabb, F->aabb);
				C->aabb = AABBf::Combine(A->aabb, G->aabb);

				A->height = 1 + std::max(B->height, F->height);
				C->height = 1 + std::max(A->height, G->height);
			}

			return iC;
		}

		// Rotate B up
		if (balance < -1) {
			int32_t iD = B->child1;
			int32_t iE = B->child2;
			TreeNode* D = m_nodes + iD;
			TreeNode* E = m_nodes + iE;
			//b2Assert(0 <= iD && iD < m_nodeCapacity);
			//b2Assert(0 <= iE && iE < m_nodeCapacity);

			// Swap A and B
			B->child1 = iA;
			B->parent = A->parent;
			A->parent = iB;

			// A's old parent should point to B
			if (B->parent != NullNode) {
				if (m_nodes[B->parent].child1 == iA) {
					m_nodes[B->parent].child1 = iB;
				} else {
					//b2Assert(m_nodes[B->parent].child2 == iA);
					m_nodes[B->parent].child2 = iB;
				}
			} else {
				m_root = iB;
			}

			// Rotate
			if (D->height > E->height) {
				B->child2 = iD;
				A->child1 = iE;
				E->parent = iA;
				A->aabb = AABBf::Combine(C->aabb, E->aabb);
				B->aabb = AABBf::Combine(A->aabb, D->aabb);

				A->height = 1 + std::max(C->height, E->height);
				B->height = 1 + std::max(A->height, D->height);
			} else {
				B->child2 = iE;
				A->child1 = iD;
				D->parent = iA;
				A->aabb = AABBf::Combine(C->aabb, D->aabb);
				B->aabb = AABBf::Combine(A->aabb, E->aabb);

				A->height = 1 + std::max(C->height, D->height);
				B->height = 1 + std::max(A->height, E->height);
			}

			return iB;
		}

		return iA;
	}

	int32_t DynamicTree::GetHeight() const
	{
		if (m_root == NullNode) {
			return 0;
		}

		return m_nodes[m_root].height;
	}

	//
	float DynamicTree::GetAreaRatio() const
	{
		if (m_root == NullNode) {
			return 0.0f;
		}

		const TreeNode* root = m_nodes + m_root;
		float rootArea = root->aabb.GetPerimeter();

		float totalArea = 0.0f;
		for (int32_t i = 0; i < m_nodeCapacity; ++i) {
			const TreeNode* node = m_nodes + i;
			if (node->height < 0) {
				// Free node in pool
				continue;
			}

			totalArea += node->aabb.GetPerimeter();
		}

		return totalArea / rootArea;
	}

	// Compute the height of a sub-tree.
	int32_t DynamicTree::ComputeHeight(int32_t nodeId) const
	{
		//b2Assert(0 <= nodeId && nodeId < m_nodeCapacity);
		TreeNode* node = m_nodes + nodeId;

		if (node->IsLeaf()) {
			return 0;
		}

		int32_t height1 = ComputeHeight(node->child1);
		int32_t height2 = ComputeHeight(node->child2);
		return 1 + std::max(height1, height2);
	}

	int32_t DynamicTree::ComputeHeight() const
	{
		int32_t height = ComputeHeight(m_root);
		return height;
	}

	void DynamicTree::ValidateStructure(int32_t index) const
	{
		if (index == NullNode) {
			return;
		}

		if (index == m_root) {
			//b2Assert(m_nodes[index].parent == NullNode);
		}

		const TreeNode* node = m_nodes + index;

		int32_t child1 = node->child1;
		int32_t child2 = node->child2;

		if (node->IsLeaf()) {
			//b2Assert(child1 == NullNode);
			//b2Assert(child2 == NullNode);
			//b2Assert(node->height == 0);
			return;
		}

		//b2Assert(0 <= child1 && child1 < m_nodeCapacity);
		//b2Assert(0 <= child2 && child2 < m_nodeCapacity);

		//b2Assert(m_nodes[child1].parent == index);
		//b2Assert(m_nodes[child2].parent == index);

		ValidateStructure(child1);
		ValidateStructure(child2);
	}

	/*void DynamicTree::ValidateMetrics(int32_t index) const
	{
		if (index == NullNode) {
			return;
		}

		const TreeNode* node = m_nodes + index;

		int32_t child1 = node->child1;
		int32_t child2 = node->child2;

		if (node->IsLeaf()) {
			//b2Assert(child1 == NullNode);
			//b2Assert(child2 == NullNode);
			//b2Assert(node->height == 0);
			return;
		}

		//b2Assert(0 <= child1 && child1 < m_nodeCapacity);
		//b2Assert(0 <= child2 && child2 < m_nodeCapacity);

		int32_t height1 = m_nodes[child1].height;
		int32_t height2 = m_nodes[child2].height;
		int32_t height = 1 + std::max(height1, height2);
		//b2Assert(node->height == height);

		//AABBf aabb = AABBf::Combine(m_nodes[child1].aabb, m_nodes[child2].aabb);
		//b2Assert(aabb.lowerBound == node->aabb.lowerBound);
		//b2Assert(aabb.upperBound == node->aabb.upperBound);

		ValidateMetrics(child1);
		ValidateMetrics(child2);
	}*/

	void DynamicTree::Validate() const
	{
#if defined(b2DEBUG)
		ValidateStructure(m_root);
		//ValidateMetrics(m_root);

		int32_t freeCount = 0;
		int32_t freeIndex = m_freeList;
		while (freeIndex != NullNode) {
			//b2Assert(0 <= freeIndex && freeIndex < m_nodeCapacity);
			freeIndex = m_nodes[freeIndex].next;
			++freeCount;
		}

		//b2Assert(GetHeight() == ComputeHeight());
		//b2Assert(m_nodeCount + freeCount == m_nodeCapacity);
#endif
	}

	int32_t DynamicTree::GetMaxBalance() const
	{
		int32_t maxBalance = 0;
		for (int32_t i = 0; i < m_nodeCapacity; ++i) {
			const TreeNode* node = m_nodes + i;
			if (node->height <= 1) {
				continue;
			}

			//b2Assert(node->IsLeaf() == false);

			int32_t child1 = node->child1;
			int32_t child2 = node->child2;
			int32_t balance = std::abs(m_nodes[child2].height - m_nodes[child1].height);
			maxBalance = std::max(maxBalance, balance);
		}

		return maxBalance;
	}

	void DynamicTree::RebuildBottomUp()
	{
		int32_t* nodes = (int32_t*)malloc(m_nodeCount * sizeof(int32_t));
		int32_t count = 0;

		// Build array of leaves. Free the rest.
		for (int32_t i = 0; i < m_nodeCapacity; ++i) {
			if (m_nodes[i].height < 0) {
				// free node in pool
				continue;
			}

			if (m_nodes[i].IsLeaf()) {
				m_nodes[i].parent = NullNode;
				nodes[count] = i;
				++count;
			} else {
				FreeNode(i);
			}
		}

		while (count > 1) {
			float minCost = FLT_MAX;
			int32_t iMin = -1, jMin = -1;
			for (int32_t i = 0; i < count; ++i) {
				AABBf aabbi = m_nodes[nodes[i]].aabb;

				for (int32_t j = i + 1; j < count; ++j) {
					AABBf aabbj = m_nodes[nodes[j]].aabb;
					AABBf b = AABBf::Combine(aabbi, aabbj);
					float cost = b.GetPerimeter();
					if (cost < minCost) {
						iMin = i;
						jMin = j;
						minCost = cost;
					}
				}
			}

			int32_t index1 = nodes[iMin];
			int32_t index2 = nodes[jMin];
			TreeNode* child1 = m_nodes + index1;
			TreeNode* child2 = m_nodes + index2;

			int32_t parentIndex = AllocateNode();
			TreeNode* parent = m_nodes + parentIndex;
			parent->child1 = index1;
			parent->child2 = index2;
			parent->height = 1 + std::max(child1->height, child2->height);
			parent->aabb = AABBf::Combine(child1->aabb, child2->aabb);
			parent->parent = NullNode;

			child1->parent = parentIndex;
			child2->parent = parentIndex;

			nodes[jMin] = nodes[count - 1];
			nodes[iMin] = parentIndex;
			--count;
		}

		m_root = nodes[0];
		free(nodes);

		Validate();
	}

	void DynamicTree::ShiftOrigin(const Vector2f& newOrigin)
	{
		// Build array of leaves. Free the rest.
		for (int32_t i = 0; i < m_nodeCapacity; ++i) {
			m_nodes[i].aabb.L -= newOrigin.X;
			m_nodes[i].aabb.T -= newOrigin.Y;
			m_nodes[i].aabb.R -= newOrigin.X;
			m_nodes[i].aabb.B -= newOrigin.Y;
		}
	}
}