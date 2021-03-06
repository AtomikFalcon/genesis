#include "texture.hpp"
#include "debug_gl.hpp"
#include "gui.hpp"
#include "gui_window.hpp"

Texture::Texture(Gui *gui) :
    _gui(gui),
    _width(0),
    _height(0)
{
    glGenTextures(1, &_texture_id);
    glBindTexture(GL_TEXTURE_2D, _texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

Texture::~Texture() {
    glDeleteTextures(1, &_texture_id);
}

void Texture::send_pixels(const ByteBuffer &pixels, int width, int height) {
    _width = width;
    _height = height;

    if (pixels.length() != width * height * 4)
        panic("invalid pixels length");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture_id);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
            0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.raw());

    assert_no_gl_error();
}

void Texture::draw(GuiWindow *window, const glm::mat4 &mvp) {
    _gui->_shader_program_manager._texture_shader_program.bind();

    _gui->_shader_program_manager._texture_shader_program.set_uniform(
            _gui->_shader_program_manager._texture_uniform_tex, 0);

    _gui->_shader_program_manager._texture_shader_program.set_uniform(
            _gui->_shader_program_manager._texture_uniform_mvp, mvp);

    glBindBuffer(GL_ARRAY_BUFFER, _gui->_static_geometry._rect_2d_vertex_buffer);
    glEnableVertexAttribArray(_gui->_shader_program_manager._texture_attrib_position);
    glVertexAttribPointer(_gui->_shader_program_manager._texture_attrib_position, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    glBindBuffer(GL_ARRAY_BUFFER, _gui->_static_geometry._rect_2d_tex_coord_buffer);
    glEnableVertexAttribArray(_gui->_shader_program_manager._texture_attrib_tex_coord);
    glVertexAttribPointer(_gui->_shader_program_manager._texture_attrib_tex_coord, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture_id);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
