#include <Rendering/RenderPass.h>
class Quad;

namespace PostProcessing
{
	/*Box Blur using quadric interpolation*/
	class BoxBlur
	{
	public:
		BoxBlur(int width, int height, Quad* quad = nullptr);
		~BoxBlur();

		std::vector<GLuint> m_mipmapFBOHandles;
		GLuint m_mipmapTextureHandle;

		ShaderProgram m_pushShaderProgram;
		
		void pull(); // generate mipmaps from current content of level 0
		void push(int numLevels, int beginLevel = 0); // blur levels (beginLevel + numLevels) down to beginLevel
		
		const int m_width;
		const int m_height;
	private:
		Quad* m_quad;
		bool ownQuad;
	};

	/*Box Blur using quadric interpolation*/
	class DepthOfField
	{
	public:
		DepthOfField(int width, int height, Quad* quad = nullptr);
		~DepthOfField();

		FrameBufferObject* m_cocFBO;  
		FrameBufferObject* m_hDofFBO; // horizontal pass
		FrameBufferObject* m_vDofFBO; // vertical pass
		FrameBufferObject* m_dofCompFBO; // composed image

		ShaderProgram m_calcCoCShader;
		ShaderProgram m_dofShader;
		ShaderProgram m_dofCompShader;

		void execute(GLuint positionMap, GLuint colorMap); 
		
		const int m_width;
		const int m_height;

		// variables / parameters
		glm::vec4 m_focusPlaneDepths;
		glm::vec2 m_focusPlaneRadi;
		float m_farRadiusRescale;

		bool m_disable_near_field;
		bool m_disable_far_field;

		// Imgui
		void imguiInterfaceEditParameters();
		void updateUniforms(); // update uniforms with current member variables values

	private:
		Quad* m_quad;
		bool ownQuad;
	};

	/* renders a SkyBox */
	class SkyboxRendering
	{
	public:
		SkyboxRendering(std::string fShader = "/cubemap/cubemap.frag", std::string vShader = "/cubemap/cubemap.vert", Renderable* skybox = nullptr);
		~SkyboxRendering();

		Renderable* m_skybox;
		ShaderProgram m_skyboxShader;
		void render(GLuint cubeMapTexture, FrameBufferObject* target = nullptr);
	private:
		bool ownSkybox;
	};

	class SunOcclusionQuery
	{
	public:
		GLuint mQueryId;
		GLuint lastNumVisiblePixels;

		SunOcclusionQuery(GLuint depthTexture = -1, glm::vec2 textureSize = glm::vec2(1.0f,1.0f), Renderable* sun = nullptr);
		~SunOcclusionQuery();

		GLuint performQuery(const glm::vec4& sunScreenPos);

		ShaderProgram m_occlusionShader;
		FrameBufferObject* m_occlusionFBO;

	private:
		Renderable* m_sun;
		bool ownRenderable;

	};

	class LensFlare
	{
	public:
		ShaderProgram m_downSampleShader; // produces dark downsampled version of input
		ShaderProgram m_ghostingShader; // produces "ghosts"
		ShaderProgram m_upscaleBlendShader; // produces lens flares

		LensFlare(int width, int height);
		~LensFlare();

		void renderLensFlare(GLuint sourceTexture, FrameBufferObject* target = nullptr);

		GLuint m_lensColorTexture;
		GLuint m_lensStarTexture;
		GLuint m_lensDirtTexture;

		FrameBufferObject* m_downSampleFBO;
		FrameBufferObject* m_featuresFBO; // aka lens flares / ghosts

		BoxBlur* m_boxBlur;

		glm::mat3 updateLensStarMatrix(const glm::mat3& view);
		glm::mat3 updateLensStarMatrix(const glm::mat4& view);

		// variables
		int m_blur_strength;
		float m_scale;
		float m_bias;
		int m_num_ghosts;
		float m_ghost_dispersal;
		float m_halo_width;
		float m_distortion;
		float m_strength;
		
		// Imgui
		void imguiInterfaceEditParameters();
		void updateUniforms(); // update uniforms with current member variables values

	private:
		Quad m_quad;
		GLuint loadLensColorTexture(std::string resourcesPath = RESOURCES_PATH);
	};

} // namespace PostProcessing
