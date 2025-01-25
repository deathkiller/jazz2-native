#pragma once

#include "../Primitives/Colorf.h"
#include "../Primitives/Vector2.h"
#include "../Primitives/Rect.h"
#include "../Base/BitSet.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class SceneNode;
	class Camera;
	class RenderQueue;
	class GLFramebuffer;
	class Texture;

	/// Handles a viewport and its corresponding render target texture
	class Viewport
	{
		friend class Application;
		friend class ScreenViewport;

	public:
		/// Types of viewports available
		enum class Type
		{
			/// The viewport renders in one or more textures
			WithTexture,
			/// The viewport has no texture of its own, it uses the one from the previous viewport
			NoTexture,
			/// The viewport is the screen
			Screen
		};

		/// Clear mode for a viewport with a texture or for the screen
		enum class ClearMode
		{
			/// The viewport is cleared every time it is drawn
			EveryDraw,
			/// The viewport is cleared once per frame (default behavior)
			EveryFrame,
			/// The viewport is cleared only once, at this frame
			ThisFrameOnly,
			/// The viewport is cleared only once, at next frame
			NextFrameOnly,
			/// The viewport is never cleared
			Never
		};

		/// Depth and stencil format for a viewport with a texture or for the screen
		enum class DepthStencilFormat
		{
			None,
			Depth16,
			Depth24,
			Depth24_Stencil8
		};

		/// Creates a new viewport with the specified name and texture, plus a depth and stencil renderbuffer
		Viewport(const char* name, Texture* texture, DepthStencilFormat depthStencilFormat);
		/// Creates a new viewport with the specified texture, plus a depth and stencil renderbuffer
		Viewport(Texture* texture, DepthStencilFormat depthStencilFormat);
		/// Creates a new viewport with the specified name and texture
		Viewport(const char* name, Texture* texture);
		/// Creates a new viewport with the specified texture
		explicit Viewport(Texture* texture);
		/// Creates a new viewport with no texture
		Viewport();

		~Viewport();

		Viewport(const Viewport&) = delete;
		Viewport& operator=(const Viewport&) = delete;

		/// Returns the viewport type
		inline Type type() const {
			return type_;
		}

		/// Returns the texture at the specified viewport's FBO color attachment index, if any
		Texture* texture(std::uint32_t index);
		/// Returns the texture at the first viewport's FBO color attachment index
		inline Texture* texture() {
			return textures_[0];
		}
		/// Adds or removes a texture at the specified viewport's FBO color attachment index
		bool setTexture(std::uint32_t index, Texture* texture);
		/// Adds or removes a texture at the first viewport's FBO color attachment index
		inline bool setTexture(Texture* texture) {
			return setTexture(0, texture);
		}

		/// Returns the depth and stencil format of the viewport's FBO renderbuffer
		inline DepthStencilFormat depthStencilFormat() const {
			return depthStencilFormat_;
		}
		/// Sets the depth and stencil format of the viewport's FBO renderbuffer
		bool setDepthStencilFormat(DepthStencilFormat depthStencilFormat);

		/// Removes all textures and the depth stencil renderbuffer from the viewport's FBO
		bool removeAllTextures();

		/// Returns viewport's FBO size as a `Vector2i` object, or a zero vector if no texture is present
		inline Vector2i size() const {
			return Vector2i(width_, height_);
		}
		/// Returns viewport's FBO width or zero if no texture is present
		inline std::int32_t width() const {
			return width_;
		}
		/// Returns viewport's FBO height or zero if no texture is present
		inline std::int32_t height() const {
			return height_;
		}

		/// Returns the number of color attachments of the viewport's FBO
		inline std::uint32_t numColorAttachments() const {
			return numColorAttachments_;
		}

		/// Returns the OpenGL viewport rectangle
		inline Recti viewportRect() const {
			return viewportRect_;
		}
		/// Sets the OpenGL viewport rectangle through a `Recti` object
		inline void setViewportRect(const Recti& viewportRect) {
			viewportRect_ = viewportRect;
		}

		/// Returns the OpenGL scissor test rectangle
		inline Recti scissorRect() const {
			return scissorRect_;
		}
		/// Sets the OpenGL scissor test rectangle through a `Recti` object
		inline void setScissorRect(const Recti& scissorRect) {
			scissorRect_ = scissorRect;
		}

		/// Returns the rectangle for screen culling
		inline Rectf cullingRect() const {
			return cullingRect_;
		}

		/// Returns the last frame this viewport was cleared
		inline unsigned long int lastFrameCleared() const {
			return lastFrameCleared_;
		}

		/// Returns the viewport clear mode
		inline ClearMode clearMode() const {
			return clearMode_;
		}
		/// Sets the viewport clear mode
		inline void setClearMode(ClearMode clearMode) {
			clearMode_ = clearMode;
		}

		/// Returns the viewport clear color as a `Colorf` object
		inline Colorf clearColor() const {
			return clearColor_;
		}
		/// Sets the viewport clear color through four floats
		inline void setClearColor(float red, float green, float blue, float alpha) {
			clearColor_.Set(red, green, blue, alpha);
		}
		/// Sets the viewport clear color through a `Colorf` object
		inline void setClearColor(const Colorf& color) {
			clearColor_ = color;
		}

		/// Returns the root node as a constant
		inline const SceneNode* rootNode() const {
			return rootNode_;
		}
		/// Returns the root node
		inline SceneNode* rootNode() {
			return rootNode_;
		}
		/// Sets the root node
		inline void setRootNode(SceneNode* rootNode) {
			rootNode_ = rootNode;
		}

		/// Returns the reverse ordered array of viewports to be drawn before the screen
		static SmallVectorImpl<Viewport*>& chain() {
			return chain_;
		}

		/// Returns the camera used for rendering as a constant
		inline const Camera* camera() const {
			return camera_;
		}
		/// Returns the camera used for rendering
		inline Camera* camera() {
			return camera_;
		}
		/// Sets the camera to be used for rendering
		inline void setCamera(Camera* camera) {
			camera_ = camera;
		}

		/// Sets the OpenGL object label for the viewport framebuffer object
		void setGLFramebufferLabel(const char* label);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/// Bit positions inside the state bitset
		enum StateBitPositions
		{
			UpdatedBit = 0,
			VisitedBit = 1,
			CommittedBit = 2
		};

		/// Reverse ordered array of viewports to be drawn before the screen
		static SmallVector<Viewport*> chain_;

		Type type_;

		std::int32_t width_;
		std::int32_t height_;
		Recti viewportRect_;
		Recti scissorRect_;
		Rectf cullingRect_;

		DepthStencilFormat depthStencilFormat_;

		/// Last frame this viewport was cleared
		std::uint32_t lastFrameCleared_;
		ClearMode clearMode_;
		Colorf clearColor_;

		/// Render queue of commands for this viewport/RT
		std::unique_ptr<RenderQueue> renderQueue_;

		std::unique_ptr<GLFramebuffer> fbo_;

		static const std::uint32_t MaxNumTextures = 4;
		Texture* textures_[MaxNumTextures];

		/// Root scene node for this viewport/RT
		SceneNode* rootNode_;

		/// Camera used by this viewport
		/*! \note If set to `nullptr` it will use the default camera */
		Camera* camera_;

		/// Bitset that stores the various states bits
		BitSet<std::uint8_t> stateBits_;
#endif

		void calculateCullingRect();

		void update();
		void visit();
		void sortAndCommitQueue();
		void draw(std::uint32_t nextIndex);

	private:
		std::uint32_t numColorAttachments_;

		void updateCulling(SceneNode* node);
	};
}
