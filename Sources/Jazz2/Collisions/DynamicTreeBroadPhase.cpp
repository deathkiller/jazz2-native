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

#include "DynamicTreeBroadPhase.h"

namespace Jazz2::Collisions
{
	DynamicTreeBroadPhase::DynamicTreeBroadPhase()
	{
		_proxyCount = 0;

		_pairCapacity = DefaultPairCapacity;
		_pairCount = 0;
		_pairBuffer = new CollisionPair[_pairCapacity];

		_moveCapacity = DefaultMoveCapacity;
		_moveCount = 0;
		_moveBuffer = new std::int32_t[_moveCapacity];
	}

	DynamicTreeBroadPhase::~DynamicTreeBroadPhase()
	{
		delete[] _moveBuffer;
		delete[] _pairBuffer;
	}

	int32_t DynamicTreeBroadPhase::CreateProxy(const AABBf& aabb, void* userData)
	{
		std::int32_t proxyId = _tree.CreateProxy(aabb, userData);
		++_proxyCount;
		BufferMove(proxyId);
		return proxyId;
	}

	void DynamicTreeBroadPhase::DestroyProxy(std::int32_t proxyId)
	{
		UnBufferMove(proxyId);
		--_proxyCount;
		_tree.DestroyProxy(proxyId);
	}

	void DynamicTreeBroadPhase::MoveProxy(std::int32_t proxyId, const AABBf& aabb, Vector2f displacement)
	{
		// NOTE: Touch proxy everytime, because it's called only when something changes
		/*bool buffer =*/ _tree.MoveProxy(proxyId, aabb, displacement);
		//if (buffer) {
			BufferMove(proxyId);
		//}
	}

	void DynamicTreeBroadPhase::TouchProxy(std::int32_t proxyId)
	{
		BufferMove(proxyId);
	}

	void DynamicTreeBroadPhase::BufferMove(std::int32_t proxyId)
	{
		if (_moveCount == _moveCapacity) {
			std::int32_t* oldBuffer = _moveBuffer;
			_moveCapacity *= 2;
			_moveBuffer = new std::int32_t[_moveCapacity];
			std::memcpy(_moveBuffer, oldBuffer, _moveCount * sizeof(std::int32_t));
			delete[] oldBuffer;
		}

		_moveBuffer[_moveCount] = proxyId;
		++_moveCount;
	}

	void DynamicTreeBroadPhase::UnBufferMove(std::int32_t proxyId)
	{
		for (std::int32_t i = 0; i < _moveCount; ++i) {
			if (_moveBuffer[i] == proxyId) {
				_moveBuffer[i] = NullNode;
			}
		}
	}

	// This is called from b2DynamicTree::Query when we are gathering pairs.
	bool DynamicTreeBroadPhase::OnCollisionQuery(std::int32_t proxyId)
	{
		// A proxy cannot form a pair with itself.
		if (proxyId == _queryProxyId) {
			return true;
		}

		const bool moved = _tree.WasMoved(proxyId);
		if (moved && proxyId > _queryProxyId) {
			// Both proxies are moving. Avoid duplicate pairs.
			return true;
		}

		// Grow the pair buffer as needed.
		if (_pairCount == _pairCapacity) {
			CollisionPair* oldBuffer = _pairBuffer;
			_pairCapacity = _pairCapacity + (_pairCapacity >> 1);
			_pairBuffer = new CollisionPair[_pairCapacity];
			std::memcpy(_pairBuffer, oldBuffer, _pairCount * sizeof(CollisionPair));
			delete[] oldBuffer;
		}

		_pairBuffer[_pairCount].ProxyIdA = std::min(proxyId, _queryProxyId);
		_pairBuffer[_pairCount].ProxyIdB = std::max(proxyId, _queryProxyId);
		++_pairCount;

		return true;
	}
}