﻿// MIT License
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

	/** @{ @name Constants */

	/** @brief Invalid node */
	constexpr std::int32_t NullNode = -1;
	/** @brief Length units per meter */
	constexpr float LengthUnitsPerMeter = 1.0f;
	/** @brief AABB size extension to fat AABB */
	constexpr float AabbExtension = 0.1f * LengthUnitsPerMeter;
	/** @brief AABB movement multiplier */
	constexpr float AabbMultiplier = 4.0f;

	/** @} */

	/**
		@brief Node in the dynamic tree

		The client does not interact with this directly.
	*/
	struct TreeNode
	{
		/** @brief Enlarged AABB */
		AABBf Aabb;

		/** @brief Opaque pointer to user-supplied data */
		void* UserData;

		union
		{
			/** @brief Node ID of parent node */
			std::int32_t Parent;
			/** @brief Node ID of next node */
			std::int32_t Next;
		};

		/** @brief Node ID of the first child */
		std::int32_t Child1;
		/** @brief Node ID of the second child */
		std::int32_t Child2;

		/** @brief Height (leaf = 0, free node = -1) */
		std::int32_t Height;

		/** @brief Whether node has been moved */
		bool Moved;

		/** @brief Returns whether the node is leaf */
		bool IsLeaf() const
		{
			return (Child1 == NullNode);
		}
	};

	/**
		@brief Dynamic AABB tree broad-phase

		A dynamic tree arranges data in a binary tree to accelerate
		queries such as volume queries and ray casts. Leafs are proxies
		with an AABB. In the tree we expand the proxy AABB by @ref AabbExtension
		so that the proxy AABB is bigger than the client object. This allows the client
		object to move by small amounts without triggering a tree update.

		Nodes are pooled and relocatable, so we use node indices rather than pointers.
	*/
	class DynamicTree
	{
	public:
		/** @brief Constructing the tree initializes the node pool */
		DynamicTree();

		/** @brief Destroys the tree, freeing the node pool */
		~DynamicTree();

		/** @brief Creates a proxy */
		std::int32_t CreateProxy(const AABBf& aabb, void* userData);

		/** @brief Destroys a proxy */
		void DestroyProxy(std::int32_t proxyId);

		/**
		 * @brief Moves a proxy with a swepted AABB
		 * 
		 * If the proxy has moved outside of its fattened AABB, then the proxy is removed from
		 * the tree and re-inserted. Otherwise the function returns immediately.
		 * 
		 * @return `true` if the proxy was re-inserted.
		 */
		bool MoveProxy(std::int32_t proxyId, const AABBf& aabb1, Vector2f displacement);

		/**
		 * @brief Returns proxy user data.
		 * 
		 * @return @return the proxy user data or `0` if the id is invalid.
		 */
		void* GetUserData(std::int32_t proxyId) const;

		/** @brief Returns `true` if a proxy has been moved */
		bool WasMoved(std::int32_t proxyId) const;
		/** @brief Clears moved status of a proxy */
		void ClearMoved(std::int32_t proxyId);

		/** @brief Returns the fat AABB for a proxy */
		const AABBf& GetFatAABB(std::int32_t proxyId) const;

		/** @brief Queries an AABB for overlapping proxies, the callback is called for each proxy that overlaps the supplied AABB */
		template<typename T>
		void Query(T* callback, const AABBf& aabb) const;

		// @brief Ray-cast against the proxies in the tree
		//
		// This relies on the callback to perform a exact ray-cast in the case were the proxy contains a shape.
		// The callback also performs the any collision filtering. This has performance
		// roughly equal to @f$ k * log(n) @f$, where k is the number of collisions and n is the
		// number of proxies in the tree.
		// @param input the ray-cast input data. The ray extends from p1 to p1 + maxFraction * (p2 - p1).
		// @param callback a callback class that is called for each proxy that is hit by the ray.
		//
		//template <typename T>
		//void RayCast(T* callback, const b2RayCastInput& input) const;

		/** @brief Validates this tree --- for testing only */
		void Validate() const;

		/** @brief Computes the height of the binary tree in @f$ \mathcal{O}(n) @f$ time, should not be called often */
		std::int32_t GetHeight() const;

		/** @brief Returns the maximum balance of an node in the tree
		 *
		 * The balance is the difference in height of the two children of a node.
		 */
		std::int32_t GetMaxBalance() const;

		/** @brief Returns the ratio of the sum of the node areas to the root area */
		float GetAreaRatio() const;

		/** @brief Builds an optimal tree, very expensive --- for testing only */
		void RebuildBottomUp();

		/**
		 * @brief Shifts the world origin
		 * 
		 * Useful for large worlds. The shift formula is: `position -= newOrigin`
		 * 
		 * @param newOrigin the new origin with respect to the old origin
		 */
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