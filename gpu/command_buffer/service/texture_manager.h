// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_TEXTURE_MANAGER_H_
#define GPU_COMMAND_BUFFER_SERVICE_TEXTURE_MANAGER_H_

#include <vector>
#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/command_buffer/service/gl_utils.h"

namespace gpu {
namespace gles2 {

class GLES2Decoder;

// This class keeps track of the textures and their sizes so we can do NPOT and
// texture complete checking.
//
// NOTE: To support shared resources an instance of this class will need to be
// shared by multiple GLES2Decoders.
class TextureManager {
 public:
  enum DefaultAndBlackTextures {
    kTexture2D,
    kCubeMap,
    kExternalOES,
    kRectangleARB,
    kNumDefaultTextures
  };

  // Info about Textures currently in the system.
  class TextureInfo : public base::RefCounted<TextureInfo> {
   public:
    typedef scoped_refptr<TextureInfo> Ref;

    TextureInfo(TextureManager* manager, GLuint service_id)
        : manager_(manager),
          service_id_(service_id),
          deleted_(false),
          cleared_(true),
          num_uncleared_mips_(0),
          target_(0),
          min_filter_(GL_NEAREST_MIPMAP_LINEAR),
          mag_filter_(GL_LINEAR),
          wrap_s_(GL_REPEAT),
          wrap_t_(GL_REPEAT),
          usage_(GL_NONE),
          max_level_set_(-1),
          texture_complete_(false),
          cube_complete_(false),
          npot_(false),
          has_been_bound_(false),
          framebuffer_attachment_count_(0),
          owned_(true),
          stream_texture_(false),
          immutable_(false),
          estimated_size_(0) {
      if (manager_)
        ++manager_->texture_info_count_;
    }

    GLenum min_filter() const {
      return min_filter_;
    }

    GLenum mag_filter() const {
      return mag_filter_;
    }

    GLenum wrap_s() const {
      return wrap_s_;
    }

    GLenum wrap_t() const {
      return wrap_t_;
    }

    GLenum usage() const {
      return usage_;
    }

    int num_uncleared_mips() const {
      return num_uncleared_mips_;
    }

    uint32 estimated_size() const {
      return estimated_size_;
    }

    bool CanRenderTo() const {
      return !stream_texture_ && target_ != GL_TEXTURE_EXTERNAL_OES;
    }

    // The service side OpenGL id of the texture.
    GLuint service_id() const {
      return service_id_;
    }

    void SetServiceId(GLuint service_id) {
      service_id_ = service_id;
    }

    // Returns the target this texure was first bound to or 0 if it has not
    // been bound. Once a texture is bound to a specific target it can never be
    // bound to a different target.
    GLenum target() const {
      return target_;
    }

    // In GLES2 "texture complete" means it has all required mips for filtering
    // down to a 1x1 pixel texture, they are in the correct order, they are all
    // the same format.
    bool texture_complete() const {
      return texture_complete_;
    }

    // In GLES2 "cube complete" means all 6 faces level 0 are defined, all the
    // same format, all the same dimensions and all width = height.
    bool cube_complete() const {
      return cube_complete_;
    }

    // Whether or not this texture is a non-power-of-two texture.
    bool npot() const {
      return npot_;
    }

    bool SafeToRenderFrom() const {
      return cleared_;
    }

    // Get the width and height for a particular level. Returns false if level
    // does not exist.
    bool GetLevelSize(
        GLint face, GLint level, GLsizei* width, GLsizei* height) const;

    // Get the type of a level. Returns false if level does not exist.
    bool GetLevelType(
        GLint face, GLint level, GLenum* type, GLenum* internal_format) const;

    bool IsDeleted() const {
      return deleted_;
    }

    // Returns true of the given dimensions are inside the dimensions of the
    // level and if the format and type match the level.
    bool ValidForTexture(
        GLint face,
        GLint level,
        GLint xoffset,
        GLint yoffset,
        GLsizei width,
        GLsizei height,
        GLenum format,
        GLenum type) const;

    bool IsValid() const {
      return target() && !IsDeleted();
    }

    void SetNotOwned() {
      owned_ = false;
    }

    bool IsAttachedToFramebuffer() const {
      return framebuffer_attachment_count_ != 0;
    }

    void AttachToFramebuffer() {
      ++framebuffer_attachment_count_;
    }

    void DetachFromFramebuffer() {
      DCHECK_GT(framebuffer_attachment_count_, 0);
      --framebuffer_attachment_count_;
    }

    void SetStreamTexture(bool stream_texture) {
      stream_texture_ = stream_texture;
    }

    int IsStreamTexture() {
      return stream_texture_;
    }

    void SetImmutable(bool immutable) {
      DCHECK(!immutable_);
      immutable_ = immutable;
    }

    bool IsImmutable() {
      return immutable_;
    }

    // Whether a particular level/face is cleared.
    bool IsLevelCleared(GLenum target, GLint level);

   private:
    friend class TextureManager;
    friend class base::RefCounted<TextureInfo>;

    ~TextureInfo();

    struct LevelInfo {
      LevelInfo()
         : cleared(true),
           target(0),
           level(-1),
           internal_format(0),
           width(0),
           height(0),
           depth(0),
           border(0),
           format(0),
           type(0),
           estimated_size(0) {
      }

      bool cleared;
      GLenum target;
      GLint level;
      GLenum internal_format;
      GLsizei width;
      GLsizei height;
      GLsizei depth;
      GLint border;
      GLenum format;
      GLenum type;
      uint32 estimated_size;
    };

    // Set the info for a particular level.
    void SetLevelInfo(
        const FeatureInfo* feature_info,
        GLenum target,
        GLint level,
        GLenum internal_format,
        GLsizei width,
        GLsizei height,
        GLsizei depth,
        GLint border,
        GLenum format,
        GLenum type,
        bool cleared);

    // Marks a particular level as cleared or uncleared.
    void SetLevelCleared(GLenum target, GLint level);

    // Updates the cleared flag for this texture by inspecting all the mips.
    void UpdateCleared();

    // Clears any renderable uncleared levels.
    // Returns false if a GL error was generated.
    bool ClearRenderableLevels(GLES2Decoder* decoder);

    // Clears the level.
    // Returns false if a GL error was generated.
    bool ClearLevel(GLES2Decoder* decoder, GLenum target, GLint level);

    // Sets a texture parameter.
    // TODO(gman): Expand to SetParameteri,f,iv,fv
    // Returns false if param was INVALID_ENUN
    bool SetParameter(
        const FeatureInfo* feature_info, GLenum pname, GLint param);

    // Makes each of the mip levels as though they were generated.
    bool MarkMipmapsGenerated(const FeatureInfo* feature_info);

    void MarkAsDeleted() {
      service_id_ = 0;
      deleted_ = true;
    }

    bool NeedsMips() const {
      return min_filter_ != GL_NEAREST && min_filter_ != GL_LINEAR;
    }

    // True if this texture meets all the GLES2 criteria for rendering.
    // See section 3.8.2 of the GLES2 spec.
    bool CanRender(const FeatureInfo* feature_info) const;

    // Returns true if mipmaps can be generated by GL.
    bool CanGenerateMipmaps(const FeatureInfo* feature_info) const;

    // Sets the TextureInfo's target
    // Parameters:
    //   target: GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP or
    //           GL_TEXTURE_EXTERNAL_OES or GL_TEXTURE_RECTANGLE_ARB
    //   max_levels: The maximum levels this type of target can have.
    void SetTarget(GLenum target, GLint max_levels);

    // Update info about this texture.
    void Update(const FeatureInfo* feature_info);

    // Info about each face and level of texture.
    std::vector<std::vector<LevelInfo> > level_infos_;

    // The texture manager that manages this TextureInfo.
    TextureManager* manager_;

    // The id of the texure
    GLuint service_id_;

    // Whether this texture has been deleted.
    bool deleted_;

    // Whether all renderable mips of this texture have been cleared.
    bool cleared_;

    int num_uncleared_mips_;

    // The target. 0 if unset, otherwise GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP.
    GLenum target_;

    // Texture parameters.
    GLenum min_filter_;
    GLenum mag_filter_;
    GLenum wrap_s_;
    GLenum wrap_t_;
    GLenum usage_;

    // The maximum level that has been set.
    GLint max_level_set_;

    // Whether or not this texture is "texture complete"
    bool texture_complete_;

    // Whether or not this texture is "cube complete"
    bool cube_complete_;

    // Whether or not this texture is non-power-of-two
    bool npot_;

    // Whether this texture has ever been bound.
    bool has_been_bound_;

    // The number of framebuffers this texture is attached to.
    int framebuffer_attachment_count_;

    // Whether the associated context group owns this texture and should delete
    // it.
    bool owned_;

    // Whether this is a special streaming texture.
    bool stream_texture_;

    // Whether the texture is immutable and no further changes to the format
    // or dimensions of the texture object can be made.
    bool immutable_;

    // Size in bytes this texture is assumed to take in memory.
    uint32 estimated_size_;

    DISALLOW_COPY_AND_ASSIGN(TextureInfo);
  };

  TextureManager(FeatureInfo* feature_info,
                 GLsizei max_texture_size,
                 GLsizei max_cube_map_texture_size);
  ~TextureManager();

  // Init the texture manager.
  bool Initialize();

  // Must call before destruction.
  void Destroy(bool have_context);

  // Returns the maximum number of levels.
  GLint MaxLevelsForTarget(GLenum target) const {
    switch (target) {
      case GL_TEXTURE_2D:
        return  max_levels_;
      case GL_TEXTURE_EXTERNAL_OES:
        return 1;
      default:
        return max_cube_map_levels_;
    }
  }

  // Returns the maximum size.
  GLsizei MaxSizeForTarget(GLenum target) const {
    switch (target) {
      case GL_TEXTURE_2D:
      case GL_TEXTURE_EXTERNAL_OES:
        return max_texture_size_;
      default:
        return max_cube_map_texture_size_;
    }
  }

  // Returns the maxium number of levels a texture of the given size can have.
  static GLsizei ComputeMipMapCount(
    GLsizei width, GLsizei height, GLsizei depth);

  // Checks if a dimensions are valid for a given target.
  bool ValidForTarget(
      GLenum target, GLint level,
      GLsizei width, GLsizei height, GLsizei depth);

  // True if this texture meets all the GLES2 criteria for rendering.
  // See section 3.8.2 of the GLES2 spec.
  bool CanRender(const TextureInfo* texture) const {
    return texture->CanRender(feature_info_);
  }

  // Returns true if mipmaps can be generated by GL.
  bool CanGenerateMipmaps(const TextureInfo* texture) const {
    return texture->CanGenerateMipmaps(feature_info_);
  }

  // Sets the TextureInfo's target
  // Parameters:
  //   target: GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP
  //   max_levels: The maximum levels this type of target can have.
  void SetInfoTarget(
      TextureInfo* info,
      GLenum target);

  // Set the info for a particular level in a TexureInfo.
  void SetLevelInfo(
      TextureInfo* info,
      GLenum target,
      GLint level,
      GLenum internal_format,
      GLsizei width,
      GLsizei height,
      GLsizei depth,
      GLint border,
      GLenum format,
      GLenum type,
      bool cleared);

  // Sets a mip as cleared.
  void SetLevelCleared(TextureInfo* info, GLenum target, GLint level);

  // Sets a texture parameter of a TextureInfo
  // TODO(gman): Expand to SetParameteri,f,iv,fv
  bool SetParameter(
      TextureInfo* info, GLenum pname, GLint param);

  // Makes each of the mip levels as though they were generated.
  // Returns false if that's not allowed for the given texture.
  bool MarkMipmapsGenerated(TextureInfo* info);

  // Clears any uncleared renderable levels.
  bool ClearRenderableLevels(GLES2Decoder* decoder, TextureInfo* info);

  // Clear a specific level.
  bool ClearTextureLevel(
      GLES2Decoder* decoder,TextureInfo* info, GLenum target, GLint level);

  // Creates a new texture info.
  TextureInfo* CreateTextureInfo(GLuint client_id, GLuint service_id);

  // Gets the texture info for the given texture.
  TextureInfo* GetTextureInfo(GLuint client_id);

  // Removes a texture info.
  void RemoveTextureInfo(GLuint client_id);

  // Gets a client id for a given service id.
  bool GetClientId(GLuint service_id, GLuint* client_id) const;

  TextureInfo* GetDefaultTextureInfo(GLenum target) {
    switch (target) {
      case GL_TEXTURE_2D:
        return default_textures_[kTexture2D];
      case GL_TEXTURE_CUBE_MAP:
        return default_textures_[kCubeMap];
      case GL_TEXTURE_EXTERNAL_OES:
        return default_textures_[kExternalOES];
      case GL_TEXTURE_RECTANGLE_ARB:
        return default_textures_[kRectangleARB];
      default:
        NOTREACHED();
        return NULL;
    }
  }

  bool HaveUnrenderableTextures() const {
    return num_unrenderable_textures_ > 0;
  }

  bool HaveUnsafeTextures() const {
    return num_unsafe_textures_ > 0;
  }

  bool HaveUnclearedMips() const {
    return num_uncleared_mips_ > 0;
  }

  GLuint black_texture_id(GLenum target) const {
    switch (target) {
      case GL_SAMPLER_2D:
        return black_texture_ids_[kTexture2D];
      case GL_SAMPLER_CUBE:
        return black_texture_ids_[kCubeMap];
      case GL_SAMPLER_EXTERNAL_OES:
        return black_texture_ids_[kExternalOES];
      case GL_SAMPLER_2D_RECT_ARB:
        return black_texture_ids_[kRectangleARB];
      default:
        NOTREACHED();
        return 0;
    }
  }

 private:
  // Helper for Initialize().
  TextureInfo::Ref CreateDefaultAndBlackTextures(
      GLenum target,
      GLuint* black_texture);

  void UpdateMemRepresented();

  void StopTracking(TextureInfo* info);

  FeatureInfo::Ref feature_info_;

  // Info for each texture in the system.
  typedef base::hash_map<GLuint, TextureInfo::Ref> TextureInfoMap;
  TextureInfoMap texture_infos_;

  GLsizei max_texture_size_;
  GLsizei max_cube_map_texture_size_;
  GLint max_levels_;
  GLint max_cube_map_levels_;

  int num_unrenderable_textures_;
  int num_unsafe_textures_;
  int num_uncleared_mips_;

  // Counts the number of TextureInfo allocated with 'this' as its manager.
  // Allows to check no TextureInfo will outlive this.
  unsigned int texture_info_count_;

  uint32 mem_represented_;
  uint32 last_reported_mem_represented_;

  // Black (0,0,0,1) textures for when non-renderable textures are used.
  // NOTE: There is no corresponding TextureInfo for these textures.
  // TextureInfos are only for textures the client side can access.
  GLuint black_texture_ids_[kNumDefaultTextures];

  // The default textures for each target (texture name = 0)
  TextureInfo::Ref default_textures_[kNumDefaultTextures];

  DISALLOW_COPY_AND_ASSIGN(TextureManager);
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_TEXTURE_MANAGER_H_
