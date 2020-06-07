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
    void GenerateSSAOBlurFBO(int w, int h);

private:

    void passLights(Camera *camera);
    void passMeshes(Camera *camera);
    void passOutline(Camera *camera);
    void passGrid(Camera *camera);
    void passBlit();

    // Shaders
    ShaderProgram *deferredGeometry = nullptr;
    ShaderProgram* outlineGeometry = nullptr;
    ShaderProgram *gridProgram = nullptr;
    ShaderProgram *blitProgram = nullptr;
    ShaderProgram *deferredLight = nullptr;
    ShaderProgram *SSAOBlur = nullptr;

    GLuint fboPosition = 0;
    GLuint fboNormal = 0;
    GLuint fboAlbedo = 0;
    GLuint fboFinal = 0;
    GLuint fboDepth = 0;
    GLuint selectionTexture = 0;
    GLuint outlineTexture = 0;
    GLuint gridTexture = 0;
    GLuint SSAOBlurTexture = 0;

    FramebufferObject *fboGeometry = nullptr;
    FramebufferObject *fboLight = nullptr;
    FramebufferObject *fboOutline = nullptr;
    FramebufferObject *fboGrid = nullptr;
    FramebufferObject *SSAOBlurFBO = nullptr;

public:
     int width = 0;
     int height = 0;

     float selectedColor = 0.0;
};

#endif // DEFERREDRENDERER_H
