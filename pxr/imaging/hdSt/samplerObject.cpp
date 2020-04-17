//
// Copyright 2020 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/imaging/glf/glew.h"

#include "pxr/imaging/hdSt/samplerObject.h"

#include "pxr/imaging/hdSt/textureObject.h"
#include "pxr/imaging/hdSt/glConversions.h"

#include "pxr/imaging/glf/diagnostic.h"
#include "pxr/imaging/hgiGL/texture.h"

PXR_NAMESPACE_OPEN_SCOPE

bool
HdStSamplerParameters::operator==(const HdStSamplerParameters &other) const
{
    return
        (wrapS == other.wrapS) &&
        (wrapT == other.wrapT) &&
        (wrapR == other.wrapR) &&
        (minFilter == other.minFilter) &&
        (magFilter == other.magFilter);
}

bool
HdStSamplerParameters::operator!=(const HdStSamplerParameters &other) const
{
    return !(*this == other);
}

///////////////////////////////////////////////////////////////////////////////
// HdStTextureObject

HdStSamplerObject::~HdStSamplerObject() = default;

///////////////////////////////////////////////////////////////////////////////
// Helpers

// Generate GL sampler
static
GLuint
_GenGLSampler(HdStSamplerParameters const &samplerParameters)
{
    GLuint result = 0;
    glGenSamplers(1, &result);

    glSamplerParameteri(
        result,
        GL_TEXTURE_WRAP_S,
        HdStGLConversions::GetWrap(samplerParameters.wrapS));

    glSamplerParameteri(
        result,
        GL_TEXTURE_WRAP_T,
        HdStGLConversions::GetWrap(samplerParameters.wrapT));

    glSamplerParameteri(
        result,
        GL_TEXTURE_WRAP_R,
        HdStGLConversions::GetWrap(samplerParameters.wrapR));

    glSamplerParameteri(
        result,
        GL_TEXTURE_MIN_FILTER,
        HdStGLConversions::GetMinFilter(samplerParameters.minFilter));

    glSamplerParameteri(
        result,
        GL_TEXTURE_MAG_FILTER,
        HdStGLConversions::GetMagFilter(samplerParameters.magFilter));

    static const GfVec4f borderColor(0.0);
    glSamplerParameterfv(
        result,
        GL_TEXTURE_BORDER_COLOR,
        borderColor.GetArray());

    static const float _maxAnisotropy = 16.0;

    glSamplerParameterf(
        result,
        GL_TEXTURE_MAX_ANISOTROPY_EXT,
        _maxAnisotropy);

    GLF_POST_PENDING_GL_ERRORS();

    return result;
}

// Get texture sampler handle for bindless textures.
static
GLuint64EXT 
_GenGLTextureSamplerHandle(HgiTextureHandle const &textureHandle,
                           const GLuint samplerName,
                           const bool createBindlessHandle)
{
    if (!createBindlessHandle) {
        return 0;
    }

    HgiTexture * const texture = textureHandle.Get();
    if (texture == nullptr) {
        return 0;
    }

    HgiGLTexture * const glTexture = dynamic_cast<HgiGLTexture*>(texture);
    if (glTexture == nullptr) {
        TF_CODING_ERROR("Only OpenGL textures supported");
        return 0;
    }

    const GLuint textureName = glTexture->GetTextureId();

    if (textureName == 0) {
        return 0;
    }

    if (samplerName == 0) {
        return 0;
    }

    const GLuint64EXT result =
        glGetTextureSamplerHandleARB(textureName, samplerName);

    glMakeTextureHandleResidentARB(result);

    GLF_POST_PENDING_GL_ERRORS();

    return result;
}

// Get texture handle for bindless textures.
static
GLuint64EXT
_GenGlTextureHandle(const GLuint textureName,
                    const bool createGLTextureHandle)
{
    if (!createGLTextureHandle) {
        return 0;
    }

    if (textureName == 0) {
        return 0;
    }

    const GLuint64EXT result = glGetTextureHandleARB(textureName);
    glMakeTextureHandleResidentARB(result);

    GLF_POST_PENDING_GL_ERRORS();

    return result;
}

///////////////////////////////////////////////////////////////////////////////
// Uv sampler

// Resolve a wrap parameter using the opinion authored in the metadata of a
// texture file.
static
void
_ResolveSamplerParameter(
    const HdWrap textureOpinion,
    HdWrap * const parameter)
{
    if (*parameter == HdWrapNoOpinion) {
        *parameter = textureOpinion;
    }

    // Legacy behavior for HwUvTexture_1
    if (*parameter == HdWrapLegacyNoOpinionFallbackRepeat) {
        if (textureOpinion == HdWrapNoOpinion) {
            // Use repeat if there is no opinion on either the
            // texture node or in the texture file.
            *parameter = HdWrapRepeat;
        } else {
            *parameter = textureOpinion;
        }
    }
}

// Resolve wrapS or wrapT of the samplerParameters using metadata
// from the texture file.
static
HdStSamplerParameters
_ResolveUvSamplerParameters(
    HdStUvTextureObject const &texture,
    HdStSamplerParameters const &samplerParameters)
{
    HdStSamplerParameters result = samplerParameters;
    _ResolveSamplerParameter(
        texture.GetWrapParameters().first,
        &result.wrapS);

    _ResolveSamplerParameter(
        texture.GetWrapParameters().second,
        &result.wrapT);

    return result;
}

HdStUvSamplerObject::HdStUvSamplerObject(
    HdStUvTextureObject const &texture,
    HdStSamplerParameters const &samplerParameters,
    const bool createBindlessHandle)
  : _glSamplerName(
      _GenGLSampler(
          _ResolveUvSamplerParameters(
              texture, samplerParameters)))
  , _glTextureSamplerHandle(
      _GenGLTextureSamplerHandle(
          texture.GetTexture(),
          _glSamplerName,
          createBindlessHandle))
{
}

HdStUvSamplerObject::~HdStUvSamplerObject()
{
    // Deleting the GL sampler automatically deletes the
    // texture sampler handle.
    // In fact, even destroying the underlying texture (which
    // is out of our control here), deletes the texture sampler
    // handle and the same texture sampler handle might be re-used
    // by the driver, so it is unsafe to call
    // glMakeTextureHandleNonResidentARB(_glTextureSamplerHandle);
    // here: HdStTextureObject might destroy a GPU texture either
    // because it itself was destroyed or because the file was
    // reloaded or target memory was changed.

    if (_glSamplerName) {
        glDeleteSamplers(1, &_glSamplerName);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Field sampler

HdStFieldSamplerObject::HdStFieldSamplerObject(
    HdStFieldTextureObject const &texture,
    HdStSamplerParameters const &samplerParameters,
    const bool createBindlessHandle)
  : _glSamplerName(
      _GenGLSampler(
          samplerParameters))
  , _glTextureSamplerHandle(
      _GenGLTextureSamplerHandle(
          texture.GetTexture(),
          _glSamplerName,
          createBindlessHandle))
{
}

HdStFieldSamplerObject::~HdStFieldSamplerObject()
{
    // See above comment about destroying _glTextureSamplerHandle
    if (_glSamplerName) {
        glDeleteSamplers(1, &_glSamplerName);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Ptex sampler

HdStPtexSamplerObject::HdStPtexSamplerObject(
    HdStPtexTextureObject const &ptexTexture,
    // samplerParameters are ignored are ptex
    HdStSamplerParameters const &samplerParameters,
    const bool createBindlessHandle)
  : _texelsGLTextureHandle(
      _GenGlTextureHandle(
          ptexTexture.GetTexelGLTextureName(),
          createBindlessHandle))
  , _layoutGLTextureHandle(
      _GenGlTextureHandle(
          ptexTexture.GetLayoutGLTextureName(),
          createBindlessHandle))
{
}

HdStPtexSamplerObject::~HdStPtexSamplerObject()
{
    if (_texelsGLTextureHandle) {
        glMakeTextureHandleNonResidentARB(_texelsGLTextureHandle);
    }
    if (_layoutGLTextureHandle) {
        glMakeTextureHandleNonResidentARB(_layoutGLTextureHandle);
    }
}
    
PXR_NAMESPACE_CLOSE_SCOPE
