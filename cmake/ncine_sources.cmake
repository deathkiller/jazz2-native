set(SOURCES
	${NCINE_ROOT}/Jazz2/nCine/Base/BitArray.cpp
	${NCINE_ROOT}/Jazz2/nCine/Base/Clock.cpp
	${NCINE_ROOT}/Jazz2/nCine/Base/FrameTimer.cpp
	${NCINE_ROOT}/Jazz2/nCine/Base/Object.cpp
	${NCINE_ROOT}/Jazz2/nCine/Base/Random.cpp
	${NCINE_ROOT}/Jazz2/nCine/Base/Timer.cpp
	${NCINE_ROOT}/Jazz2/nCine/Base/TimeStamp.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/AnimatedSprite.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/BaseSprite.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/Camera.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/DrawableNode.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/Geometry.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GfxCapabilities.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLAttribute.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLBlending.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLBufferObject.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLClearColor.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLCullFace.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLDebug.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLDepthTest.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLFramebuffer.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLRenderbuffer.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLScissorTest.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLShader.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLShaderAttributes.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLShaderProgram.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLShaderUniformBlocks.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLShaderUniforms.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLTexture.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLUniform.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLUniformBlock.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLUniformBlockCache.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLUniformCache.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLVertexArrayObject.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLVertexFormat.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/GL/GLViewport.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/IGfxDevice.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/ITextureLoader.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/ITextureSaver.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/Material.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/MeshSprite.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/Particle.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/ParticleAffectors.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/ParticleInitializer.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/ParticleSystem.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RectAnimation.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderBatcher.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderBuffersManager.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderCommand.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderCommandPool.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderQueue.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderResources.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderStatistics.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/RenderVaoPool.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/SceneNode.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/ScreenViewport.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/Sprite.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/Texture.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextureFormat.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextureLoaderDds.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextureLoaderKtx.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextureLoaderPvr.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextureLoaderRaw.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextureLoaderPng.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextureLoaderQoi.cpp
	${NCINE_ROOT}/Jazz2/nCine/Graphics/Viewport.cpp
	${NCINE_ROOT}/Jazz2/nCine/Input/IInputManager.cpp
	${NCINE_ROOT}/Jazz2/nCine/Input/JoyMapping.cpp
	${NCINE_ROOT}/Jazz2/nCine/IO/FileSystem.cpp
	${NCINE_ROOT}/Jazz2/nCine/IO/IFileStream.cpp
	${NCINE_ROOT}/Jazz2/nCine/IO/MemoryFile.cpp
	${NCINE_ROOT}/Jazz2/nCine/IO/StandardFile.cpp
	${NCINE_ROOT}/Jazz2/nCine/Primitives/Color.cpp
	${NCINE_ROOT}/Jazz2/nCine/Primitives/Colorf.cpp
	${NCINE_ROOT}/Jazz2/nCine/Primitives/ColorHdr.cpp
	${NCINE_ROOT}/Jazz2/nCine/AppConfiguration.cpp
	${NCINE_ROOT}/Jazz2/nCine/Application.cpp
    ${NCINE_ROOT}/Jazz2/nCine/ArrayIndexer.cpp
    ${NCINE_ROOT}/Jazz2/nCine/ServiceLocator.cpp
#	${NCINE_ROOT}/Jazz2/nCine/Base/CString.cpp
#	${NCINE_ROOT}/Jazz2/nCine/Base/String.cpp
#	${NCINE_ROOT}/Jazz2/nCine/Base/Utf8.cpp
#	${NCINE_ROOT}/Jazz2/nCine/FileLogger.cpp
#	${NCINE_ROOT}/Jazz2/nCine/FntParser.cpp
#	${NCINE_ROOT}/Jazz2/nCine/Font.cpp
#	${NCINE_ROOT}/Jazz2/nCine/FontGlyph.cpp
#	${NCINE_ROOT}/Jazz2/nCine/Graphics/TextNode.cpp
)

list(APPEND SOURCES
    ${NCINE_ROOT}/Shared/SmallVector.cpp
    ${NCINE_ROOT}/Shared/Utf8.cpp
)

list(APPEND SOURCES
    ${NCINE_ROOT}/Jazz2/Main.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/ActorBase.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/ContentResolver.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/LevelHandler.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Player.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/PlayerCorpse.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/SolidObjectBase.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Collectibles/AmmoCollectible.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Collectibles/CarrotCollectible.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Collectibles/CoinCollectible.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Collectibles/CollectibleBase.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Collectibles/FoodCollectible.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Enemies/Bat.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Enemies/EnemyBase.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Enemies/Turtle.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Enemies/TurtleShell.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Environment/BonusWarp.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Environment/Checkpoint.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Environment/Spring.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Weapons/BlasterShot.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Weapons/BouncerShot.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Actors/Weapons/ShotBase.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Collisions/DynamicTree.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Collisions/DynamicTreeBroadPhase.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Events/EventMap.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Events/EventSpawner.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Tiles/TileMap.cpp
    ${NCINE_ROOT}/Jazz2/Jazz2/Tiles/TileSet.cpp
)