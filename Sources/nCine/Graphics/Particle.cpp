#include "Particle.h"
#include "RenderCommand.h"

namespace nCine
{
	Particle::Particle(SceneNode* parent, Texture* texture)
		: Sprite(parent, texture), _life(0.0f), startingLife(0.0f), startingRotation(0.0f), _inLocalSpace(false)
	{
		_type = ObjectType::Particle;
		_renderCommand.SetType(RenderCommand::Type::Particle);
		SetEnabled(false);
	}

	Particle::Particle(const Particle& other)
		: Sprite(other), _life(other._life), startingLife(other.startingLife), startingRotation(other.startingRotation), _inLocalSpace(other._inLocalSpace)
	{
		_type = ObjectType::Particle;
		_renderCommand.SetType(RenderCommand::Type::Particle);
	}

	void Particle::Init(float life, Vector2f pos, Vector2f vel, float rot, bool inLocalSpace)
	{
		_life = life;
		startingLife = life;
		startingRotation = rot;
		SetPosition(pos);
		_velocity = vel;
		SetRotation(rot);
		_inLocalSpace = inLocalSpace;
		SetEnabled(true);
	}

	void Particle::OnUpdate(float timeMult)
	{
		if (timeMult >= _life) {
			_life = 0.0f; // dead particle
			SetEnabled(false);
		} else {
			_life -= timeMult;
			move(_velocity * timeMult);
		}
	}

	void Particle::Transform()
	{
		SceneNode::Transform();

		if (!_inLocalSpace) {
			_worldMatrix = _localMatrix;

			// Always independent movement
			_absScaleFactor = _scaleFactor;
			_absRotation = _rotation;
			_absPosition = _position;
		}
	}
}