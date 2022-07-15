// MIT License

// Copyright (c) 2019 Erin Catto

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include "DynamicTreeBroadPhase.h"

namespace Jazz2::Collisions
{
	DynamicTreeBroadPhase::DynamicTreeBroadPhase()
	{
		m_proxyCount = 0;

		m_pairCapacity = 16;
		m_pairCount = 0;
		m_pairBuffer = (CollisionPair*)malloc(m_pairCapacity * sizeof(CollisionPair));

		m_moveCapacity = 16;
		m_moveCount = 0;
		m_moveBuffer = (int32_t*)malloc(m_moveCapacity * sizeof(int32_t));
	}

	DynamicTreeBroadPhase::~DynamicTreeBroadPhase()
	{
		free(m_moveBuffer);
		free(m_pairBuffer);
	}

	int32_t DynamicTreeBroadPhase::CreateProxy(const AABBf& aabb, void* userData)
	{
		int32_t proxyId = m_tree.CreateProxy(aabb, userData);
		++m_proxyCount;
		BufferMove(proxyId);
		return proxyId;
	}

	void DynamicTreeBroadPhase::DestroyProxy(int32_t proxyId)
	{
		UnBufferMove(proxyId);
		--m_proxyCount;
		m_tree.DestroyProxy(proxyId);
	}

	void DynamicTreeBroadPhase::MoveProxy(int32_t proxyId, const AABBf& aabb, const Vector2f& displacement)
	{
		bool buffer = m_tree.MoveProxy(proxyId, aabb, displacement);
		if (buffer) {
			BufferMove(proxyId);
		}
	}

	void DynamicTreeBroadPhase::TouchProxy(int32_t proxyId)
	{
		BufferMove(proxyId);
	}

	void DynamicTreeBroadPhase::BufferMove(int32_t proxyId)
	{
		if (m_moveCount == m_moveCapacity) {
			int32_t* oldBuffer = m_moveBuffer;
			m_moveCapacity *= 2;
			m_moveBuffer = (int32_t*)malloc(m_moveCapacity * sizeof(int32_t));
			memcpy(m_moveBuffer, oldBuffer, m_moveCount * sizeof(int32_t));
			free(oldBuffer);
		}

		m_moveBuffer[m_moveCount] = proxyId;
		++m_moveCount;
	}

	void DynamicTreeBroadPhase::UnBufferMove(int32_t proxyId)
	{
		for (int32_t i = 0; i < m_moveCount; ++i) {
			if (m_moveBuffer[i] == proxyId) {
				m_moveBuffer[i] = NullNode;
			}
		}
	}

	// This is called from b2DynamicTree::Query when we are gathering pairs.
	bool DynamicTreeBroadPhase::OnCollisionQuery(int32_t proxyId)
	{
		// A proxy cannot form a pair with itself.
		if (proxyId == m_queryProxyId) {
			return true;
		}

		const bool moved = m_tree.WasMoved(proxyId);
		if (moved && proxyId > m_queryProxyId) {
			// Both proxies are moving. Avoid duplicate pairs.
			return true;
		}

		// Grow the pair buffer as needed.
		if (m_pairCount == m_pairCapacity) {
			CollisionPair* oldBuffer = m_pairBuffer;
			m_pairCapacity = m_pairCapacity + (m_pairCapacity >> 1);
			m_pairBuffer = (CollisionPair*)malloc(m_pairCapacity * sizeof(CollisionPair));
			memcpy(m_pairBuffer, oldBuffer, m_pairCount * sizeof(CollisionPair));
			free(oldBuffer);
		}

		m_pairBuffer[m_pairCount].proxyIdA = std::min(proxyId, m_queryProxyId);
		m_pairBuffer[m_pairCount].proxyIdB = std::max(proxyId, m_queryProxyId);
		++m_pairCount;

		return true;
	}
}