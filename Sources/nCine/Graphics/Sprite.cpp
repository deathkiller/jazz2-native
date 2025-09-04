#include "Sprite.h"
#include "RenderCommand.h"
#include "../tracy.h"

namespace nCine
{
	Sprite::Sprite()
		: Sprite(nullptr, nullptr, 0.0f, 0.0f)
	{
	}

	Sprite::Sprite(SceneNode* parent, Texture* texture)
		: Sprite(parent, texture, 0.0f, 0.0f)
	{
	}

	Sprite::Sprite(Texture* texture)
		: Sprite(nullptr, texture, 0.0f, 0.0f)
	{
	}

	Sprite::Sprite(SceneNode* parent, Texture* texture, float xx, float yy)
		: BaseSprite(parent, texture, xx, yy)
	{
		init();
	}

	Sprite::Sprite(SceneNode* parent, Texture* texture, Vector2f position)
		: Sprite(parent, texture, position.X, position.Y)
	{
	}

	Sprite::Sprite(Texture* texture, float xx, float yy)
		: Sprite(nullptr, texture, xx, yy)
	{
	}

	Sprite::Sprite(Texture* texture, Vector2f position)
		: Sprite(nullptr, texture, position.X, position.Y)
	{
	}

	Sprite::Sprite(const Sprite& other)
		: BaseSprite(other)
	{
		init();
		setTexRect(other.texRect_);
	}

	void Sprite::init()
	{
		ZoneScopedC(0x81A861);
		/*if (texture_ != nullptr && texture_->name() != nullptr){
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(texture_->name(), nctl::strnlen(texture_->name(), Object::MaxNameLength));
		}*/

		_type = ObjectType::Sprite;
		renderCommand_.SetType(RenderCommand::Type::Sprite);

		Material::ShaderProgramType shaderProgramType = (texture_ != nullptr ? Material::ShaderProgramType::Sprite : Material::ShaderProgramType::SpriteNoTexture);
		renderCommand_.GetMaterial().SetShaderProgramType(shaderProgramType);
		shaderHasChanged();
		renderCommand_.GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

		if (texture_ != nullptr) {
			setTexRect(Recti(0, 0, texture_->GetWidth(), texture_->GetHeight()));
		}
	}

	void Sprite::textureHasChanged(Texture* newTexture)
	{
		if (renderCommand_.GetMaterial().GetShaderProgramType() != Material::ShaderProgramType::Custom) {
			Material::ShaderProgramType shaderProgramType = (newTexture != nullptr ? Material::ShaderProgramType::Sprite : Material::ShaderProgramType::SpriteNoTexture);
			const bool hasChanged = renderCommand_.GetMaterial().SetShaderProgramType(shaderProgramType);
			if (hasChanged) {
				shaderHasChanged();
			}
		}

		if (newTexture != nullptr) {
			if (texture_ != nullptr && texture_ != newTexture) {
				// Trying to keep the old texture rectangle aspect ratio
				Recti texRect = texRect_;
				texRect.X = (int)((texRect.X / float(texture_->GetWidth())) * float(newTexture->GetWidth()));
				texRect.Y = (int)((texRect.Y / float(texture_->GetHeight())) * float(newTexture->GetHeight()));
				texRect.W = (int)((texRect.W / float(texture_->GetWidth())) * float(newTexture->GetWidth()));
				texRect.H = (int)((texRect.H / float(texture_->GetHeight())) * float(newTexture->GetHeight()));
				setTexRect(texRect); // it also sets width_ and height_
			} else {
				// Assigning a new texture where there wasn't any or reassigning the same texture (that might have changed size)
				setTexRect(Recti(0, 0, newTexture->GetWidth(), newTexture->GetHeight()));
			}
		}
	}
}
