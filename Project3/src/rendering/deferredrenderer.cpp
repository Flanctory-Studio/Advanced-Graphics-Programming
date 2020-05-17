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

DeferredRenderer::DeferredRenderer() :
    fboPosition(QOpenGLTexture::Target2D),
    fboFinal(QOpenGLTexture::Target2D),
    fboDepth(QOpenGLTexture::Target2D)
{
    fboGeometry = nullptr;
    fboLight = nullptr;

    // List of textures
    addTexture("Final");
    addTexture("Position");
    addTexture("Normals");
    addTexture("Albedo");
    addTexture("Depth");
}

DeferredRenderer::~DeferredRenderer()
{
    delete fboGeometry;
    delete fboLight;
}

void DeferredRenderer::initialize()
{
    OpenGLErrorGuard guard(__FUNCTION__);

    // Create programs

    deferredGeometry = resourceManager->createShaderProgram();
    deferredGeometry->name = "Deferred Geometry";
    deferredGeometry->vertexShaderFilename = "res/shaders/deferred_shading.vert";
    deferredGeometry->fragmentShaderFilename = "res/shaders/deferred_shading.frag";
    deferredGeometry->includeForSerialization = false;

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

    // Create FBO

    fboGeometry = new FramebufferObject();
    fboGeometry->create();

    fboLight = new FramebufferObject();
    fboLight->create();
}

void DeferredRenderer::finalize()
{
    fboGeometry->destroy();
    delete fboGeometry;

    fboLight->destroy();
    delete fboLight;
}

void DeferredRenderer::GenerateGeometryFBO(int w, int h)
{
    if (fboPosition == 0) gl->glDeleteTextures(1, &fboPosition);
    gl->glGenTextures(1, &fboPosition);
    gl->glBindTexture(GL_TEXTURE_2D, fboPosition);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (fboNormal == 0) gl->glDeleteTextures(1, &fboNormal);
    gl->glGenTextures(1, &fboNormal);
    gl->glBindTexture(GL_TEXTURE_2D, fboNormal);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (fboAlbedo == 0) gl->glDeleteTextures(1, &fboAlbedo);
    gl->glGenTextures(1, &fboAlbedo);
    gl->glBindTexture(GL_TEXTURE_2D, fboAlbedo);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (fboDepth == 0) gl->glDeleteTextures(1, &fboDepth);
    gl->glGenTextures(1, &fboDepth);
    gl->glBindTexture(GL_TEXTURE_2D, fboDepth);
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
    };
    gl->glDrawBuffers(3, buffs);

    fboGeometry->addColorAttachment(0, fboPosition);
    fboGeometry->addColorAttachment(1, fboNormal);
    fboGeometry->addColorAttachment(2, fboAlbedo);
    fboGeometry->addDepthAttachment(fboDepth);
    fboGeometry->checkStatus();
    fboGeometry->release();
}

void DeferredRenderer::GenerateLightFBO(int w, int h)
{
//    if (fboFinal == 0) gl->glDeleteTextures(1, &fboFinal);
//    gl->glGenTextures(1, &fboFinal);
//    gl->glBindTexture(GL_TEXTURE_2D, fboFinal);
//    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
//    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

//    glBindTexture(GL_TEXTURE_2D,0);
//    // Attach textures to the fbo
//    fboLight->bind();

//    // Draw on selected buffers
//    GLenum buffs[]=
//    {
//        GL_COLOR_ATTACHMENT0,
//    };
//    gl->glDrawBuffers(1, buffs);

//    fboLight->addColorAttachment(0, fboFinal);
//    fboLight->checkStatus();
//    fboLight->release();

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

void DeferredRenderer::resize(int w, int h)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    // Regenerate render targets

    // fbo Geometry
    GenerateGeometryFBO(w, h);

    // fbo Light
    GenerateLightFBO(w, h);
}

void DeferredRenderer::render(Camera *camera)
{
    OpenGLErrorGuard guard(__FUNCTION__);

    fboGeometry->bind();

    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Passes
    passMeshes(camera);
    fboGeometry->release();


    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    fboLight->bind();
    // Clear color
    gl->glClearDepth(1.0);
    gl->glClearColor(0.0, 0.0, 0.0,1.0);

    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    passLights(camera);
    fboLight->release();

    gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    passBlit();
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
        for (auto meshRenderer : meshRenderers)
        {
            auto mesh = meshRenderer->mesh;

            if (mesh != nullptr)
            {
                QMatrix4x4 worldMatrix = meshRenderer->entity->transform->matrix();
                QMatrix4x4 worldViewMatrix = camera->viewMatrix * worldMatrix;
                QMatrix3x3 normalMatrix = worldViewMatrix.normalMatrix();

                program.setUniformValue("worldMatrix", worldMatrix);
                program.setUniformValue("worldViewMatrix", worldViewMatrix);
                program.setUniformValue("normalMatrix", normalMatrix);

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
            gl->glBindTexture(GL_TEXTURE_2D, fboFinal);
        }
        if (shownTexture() == "Position") {
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

        resourceManager->quad->submeshes[0]->draw();
        program.release();
    }

    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glEnable(GL_DEPTH_TEST);
}
