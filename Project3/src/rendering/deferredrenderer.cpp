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
    glGenTextures(1, &noiseTexture);
    glBindTexture(GL_TEXTURE_2D, noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

DeferredRenderer::DeferredRenderer() :
    fboPosition(QOpenGLTexture::Target2D),
    fboFinal(QOpenGLTexture::Target2D),
    fboDepth(QOpenGLTexture::Target2D),
    selectionTexture(QOpenGLTexture::Target2D)
{
    fboGeometry = nullptr;
    fboLight = nullptr;

    // List of textures
    addTexture("Final");
    addTexture("Position");
    addTexture("Normals");
    addTexture("Albedo");
    addTexture("Depth");
    addTexture("Selection");
    addTexture("Outline");
    //addTexture("Grid");
    addTexture("GlobalPos");
    addTexture("SSAO");

    rendererType = RendererType::DEFERRED;


    GenerateSSAOTextures();
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
    if (fboPosition != 0) gl->glDeleteTextures(1, &fboPosition);
    gl->glGenTextures(1, &fboPosition);
    gl->glBindTexture(GL_TEXTURE_2D, fboPosition);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (fboNormal != 0) gl->glDeleteTextures(1, &fboNormal);
    gl->glGenTextures(1, &fboNormal);
    gl->glBindTexture(GL_TEXTURE_2D, fboNormal);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (fboAlbedo != 0) gl->glDeleteTextures(1, &fboAlbedo);
    gl->glGenTextures(1, &fboAlbedo);
    gl->glBindTexture(GL_TEXTURE_2D, fboAlbedo);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (selectionTexture != 0) gl->glDeleteTextures(1, &selectionTexture);
    gl->glGenTextures(1, &selectionTexture);
    gl->glBindTexture(GL_TEXTURE_2D, selectionTexture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (fboDepth != 0) gl->glDeleteTextures(1, &fboDepth);
    gl->glGenTextures(1, &fboDepth);
    gl->glBindTexture(GL_TEXTURE_2D, fboDepth);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (fboWorldPos != 0) gl->glDeleteTextures(1, &fboWorldPos);
    gl->glGenTextures(1, &fboWorldPos);
    gl->glBindTexture(GL_TEXTURE_2D, fboWorldPos);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

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
        GL_COLOR_ATTACHMENT5
    };
    gl->glDrawBuffers(6, buffs);

    fboGeometry->addColorAttachment(0, fboPosition);
    fboGeometry->addColorAttachment(1, fboNormal);
    fboGeometry->addColorAttachment(2, fboAlbedo);
    fboGeometry->addColorAttachment(3, selectionTexture);
    fboGeometry->addColorAttachment(4, fboWorldPos);
    fboGeometry->addColorAttachment(5, fboDepth);
    fboGeometry->checkStatus();
    fboGeometry->release();
}

void DeferredRenderer::GenerateLightFBO(int w, int h)
{
    if (fboFinal == 0) gl->glDeleteTextures(1, &fboFinal);
    gl->glGenTextures(1, &fboFinal);
    gl->glBindTexture(GL_TEXTURE_2D, fboFinal);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo

    fboLight->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0
    };
    gl->glDrawBuffers(1, buffs);

    fboLight->addColorAttachment(0, fboFinal);
    fboLight->checkStatus();
    fboLight->release();
}

void DeferredRenderer::GenerateOutlineFBO(int w, int h)
{
    if (outlineTexture != 0) gl->glDeleteTextures(1, &outlineTexture);
    gl->glGenTextures(1, &outlineTexture);
    gl->glBindTexture(GL_TEXTURE_2D, outlineTexture);
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

    fboOutline->addColorAttachment(0, outlineTexture);
    fboOutline->checkStatus();
    fboOutline->release();
}

void DeferredRenderer::GenerateGridFBO(int w, int h)
{
    if (gridTexture != 0) gl->glDeleteTextures(1, &gridTexture);
    gl->glGenTextures(1, &gridTexture);
    gl->glBindTexture(GL_TEXTURE_2D, gridTexture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

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

    fboGrid->addColorAttachment(0, gridTexture);
    fboGrid->checkStatus();
    fboGrid->release();
}

void DeferredRenderer::GenerateSSAOFBO(int w, int h)
{
    if (textureSSAO != 0) gl->glDeleteTextures(1, &textureSSAO);
    gl->glGenTextures(1, &textureSSAO);
    gl->glBindTexture(GL_TEXTURE_2D, textureSSAO);

    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_FLOAT, NULL);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureSSAO, 0);

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
    if (SSAOBlurTexture != 0) gl->glDeleteTextures(1, &SSAOBlurTexture);
    gl->glGenTextures(1, &SSAOBlurTexture);
    gl->glBindTexture(GL_TEXTURE_2D, SSAOBlurTexture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D,0);

    // Attach textures to the fbo
    SSAOBlurFBO->bind();

    // Draw on selected buffers
    GLenum buffs[]=
    {
        GL_COLOR_ATTACHMENT0
    };
    gl->glDrawBuffers(1, buffs);

    SSAOBlurFBO->addColorAttachment(0, SSAOBlurTexture);
    SSAOBlurFBO->checkStatus();
    SSAOBlurFBO->release();
}

void DeferredRenderer::resize(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

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

void DeferredRenderer::render(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);


    fboGrid->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0, 1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //Grid
    passGrid(camera);

    fboGrid->release();


//    gl->glEnable(GL_DEPTH_TEST);
    fboGeometry->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Passes
    passMeshes(camera);

    if(miscSettings->useReliefMapping)
    {
        //TODO: APPLY RELIEF MAPPING EFFECT
    }

//    gl->glDisable(GL_DEPTH_TEST);
    fboGeometry->release();

    fboOutline->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Passes
    passOutline(camera);

    fboOutline->release();

    fboSSAO->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0, 1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //SSAO
    passSSAO(camera);

    fboSSAO->release();


    fboLight->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);

    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    passLights(camera);

    if(miscSettings->useDepthOfField)
    {
        //TODO: APPLY DEPTH OF FIELD EFFECT
    }

    fboLight->release();

    gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    passBlit();

    //Storage the new selection texture pixels

    fboGeometry->bind();

    gl->glReadBuffer(GL_COLOR_ATTACHMENT0 + 3);

    int pixelsSize = width * height;
    selectionPixels.resize(pixelsSize);

    gl->glReadPixels(0, 0, width, height, GL_RED, GL_FLOAT, selectionPixels.data());

    gl->glReadBuffer(0);
    fboGeometry->release();
}

void DeferredRenderer::passMeshes(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    QOpenGLShaderProgram &program = deferredGeometry->program;

    if (program.bind())
    {
        program.setUniformValue("viewMatrix", camera->viewMatrix);
        program.setUniformValue("projectionMatrix", camera->projectionMatrix);

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
                QMatrix4x4 worldMatrix = meshRenderer->entity->transform->matrix();
                QMatrix4x4 worldViewMatrix = camera->viewMatrix * worldMatrix;
                QMatrix3x3 normalMatrix = worldViewMatrix.normalMatrix();

                program.setUniformValue("worldMatrix", worldMatrix);
                program.setUniformValue("worldViewMatrix", worldViewMatrix);
                program.setUniformValue("normalMatrix", normalMatrix);
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

        // Light spheres
        if (miscSettings->renderLightSources)
        {
            for (auto lightSource : lightSources)
            {
                QMatrix4x4 worldMatrix = lightSource->entity->transform->matrix();
                QMatrix4x4 scaleMatrix; scaleMatrix.scale(0.1f, 0.1f, 0.1f);
                QMatrix4x4 worldViewMatrix = camera->viewMatrix * worldMatrix * scaleMatrix;
                QMatrix3x3 normalMatrix = worldViewMatrix.normalMatrix();
                program.setUniformValue("worldMatrix", worldMatrix);
                program.setUniformValue("worldViewMatrix", worldViewMatrix);
                program.setUniformValue("normalMatrix", normalMatrix);

                for (auto submesh : resourceManager->sphere->submeshes)
                {
                    // Send the material to the shader
                    Material *material = resourceManager->materialLight;
                    program.setUniformValue("albedo", material->albedo);
                    program.setUniformValue("emissive", material->emissive);
                    program.setUniformValue("smoothness", material->smoothness);

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

        program.setUniformValue("gPosition", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, fboPosition);
        program.setUniformValue("gNormal", 1);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, fboNormal);
        program.setUniformValue("gAlbedoSpec", 2);
        gl->glActiveTexture(GL_TEXTURE2);
        gl->glBindTexture(GL_TEXTURE_2D, fboAlbedo);

        resourceManager->quad->submeshes[0]->draw();

        program.release();
    }

        gl->glEnable(GL_DEPTH_TEST);
}

void DeferredRenderer::passBlit()
{
    gl->glDisable(GL_DEPTH_TEST);

    QOpenGLShaderProgram &program = blitProgram->program;

    if (program.bind())
    {
        program.setUniformValue("colorTexture", 0);
        gl->glActiveTexture(GL_TEXTURE0);

        if (shownTexture() == "Final") {
            gl->glBindTexture(GL_TEXTURE_2D, gridTexture);
        }
        else if (shownTexture() == "Position") {
            gl->glBindTexture(GL_TEXTURE_2D, fboPosition);
        }
        else if (shownTexture() == "Normals") {
            gl->glBindTexture(GL_TEXTURE_2D, fboNormal);
        }

        else if (shownTexture() == "Albedo") {
            gl->glBindTexture(GL_TEXTURE_2D, fboAlbedo);
        }
        else if (shownTexture() == "Depth") {
            gl->glBindTexture(GL_TEXTURE_2D, fboDepth);
        }
        else if(shownTexture() == "Selection") {
            gl->glBindTexture(GL_TEXTURE_2D, selectionTexture);
        }
        else if(shownTexture() == "Outline") {
            gl->glBindTexture(GL_TEXTURE_2D, outlineTexture);
        }
        else if(shownTexture() == "SSAO") {
            gl->glBindTexture(GL_TEXTURE_2D, textureSSAO);
        }

        program.setUniformValue("outlineTexture", 1);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, outlineTexture);

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

void DeferredRenderer::passGrid(Camera *camera)
{
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
        gl->glBindTexture(GL_TEXTURE_2D, fboWorldPos);

        program.setUniformValue("finalText", 1);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, fboFinal);

        resourceManager->quad->submeshes[0]->draw();

        program.release();
    }
}

void DeferredRenderer::passSSAO(Camera* camera)
{
    QOpenGLShaderProgram &program = SSAOProgram->program;

    if(program.bind())
    {
        program.setUniformValueArray("samples", &ssaoKernel[0], ssaoKernel.size());
        program.setUniformValue("projection", camera->projectionMatrix);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, fboPosition);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, fboNormal);
        gl->glActiveTexture(GL_TEXTURE2);
        gl->glBindTexture(GL_TEXTURE_2D, noiseTexture);

        resourceManager->quad->submeshes[0]->draw();

        program.release();
    }
}
