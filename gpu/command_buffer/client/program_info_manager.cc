// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../client/program_info_manager.h"
#include "../client/gles2_implementation.h"

#include <map>

namespace gpu {
namespace gles2 {

class NonCachedProgramInfoManager : public ProgramInfoManager {
 public:
  NonCachedProgramInfoManager();
  virtual ~NonCachedProgramInfoManager();

  virtual void CreateInfo(GLuint program);

  virtual void DeleteInfo(GLuint program);

  virtual bool GetProgramiv(
      GLES2Implementation* gl, GLuint program, GLenum pname, GLint* params);

  virtual GLint GetAttribLocation(
      GLES2Implementation* gl, GLuint program, const char* name);

  virtual GLint GetUniformLocation(
      GLES2Implementation* gl, GLuint program, const char* name);

  virtual bool GetActiveAttrib(
      GLES2Implementation* gl,
      GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
      GLint* size, GLenum* type, char* name);

  virtual bool GetActiveUniform(
      GLES2Implementation* gl,
      GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
      GLint* size, GLenum* type, char* name);

};

NonCachedProgramInfoManager::NonCachedProgramInfoManager() {
}

NonCachedProgramInfoManager::~NonCachedProgramInfoManager() {
}

void NonCachedProgramInfoManager::CreateInfo(GLuint /* program */) {
}

void NonCachedProgramInfoManager::DeleteInfo(GLuint /* program */) {
}

bool NonCachedProgramInfoManager::GetProgramiv(
    GLES2Implementation* /* gl */,
    GLuint /* program */,
    GLenum /* pname */,
    GLint* /* params */) {
  return false;
}

GLint NonCachedProgramInfoManager::GetAttribLocation(
    GLES2Implementation* gl, GLuint program, const char* name) {
  return gl->GetAttribLocationHelper(program, name);
}

GLint NonCachedProgramInfoManager::GetUniformLocation(
    GLES2Implementation* gl, GLuint program, const char* name) {
  return gl->GetUniformLocationHelper(program, name);
}

bool NonCachedProgramInfoManager::GetActiveAttrib(
    GLES2Implementation* gl,
    GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
    GLint* size, GLenum* type, char* name) {
  return gl->GetActiveAttribHelper(
      program, index, bufsize, length, size, type, name);
}

bool NonCachedProgramInfoManager::GetActiveUniform(
    GLES2Implementation* gl,
    GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
    GLint* size, GLenum* type, char* name) {
  return gl->GetActiveUniformHelper(
      program, index, bufsize, length, size, type, name);
}

class CachedProgramInfoManager : public ProgramInfoManager {
 public:
  CachedProgramInfoManager();
  virtual ~CachedProgramInfoManager();

  virtual void CreateInfo(GLuint program);

  virtual void DeleteInfo(GLuint program);

  virtual bool GetProgramiv(
      GLES2Implementation* gl,
      GLuint program, GLenum pname, GLint* params);

  virtual GLint GetAttribLocation(
      GLES2Implementation* gl, GLuint program, const char* name);

  virtual GLint GetUniformLocation(
      GLES2Implementation* gl, GLuint program, const char* name);

  virtual bool GetActiveAttrib(
      GLES2Implementation* gl,
      GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
      GLint* size, GLenum* type, char* name);

  virtual bool GetActiveUniform(
      GLES2Implementation* gl,
      GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
      GLint* size, GLenum* type, char* name);

 private:
  class ProgramInfo {
   public:
    struct UniformInfo {
      UniformInfo(GLsizei _size, GLenum _type, const std::string& _name);

      GLsizei size;
      GLenum type;
      bool is_array;
      std::string name;
      std::vector<GLint> element_locations;
    };
    struct VertexAttribInfo {
      VertexAttribInfo(GLsizei _size, GLenum _type, const std::string& _name,
                       GLint _location)
          : size(_size),
            type(_type),
            location(_location),
            name(_name) {
      }
      GLsizei size;
      GLenum type;
      GLint location;
      std::string name;
    };

    typedef std::vector<UniformInfo> UniformInfoVector;
    typedef std::vector<VertexAttribInfo> AttribInfoVector;

    ProgramInfo();

    const AttribInfoVector& GetAttribInfos() const {
      return attrib_infos_;
    }

    const VertexAttribInfo* GetAttribInfo(GLint index) const {
      return (static_cast<size_t>(index) < attrib_infos_.size()) ?
         &attrib_infos_[index] : NULL;
    }

    GLint GetAttribLocation(const std::string& name) const;

    const UniformInfo* GetUniformInfo(GLint index) const {
      return (static_cast<size_t>(index) < uniform_infos_.size()) ?
         &uniform_infos_[index] : NULL;
    }

    // Gets the location of a uniform by name.
    GLint GetUniformLocation(const std::string& name) const;

    bool GetProgramiv(GLenum pname, GLint* params);

    // Updates the program info after a successful link.
    void Update(GLES2Implementation* gl, GLuint program);

   private:
    bool cached_;

    GLsizei max_attrib_name_length_;

    // Attrib by index.
    AttribInfoVector attrib_infos_;

    GLsizei max_uniform_name_length_;

    // Uniform info by index.
    UniformInfoVector uniform_infos_;

    // This is true if glLinkProgram was successful last time it was called.
    bool link_status_;
  };

  ProgramInfo* GetProgramInfo(GLES2Implementation* gl, GLuint program);

  // TODO(gman): Switch to a faster container.
  typedef std::map<GLuint, ProgramInfo> ProgramInfoMap;

  ProgramInfoMap program_infos_;
};

CachedProgramInfoManager::ProgramInfo::UniformInfo::UniformInfo(
    GLsizei _size, GLenum _type, const std::string& _name)
    : size(_size),
      type(_type),
      name(_name) {
  is_array = (!name.empty() && name[name.size() - 1] == ']');
  GPU_DCHECK(!(size > 1 && !is_array));
}

CachedProgramInfoManager::ProgramInfo::ProgramInfo()
    : cached_(false),
      max_attrib_name_length_(0),
      max_uniform_name_length_(0),
      link_status_(false) {
}

// TODO(gman): Add a faster lookup.
GLint CachedProgramInfoManager::ProgramInfo::GetAttribLocation(
    const std::string& name) const {
  for (GLuint ii = 0; ii < attrib_infos_.size(); ++ii) {
    const VertexAttribInfo& info = attrib_infos_[ii];
    if (info.name == name) {
      return info.location;
    }
  }
  return -1;
}

// TODO(gman): Add a faster lookup.
GLint CachedProgramInfoManager::ProgramInfo::GetUniformLocation(
    const std::string& name) const {
  for (GLuint ii = 0; ii < uniform_infos_.size(); ++ii) {
    const UniformInfo& info = uniform_infos_[ii];
    if (info.name == name ||
        (info.is_array &&
         info.name.compare(0, info.name.size() - 3, name) == 0)) {
      return info.element_locations[0];
    } else if (info.is_array &&
               name.size() >= 3 && name[name.size() - 1] == ']') {
      // Look for an array specification.
      size_t open_pos = name.find_last_of('[');
      if (open_pos != std::string::npos &&
          open_pos < name.size() - 2 &&
          info.name.size() > open_pos &&
          name.compare(0, open_pos, info.name, 0, open_pos) == 0) {
        GLint index = 0;
        size_t last = name.size() - 1;
        bool bad = false;
        for (size_t pos = open_pos + 1; pos < last; ++pos) {
          int8 digit = name[pos] - '0';
          if (digit < 0 || digit > 9) {
            bad = true;
            break;
          }
          index = index * 10 + digit;
        }
        if (!bad && index >= 0 && index < info.size) {
          return info.element_locations[index];
        }
      }
    }
  }
  return -1;
}

bool CachedProgramInfoManager::ProgramInfo::GetProgramiv(
    GLenum pname, GLint* params) {
  switch (pname) {
    case GL_LINK_STATUS:
      *params = link_status_;
      return true;
    case GL_ACTIVE_ATTRIBUTES:
      *params = attrib_infos_.size();
      return true;
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
      *params = max_attrib_name_length_;
      return true;
    case GL_ACTIVE_UNIFORMS:
      *params = uniform_infos_.size();
      return true;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH:
      *params = max_uniform_name_length_;
      return true;
    default:
      break;
  }
  return false;
}

template<typename T> static T LocalGetAs(
    const std::vector<int8>& data, uint32 offset, size_t size) {
  const int8* p = &data[0] + offset;
  if (offset + size > data.size()) {
    GPU_NOTREACHED();
    return NULL;
  }
  return static_cast<T>(static_cast<const void*>(p));
}

void CachedProgramInfoManager::ProgramInfo::Update(
    GLES2Implementation* gl, GLuint program) {
  if (cached_) {
    return;
  }
  std::vector<int8> result;
  gl->GetProgramInfoCHROMIUMHelper(program, &result);
  if (result.empty()) {
    // This should only happen on a lost context.
    return;
  }
  GPU_DCHECK_GE(result.size(), sizeof(ProgramInfoHeader));
  const ProgramInfoHeader* header = LocalGetAs<const ProgramInfoHeader*>(
      result, 0, sizeof(header));
  link_status_ = header->link_status != 0;
  if (!link_status_) {
    return;
  }
  attrib_infos_.clear();
  uniform_infos_.clear();
  max_attrib_name_length_ = 0;
  max_uniform_name_length_ = 0;
  const ProgramInput* inputs = LocalGetAs<const ProgramInput*>(
      result, sizeof(*header),
      sizeof(ProgramInput) * (header->num_attribs + header->num_uniforms));
  const ProgramInput* input = inputs;
  for (uint32 ii = 0; ii < header->num_attribs; ++ii) {
    const int32* location = LocalGetAs<const int32*>(
        result, input->location_offset, sizeof(int32));
    const char* name_buf = LocalGetAs<const char*>(
        result, input->name_offset, input->name_length);
    std::string name(name_buf, input->name_length);
    attrib_infos_.push_back(
        VertexAttribInfo(input->size, input->type, name, *location));
    max_attrib_name_length_ = std::max(
        static_cast<GLsizei>(name.size() + 1), max_attrib_name_length_);
    ++input;
  }
  for (uint32 ii = 0; ii < header->num_uniforms; ++ii) {
    const int32* locations = LocalGetAs<const int32*>(
        result, input->location_offset, sizeof(int32) * input->size);
    const char* name_buf = LocalGetAs<const char*>(
        result, input->name_offset, input->name_length);
    std::string name(name_buf, input->name_length);
    UniformInfo info(input->size, input->type, name);
    max_uniform_name_length_ = std::max(
        static_cast<GLsizei>(name.size() + 1), max_uniform_name_length_);
    for (int32 jj = 0; jj < input->size; ++jj) {
      info.element_locations.push_back(locations[jj]);
    }
    uniform_infos_.push_back(info);
    ++input;
  }
  GPU_DCHECK_EQ(header->num_attribs + header->num_uniforms,
                static_cast<uint32>(input - inputs));
  cached_ = true;
}

CachedProgramInfoManager::CachedProgramInfoManager() {
}

CachedProgramInfoManager::~CachedProgramInfoManager() {

}

CachedProgramInfoManager::ProgramInfo*
    CachedProgramInfoManager::GetProgramInfo(
        GLES2Implementation* gl, GLuint program) {
  ProgramInfoMap::iterator it = program_infos_.find(program);
  if (it == program_infos_.end()) {
    return NULL;
  }
  ProgramInfo* info = &it->second;
  info->Update(gl, program);
  return info;
}

void CachedProgramInfoManager::CreateInfo(GLuint program) {
  DeleteInfo(program);
  std::pair<ProgramInfoMap::iterator, bool> result =
      program_infos_.insert(std::make_pair(program, ProgramInfo()));

  GPU_DCHECK(result.second);
}

void CachedProgramInfoManager::DeleteInfo(GLuint program) {
  program_infos_.erase(program);
}

bool CachedProgramInfoManager::GetProgramiv(
    GLES2Implementation* gl, GLuint program, GLenum pname, GLint* params) {
  ProgramInfo* info = GetProgramInfo(gl, program);
  if (!info) {
    return false;
  }
  return info->GetProgramiv(pname, params);
}

GLint CachedProgramInfoManager::GetAttribLocation(
    GLES2Implementation* gl, GLuint program, const char* name) {
  ProgramInfo* info = GetProgramInfo(gl, program);
  if (info) {
    return info->GetAttribLocation(name);
  }
  return gl->GetAttribLocationHelper(program, name);
}

GLint CachedProgramInfoManager::GetUniformLocation(
    GLES2Implementation* gl, GLuint program, const char* name) {
  ProgramInfo* info = GetProgramInfo(gl, program);
  if (info) {
    return info->GetUniformLocation(name);
  }
  return gl->GetUniformLocationHelper(program, name);
}

bool CachedProgramInfoManager::GetActiveAttrib(
    GLES2Implementation* gl,
    GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
    GLint* size, GLenum* type, char* name) {
  ProgramInfo* info = GetProgramInfo(gl, program);
  if (info) {
    const ProgramInfo::VertexAttribInfo* attrib_info =
        info->GetAttribInfo(index);
    if (attrib_info) {
      if (size) {
        *size = attrib_info->size;
      }
      if (type) {
        *type = attrib_info->type;
      }
      if (length || name) {
        GLsizei max_size = std::min(static_cast<size_t>(bufsize) - 1,
                                    std::max(static_cast<size_t>(0),
                                             attrib_info->name.size()));
        if (length) {
          *length = max_size;
        }
        if (name && bufsize > 0) {
          memcpy(name, attrib_info->name.c_str(), max_size);
          name[max_size] = '\0';
        }
      }
      return true;
    }
  }
  return gl->GetActiveAttribHelper(
      program, index, bufsize, length, size, type, name);
}

bool CachedProgramInfoManager::GetActiveUniform(
    GLES2Implementation* gl,
    GLuint program, GLuint index, GLsizei bufsize, GLsizei* length,
    GLint* size, GLenum* type, char* name) {
  ProgramInfo* info = GetProgramInfo(gl, program);
  if (info) {
    const ProgramInfo::UniformInfo* uniform_info = info->GetUniformInfo(index);
    if (uniform_info) {
      if (size) {
        *size = uniform_info->size;
      }
      if (type) {
        *type = uniform_info->type;
      }
      if (length || name) {
        GLsizei max_size = std::min(static_cast<size_t>(bufsize) - 1,
                                    std::max(static_cast<size_t>(0),
                                             uniform_info->name.size()));
        if (length) {
          *length = max_size;
        }
        if (name && bufsize > 0) {
          memcpy(name, uniform_info->name.c_str(), max_size);
          name[max_size] = '\0';
        }
      }
      return true;
    }
  }
  return gl->GetActiveUniformHelper(
      program, index, bufsize, length, size, type, name);
}

ProgramInfoManager::ProgramInfoManager() {
}

ProgramInfoManager::~ProgramInfoManager() {
}

ProgramInfoManager* ProgramInfoManager::Create(bool shared_resources) {
  if (shared_resources) {
    return new NonCachedProgramInfoManager();
  } else {
    return new CachedProgramInfoManager();
  }
}

}  // namespace gles2
}  // namespace gpu

