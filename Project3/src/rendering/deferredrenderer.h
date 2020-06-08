#ifndef DEFERREDRENDERER_H
#define DEFERREDRENDERER_H

#include "renderer.h"
#include "gl.h"

class ShaderProgram;
class FramebufferObject;

class DeferredRenderer : public Renderer
{
public:

    DeferredRenderer();
    ~DeferredRenderer() override;

    void initialize() override;
    void finalize() override;

    void resize(int width, int height) override;
    void render(Camera *camera) override;

    void GenerateGeometryFBO(int w, int h);
    void GenerateLightFBO(int w, int h);
    void GenerateOutlineFBO(int w, int h);
    void GenerateGridFBO(int w, int h);
    void GenerateSSAOFBO(int w, int h);
    void GenerateSSAOBlurFBO(int w, int h);

private:

    void passLights(Camera *camera);
    void passMeshes(Camera *camera);
    void passOutline(Camera *camera);
    void passGrid(Camera *camera);
    void passSSAO(Camera *camera);
    void passSSAOBlur();
    void passBlit();

    float Lerp(float a, float b, float f);
    void GenerateSSAOTextures();

    void RenderGeometry(Camera* camera);
    void RenderOutline(Camera* camera);
    void RenderSSAO(Camera* camera);
    void RenderLight(Camera* camera);
    void RenderGrid(Camera* camera);
    void RenderSSAOBlur(Camera *camera);
    void StoreSelectionPixels();

private:

    // Shaders
    ShaderProgram *deferredGeometry = nullptr;
    ShaderProgram* outlineGeometry = nullptr;
    ShaderProgram *gridProgram = nullptr;
    ShaderProgram *blitProgram = nullptr;
    ShaderProgram *deferredLight = nullptr;
    ShaderProgram* SSAOProgram = nullptr;
    ShaderProgram *SSAOBlur = nullptr;

    GLuint texturePosition = 0;
    GLuint textureNormal = 0;
    GLuint textureAlbedo = 0;
    GLuint textureFinal = 0;
    GLuint textureDepth = 0;
    GLuint textureSelection = 0;
    GLuint textureOutline = 0;
    GLuint textureGrid = 0;
    GLuint textureSSAOBlur = 0;
    GLuint textureWorldPos = 0;
    GLuint textureSSAO = 0;
    GLuint textureMPosition = 0;
    GLuint textureMNormals = 0;

    GLuint depthAttachment = 0;

    FramebufferObject *fboGeometry = nullptr;
    FramebufferObject *fboLight = nullptr;
    FramebufferObject *fboOutline = nullptr;
    FramebufferObject *fboGrid = nullptr;
    FramebufferObject* fboSSAO= nullptr;
    FramebufferObject *SSAOBlurFBO = nullptr;


    // SSAO
    std::vector<QVector3D> ssaoKernel;
    GLuint noiseTexture = 0;

public:
     int width = 0;
     int height = 0;

     float selectedColor = 0.0;
};

#endif // DEFERREDRENDERER_H
