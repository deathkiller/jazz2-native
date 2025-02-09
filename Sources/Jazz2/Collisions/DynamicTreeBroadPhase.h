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

#include "DynamicTree.h"

namespace Jazz2::Collisions
{
	/** @brief Collided pair of objects found by collision detection */
	struct CollisionPair {
		/** @brief Proxy ID of the first node */
		std::int32_t ProxyIdA;
		/** @brief Proxy ID of the second node */
		std::int32_t ProxyIdB;
	};

	/**
		@brief Broad-phase for collision detection

		The broad-phase is used for computing pairs and performing volume queries and ray casts.
		This broad-phase does not persist pairs. Instead, this reports potentially new pairs.
		It is up to the client to consume the new pairs and to track subsequent overlap.
	*/
	class DynamicTreeBroadPhase
	{
		friend class DynamicTree;

	public:
		DynamicTreeBroadPhase();
		~DynamicTreeBroadPhase();

		/**
		 * @brief Creates a proxy with an initial AABB
		 * 
		 * Pairs are not reported until @ref UpdatePairs() is called
		 */
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
		void MoveProxy(std::int32_t proxyId, const AABBf& aabb, Vector2f displacement);

		/** @brief Triggers a re-processing of it's pairs on the next call to @ref UpdatePairs() */
		void TouchProxy(std::int32_t proxyId);

		/** @brief Returns the fat AABB for a proxy */
		const AABBf& GetFatAABB(std::int32_t proxyId) const;

		/** @brief Returns a user data from a proxy */
		void* GetUserData(std::int32_t proxyId) const;

		/** @brief Tests overlap of fat AABBs */
		bool TestOverlap(std::int32_t proxyIdA, std::int32_t proxyIdB) const;

		/** @brief Returns the number of proxies */
		std::int32_t GetProxyCount() const;

		/** @brief Updates the pairs */
		template <typename T>
		void UpdatePairs(T* callback);

		/**
		 * @brief Queries an AABB for overlapping proxies
		 * 
		 * The callback class is called for each proxy that overlaps the supplied AABB.
		 */
		template <typename T>
		void Query(T* callback, const AABBf& aabb) const;

		// Ray-cast against the proxies in the tree. This relies on the callback
		// to perform a exact ray-cast in the case were the proxy contains a shape.
		// The callback also performs the any collision filtering. This has performance
		// roughly equal to k * log(n), where k is the number of collisions and n is the
		// number of proxies in the tree.
		// @param input the ray-cast input data. The ray extends from p1 to p1 + maxFraction * (p2 - p1).
		// @param callback a callback class that is called for each proxy that is hit by the ray.
		//template <typename T>
		//void RayCast(T* callback, const b2RayCastInput& input) const;

		/** @brief Returns the height of the embedded tree */
		std::int32_t GetTreeHeight() const;

		/** @brief Returns the balance of the embedded tree */
		std::int32_t GetTreeBalance() const;

		/** @brief Returns the quality metric of the embedded tree */
		float GetTreeQuality() const;

		/**
		 * @brief Shifts the world origin
		 *
		 * Useful for large worlds. The shift formula is: `position -= newOrigin`
		 *
		 * @param newOrigin the new origin with respect to the old origin
		 */
		void ShiftOrigin(Vector2f newOrigin);

	private:
		static constexpr std::int32_t DefaultPairCapacity = 16;
		static constexpr std::int32_t DefaultMoveCapacity = /*16*/64;

		DynamicTree _tree;

		std::int32_t _proxyCount;

		std::int32_t* _moveBuffer;
		std::int32_t _moveCapacity;
		std::int32_t _moveCount;

		CollisionPair* _pairBuffer;
		std::int32_t _pairCapacity;
		std::int32_t _pairCount;

		std::int32_t _queryProxyId;

		void BufferMove(std::int32_t proxyId);
		void UnBufferMove(std::int32_t proxyId);

		bool OnCollisionQuery(std::int32_t proxyId);
	};

	inline void* DynamicTreeBroadPhase::GetUserData(std::int32_t proxyId) const
	{
		return _tree.GetUserData(proxyId);
	}

	inline bool DynamicTreeBroadPhase::TestOverlap(std::int32_t proxyIdA, std::int32_t proxyIdB) const
	{
		const AABBf& aabbA = _tree.GetFatAABB(proxyIdA);
		const AABBf& aabbB = _tree.GetFatAABB(proxyIdB);
		return aabbA.Overlaps(aabbB);
	}

	inline const AABBf& DynamicTreeBroadPhase::GetFatAABB(std::int32_t proxyId) const
	{
		return _tree.GetFatAABB(proxyId);
	}

	inline std::int32_t DynamicTreeBroadPhase::GetProxyCount() const
	{
		return _proxyCount;
	}

	inline std::int32_t DynamicTreeBroadPhase::GetTreeHeight() const
	{
		return _tree.GetHeight();
	}

	inline std::int32_t DynamicTreeBroadPhase::GetTreeBalance() const
	{
		return _tree.GetMaxBalance();
	}

	inline float DynamicTreeBroadPhase::GetTreeQuality() const
	{
		return _tree.GetAreaRatio();
	}

	template <typename T>
	void DynamicTreeBroadPhase::UpdatePairs(T* callback)
	{
		// Reset pair buffer
		_pairCount = 0;

		// Perform tree queries for all moving proxies.
		for (std::int32_t i = 0; i < _moveCount; ++i) {
			_queryProxyId = _moveBuffer[i];
			if (_queryProxyId == NullNode) {
				continue;
			}

			// We have to query the tree with the fat AABB so that
			// we don't fail to create a pair that may touch later.
			const AABBf& fatAABB = _tree.GetFatAABB(_queryProxyId);

			// Query tree, create pairs and add them pair buffer.
			_tree.Query(this, fatAABB);
		}

		// Send pairs to caller
		for (std::int32_t i = 0; i < _pairCount; ++i) {
			CollisionPair* primaryPair = &_pairBuffer[i];
			void* userDataA = _tree.GetUserData(primaryPair->ProxyIdA);
			void* userDataB = _tree.GetUserData(primaryPair->ProxyIdB);

			callback->OnPairAdded(userDataA, userDataB);
		}

		// Clear move flags
		for (std::int32_t i = 0; i < _moveCount; ++i) {
			std::int32_t proxyId = _moveBuffer[i];
			if (proxyId == NullNode) {
				continue;
			}

			_tree.ClearMoved(proxyId);
		}

		// Reset move buffer
		_moveCount = 0;
	}

	template <typename T>
	inline void DynamicTreeBroadPhase::Query(T* callback, const AABBf& aabb) const
	{
		_tree.Query(callback, aabb);
	}

	/*template <typename T>
	inline void DynamicTreeBroadPhase::RayCast(T* callback, const b2RayCastInput& input) const
	{
		_tree.RayCast(callback, input);
	}*/

	inline void DynamicTreeBroadPhase::ShiftOrigin(Vector2f newOrigin)
	{
		_tree.ShiftOrigin(newOrigin);
	}
}