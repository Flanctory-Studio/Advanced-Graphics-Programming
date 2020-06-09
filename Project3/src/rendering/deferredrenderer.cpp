#include "deferredrenderer.h"
#include "miscsettings.h"
#include "ecs/scene.h"
#include "ecs/camera.h"
#include "resources/material.h"
#include "resources/mesh.h"
#include "resources/texture.h"
#include "resources/shaderprogram.h"
#include "resources/resourcemanager.h"
#include "framebufferobject.h"
#include "gl.h"
#include "globals.h"
#include <QVector>
#include <QVector3D>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>

#include <iostream>
#include <random>

static void sendLightsToProgram(QOpenGLShaderProgram &program, const QMatrix4x4 &viewMatrix)
{
    QVector<int> lightType;
    QVector<QVector3D> lightPosition;
    QVector<QVector3D> lightDirection;
    QVector<QVector3D> lightColor;
    for (auto entity : scene->entities)
    {
        if (entity->active && entity->lightSource != nullptr)
        {
            auto light = entity->lightSource;
            lightType.push_back(int(light->type));
            lightPosition.push_back(QVector3D(viewMatrix * entity->transform->matrix() * QVector4D(0.0, 0.0, 0.0, 1.0)));
            lightDirection.push_back(QVector3D(viewMatrix * entity->transform->matrix() * QVector4D(0.0, 1.0, 0.0, 0.0)));
            QVector3D color(light->color.redF(), light->color.greenF(), light->color.blueF());
            lightColor.push_back(color * light->intensity);
        }
    }
    if (lightPosition.size() > 0)
    {
        program.setUniformValueArray("lightType", &lightType[0], lightType.size());
        program.setUniformValueArray("lightPosition", &lightPosition[0], lightPosition.size());
        program.setUniformValueArray("lightDirection", &lightDirection[0], lightDirection.size());
        program.setUniformValueArray("lightColor", &lightColor[0], lightColor.size());
    }
    program.setUniformValue("lightCount", lightPosition.size());
}

float DeferredRenderer::Lerp(float a, float b, float f)
{
    return a + f * (b - a);
}

void DeferredRenderer::GenerateSSAOTextures()
{
    if(noiseTexture == 0)
    {
        // generate sample kernel
        // ----------------------
        std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0); // generates random floats between 0.0 and 1.0
        std::default_random_engine generator;
        for (unsigned int i = 0; i < 64; ++i)
        {
            QVector3D sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
            sample.normalize();
            sample *= randomFloats(generator);
            float scale = float(i) / 64.0;

            // scale samples s.t. they're more aligned to center of kernel
            scale = Lerp(0.1f, 1.0f, scale * scale);
            sample *= scale;
            ssaoKernel.push_back(sample);
        }

        // generate noise texture
        // ----------------------
        std::vector<QVector3D> ssaoNoise;
        for (unsigned int i = 0; i < 16; i++)
        {
            QVector3D noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f); // rotate around z-axis (in tangent space)
            ssaoNoise.push_back(noise);
        }
        gl->glGenTextures(1, &noiseTexture);
        gl->glBindTexture(GL_TEXTURE_2D, noiseTexture);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
}

DeferredRenderer::DeferredRenderer() :
    texturePosition(QOpenGLTexture::Target2D),
    textureFinal(QOpenGLTexture::Target2D),
    textureDepth(QOpenGLTexture::Target2D),
    textureSelection(QOpenGLTexture::Target2D)
{
    fboGeometry = nullptr;
    fboLight = nullptr;

    // List of textures
    addTexture("Final");
    addTexture("Position");
    addTexture("Model Position");
    addTexture("Normals");
    addTexture("Model Normals");
    addTexture("Albedo");
    addTexture("Depth");
    addTexture("Selection");
    addTexture("Outline");
    //addTexture("Grid");
    addTexture("GlobalPos");
    addTexture("SSAO");
    addTexture("SSAO Blur");

    rendererType = RendererType::DEFERRED;
}

DeferredRenderer::~DeferredRenderer()
{
    delete fboGeometry;
    delete fboLight;
    delete fboOutline;
    delete fboGrid;
}

void DeferredRenderer::initialize()
{
    OpenGLErrorGuard guard(__FUNCTION__);

    // Create shader programs
    deferredGeometry = resourceManager->createShaderProgram();
    deferredGeometry->name = "Deferred Geometry";
    deferredGeometry->vertexShaderFilename = "res/shaders/deferred_shading.vert";
    deferredGeometry->fragmentShaderFilename = "res/shaders/deferred_shading.frag";
    deferredGeometry->includeForSerialization = false;

    outlineGeometry = resourceManager->createShaderProgram();
    outlineGeometry->name = "Outline";
    outlineGeometry->vertexShaderFilename = "res/shaders/outline.vert";
    outlineGeometry->fragmentShaderFilename = "res/shaders/outline.frag";
    outlineGeometry->includeForSerialization = false;

    deferredLight = resourceManager->createShaderProgram();
    deferredLight->name = "Deferred Light";
    deferredLight->vertexShaderFilename = "res/shaders/light_pass.vert";
    deferredLight->fragmentShaderFilename = "res/shaders/light_pass.frag";
    deferredLight->includeForSerialization = false;

    blitProgram = resourceManager->createShaderProgram();
    blitProgram->name = "Blit";
    blitProgram->vertexShaderFilename = "res/shaders/blit.vert";
    blitProgram->fragmentShaderFilename = "res/shaders/blit.frag";
    blitProgram->includeForSerialization = false;

    gridProgram = resourceManager->createShaderProgram();
    gridProgram->name = "Grid Program";
    gridProgram->vertexShaderFilename = "res/shaders/grid.vert";
    gridProgram->fragmentShaderFilename = "res/shaders/grid.frag";
    gridProgram->includeForSerialization = false;

    SSAOProgram = resourceManager->createShaderProgram();
    SSAOProgram->name = "SSAO Program";
    SSAOProgram->vertexShaderFilename = "res/shaders/ssao.vert";
    SSAOProgram->fragmentShaderFilename = "res/shaders/ssao.frag";
    SSAOProgram->includeForSerialization = false;

    SSAOBlur = resourceManager->createShaderProgram();
    SSAOBlur->name = "SSAO Blur Program";
    SSAOBlur->vertexShaderFilename = "res/shaders/ssao.vert";
    SSAOBlur->fragmentShaderFilename = "res/shaders/ssao_blur.frag";
    SSAOBlur->includeForSerialization = false;

    // Create FBO
    fboGeometry = new FramebufferObject();
    fboGeometry->create();

    fboLight = new FramebufferObject();
    fboLight->create();

    fboOutline = new FramebufferObject();
    fboOutline->create();

    fboGrid = new FramebufferObject();
    fboGrid->create();

    fboSSAO = new FramebufferObject();
    fboSSAO->create();

    SSAOBlurFBO = new FramebufferObject();
    SSAOBlurFBO->create();
}

void DeferredRenderer::finalize()
{
    fboGeometry->destroy();
    delete fboGeometry;

    fboLight->destroy();
    delete fboLight;

    fboOutline->destroy();
    delete fboOutline;

    fboGrid->destroy();
    delete fboGrid;

    fboSSAO->destroy();
    delete fboSSAO;

    SSAOBlurFBO->destroy();
    delete SSAOBlurFBO;
}

void DeferredRenderer::GenerateGeometryFBO(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    if (texturePosition != 0) gl->glDeleteTextures(1, &texturePosition);
    gl->glGenTextures(1, &texturePosition);
    gl->glBindTexture(GL_TEXTURE_2D, texturePosition);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (textureNormal != 0) gl->glDeleteTextures(1, &textureNormal);
    gl->glGenTextures(1, &textureNormal);
    gl->glBindTexture(GL_TEXTURE_2D, textureNormal);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (textureAlbedo != 0) gl->glDeleteTextures(1, &textureAlbedo);
    gl->glGenTextures(1, &textureAlbedo);
    gl->glBindTexture(GL_TEXTURE_2D, textureAlbedo);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (textureSelection != 0) gl->glDeleteTextures(1, &textureSelection);
    gl->glGenTextures(1, &textureSelection);
    gl->glBindTexture(GL_TEXTURE_2D, textureSelection);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (textureDepth != 0) gl->glDeleteTextures(1, &textureDepth);
    gl->glGenTextures(1, &textureDepth);
    gl->glBindTexture(GL_TEXTURE_2D, textureDepth);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (textureWorldPos != 0) gl->glDeleteTextures(1, &textureWorldPos);
    gl->glGenTextures(1, &textureWorldPos);
    gl->glBindTexture(GL_TEXTURE_2D, textureWorldPos);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (textureMPosition != 0) gl->glDeleteTextures(1, &textureMPosition);
    gl->glGenTextures(1, &textureMPosition);
    gl->glBindTexture(GL_TEXTURE_2D, textureMPosition);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (textureMNormals != 0) gl->glDeleteTextures(1, &textureMNormals);
    gl->glGenTextures(1, &textureMNormals);
    gl->glBindTexture(GL_TEXTURE_2D, textureMNormals);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (depthAttachment != 0) gl->glDeleteTextures(1, &depthAttachment);
    gl->glGenTextures(1, &depthAttachment);
    gl->glBindTexture(GL_TEXTURE_2D, depthAttachment);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo
    fboGeometry->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3,
        GL_COLOR_ATTACHMENT4,
        GL_COLOR_ATTACHMENT5,
        GL_COLOR_ATTACHMENT6,
        GL_COLOR_ATTACHMENT7,
    };
    gl->glDrawBuffers(8, buffs);

    fboGeometry->addColorAttachment(0, texturePosition);
    fboGeometry->addColorAttachment(1, textureNormal);
    fboGeometry->addColorAttachment(2, textureAlbedo);
    fboGeometry->addColorAttachment(3, textureSelection);
    fboGeometry->addColorAttachment(4, textureWorldPos);
    fboGeometry->addColorAttachment(5, textureDepth);
    fboGeometry->addColorAttachment(6, textureMPosition);
    fboGeometry->addColorAttachment(7, textureMNormals);
    fboGeometry->addDepthAttachment(depthAttachment);
    fboGeometry->checkStatus();
    fboGeometry->release();
}

void DeferredRenderer::GenerateLightFBO(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    if (textureFinal == 0) gl->glDeleteTextures(1, &textureFinal);
    gl->glGenTextures(1, &textureFinal);
    gl->glBindTexture(GL_TEXTURE_2D, textureFinal);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo

    fboLight->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0
    };
    gl->glDrawBuffers(1, buffs);

    fboLight->addColorAttachment(0, textureFinal);
    fboLight->checkStatus();
    fboLight->release();
}

void DeferredRenderer::GenerateOutlineFBO(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    if (textureOutline != 0) gl->glDeleteTextures(1, &textureOutline);
    gl->glGenTextures(1, &textureOutline);
    gl->glBindTexture(GL_TEXTURE_2D, textureOutline);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo
    fboOutline->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0
    };
    gl->glDrawBuffers(1, buffs);

    fboOutline->addColorAttachment(0, textureOutline);
    fboOutline->checkStatus();
    fboOutline->release();
}

void DeferredRenderer::GenerateGridFBO(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    if (textureGrid != 0) gl->glDeleteTextures(1, &textureGrid);
    gl->glGenTextures(1, &textureGrid);
    gl->glBindTexture(GL_TEXTURE_2D, textureGrid);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo
    fboGrid->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0
    };
    gl->glDrawBuffers(1, buffs);

//    OpenGLState state;
//    state.depthTest = true;
//    state.blending = true;
//    state.blendFuncSrc = GL_SRC_ALPHA;
//    state.blendFuncDst = GL_ONE_MINUS_SRC_ALPHA;
//    state.apply();

    fboGrid->addColorAttachment(0, textureGrid);
    fboGrid->checkStatus();
    fboGrid->release();
}

void DeferredRenderer::GenerateSSAOFBO(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    if (textureSSAO != 0) gl->glDeleteTextures(1, &textureSSAO);
    gl->glGenTextures(1, &textureSSAO);
    gl->glBindTexture(GL_TEXTURE_2D, textureSSAO);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo
    fboSSAO->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0
    };
    gl->glDrawBuffers(1, buffs);

    fboSSAO->addColorAttachment(0, textureSSAO);
    fboSSAO->checkStatus();
    fboSSAO->release();
}

void DeferredRenderer::GenerateSSAOBlurFBO(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    if (textureSSAOBlur != 0) gl->glDeleteTextures(1, &textureSSAOBlur);
    gl->glGenTextures(1, &textureSSAOBlur);
    gl->glBindTexture(GL_TEXTURE_2D, textureSSAOBlur);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo
    SSAOBlurFBO->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0
    };
    gl->glDrawBuffers(1, buffs);

    SSAOBlurFBO->addColorAttachment(0, textureSSAOBlur);
    SSAOBlurFBO->checkStatus();
    SSAOBlurFBO->release();
}

void DeferredRenderer::resize(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    GenerateSSAOTextures();

    // Regenerate render targets

    // fbo Geometry
    GenerateGeometryFBO(w, h);

    // fbo Light
    GenerateLightFBO(w, h);

    // fbo Outline
    GenerateOutlineFBO(w, h);

    // fbo Grid
    GenerateGridFBO(w, h);

    // fbo SSAO
    GenerateSSAOFBO(w, h);

    //fbo SSAO Blur
    GenerateSSAOBlurFBO(w, h);

    width = w;
    height = h;
}

void DeferredRenderer::RenderGeometry(Camera *camera)
{
    fboGeometry->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Passes
    passMeshes(camera);

    fboGeometry->release();
}

void DeferredRenderer::RenderOutline(Camera *camera)
{
    fboOutline->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Passes
    if (miscSettings->useOutline)
    {
        passOutline(camera);
    }
    fboOutline->release();
}

void DeferredRenderer::RenderSSAO(Camera *camera)
{
    fboSSAO->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0, 1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //SSAO
    if (miscSettings->useSSAO)
    {
        passSSAO(camera);
    }

    fboSSAO->release();
}

void DeferredRenderer::RenderSSAOBlur(Camera *camera)
{
    SSAOBlurFBO->bind();

    //Clear Color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0, 1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //SSAO Blur
    if (miscSettings->useSSAO)
    {
        passSSAOBlur();
    }

    SSAOBlurFBO->release();
}

void DeferredRenderer::RenderLight(Camera *camera)
{
    fboLight->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);

    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    passLights(camera);

    fboLight->release();
}

void DeferredRenderer::RenderGrid(Camera *camera)
{
    fboGrid->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0, 1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //Grid
    passGrid(camera);

    fboGrid->release();
}

void DeferredRenderer::StoreSelectionPixels()
{
    fboGeometry->bind();

    gl->glReadBuffer(GL_COLOR_ATTACHMENT0 + 3);

    int pixelsSize = width * height;
    selectionPixels.resize(pixelsSize);

    gl->glReadPixels(0, 0, width, height, GL_RED, GL_FLOAT, selectionPixels.data());

    gl->glReadBuffer(0);
    fboGeometry->release();
}

void DeferredRenderer::render(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    RenderGeometry(camera);

    RenderOutline(camera);

    RenderSSAO(camera);

    RenderSSAOBlur(camera);

    RenderLight(camera);

    RenderGrid(camera);

    gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    passBlit();

    //Store the new selection texture pixels
    StoreSelectionPixels();
}

void DeferredRenderer::passMeshes(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    QOpenGLShaderProgram &program = deferredGeometry->program;

    if (program.bind())
    {
        sendLightsToProgram(program, camera->viewMatrix);

        QVector<MeshRenderer*> meshRenderers;
        QVector<LightSource*> lightSources;

        // Get components
        for (auto entity : scene->entities)
        {
            if (entity->active)
            {
                if (entity->meshRenderer != nullptr) { meshRenderers.push_back(entity->meshRenderer); }
                if (entity->lightSource != nullptr) { lightSources.push_back(entity->lightSource); }
            }
        }

        // Meshes
        for (int i = 0; i < meshRenderers.size(); ++i)
        {
            float percent = (i + 1.0f) / meshRenderers.size();

            auto meshRenderer = meshRenderers[i];
            auto mesh = meshRenderer->mesh;

            if (mesh != nullptr)
            {
                QMatrix3x3 normalMatrix = (camera->viewMatrix * meshRenderer->entity->transform->matrix()).normalMatrix();

                program.setUniformValue("viewMatrix", camera->viewMatrix);
                program.setUniformValue("normalMatrix", normalMatrix);
                program.setUniformValue("modelMatrix", meshRenderer->entity->transform->matrix());
                program.setUniformValue("projectionMatrix", camera->projectionMatrix);

                program.setUniformValue("uWorldPos", meshRenderer->entity->transform->position);

                int materialIndex = 0;
                for (auto submesh : mesh->submeshes)
                {
                    // Get material from the component
                    Material *material = nullptr;
                    if (materialIndex < meshRenderer->materials.size()) {
                        material = meshRenderer->materials[materialIndex];
                    }
                    if (material == nullptr) {
                        material = resourceManager->materialWhite;
                    }
                    materialIndex++;

                    #define SEND_TEXTURE(uniformName, tex1, tex2, texUnit) \
                        program.setUniformValue(uniformName, texUnit); \
                        if (tex1 != nullptr) { \
                        tex1->bind(texUnit); \
                                    } else { \
                        tex2->bind(texUnit); \
                        }

                    // Send the material to the shader
                    program.setUniformValue("albedo", material->albedo);
                    program.setUniformValue("emissive", material->emissive);
                    program.setUniformValue("specular", material->specular);
                    program.setUniformValue("smoothness", material->smoothness);
                    program.setUniformValue("bumpiness", material->bumpiness);
                    program.setUniformValue("tiling", material->tiling);

                    program.setUniformValue("selectionColor", percent);
                    program.setUniformValue("nearPlane", camera->znear);
                    program.setUniformValue("farPlane", camera->zfar);

                    SEND_TEXTURE("albedoTexture", material->albedoTexture, resourceManager->texWhite, 0);
                    SEND_TEXTURE("emissiveTexture", material->emissiveTexture, resourceManager->texBlack, 1);
                    SEND_TEXTURE("specularTexture", material->specularTexture, resourceManager->texBlack, 2);
                    SEND_TEXTURE("normalTexture", material->normalsTexture, resourceManager->texNormal, 3);
                    SEND_TEXTURE("bumpTexture", material->bumpTexture, resourceManager->texWhite, 4);

                    submesh->draw();
                }
            }
        }
        program.release();
    }
}

void DeferredRenderer::passOutline(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    QOpenGLShaderProgram &program = outlineGeometry->program;

    if (program.bind())
    {
        program.setUniformValue("viewMatrix", camera->viewMatrix);
        program.setUniformValue("projectionMatrix", camera->projectionMatrix);

        sendLightsToProgram(program, camera->viewMatrix);

        QList<Entity*> entities = selection->GetEntities();
        QVector<MeshRenderer*> meshRenderers;

        // Get components
        for (int i = 0; i < entities.size(); ++i) {
            if (entities.at(i)->active)
            {
                if (entities.at(i)->meshRenderer != nullptr) { meshRenderers.push_back(entities.at(i)->meshRenderer); }
            }
        }

        // Meshes
        for (int i = 0; i < meshRenderers.size(); ++i)
        {

            auto meshRenderer = meshRenderers[i];
            auto mesh = meshRenderer->mesh;

            if (mesh != nullptr)
            {
                QMatrix4x4 worldMatrix = meshRenderer->entity->transform->matrix();
                QMatrix4x4 worldViewMatrix = camera->viewMatrix * worldMatrix;
                QMatrix3x3 normalMatrix = worldViewMatrix.normalMatrix();

                program.setUniformValue("worldMatrix", worldMatrix);
                program.setUniformValue("worldViewMatrix", worldViewMatrix);
                program.setUniformValue("normalMatrix", normalMatrix);

                for (auto submesh : mesh->submeshes)
                {
                    if (selection->contains(meshRenderer->entity))
                        submesh->draw();
                }
            }
        }

        program.release();
    }
}


void DeferredRenderer::passLights(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    gl->glDisable(GL_DEPTH_TEST);

    QOpenGLShaderProgram &program = deferredLight->program;

    if(program.bind())
    {
        program.setUniformValue("viewMatrix", camera->viewMatrix);
        program.setUniformValue("projectionMatrix", camera->projectionMatrix);

        QVector<QVector3D> lightPosition;
        QVector<QVector3D> lightColors;
        QVector<GLfloat> lightIntensity;
        QVector<GLfloat> lightRange;

        for (auto entity : scene->entities)
        {
            if (entity->active && entity->lightSource != nullptr)
            {
                lightPosition.push_back(entity->transform->position);
                lightColors.push_back(QVector3D(entity->lightSource->color.redF(), entity->lightSource->color.greenF(), entity->lightSource->color.blueF()));
                lightIntensity.push_back(entity->lightSource->intensity);
                lightRange.push_back(entity->lightSource->range);
            }
        }

        if (miscSettings->renderLightSources)
        {
            if(lightPosition.length() > 0 && lightColors.length() > 0)
            {
                program.setUniformValueArray("lightPositions", &lightPosition[0], lightPosition.length());
                program.setUniformValueArray("lightColors", &lightColors[0], lightColors.length());
                program.setUniformValueArray("lightIntensity", &lightIntensity[0], lightIntensity.length(), 1);
                program.setUniformValueArray("lightRange", &lightRange[0], lightRange.length(), 1);
            }
        }

        program.setUniformValue("viewPos", camera->position);
        program.setUniformValue("backgroundColor", QVector3D(miscSettings->backgroundColor.redF(), miscSettings->backgroundColor.greenF(), miscSettings->backgroundColor.blueF()));
        program.setUniformValue("useSSAO", miscSettings->useSSAO);

        program.setUniformValue("gPosition", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, textureWorldPos);
        program.setUniformValue("gNormal", 1);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, textureNormal);
        program.setUniformValue("gAlbedoSpec", 2);
        gl->glActiveTexture(GL_TEXTURE2);
        gl->glBindTexture(GL_TEXTURE_2D, textureAlbedo);
        program.setUniformValue("gSSAO", 3);
        gl->glActiveTexture(GL_TEXTURE3);
        gl->glBindTexture(GL_TEXTURE_2D, textureSSAOBlur);

        resourceManager->quad->submeshes[0]->draw();

        program.release();
    }

        gl->glEnable(GL_DEPTH_TEST);
}


void DeferredRenderer::passGrid(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    QOpenGLShaderProgram &program = gridProgram->program;

    if(program.bind())
    {
        QVector4D cameraParameters = camera->getLeftRightBottomTop();
        program.setUniformValue("left", cameraParameters.x());
        program.setUniformValue("right", cameraParameters.y());
        program.setUniformValue("bottom", cameraParameters.z());
        program.setUniformValue("top", cameraParameters.w());
        program.setUniformValue("znear", camera->znear);

        program.setUniformValue("worldMatrix", camera->worldMatrix);
        program.setUniformValue("viewlatrix", camera->viewMatrix);

        program.setUniformValue("drawGrid", miscSettings->renderGrid) ;

        program.setUniformValue("worldPos", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, textureWorldPos);

        program.setUniformValue("finalText", 1);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, textureFinal);

        resourceManager->quad->submeshes[0]->draw();

        program.release();
    }
}

void DeferredRenderer::passSSAO(Camera* camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    QOpenGLShaderProgram &program = SSAOProgram->program;

    if(program.bind())
    {
        program.setUniformValueArray("samples", &ssaoKernel[0], int(ssaoKernel.size()));
        program.setUniformValue("projection", camera->projectionMatrix);

        program.setUniformValue("width", float(width));
        program.setUniformValue("height", float(height));

        program.setUniformValue("gPosition", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, textureMPosition);
        program.setUniformValue("gNormal", 1);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, textureMNormals);
        program.setUniformValue("texNoise", 2);
        gl->glActiveTexture(GL_TEXTURE2);
        gl->glBindTexture(GL_TEXTURE_2D, noiseTexture);

        resourceManager->quad->submeshes[0]->draw();

        program.release();
    }
}

void DeferredRenderer::passSSAOBlur()
{
    OpenGLErrorGuard guard(__FUNCTION__);

    QOpenGLShaderProgram &program = SSAOBlur->program;

    if(program.bind())
    {
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, textureSSAO);

        resourceManager->quad->submeshes[0]->draw();

        program.release();
    }
}

void DeferredRenderer::passBlit()
{
    OpenGLErrorGuard guard(__FUNCTION__);

    gl->glDisable(GL_DEPTH_TEST);

    QOpenGLShaderProgram &program = blitProgram->program;

    if (program.bind())
    {
        program.setUniformValue("colorTexture", 0);
        gl->glActiveTexture(GL_TEXTURE0);

        if (shownTexture() == "Final") {
            gl->glBindTexture(GL_TEXTURE_2D, textureGrid);
        }
        else if (shownTexture() == "Position") {
            gl->glBindTexture(GL_TEXTURE_2D, texturePosition);
        }
        else if (shownTexture() == "Normals") {
            gl->glBindTexture(GL_TEXTURE_2D, textureNormal);
        }

        else if (shownTexture() == "Albedo") {
            gl->glBindTexture(GL_TEXTURE_2D, textureAlbedo);
        }
        else if (shownTexture() == "Depth") {
            gl->glBindTexture(GL_TEXTURE_2D, textureDepth);
        }
        else if(shownTexture() == "Selection") {
            gl->glBindTexture(GL_TEXTURE_2D, textureSelection);
        }
        else if(shownTexture() == "Outline") {
            gl->glBindTexture(GL_TEXTURE_2D, textureOutline);
        }
        else if(shownTexture() == "SSAO") {
            gl->glBindTexture(GL_TEXTURE_2D, textureSSAO);
        }
        else if(shownTexture() == "SSAO Blur") {
            gl->glBindTexture(GL_TEXTURE_2D, textureSSAOBlur);
        }
        else if(shownTexture() == "Model Position") {
            gl->glBindTexture(GL_TEXTURE_2D, textureMPosition);
        }
        else if(shownTexture() == "Model Normals") {
            gl->glBindTexture(GL_TEXTURE_2D, textureMNormals);
        }

        program.setUniformValue("outlineTexture", 1);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, textureOutline);

        double r, g, b;
        miscSettings->outlineColor.getRgbF(&r, &g, &b);

        program.setUniformValue("outlineColor", QVector3D(r, g, b));
        program.setUniformValue("outlineWidth", float(miscSettings->outlineWidth));

        resourceManager->quad->submeshes[0]->draw();
        program.release();
    }

    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glEnable(GL_DEPTH_TEST);
}

