//#ifndef CLASS_NCINE_NUKLEARDRAWING
//#define CLASS_NCINE_NUKLEARDRAWING
//
//#include "NuklearContext.h"
//#include "nuklear.h"
//
//#include <nctl/UniquePtr.h>
//#include "GLTexture.h"
//#include "Matrix4x4.h"
//
//namespace nCine {
//
//class GLShaderProgram;
//class GLShaderUniforms;
//class GLShaderAttributes;
//class GLBufferObject;
//class RenderCommand;
//class RenderQueue;
//
///// The class the handles Nuklear drawing
//class NuklearDrawing
//{
//  public:
//	explicit NuklearDrawing(bool withSceneGraph);
//
//	/// Bakes the Nuklear font atlas and uploads it to a texture
//	bool bakeFonts();
//
//	void newFrame();
//	/// Renders Nuklear with render commands
//	void endFrame(RenderQueue &renderQueue);
//	/// Renders Nuklear directly with OpenGL
//	void endFrame();
//
//  private:
//	bool withSceneGraph_;
//	std::unique_ptr<GLTexture> texture_;
//	std::unique_ptr<GLShaderProgram> nuklearShaderProgram_;
//
//	std::unique_ptr<GLBufferObject> vbo_;
//	std::unique_ptr<GLBufferObject> ibo_;
//
//	static const int UniformsBufferSize = 65;
//	unsigned char uniformsBuffer_[UniformsBufferSize];
//	std::unique_ptr<GLShaderUniforms> nuklearShaderUniforms_;
//	std::unique_ptr<GLShaderAttributes> nuklearShaderAttributes_;
//
//	int lastFrameWidth_;
//	int lastFrameHeight_;
//	Matrix4x4f projectionMatrix_;
//	uint16_t lastLayerValue_;
//
//	struct nk_convert_config config_;
//	struct nk_draw_null_texture null_;
//	std::unique_ptr<GLTexture> fontTex_;
//
//	RenderCommand *retrieveCommandFromPool();
//	void setupRenderCmd(RenderCommand &cmd);
//	void draw(RenderQueue &renderQueue);
//
//	void setupBuffersAndShader();
//	void draw();
//};
//
//}
//
//#endif
