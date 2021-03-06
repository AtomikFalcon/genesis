#ifndef SHADER_PROGRAM_MANAGER
#define SHADER_PROGRAM_MANAGER

#include "shader_program.hpp"

class ShaderProgramManager {
public:
    ShaderProgramManager();
    ~ShaderProgramManager();

    ShaderProgramManager(const ShaderProgramManager &copy) = delete;
    ShaderProgramManager &operator=(const ShaderProgramManager &copy) = delete;

    ShaderProgram _texture_shader_program;
    GLint _texture_attrib_tex_coord;
    GLint _texture_attrib_position;
    GLint _texture_uniform_mvp;
    GLint _texture_uniform_tex;

    ShaderProgram _text_shader_program;
    GLint _text_attrib_tex_coord;
    GLint _text_attrib_position;
    GLint _text_uniform_mvp;
    GLint _text_uniform_tex;
    GLint _text_uniform_color;

    ShaderProgram _primitive_shader_program;
    GLint _primitive_attrib_position;
    GLint _primitive_uniform_mvp;
    GLint _primitive_uniform_color;

    ShaderProgram texture_color_program;
    GLint texture_color_attrib_tex_coord;
    GLint texture_color_attrib_position;
    GLint texture_color_uniform_mvp;
    GLint texture_color_uniform_tex;
    GLint texture_color_uniform_color;

    ShaderProgram gradient_program;
    GLint gradient_attrib_position;
    GLint gradient_uniform_mvp;
    GLint gradient_uniform_color_top;
    GLint gradient_uniform_color_bottom;
};

#endif
