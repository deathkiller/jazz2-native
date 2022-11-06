#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#include "../../Primitives/Colorf.h"

namespace nCine
{
	/// A class to handle OpenGL clear color
	class GLClearColor
	{
	public:
		struct State
		{
			Colorf color = Colorf(0.0f, 0.0f, 0.0f, 0.0f);
		};

		static Colorf color() {
			return state_.color;
		}
		static void setColor(const Colorf& color);
		static void setColor(float red, float green, float blue, float alpha);

		static State state() {
			return state_;
		}
		static void setState(State newState);

	private:
		static State state_;

		/// Deleted default constructor
		GLClearColor() = delete;
		/// Deleted destructor
		~GLClearColor() = delete;

		/// Deleted copy constructor
		GLClearColor(const GLClearColor&) = delete;
		/// Deleted assignment operator
		GLClearColor& operator=(const GLClearColor&) = delete;
	};

}
