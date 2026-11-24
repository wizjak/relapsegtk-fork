#include "topographic.h"

#include <epoxy/gl.h>

#define TOPOGRAPHIC_BACKGROUND_TYPE (topographic_background_get_type())
G_DECLARE_FINAL_TYPE(TopographicBackground, topographic_background, RELAPSE,
                     TOPOGRAPHIC_BACKGROUND, GtkGLArea)

struct _TopographicBackground {
    GtkGLArea parent_instance;
    GLuint program;
    GLuint vao;
    GLuint vbo;
    GLint u_res;
    GLint u_accent;
    GLint u_time;
    GLint u_scale;
    GdkRGBA accent;
    guint tick_id;
    gint64 start_time;
};

G_DEFINE_FINAL_TYPE(TopographicBackground, topographic_background, GTK_TYPE_GL_AREA)

static const char *VS_SRC =
    "#version 300 es\n"
    "layout(location=0) in vec2 pos;\n"
    "void main(){ gl_Position = vec4(pos, 0.0, 1.0); }\n";

/* Procedural topographic contour lines.
 *
 * Why this looks crisp at any size:
 *  - The height field is built from value noise that returns its analytic
 *    gradient (including the chain rule through the domain warp), so we know
 *    exactly how many contour levels cross one pixel. No fwidth(), whose 2x2
 *    quad estimate is blocky and direction dependent.
 *  - From that gradient we compute the real distance in pixels to the nearest
 *    iso-line and anti-alias with a 1px linear ramp. Line width is therefore
 *    constant in pixels (scaled by the HiDPI factor), never "sometimes thin".
 *  - Lines are alpha-blended at low opacity with a minor/major hierarchy
 *    instead of added on top of a near-black background, which tames the
 *    contrast. Where contours get packed tighter than ~2px they fade out
 *    instead of producing moire.
 *  - Integer (PCG) hashing keeps the noise stable over long run times, and a
 *    tiny triangular dither removes banding in the dark gradients.
 *
 * Waves:
 *  - A rolling swell (three crossing travelling wave trains, bent by the same
 *    domain warp and gated by a slow noise envelope so it comes and goes in
 *    patches) and expanding ring "drops" that respawn at PCG-hashed positions
 *    are added to the height itself, so the contours ripple on their own.
 *  - Their slopes are analytic as well (chain rule through the warp), so the
 *    lines stay exactly as crisp as before.
 *  - A slope-based sheen (light from the upper left) shades only the wave
 *    part of the surface, and lines inside active waves get brighter and
 *    shift towards the accent colour. Tweak SWELL_AMP and the DROP_* constants
 *    at the top of the shader to calm it down or push it harder. */
static const char *FS_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "out vec4 frag;\n"
    "uniform vec2 u_res;\n"
    "uniform vec3 u_accent;\n"
    "uniform float u_time;\n"
    "uniform float u_scale;\n"
    "\n"
    "const float LEVELS = 12.0;       /* contour lines per unit of height */\n"
    "const float MAJOR_EVERY = 5.0;   /* every Nth contour is a major one */\n"
    "\n"
    "/* Swell: three crossing wave trains that bend with the domain warp. */\n"
    "const float SWELL_AMP = 2.3;     /* global gain for the swell */\n"
    "\n"
    "/* Drops: expanding ring packets that spawn at random spots. */\n"
    "const float DROP_PERIOD = 7.5;   /* seconds between drops, per source */\n"
    "const float DROP_RANGE  = 2.7;   /* how far a ring travels (noise units) */\n"
    "const float DROP_K      = 22.0;  /* ring frequency (rad per noise unit) */\n"
    "const float DROP_SIGMA  = 0.38;  /* width of the ring packet */\n"
    "const float DROP_AMP    = 0.16;\n"
    "\n"
    "uint pcg(uint v){\n"
    "    uint s = v * 747796405u + 2891336453u;\n"
    "    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;\n"
    "    return (w >> 22u) ^ w;\n"
    "}\n"
    "float hash(vec2 p){\n"
    "    ivec2 q = ivec2(p);\n"
    "    return float(pcg(uint(q.x) + pcg(uint(q.y)))) * (1.0 / 4294967295.0);\n"
    "}\n"
    "float hashu(uint n){\n"
    "    return float(pcg(n)) * (1.0 / 4294967295.0);\n"
    "}\n"
    "\n"
    "/* Quintic value noise: returns (value, d/dx, d/dy). */\n"
    "vec3 noised(vec2 p){\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = fract(p);\n"
    "    vec2 u  = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
    "    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);\n"
    "    float a = hash(i);\n"
    "    float b = hash(i + vec2(1.0, 0.0));\n"
    "    float c = hash(i + vec2(0.0, 1.0));\n"
    "    float d = hash(i + vec2(1.0, 1.0));\n"
    "    float k1 = b - a;\n"
    "    float k2 = c - a;\n"
    "    float k4 = a - b - c + d;\n"
    "    float v = a + k1 * u.x + k2 * u.y + k4 * u.x * u.y;\n"
    "    return vec3(v, du * vec2(k1 + k4 * u.y, k2 + k4 * u.x));\n"
    "}\n"
    "\n"
    "/* 4-octave fbm with analytic gradient w.r.t. the input point. */\n"
    "vec3 fbmd(vec2 p){\n"
    "    const mat2 rot = mat2(0.80, 0.60, -0.60, 0.80) * 2.0;\n"
    "    float value = 0.0;\n"
    "    vec2 grad = vec2(0.0);\n"
    "    float amp = 0.5;\n"
    "    mat2 m = mat2(1.0);              /* d p_octave / d p_input */\n"
    "    for (int i = 0; i < 4; ++i) {\n"
    "        vec3 n = noised(p);\n"
    "        value += amp * n.x;\n"
    "        grad += amp * (n.yz * m);\n"
    "        p = rot * p + 11.3;\n"
    "        m = rot * m;\n"
    "        amp *= 0.5;\n"
    "    }\n"
    "    return vec3(value, grad);\n"
    "}\n"
    "\n"
    "/* Pixel distance to the nearest iso-line of a field that has `step`\n"
    " * between lines, given f and its gradient magnitude in units/pixel. */\n"
    "float iso_dist(float f, float step_, float gf){\n"
    "    float g = f / step_;\n"
    "    return abs(g - floor(g + 0.5)) * step_ / gf;\n"
    "}\n"
    "\n"
    "/* Travelling plane wave amp*sin(k*dot(dir,q) - w*t + ph): (value, gradient). */\n"
    "vec3 plane_wave(vec2 q, float ang, float k, float w, float amp, float ph, float t){\n"
    "    vec2 dir = vec2(cos(ang), sin(ang));\n"
    "    float a = k * dot(dir, q) - w * t + ph;\n"
    "    return vec3(amp * sin(a), amp * k * cos(a) * dir);\n"
    "}\n"
    "\n"
    "vec3 swell(vec2 q, float t){\n"
    "    return SWELL_AMP * (\n"
    "          plane_wave(q,  0.35,  9.0,  1.30, 0.050, 0.0, t)\n"
    "        + plane_wave(q,  1.95, 14.0, -1.70, 0.028, 2.1, t)\n"
    "        + plane_wave(q, -0.85,  5.5,  0.80, 0.055, 4.2, t));\n"
    "}\n"
    "\n"
    "/* Three staggered drops. Each one lives DROP_PERIOD seconds, grows a\n"
    " * Gaussian-windowed ring, fades, and respawns somewhere else (PCG-hashed\n"
    " * from the cycle number). Returns (height, d/dx, d/dy) in screen space;\n"
    " * `glow` is the non-oscillating ring envelope, used to light the lines. */\n"
    "vec3 drops(vec2 s, float aspect, float t, out float glow){\n"
    "    vec3 acc = vec3(0.0);\n"
    "    glow = 0.0;\n"
    "    for (int i = 0; i < 3; ++i) {\n"
    "        float cyc = t / DROP_PERIOD + float(i) / 3.0;\n"
    "        float age = fract(cyc);\n"
    "        uint id = uint(cyc) * 3u + uint(i);\n"
    "        vec2 c = (vec2(hashu(id * 2u + 1u), hashu(id * 2u + 2u)) - 0.5)\n"
    "               * vec2(2.0 * aspect, 2.0) * 0.95;\n"
    "        vec2 dv = s - c;\n"
    "        float r = length(dv);\n"
    "        float d = r - age * DROP_RANGE;\n"
    "        float g = exp(-d * d / (DROP_SIGMA * DROP_SIGMA));\n"
    "        float life = smoothstep(0.0, 0.08, age) * (1.0 - age) * (1.0 - age);\n"
    "        float ph = DROP_K * d;\n"
    "        float sn = sin(ph);\n"
    "        float cs = cos(ph);\n"
    "        float amp = DROP_AMP * life * g;\n"
    "        float radial = amp * (DROP_K * cs - 2.0 * d / (DROP_SIGMA * DROP_SIGMA) * sn);\n"
    "        acc += vec3(amp * sn, radial * dv / max(r, 1e-4));\n"
    "        glow += life * g;\n"
    "    }\n"
    "    return acc;\n"
    "}\n"
    "\n"
    "void main(){\n"
    "    float ppx = 2.1 / max(u_res.y, 1.0);          /* noise units per pixel */\n"
    "    float aspect = u_res.x / max(u_res.y, 1.0);\n"
    "    vec2 s = (gl_FragCoord.xy - 0.5 * u_res) * ppx;   /* screen-anchored */\n"
    "    vec2 drift = vec2(u_time * 0.12, u_time * 0.06);\n"
    "    vec2 p = s + drift;\n"
    "\n"
    "    /* domain warp, with its Jacobian carried through to the gradient */\n"
    "    vec3 w1 = fbmd(p + vec2(1.3, 8.1));\n"
    "    vec3 w2 = fbmd(p + vec2(7.9, 2.4));\n"
    "    vec2 q = p + 0.45 * (vec2(w1.x, w2.x) - 0.5);\n"
    "    vec3 hd = fbmd(q * 1.2);\n"
    "    mat2 jt = mat2(1.0 + 0.45 * w1.y, 0.45 * w1.z,\n"
    "                   0.45 * w2.y, 1.0 + 0.45 * w2.z);\n"
    "\n"
    "    /* waves: swell lives in warped space (so the crests bend with the\n"
    "     * terrain) under a slow patchy envelope; drops live in screen space */\n"
    "    vec3 sw = swell(q - drift, u_time);\n"
    "    vec3 en = noised(p * 0.35 + vec2(5.2, 1.7));\n"
    "    float env = 0.25 + 1.30 * en.x;\n"
    "    vec2 genv = 1.30 * 0.35 * en.yz;\n"
    "    float glow;\n"
    "    vec3 dp = drops(s, aspect, u_time, glow);\n"
    "\n"
    "    vec2 gT = jt * (1.2 * hd.yz);                       /* terrain slope */\n"
    "    vec2 gW = jt * (env * sw.yz) + sw.x * genv + dp.yz; /* wave slope    */\n"
    "    vec2 gh = gT + gW;\n"
    "    float h = hd.x + env * sw.x + dp.x;\n"
    "\n"
    "    float f  = h * LEVELS;\n"
    "    float gf = max(LEVELS * length(gh) * ppx, 1e-4);   /* levels per pixel */\n"
    "\n"
    "    /* constant pixel width, scaled for HiDPI; half-widths in pixels */\n"
    "    float hwMinor = 0.65 * u_scale;\n"
    "    float hwMajor = 1.00 * u_scale;\n"
    "    float covMinor = clamp(hwMinor - iso_dist(f, 1.0, gf) + 0.5, 0.0, 1.0);\n"
    "    float covMajor = clamp(hwMajor - iso_dist(f, MAJOR_EVERY, gf) + 0.5, 0.0, 1.0);\n"
    "\n"
    "    /* fade out where contours are packed tighter than a few pixels */\n"
    "    covMinor *= 1.0 - smoothstep(0.20 / u_scale, 0.45 / u_scale, gf);\n"
    "    covMajor *= 1.0 - smoothstep(0.35 / u_scale, 0.80 / u_scale, gf);\n"
    "\n"
    "    /* terrain elevation (waves excluded) drives a very soft tint */\n"
    "    float hn = clamp((hd.x - 0.25) / 0.55, 0.0, 1.0);\n"
    "    vec3 base = mix(vec3(0.026, 0.030, 0.037), vec3(0.040, 0.047, 0.057), hn);\n"
    "    base += u_accent * (0.018 * hn * hn);\n"
    "\n"
    "    /* sheen: a light from the upper left catches the wave slopes only */\n"
    "    float sheen = clamp(dot(gW, vec2(-0.60, 0.80)) * 0.9, -1.0, 1.0);\n"
    "    base *= 1.0 + 0.7 * sheen;\n"
    "    base += mix(vec3(0.45, 0.62, 0.78), u_accent, 0.5) * 0.050 * max(sheen, 0.0);\n"
    "\n"
    "    /* swell patches and fresh ripples light up their contour lines */\n"
    "    float act = clamp(0.55 * (env - 0.55) + 1.4 * glow, 0.0, 1.0);\n"
    "    vec3 lineCol = mix(vec3(0.62, 0.69, 0.75), u_accent, 0.18 + 0.30 * hn);\n"
    "    lineCol = mix(lineCol, mix(u_accent, vec3(1.0), 0.30), 0.65 * act);\n"
    "    float vary = 0.80 + 0.20 * smoothstep(0.2, 0.8, w2.x);\n"
    "    float lift = 1.0 + 0.9 * act;\n"
    "\n"
    "    float aMinor = covMinor * 0.22 * vary * lift;\n"
    "    float aMajor = covMajor * 0.30 * vary * lift;\n"
    "    vec3 col = base;\n"
    "    col = mix(col, lineCol, aMinor);\n"
    "    col = mix(col, lineCol, aMajor);\n"
    "\n"
    "    /* gentle vignette keeps the eye away from the edges */\n"
    "    vec2 uv = gl_FragCoord.xy / max(u_res, vec2(1.0)) - 0.5;\n"
    "    col *= clamp(1.0 - 0.56 * dot(uv, uv), 0.0, 1.0);\n"
    "\n"
    "    /* triangular dither to avoid 8-bit banding on dark gradients */\n"
    "    col += (hash(gl_FragCoord.xy) + hash(gl_FragCoord.xy + 57.0) - 1.0) / 255.0;\n"
    "\n"
    "    frag = vec4(col, 1.0);\n"
    "}\n";

static GLuint compile(GLenum kind, const char *src)
{
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        g_warning("topographic shader compile: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer user_data)
{
    (void)clock;
    (void)user_data;
    gtk_gl_area_queue_render(GTK_GL_AREA(widget));
    return G_SOURCE_CONTINUE;
}

/* Animate only while visible, locked to the frame clock. */
static void on_map(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    TopographicBackground *self = RELAPSE_TOPOGRAPHIC_BACKGROUND(widget);
    if (!self->tick_id)
        self->tick_id = gtk_widget_add_tick_callback(widget, on_tick, NULL, NULL);
}

static void stop_tick(TopographicBackground *self)
{
    if (self->tick_id) {
        gtk_widget_remove_tick_callback(GTK_WIDGET(self), self->tick_id);
        self->tick_id = 0;
    }
}

static void on_unmap(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    stop_tick(RELAPSE_TOPOGRAPHIC_BACKGROUND(widget));
}

static void on_realize(GtkGLArea *area)
{
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area) != NULL) return;

    TopographicBackground *self = RELAPSE_TOPOGRAPHIC_BACKGROUND(area);

    GLuint vs = compile(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS_SRC);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }
    self->program = glCreateProgram();
    glAttachShader(self->program, vs);
    glAttachShader(self->program, fs);
    glLinkProgram(self->program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(self->program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(self->program, sizeof log, NULL, log);
        g_warning("topographic program link: %s", log);
        glDeleteProgram(self->program);
        self->program = 0;
        return;
    }

    self->u_res = glGetUniformLocation(self->program, "u_res");
    self->u_accent = glGetUniformLocation(self->program, "u_accent");
    self->u_time = glGetUniformLocation(self->program, "u_time");
    self->u_scale = glGetUniformLocation(self->program, "u_scale");

    static const float verts[] = { -1,-1,  3,-1,  -1, 3 };
    glGenVertexArrays(1, &self->vao);
    glGenBuffers(1, &self->vbo);
    glBindVertexArray(self->vao);
    glBindBuffer(GL_ARRAY_BUFFER, self->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
}

static void on_unrealize(GtkGLArea *area)
{
    TopographicBackground *self = RELAPSE_TOPOGRAPHIC_BACKGROUND(area);
    stop_tick(self);

    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area) != NULL) return;

    if (self->vbo) glDeleteBuffers(1, &self->vbo);
    if (self->vao) glDeleteVertexArrays(1, &self->vao);
    if (self->program) glDeleteProgram(self->program);
    self->vbo = self->vao = self->program = 0;
}

static gboolean on_render(GtkGLArea *area, GdkGLContext *ctx)
{
    (void)ctx;
    TopographicBackground *self = RELAPSE_TOPOGRAPHIC_BACKGROUND(area);
    if (!self->program) return FALSE;

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glClearColor(0.030f, 0.034f, 0.041f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(self->program);
    glUniform2f(self->u_res, (float)viewport[2], (float)viewport[3]);
    glUniform3f(self->u_accent, (float)self->accent.red, (float)self->accent.green,
                (float)self->accent.blue);
    if (self->u_scale >= 0)
        glUniform1f(self->u_scale, (float)MAX(gtk_widget_get_scale_factor(GTK_WIDGET(area)), 1));
    if (self->u_time >= 0) {
        gint64 now = g_get_monotonic_time();
        gdouble elapsed = (now - self->start_time) / 1000000.0;
        glUniform1f(self->u_time, (float)elapsed);
    }
    glBindVertexArray(self->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

static void topographic_background_init(TopographicBackground *self)
{
    self->accent = (GdkRGBA){0.204, 0.886, 0.627, 1.0};
    self->start_time = g_get_monotonic_time();
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(self), GDK_GL_API_GLES);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(self), FALSE);
    gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(self), FALSE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(self), TRUE);

    g_signal_connect(self, "realize", G_CALLBACK(on_realize), NULL);
    g_signal_connect(self, "unrealize", G_CALLBACK(on_unrealize), NULL);
    g_signal_connect(self, "render", G_CALLBACK(on_render), NULL);
    g_signal_connect(self, "map", G_CALLBACK(on_map), NULL);
    g_signal_connect(self, "unmap", G_CALLBACK(on_unmap), NULL);

    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(self), "topographic-background");
}

static void topographic_background_class_init(TopographicBackgroundClass *klass)
{
    (void)klass;
}

GtkWidget *topographic_background_new(void)
{
    return g_object_new(TOPOGRAPHIC_BACKGROUND_TYPE, NULL);
}

void topographic_background_set_accent(GtkWidget *background, const GdkRGBA *accent)
{
    if (!RELAPSE_IS_TOPOGRAPHIC_BACKGROUND(background) || !accent)
        return;
    TopographicBackground *self = RELAPSE_TOPOGRAPHIC_BACKGROUND(background);
    self->accent = *accent;
    gtk_gl_area_queue_render(GTK_GL_AREA(self));
}
