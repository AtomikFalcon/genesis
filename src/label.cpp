#include "label.hpp"
#include "gui.hpp"
#include "debug.hpp"

#include <epoxy/gl.h>
#include <epoxy/glx.h>


static void ft_ok(FT_Error err) {
    if (err)
        panic("freetype error");
}

Label::Label(Gui *gui) :
    _gui(gui),
    _width(0),
    _height(0),
    _text("Label"),
    _color(1.0f, 1.0f, 1.0f, 1.0f),
    _font_size(16)
{

    glGenTextures(1, &_texture_id);
    glBindTexture(GL_TEXTURE_2D, _texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenVertexArrays(1, &_vertex_array);
    glBindVertexArray(_vertex_array);

    glGenBuffers(1, &_vertex_buffer);
    glGenBuffers(1, &_tex_coord_buffer);


    // send dummy vertex data - real data happens at update()
    GLfloat vertexes[4][3] = {
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
    };
    glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, 4 * 3 * sizeof(GLfloat), vertexes, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(_gui->_text_attrib_position);
    glVertexAttribPointer(_gui->_text_attrib_position, 3, GL_FLOAT, GL_FALSE, 0, NULL);


    GLfloat coords[4][2] = {
        {0, 0},
        {0, 1},
        {1, 0},
        {1, 1},
    };
    glBindBuffer(GL_ARRAY_BUFFER, _tex_coord_buffer);
    glBufferData(GL_ARRAY_BUFFER, 4 * 2 * sizeof(GLfloat), coords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(_gui->_text_attrib_tex_coord);
    glVertexAttribPointer(_gui->_text_attrib_tex_coord, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    assert_no_gl_error();

    update();
}

Label::~Label() {
    glDeleteBuffers(1, &_tex_coord_buffer);
    glDeleteBuffers(1, &_vertex_buffer);
    glDeleteVertexArrays(1, &_vertex_array);
    glDeleteTextures(1, &_texture_id);
}

void Label::draw(const glm::mat4 &mvp) {
    _gui->_text_shader_program.bind();

    _gui->_text_shader_program.set_uniform(_gui->_text_uniform_color, _color);
    _gui->_text_shader_program.set_uniform(_gui->_text_uniform_tex, 0);
    _gui->_text_shader_program.set_uniform(_gui->_text_uniform_mvp, mvp);

    glBindVertexArray(_vertex_array);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture_id);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void copy_freetype_bitmap(FT_Bitmap source, ByteBuffer &dest,
        int left, int top, int dest_width)
{
    int pitch = source.pitch;
    if (pitch < 0)
        panic("flow up unsupported");

    if (source.pixel_mode != FT_PIXEL_MODE_GRAY)
        panic("only 8-bit grayscale fonts supported");

    for (int y = 0; y < source.rows; y += 1) {
        for (int x = 0; x < source.width; x += 1) {
            unsigned char alpha = source.buffer[y * pitch + x];
            int dest_index = 4 * ((top + y) * dest_width + x + left) + 3;
            dest.at(dest_index) = alpha;
        }
    }
}

void Label::update() {
    // one pass to determine width and height
    // pen_x and pen_y are on the baseline. the char can go lower than it
    float pen_x = 0.0f;
    float pen_y = 0.0f;
    int previous_glyph_index = 0;
    bool first = true;
    float above_size = 0.0f; // pixel count above the baseline
    float below_size = 0.0f; // pixel count below the baseline
    float bounding_width = 0.0f;
    _letters.clear();
    for (int i = 0; i < _text.length(); i += 1) {
        uint32_t ch = _text.at(i);
        FontCacheKey key = {_font_size, ch};
        FontCacheValue entry = _gui->font_cache_entry(key);
        if (!first) {
            FT_Face face = _gui->_default_font_face;
            FT_Vector kerning;
            ft_ok(FT_Get_Kerning(face, previous_glyph_index, entry.glyph_index,
                        FT_KERNING_DEFAULT, &kerning));
            pen_x += ((float)kerning.x) / 64.0f;
        }
        first = false;

        float bmp_start_left = (float)entry.bitmap_glyph->left;
        float bmp_start_top = (float)entry.bitmap_glyph->top;
        FT_Bitmap bitmap = entry.bitmap_glyph->bitmap;
        float bmp_width = bitmap.width;
        float bmp_height = bitmap.rows;
        float right = ceilf(pen_x + bmp_start_left + bmp_width);
        float this_above_size = pen_y + bmp_start_top;
        float this_below_size = bmp_height - this_above_size;
        above_size = (this_above_size > above_size) ? this_above_size : above_size;
        below_size = (this_below_size > below_size) ? this_below_size : below_size;
        bounding_width = right;

        _letters.append(Letter {
                ch,
                entry.bitmap_glyph->left,
                entry.bitmap_glyph->top,
                (int)(pen_x + bmp_start_left),
                bitmap.width,
                (int)ceilf(this_above_size),
                (int)ceilf(this_below_size),
        });

        previous_glyph_index = entry.glyph_index;
        pen_x += ((float)entry.glyph->advance.x) / 65536.0f;
        pen_y += ((float)entry.glyph->advance.y) / 65536.0f;

    }

    float bounding_height = ceilf(above_size + below_size);
    _width = bounding_width;
    _height = bounding_height;
    _above_size = above_size;
    _below_size = below_size;

    int img_buf_size =  4 * bounding_width * bounding_height;
    if (img_buf_size <= _img_buffer.length())
        return;

    _img_buffer.resize(img_buf_size);

    glBindVertexArray(_vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
    GLfloat vertexes[4][3] = {
        {0.0f, 0.0f, 0.0f},
        {0.0f, bounding_height, 0.0f},
        {bounding_width, 0.0f, 0.0f},
        {bounding_width, bounding_height, 0.0f},
    };
    glBufferSubData(GL_ARRAY_BUFFER, 0, 3 * 4 * sizeof(GLfloat), vertexes);

    assert_no_gl_error();

    _img_buffer.fill(0);
    // second pass to render bitmap
    for (int i = 0; i < _letters.length(); i += 1) {
        Letter *letter = &_letters.at(i);
        FontCacheKey key = {_font_size, letter->codepoint};
        FontCacheValue entry = _gui->font_cache_entry(key);
        FT_Bitmap bitmap = entry.bitmap_glyph->bitmap;
        copy_freetype_bitmap(bitmap, _img_buffer,
                letter->left, _above_size - letter->bitmap_top, _width);
    }

    // send bitmap to GPU
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bounding_width, bounding_height,
            0, GL_BGRA, GL_UNSIGNED_BYTE, _img_buffer.raw());

    assert_no_gl_error();

}