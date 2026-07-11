#pragma once

#include "RHI/RhiFwd.h"
#include "RenderQueue.h"
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
	class Texture;

	/**
		@brief Render target with its own scene root, camera and render queue
		
		Visits a scene graph from its root node through a camera and collects the resulting draw
		commands into its own render queue. A viewport can render into one or more textures (as
		multiple render targets), reuse the previous viewport's texture, or, as @ref ScreenViewport,
		draw to the screen. Viewports are chained and drawn in order before the screen.
	*/
	class Viewport
	{
		friend class Application;
		friend class ScreenViewport;

	public:
		/** @brief Type of viewport */
		enum class Type
		{
			/** @brief The viewport renders into one or more textures */
			WithTexture,
			/** @brief The viewport has no texture of its own and reuses the one from the previous viewport */
			NoTexture,
			/** @brief The viewport is the screen */
			Screen
		};

		/** @brief Clear mode for a viewport with a texture or for the screen */
		enum class ClearMode
		{
			/** @brief Cleared every time it is drawn */
			EveryDraw,
			/** @brief Cleared once per frame (default behavior) */
			EveryFrame,
			/** @brief Cleared only once, during this frame */
			ThisFrameOnly,
			/** @brief Cleared only once, during the next frame */
			NextFrameOnly,
			/** @brief Never cleared */
			Never
		};

		/** @brief Depth and stencil format for a viewport with a texture or for the screen (alias of the backend-neutral @ref nCine::DepthStencilFormat) */
		using DepthStencilFormat = nCine::DepthStencilFormat;

		/** @brief Creates a viewport with the specified name and texture, plus a depth and stencil renderbuffer */
		Viewport(const char* name, Texture* texture, DepthStencilFormat depthStencilFormat);
		/** @brief Creates a viewport with the specified texture, plus a depth and stencil renderbuffer */
		Viewport(Texture* texture, DepthStencilFormat depthStencilFormat);
		/** @brief Creates a viewport with the specified name and texture */
		Viewport(const char* name, Texture* texture);
		/** @brief Creates a viewport with the specified texture */
		explicit Viewport(Texture* texture);
		/** @brief Creates a viewport with no texture */
		Viewport();

		~Viewport();

		Viewport(const Viewport&) = delete;
		Viewport& operator=(const Viewport&) = delete;

		/** @brief Returns the viewport type */
		inline Type GetType() const {
			return type_;
		}

		/** @brief Returns the texture at the specified FBO color attachment index, or `nullptr` if none */
		Texture* GetTexture(std::uint32_t index);
		/** @brief Returns the texture at the first FBO color attachment index */
		inline Texture* GetTexture() {
			return textures_[0];
		}
		/** @brief Adds or removes a texture at the specified FBO color attachment index */
		bool SetTexture(std::uint32_t index, Texture* texture);
		/** @brief Adds or removes a texture at the first FBO color attachment index */
		inline bool SetTexture(Texture* texture) {
			return SetTexture(0, texture);
		}

		/** @brief Returns the depth and stencil format of the FBO renderbuffer */
		inline DepthStencilFormat GetDepthStencilFormat() const {
			return depthStencilFormat_;
		}
		/** @brief Sets the depth and stencil format of the FBO renderbuffer */
		bool SetDepthStencilFormat(DepthStencilFormat depthStencilFormat);

		/** @brief Removes all textures and the depth and stencil renderbuffer from the FBO */
		bool RemoveAllTextures();

		/** @brief Returns the FBO size, or a zero vector if no texture is present */
		inline Vector2i GetSize() const {
			return Vector2i(width_, height_);
		}
		/** @brief Returns the FBO width, or zero if no texture is present */
		inline std::int32_t GetWidth() const {
			return width_;
		}
		/** @brief Returns the FBO height, or zero if no texture is present */
		inline std::int32_t GetHeight() const {
			return height_;
		}

		/** @brief Returns the number of color attachments of the FBO */
		inline std::uint32_t GetColorAttachmentCount() const {
			return numColorAttachments_;
		}

		/** @brief Returns the OpenGL viewport rectangle */
		inline Recti GetViewportRect() const {
			return viewportRect_;
		}
		/** @brief Sets the OpenGL viewport rectangle */
		inline void SetViewportRect(const Recti& viewportRect) {
			viewportRect_ = viewportRect;
		}

		/** @brief Returns the OpenGL scissor test rectangle */
		inline Recti GetScissorRect() const {
			return scissorRect_;
		}
		/** @brief Sets the OpenGL scissor test rectangle */
		inline void SetScissorRect(const Recti& scissorRect) {
			scissorRect_ = scissorRect;
		}

		/** @brief Returns the rectangle used for screen culling */
		inline Rectf GetCullingRect() const {
			return cullingRect_;
		}

		/** @brief Returns the last frame this viewport was cleared */
		inline unsigned long int GetLastFrameCleared() const {
			return lastFrameCleared_;
		}

		/** @brief Returns the viewport clear mode */
		inline ClearMode GetClearMode() const {
			return clearMode_;
		}
		/** @brief Sets the viewport clear mode */
		inline void SetClearMode(ClearMode clearMode) {
			clearMode_ = clearMode;
		}

		/** @brief Returns the viewport clear color */
		inline Colorf GetClearColor() const {
			return clearColor_;
		}
		/** @brief Sets the viewport clear color through four float components */
		inline void SetClearColor(float red, float green, float blue, float alpha) {
			clearColor_.Set(red, green, blue, alpha);
		}
		/** @brief Sets the viewport clear color through a `Colorf` object */
		inline void SetClearColor(const Colorf& color) {
			clearColor_ = color;
		}

		/** @brief Returns the scene root node as constant */
		inline const SceneNode* GetRootNode() const {
			return rootNode_;
		}
		/** @brief Returns the scene root node */
		inline SceneNode* GetRootNode() {
			return rootNode_;
		}
		/** @brief Sets the scene root node */
		inline void SetRootNode(SceneNode* rootNode) {
			rootNode_ = rootNode;
		}

		/** @brief Returns the reverse-ordered array of viewports to be drawn before the screen */
		static SmallVectorImpl<Viewport*>& GetChain() {
			return chain_;
		}

		/** @brief Returns the camera used for rendering as constant */
		inline const Camera* GetCamera() const {
			return camera_;
		}
		/** @brief Returns the camera used for rendering */
		inline Camera* GetCamera() {
			return camera_;
		}
		/** @brief Sets the camera to be used for rendering */
		inline void SetCamera(Camera* camera) {
			camera_ = camera;
		}

		/** @brief Sets the debug object label for the viewport's render target */
		void SetRenderTargetLabel(const char* label);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Bit positions inside the state bitset */
		enum StateBitPositions
		{
			UpdatedBit = 0,
			VisitedBit = 1,
			CommittedBit = 2
		};

		/** @brief Reverse-ordered array of viewports to be drawn before the screen */
		static SmallVector<Viewport*> chain_;

		Type type_;

		std::int32_t width_;
		std::int32_t height_;
		Recti viewportRect_;
		Recti scissorRect_;
		Rectf cullingRect_;

		DepthStencilFormat depthStencilFormat_;

		/** @brief Last frame this viewport was cleared */
		std::uint32_t lastFrameCleared_;
		ClearMode clearMode_;
		Colorf clearColor_;

		/** @brief Render queue of commands for this viewport or render target */
		RenderQueue renderQueue_;

		std::unique_ptr<Rhi::RenderTarget> fbo_;

		static const std::uint32_t MaxNumTextures = 4;
		Texture* textures_[MaxNumTextures];

		/** @brief Root scene node for this viewport or render target */
		SceneNode* rootNode_;

		/**
		 * @brief Camera used by this viewport
		 *
		 * @note If set to `nullptr` the default camera is used.
		 */
		Camera* camera_;

		/** @brief Bitset that stores the various state bits */
		BitSet<std::uint8_t> stateBits_;
#endif

		void CalculateCullingRect();

		void Update();
		void Visit();
		void SortAndCommitQueue();
		void Draw(std::uint32_t nextIndex);

	private:
		std::uint32_t numColorAttachments_;

		void UpdateCulling(SceneNode* node);
	};
}
