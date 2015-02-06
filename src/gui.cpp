#include "gui.hpp"

#include <new>

uint32_t hash_font_key(const FontCacheKey &k) {
    return ((uint32_t)k.font_size) * 3U + k.codepoint * 2147483647U;
}

static void ft_ok(FT_Error err) {
    if (err)
        panic("freetype error");
}

Gui::Gui(SDL_Window *window) :
    _text_shader_program(R"VERTEX(

#version 150 core

in vec3 VertexPosition;
in vec2 TexCoord;

out vec2 FragTexCoord;

uniform mat4 MVP;

void main(void)
{
    FragTexCoord = TexCoord;
    gl_Position = MVP * vec4(VertexPosition, 1.0);
}

)VERTEX", R"FRAGMENT(

#version 150 core

in vec2 FragTexCoord;
out vec4 FragColor;

uniform sampler2D Tex;
uniform vec4 Color;

void main(void)
{
    FragColor = vec4(1, 1, 1, texture(Tex, FragTexCoord).a) * Color;
}

)FRAGMENT", NULL),
    _window(window)
{
    _attrib_tex_coord = _text_shader_program.attrib_location("TexCoord");
    _attrib_position = _text_shader_program.attrib_location("VertexPosition");
    _uniform_mvp = _text_shader_program.uniform_location("MVP");
    _uniform_tex = _text_shader_program.uniform_location("Tex");
    _uniform_color = _text_shader_program.uniform_location("Color");

    ft_ok(FT_Init_FreeType(&_ft_library));
    ft_ok(FT_New_Face(_ft_library, "assets/OpenSans-Regular.ttf", 0, &_default_font_face));

    // disable vsync for now because of https://bugs.launchpad.net/unity/+bug/1415195
    SDL_GL_SetSwapInterval(0);

    glClearColor(0.3, 0.3, 0.3, 1.0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    resize();
}

Gui::~Gui() {
    FT_Done_Face(_default_font_face);
    FT_Done_FreeType(_ft_library);
}

void Gui::exec() {
    bool running = true;
    while (running) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                default:
                    break;
                }
                break;
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_RESIZED:
                    case SDL_WINDOWEVENT_MAXIMIZED:
                    case SDL_WINDOWEVENT_RESTORED:
                        resize();
                        break;
                }
                break;
            }
        }

        glClear(GL_COLOR_BUFFER_BIT);

        for (int i = 0; i < _widget_list.length(); i += 1) {
            LabelWidget *label_widget = reinterpret_cast<LabelWidget *>(_widget_list.at(i));
            if (label_widget->is_visible())
                label_widget->draw(_projection);
        }

        SDL_GL_SwapWindow(_window);
        SDL_Delay(17);
    }
}

void Gui::resize() {
    SDL_GL_GetDrawableSize(_window, &_width, &_height);
    _projection = glm::ortho(0.0f, (float)_width, (float)_height, 0.0f);
}

LabelWidget * Gui::create_label_widget() {
    LabelWidget *label_widget = allocate<LabelWidget>(1);
    label_widget->init(this, _widget_list.length());
    _widget_list.append(label_widget);
    return label_widget;
}

FontCacheValue Gui::font_cache_entry(const FontCacheKey &key) {
    FontCacheValue value;
    if (_font_cache.get(key, &value))
        return value;

    ft_ok(FT_Set_Char_Size(_default_font_face, 0, key.font_size * 64, 0, 0));
    FT_UInt glyph_index = FT_Get_Char_Index(_default_font_face, key.codepoint);
    ft_ok(FT_Load_Glyph(_default_font_face, glyph_index, FT_LOAD_RENDER));
    FT_GlyphSlot glyph_slot = _default_font_face->glyph;
    FT_Glyph glyph;
    ft_ok(FT_Get_Glyph(glyph_slot, &glyph));
    ft_ok(FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, NULL, 0));
    FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph) glyph;
    value = FontCacheValue{glyph, bitmap_glyph, glyph_index};
    _font_cache.put(key, value);
    return value;
}
