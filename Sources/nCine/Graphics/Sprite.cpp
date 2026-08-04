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
		setTexRect(other._texRect);
	}

	void Sprite::init()
	{
		ZoneScopedC(0x81A861);
		/*if (_texture != nullptr && _texture->name() != nullptr){
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(_texture->name(), nctl::strnlen(_texture->name(), Object::MaxNameLength));
		}*/

		_type = ObjectType::Sprite;
		_renderCommand.SetType(RenderCommand::Type::Sprite);

		Material::ShaderProgramType shaderProgramType = (_texture != nullptr ? Material::ShaderProgramType::Sprite : Material::ShaderProgramType::SpriteNoTexture);
		_renderCommand.GetMaterial().SetShaderProgramType(shaderProgramType);
		shaderHasChanged();
		_renderCommand.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

		if (_texture != nullptr) {
			setTexRect(Recti(0, 0, _texture->GetWidth(), _texture->GetHeight()));
		}
	}

	void Sprite::textureHasChanged(Texture* newTexture)
	{
		if (_renderCommand.GetMaterial().GetShaderProgramType() != Material::ShaderProgramType::Custom) {
			Material::ShaderProgramType shaderProgramType = (newTexture != nullptr ? Material::ShaderProgramType::Sprite : Material::ShaderProgramType::SpriteNoTexture);
			const bool hasChanged = _renderCommand.GetMaterial().SetShaderProgramType(shaderProgramType);
			if (hasChanged) {
				shaderHasChanged();
			}
		}

		if (newTexture != nullptr) {
			if (_texture != nullptr && _texture != newTexture) {
				// Trying to keep the old texture rectangle aspect ratio
				Recti texRect = _texRect;
				texRect.X = (int)((texRect.X / float(_texture->GetWidth())) * float(newTexture->GetWidth()));
				texRect.Y = (int)((texRect.Y / float(_texture->GetHeight())) * float(newTexture->GetHeight()));
				texRect.W = (int)((texRect.W / float(_texture->GetWidth())) * float(newTexture->GetWidth()));
				texRect.H = (int)((texRect.H / float(_texture->GetHeight())) * float(newTexture->GetHeight()));
				setTexRect(texRect); // it also sets _width and _height
			} else {
				// Assigning a new texture where there wasn't any or reassigning the same texture (that might have changed size)
				setTexRect(Recti(0, 0, newTexture->GetWidth(), newTexture->GetHeight()));
			}
		}
	}
}
