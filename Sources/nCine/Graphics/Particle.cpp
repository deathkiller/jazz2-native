#include "Particle.h"
#include "RenderCommand.h"

namespace nCine
{
	Particle::Particle(SceneNode* parent, Texture* texture)
		: Sprite(parent, texture), life_(0.0f), startingLife(0.0f), startingRotation(0.0f), inLocalSpace_(false)
	{
		_type = ObjectType::Particle;
		renderCommand_.setType(RenderCommand::Type::Particle);
		setEnabled(false);
	}

	Particle::Particle(const Particle& other)
		: Sprite(other), life_(other.life_), startingLife(other.startingLife), startingRotation(other.startingRotation), inLocalSpace_(other.inLocalSpace_)
	{
		_type = ObjectType::Particle;
		renderCommand_.setType(RenderCommand::Type::Particle);
	}

	void Particle::init(float life, Vector2f pos, Vector2f vel, float rot, bool inLocalSpace)
	{
		life_ = life;
		startingLife = life;
		startingRotation = rot;
		setPosition(pos);
		velocity_ = vel;
		setRotation(rot);
		inLocalSpace_ = inLocalSpace;
		setEnabled(true);
	}

	void Particle::OnUpdate(float timeMult)
	{
		if (timeMult >= life_) {
			life_ = 0.0f; // dead particle
			setEnabled(false);
		} else {
			life_ -= timeMult;
			move(velocity_ * timeMult);
		}
	}

	void Particle::transform()
	{
		SceneNode::transform();

		if (!inLocalSpace_) {
			worldMatrix_ = localMatrix_;

			// Always independent movement
			absScaleFactor_ = scaleFactor_;
			absRotation_ = rotation_;
			absPosition_ = position_;
		}
	}
}