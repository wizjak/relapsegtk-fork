#include "shader_strip.h"

#include <epoxy/gl.h>

#define SHADER_STRIP_TYPE (shader_strip_get_type())
G_DECLARE_FINAL_TYPE(ShaderStrip, shader_strip, RELAPSE, SHADER_STRIP, GtkGLArea)

struct _ShaderStrip {
    GtkGLArea parent_instance;
    GLuint program;
    GLuint vao;
    GLuint vbo;
    GLint u_time;
    GLint u_res;
    GLint u_energy;
    GLint u_base;
    GLint u_accent;
    float energy;
    GdkRGBA base_color;
    GdkRGBA accent_color;
    GTimer *timer;
    guint tick_id;
};

G_DEFINE_FINAL_TYPE(ShaderStrip, shader_strip, GTK_TYPE_GL_AREA)

static const char *VS_SRC =
    "#version 300 es\n"
    "layout(location=0) in vec2 pos;\n"
    "out vec2 uv;\n"
    "void main(){ uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";

static const char *FS_SRC =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "uniform float u_time;\n"
    "uniform vec2 u_res;\n"
    "uniform float u_energy;\n"
    "uniform vec3 u_base;\n"
    "uniform vec3 u_accent;\n"
    "void main(){\n"
    "    float t = u_time;\n"
    "    float e = clamp(u_energy, 0.0, 1.0);\n"
    "    float x = uv.x;\n"
    "    float sweep = 0.5 + 0.5 * sin(x * 4.5 - t * 0.6);\n"
    "    vec3 base   = mix(u_base, u_accent, 0.10 + 0.15 * e);\n"
    "    vec3 accent = mix(u_base, u_accent, 0.55 + 0.45 * e);\n"
    "    vec3 col = mix(base, accent, sweep);\n"
    "    float glint = smoothstep(0.85, 1.0, sweep);\n"
    "    col += glint * u_accent * 0.25;\n"
    "    float y = uv.y;\n"
    "    float band = smoothstep(0.0, 0.30, y) * smoothstep(1.0, 0.70, y);\n"
    "    float alpha = 0.55 + 0.35 * band;\n"
    "    frag = vec4(col * alpha, alpha);\n"
    "}\n";

static GLuint compile(GLenum kind, const char *src)
{
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        g_warning("shader compile: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    (void)clock;
    (void)data;
    gtk_widget_queue_draw(widget);
    return G_SOURCE_CONTINUE;
}

static void on_realize(GtkGLArea *area)
{
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area) != NULL) return;

    ShaderStrip *self = RELAPSE_SHADER_STRIP(area);

    GLuint vs = compile(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS_SRC);
    self->program = glCreateProgram();
    glAttachShader(self->program, vs);
    glAttachShader(self->program, fs);
    glLinkProgram(self->program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    self->u_time = glGetUniformLocation(self->program, "u_time");
    self->u_res  = glGetUniformLocation(self->program, "u_res");
    self->u_energy = glGetUniformLocation(self->program, "u_energy");
    self->u_base = glGetUniformLocation(self->program, "u_base");
    self->u_accent = glGetUniformLocation(self->program, "u_accent");

    static const float verts[] = { -1,-1,  3,-1,  -1, 3 };
    glGenVertexArrays(1, &self->vao);
    glGenBuffers(1, &self->vbo);
    glBindVertexArray(self->vao);
    glBindBuffer(GL_ARRAY_BUFFER, self->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);

    self->timer = g_timer_new();
    self->tick_id = gtk_widget_add_tick_callback(GTK_WIDGET(area), on_tick, NULL, NULL);
}

static void on_unrealize(GtkGLArea *area)
{
    gtk_gl_area_make_current(area);
    ShaderStrip *self = RELAPSE_SHADER_STRIP(area);
    if (self->tick_id) {
        gtk_widget_remove_tick_callback(GTK_WIDGET(area), self->tick_id);
        self->tick_id = 0;
    }
    if (self->vbo) glDeleteBuffers(1, &self->vbo);
    if (self->vao) glDeleteVertexArrays(1, &self->vao);
    if (self->program) glDeleteProgram(self->program);
    self->vbo = self->vao = self->program = 0;
    if (self->timer) { g_timer_destroy(self->timer); self->timer = NULL; }
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *ctx)
{
    (void)ctx;
    ShaderStrip *self = RELAPSE_SHADER_STRIP(area);
    if (!self->program) return FALSE;

    int w = gtk_widget_get_width(GTK_WIDGET(area));
    int h = gtk_widget_get_height(GTK_WIDGET(area));

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(self->program);
    glUniform1f(self->u_time, (float)g_timer_elapsed(self->timer, NULL));
    glUniform2f(self->u_res, (float)w, (float)h);
    glUniform1f(self->u_energy, self->energy);
    glUniform3f(self->u_base, self->base_color.red, self->base_color.green,
                self->base_color.blue);
    glUniform3f(self->u_accent, self->accent_color.red, self->accent_color.green,
                self->accent_color.blue);
    glBindVertexArray(self->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

static void shader_strip_init(ShaderStrip *self)
{
    self->base_color = (GdkRGBA){.red = 0.5, .green = 0.5, .blue = 0.5, .alpha = 1.0};
    self->accent_color = (GdkRGBA){.red = 0.65, .green = 0.65, .blue = 0.65, .alpha = 1.0};
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(self), GDK_GL_API_GLES);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(self), FALSE);
    gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(self), FALSE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(self), TRUE);

    g_signal_connect(self, "realize", G_CALLBACK(on_realize), NULL);
    g_signal_connect(self, "unrealize", G_CALLBACK(on_unrealize), NULL);
    g_signal_connect(self, "render", G_CALLBACK(on_render), NULL);

    gtk_widget_set_size_request(GTK_WIDGET(self), -1, 4);
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(self), "shader-strip");
}

static void shader_strip_class_init(ShaderStripClass *klass) { (void)klass; }

void shader_strip_set_energy(GtkWidget *strip, float energy)
{
    g_return_if_fail(RELAPSE_IS_SHADER_STRIP(strip));
    ShaderStrip *self = RELAPSE_SHADER_STRIP(strip);
    self->energy = CLAMP(energy, 0.0f, 1.0f);
}

void shader_strip_set_colors(GtkWidget *strip, const GdkRGBA *base,
                             const GdkRGBA *accent)
{
    g_return_if_fail(RELAPSE_IS_SHADER_STRIP(strip));
    ShaderStrip *self = RELAPSE_SHADER_STRIP(strip);
    if (base)
        self->base_color = *base;
    if (accent)
        self->accent_color = *accent;
    gtk_widget_queue_draw(strip);
}

GtkWidget *shader_strip_new(void)
{
    return g_object_new(SHADER_STRIP_TYPE, NULL);
}
