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

#pragma once

#include "../../nCine/Primitives/AABB.h"
#include "../../nCine/Primitives/Vector2.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace Jazz2::Collisions
{
	using nCine::AABBf;
	using nCine::Vector2f;

	constexpr std::int32_t NullNode = -1;
	constexpr float LengthUnitsPerMeter = 1.0f;
	constexpr float AabbExtension = 0.1f * LengthUnitsPerMeter;
	constexpr float AabbMultiplier = 4.0f;

	/// A node in the dynamic tree. The client does not interact with this directly.
	struct TreeNode
	{
		/// Enlarged AABB
		AABBf Aabb;

		void* UserData;

		union
		{
			std::int32_t Parent;
			std::int32_t Next;
		};

		std::int32_t Child1;
		std::int32_t Child2;

		// leaf = 0, free node = -1
		std::int32_t Height;

		bool Moved;

		bool IsLeaf() const
		{
			return (Child1 == NullNode);
		}
	};

	/// A dynamic AABB tree broad-phase, inspired by Nathanael Presson's btDbvt.
	/// A dynamic tree arranges data in a binary tree to accelerate
	/// queries such as volume queries and ray casts. Leafs are proxies
	/// with an AABB. In the tree we expand the proxy AABB by b2_fatAABBFactor
	/// so that the proxy AABB is bigger than the client object. This allows the client
	/// object to move by small amounts without triggering a tree update.
	///
	/// Nodes are pooled and relocatable, so we use node indices rather than pointers.
	class DynamicTree
	{
	public:
		/// Constructing the tree initializes the node pool.
		DynamicTree();

		/// Destroy the tree, freeing the node pool.
		~DynamicTree();

		/// Create a proxy. Provide a tight fitting AABB and a userData pointer.
		std::int32_t CreateProxy(const AABBf& aabb, void* userData);

		/// Destroy a proxy. This asserts if the id is invalid.
		void DestroyProxy(std::int32_t proxyId);

		/// Move a proxy with a swepted AABB. If the proxy has moved outside of its fattened AABB,
		/// then the proxy is removed from the tree and re-inserted. Otherwise
		/// the function returns immediately.
		/// @return true if the proxy was re-inserted.
		bool MoveProxy(std::int32_t proxyId, const AABBf& aabb1, const Vector2f& displacement);

		/// Get proxy user data.
		/// @return the proxy user data or 0 if the id is invalid.
		void* GetUserData(std::int32_t proxyId) const;

		bool WasMoved(std::int32_t proxyId) const;
		void ClearMoved(std::int32_t proxyId);

		/// Get the fat AABB for a proxy.
		const AABBf& GetFatAABB(std::int32_t proxyId) const;

		/// Query an AABB for overlapping proxies. The callback class
		/// is called for each proxy that overlaps the supplied AABB.
		template<typename T>
		void Query(T* callback, const AABBf& aabb) const;

		/// Ray-cast against the proxies in the tree. This relies on the callback
		/// to perform a exact ray-cast in the case were the proxy contains a shape.
		/// The callback also performs the any collision filtering. This has performance
		/// roughly equal to k * log(n), where k is the number of collisions and n is the
		/// number of proxies in the tree.
		/// @param input the ray-cast input data. The ray extends from p1 to p1 + maxFraction * (p2 - p1).
		/// @param callback a callback class that is called for each proxy that is hit by the ray.
		//template <typename T>
		//void RayCast(T* callback, const b2RayCastInput& input) const;

		/// Validate this tree. For testing.
		void Validate() const;

		/// Compute the height of the binary tree in O(N) time. Should not be
		/// called often.
		std::int32_t GetHeight() const;

		/// Get the maximum balance of an node in the tree. The balance is the difference
		/// in height of the two children of a node.
		std::int32_t GetMaxBalance() const;

		/// Get the ratio of the sum of the node areas to the root area.
		float GetAreaRatio() const;

		/// Build an optimal tree. Very expensive. For testing.
		void RebuildBottomUp();

		/// Shift the world origin. Useful for large worlds.
		/// The shift formula is: position -= newOrigin
		/// @param newOrigin the new origin with respect to the old origin
		void ShiftOrigin(Vector2f newOrigin);

	private:
		static constexpr std::int32_t DefaultNodeCapacity = /*16*/128;

		std::int32_t AllocateNode();
		void FreeNode(std::int32_t node);

		void InsertLeaf(std::int32_t node);
		void RemoveLeaf(std::int32_t node);

		std::int32_t Balance(std::int32_t index);

		std::int32_t ComputeHeight() const;
		std::int32_t ComputeHeight(std::int32_t nodeId) const;

		void ValidateStructure(std::int32_t index) const;
		//void ValidateMetrics(std::int32_t index) const;

		std::int32_t _root;

		TreeNode* _nodes;
		std::int32_t _nodeCount;
		std::int32_t _nodeCapacity;

		std::int32_t _freeList;

		std::int32_t _insertionCount;
	};

	inline void* DynamicTree::GetUserData(std::int32_t proxyId) const
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);
		return _nodes[proxyId].UserData;
	}

	inline bool DynamicTree::WasMoved(std::int32_t proxyId) const
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);
		return _nodes[proxyId].Moved;
	}

	inline void DynamicTree::ClearMoved(std::int32_t proxyId)
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);
		_nodes[proxyId].Moved = false;
	}

	inline const AABBf& DynamicTree::GetFatAABB(std::int32_t proxyId) const
	{
		//b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);
		return _nodes[proxyId].Aabb;
	}

	template<typename T>
	inline void DynamicTree::Query(T* callback, const AABBf& aabb) const
	{
		SmallVector<std::int32_t, 256> stack;
		stack.push_back(_root);

		while (!stack.empty()) {
			std::int32_t nodeId = stack.pop_back_val();
			if (nodeId == NullNode) {
				continue;
			}

			const TreeNode* node = &_nodes[nodeId];

			if (node->Aabb.Overlaps(aabb)) {
				if (node->IsLeaf()) {
					bool proceed = callback->OnCollisionQuery(nodeId);
					if (!proceed) {
						return;
					}
				} else {
					stack.push_back(node->Child1);
					stack.push_back(node->Child2);
				}
			}
		}
	}

	/*template<typename T>
	inline void DynamicTree::RayCast(T* callback, const b2RayCastInput& input) const
	{
		b2Vec2 p1 = input.p1;
		b2Vec2 p2 = input.p2;
		b2Vec2 r = p2 - p1;
		b2Assert(r.LengthSquared() > 0.0f);
		r.Normalize();

		// v is perpendicular to the segment.
		b2Vec2 v = b2Cross(1.0f, r);
		b2Vec2 abs_v = b2Abs(v);

		// Separating axis for segment (Gino, p80).
		// |dot(v, p1 - c)| > dot(|v|, h)

		float maxFraction = input.maxFraction;

		// Build a bounding box for the segment.
		b2AABB segmentAABB;
		{
			b2Vec2 t = p1 + maxFraction * (p2 - p1);
			segmentAABB.lowerBound = b2Min(p1, t);
			segmentAABB.upperBound = b2Max(p1, t);
		}

		b2GrowableStack<int32, 256> stack;
		stack.Push(m_root);

		while (stack.GetCount() > 0) {
			int32 nodeId = stack.Pop();
			if (nodeId == b2_nullNode) {
				continue;
			}

			const TreeNode* node = m_nodes + nodeId;

			if (b2TestOverlap(node->aabb, segmentAABB) == false) {
				continue;
			}

			// Separating axis for segment (Gino, p80).
			// |dot(v, p1 - c)| > dot(|v|, h)
			b2Vec2 c = node->aabb.GetCenter();
			b2Vec2 h = node->aabb.GetExtents();
			float separation = b2Abs(b2Dot(v, p1 - c)) - b2Dot(abs_v, h);
			if (separation > 0.0f) {
				continue;
			}

			if (node->IsLeaf()) {
				b2RayCastInput subInput;
				subInput.p1 = input.p1;
				subInput.p2 = input.p2;
				subInput.maxFraction = maxFraction;

				float value = callback->RayCastCallback(subInput, nodeId);

				if (value == 0.0f) {
					// The client has terminated the ray cast.
					return;
				}

				if (value > 0.0f) {
					// Update segment bounding box.
					maxFraction = value;
					b2Vec2 t = p1 + maxFraction * (p2 - p1);
					segmentAABB.lowerBound = b2Min(p1, t);
					segmentAABB.upperBound = b2Max(p1, t);
				}
			} else {
				stack.Push(node->child1);
				stack.Push(node->child2);
			}
		}
	}*/
}