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

private:

    void passLights(Camera *camera);
    void passMeshes(Camera *camera);
    void passBlit();

    // Shaders
    ShaderProgram *deferredGeometry = nullptr;
    ShaderProgram *blitProgram = nullptr;
    ShaderProgram *deferredLight = nullptr;

    GLuint fboPosition = 0;
    GLuint fboNormal = 0;
    GLuint fboAlbedo = 0;
    GLuint fboDepth = 0;
    FramebufferObject *fbo = nullptr;
};

#endif // DEFERREDRENDERER_H
