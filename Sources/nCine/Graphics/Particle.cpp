#include "Particle.h"
#include "RenderCommand.h"

namespace nCine
{
	Particle::Particle(SceneNode* parent, Texture* texture)
		: Sprite(parent, texture), _life(0.0f), startingLife(0.0f), startingRotation(0.0f), _inLocalSpace(false)
	{
		_type = ObjectType::Particle;
		_renderCommand.SetType(RenderCommand::Type::Particle);
		setEnabled(false);
	}

	Particle::Particle(const Particle& other)
		: Sprite(other), _life(other._life), startingLife(other.startingLife), startingRotation(other.startingRotation), _inLocalSpace(other._inLocalSpace)
	{
		_type = ObjectType::Particle;
		_renderCommand.SetType(RenderCommand::Type::Particle);
	}

	void Particle::init(float life, Vector2f pos, Vector2f vel, float rot, bool inLocalSpace)
	{
		_life = life;
		startingLife = life;
		startingRotation = rot;
		setPosition(pos);
		_velocity = vel;
		setRotation(rot);
		_inLocalSpace = inLocalSpace;
		setEnabled(true);
	}

	void Particle::OnUpdate(float timeMult)
	{
		if (timeMult >= _life) {
			_life = 0.0f; // dead particle
			setEnabled(false);
		} else {
			_life -= timeMult;
			move(_velocity * timeMult);
		}
	}

	void Particle::transform()
	{
		SceneNode::transform();

		if (!_inLocalSpace) {
			_worldMatrix = _localMatrix;

			// Always independent movement
			_absScaleFactor = _scaleFactor;
			_absRotation = _rotation;
			_absPosition = _position;
		}
	}
}