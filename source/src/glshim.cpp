// glshim.cpp: fixed-function OpenGL 1.x emulation on OpenGL ES 2.0 (see glshim.h).
//
// Layout:
//   1. emulated state     (matrix stacks, current color/texcoords, client arrays, tex env, fog, alpha test)
//   2. uber shader        (GLSL ES 1.00; one program, state passed as uniforms)
//   3. draw paths         (immediate mode batching, glDrawArrays/Elements, QUADS -> TRIANGLES)
//   4. extension lookups  (shim_getprocaddress answers the engine's glXxxARB/EXT probes)
// Only the GLES2 calls in sections 2/3 (and the few direct ::gl* pass-throughs) touch the graphics API,
// so a different backend (e.g. Vulkan) can replace them while sections 1 and 4 stay as they are.

#define GLSHIM_IMPL
#include "glshim.h"

#ifdef USE_GLES2_SHIM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------- 1. emulated state

enum { MAXSTACK = 32, NUNITS = 2 };

struct matstack { GLfloat m[MAXSTACK][16]; int top; };

static matstack mvstack, projstack, texstack[NUNITS];
static GLenum matmode = GL_MODELVIEW;
static int activeunit = 0, clientunit = 0;
static bool dirty = true;

static void m_identity(GLfloat *m) { memset(m, 0, 16*sizeof(GLfloat)); m[0] = m[5] = m[10] = m[15] = 1.0f; }

// r = a*b, column-major like GL
static void m_mul(GLfloat *r, const GLfloat *a, const GLfloat *b)
{
    GLfloat t[16];
    for(int c = 0; c < 4; c++) for(int row = 0; row < 4; row++)
    {
        GLfloat s = 0;
        for(int k = 0; k < 4; k++) s += a[k*4 + row] * b[c*4 + k];
        t[c*4 + row] = s;
    }
    memcpy(r, t, sizeof(t));
}

static matstack &curstack()
{
    switch(matmode)
    {
        case GL_PROJECTION: return projstack;
        case GL_TEXTURE: return texstack[activeunit];
        default: return mvstack;
    }
}
static GLfloat *curmat() { matstack &s = curstack(); return s.m[s.top]; }

static void initstate();

void shim_glMatrixMode(GLenum mode) { matmode = mode; }
void shim_glLoadIdentity() { m_identity(curmat()); dirty = true; }
void shim_glLoadMatrixf(const GLfloat *m) { memcpy(curmat(), m, 16*sizeof(GLfloat)); dirty = true; }
void shim_glMultMatrixf(const GLfloat *m) { GLfloat *c = curmat(); m_mul(c, c, m); dirty = true; }
void shim_glPushMatrix()
{
    matstack &s = curstack();
    if(s.top + 1 >= MAXSTACK) { fprintf(stderr, "glshim: matrix stack overflow\n"); return; }
    memcpy(s.m[s.top + 1], s.m[s.top], 16*sizeof(GLfloat));
    s.top++;
    dirty = true;
}
void shim_glPopMatrix()
{
    matstack &s = curstack();
    if(s.top <= 0) { fprintf(stderr, "glshim: matrix stack underflow\n"); return; }
    s.top--;
    dirty = true;
}
void shim_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat m[16]; m_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
    shim_glMultMatrixf(m);
}
void shim_glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat m[16]; m_identity(m);
    m[0] = x; m[5] = y; m[10] = z;
    shim_glMultMatrixf(m);
}
void shim_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat len = sqrtf(x*x + y*y + z*z);
    if(len < 1e-6f) return;
    x /= len; y /= len; z /= len;
    GLfloat a = angle * 3.14159265358979f / 180.0f, c = cosf(a), s = sinf(a), t = 1.0f - c;
    GLfloat m[16]; m_identity(m);
    m[0] = t*x*x + c;     m[4] = t*x*y - s*z; m[8]  = t*x*z + s*y;
    m[1] = t*x*y + s*z;   m[5] = t*y*y + c;   m[9]  = t*y*z - s*x;
    m[2] = t*x*z - s*y;   m[6] = t*y*z + s*x; m[10] = t*z*z + c;
    shim_glMultMatrixf(m);
}
void shim_glOrtho(double l, double r, double b, double t, double n, double f)
{
    GLfloat m[16]; m_identity(m);
    m[0] = (GLfloat)(2.0/(r-l));  m[5] = (GLfloat)(2.0/(t-b));  m[10] = (GLfloat)(-2.0/(f-n));
    m[12] = (GLfloat)(-(r+l)/(r-l)); m[13] = (GLfloat)(-(t+b)/(t-b)); m[14] = (GLfloat)(-(f+n)/(f-n));
    shim_glMultMatrixf(m);
}
void shim_glFrustum(double l, double r, double b, double t, double n, double f)
{
    GLfloat m[16]; memset(m, 0, sizeof(m));
    m[0] = (GLfloat)(2.0*n/(r-l));  m[5] = (GLfloat)(2.0*n/(t-b));
    m[8] = (GLfloat)((r+l)/(r-l));  m[9] = (GLfloat)((t+b)/(t-b));
    m[10] = (GLfloat)(-(f+n)/(f-n)); m[11] = -1.0f;
    m[14] = (GLfloat)(-2.0*f*n/(f-n));
    shim_glMultMatrixf(m);
}

// current immediate-mode attributes
static GLfloat curcol[4] = { 1, 1, 1, 1 };
static GLfloat curtc[NUNITS][4] = { { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };

void shim_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { curcol[0] = r; curcol[1] = g; curcol[2] = b; curcol[3] = a; }
void shim_glTexCoord2f(GLfloat s, GLfloat t) { curtc[0][0] = s; curtc[0][1] = t; curtc[0][2] = 0; curtc[0][3] = 1; }

static int unitidx(GLenum e) { int u = (int)e - (int)GL_TEXTURE0; return u < 0 ? 0 : (u >= NUNITS ? NUNITS - 1 : u); }

void shim_glMultiTexCoord(GLenum unit, GLfloat s, GLfloat t, GLfloat r)
{
    GLfloat *tc = curtc[unitidx(unit)];
    tc[0] = s; tc[1] = t; tc[2] = r; tc[3] = 1;
}

// client arrays
struct clientarray { bool on; GLint size; GLenum type; GLsizei stride; const void *ptr; };
static clientarray carr_vtx, carr_col, carr_tc[NUNITS];

static clientarray *arrayfor(GLenum array)
{
    switch(array)
    {
        case GL_VERTEX_ARRAY: return &carr_vtx;
        case GL_COLOR_ARRAY: return &carr_col;
        case GL_TEXTURE_COORD_ARRAY: return &carr_tc[clientunit];
        default: return NULL; // normals etc. are unused (no lighting)
    }
}
void shim_glEnableClientState(GLenum array) { clientarray *a = arrayfor(array); if(a) a->on = true; }
void shim_glDisableClientState(GLenum array) { clientarray *a = arrayfor(array); if(a) a->on = false; }
static void setptr(clientarray &a, GLint size, GLenum type, GLsizei stride, const void *ptr) { a.size = size; a.type = type; a.stride = stride; a.ptr = ptr; }
void shim_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { setptr(carr_vtx, size, type, stride, ptr); }
void shim_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { setptr(carr_col, size, type, stride, ptr); }
void shim_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { setptr(carr_tc[clientunit], size, type, stride, ptr); }

// texture environment (per unit). Enum values are the desktop GL / ARB_texture_env_combine ones.
enum
{
    SH_COMBINE_RGB = 0x8571, SH_COMBINE_ALPHA = 0x8572, SH_RGB_SCALE = 0x8573, SH_ALPHA_SCALE = 0x0D1C,
    SH_SOURCE0_RGB = 0x8580, SH_SOURCE0_ALPHA = 0x8588, SH_OPERAND0_RGB = 0x8590, SH_OPERAND0_ALPHA = 0x8598,
    SH_ADD_SIGNED = 0x8574, SH_INTERPOLATE = 0x8575, SH_CONSTANT = 0x8576, SH_PRIMARY_COLOR = 0x8577, SH_PREVIOUS = 0x8578,
    SH_SUBTRACT = 0x84E7
};

struct texenvstate
{
    int mode, crgb, calpha;        // mode: 0 modulate 1 replace 2 add 3 decal 4 combine 5 blend; funcs: 0 replace 1 modulate 2 add 3 add_signed 4 interpolate 5 subtract
    int srgb[3], orgb[3], salpha[3], oalpha[3]; // sources: 0 texture 1 constant 2 primary 3 previous; operands: 0 src_color 1 1-src_color 2 src_alpha 3 1-src_alpha
    GLfloat scale[2], color[4];
};
static texenvstate envs[NUNITS];
static bool tex2d[NUNITS];

static int mapmode(GLint m)
{
    switch(m) { case GL_REPLACE: return 1; case GL_ADD: return 2; case GL_DECAL: return 3; case GL_COMBINE: return 4; case GL_BLEND: return 5; default: return 0; }
}
static int mapfunc(GLint f)
{
    switch(f) { case GL_REPLACE: return 0; case SH_ADD_SIGNED: return 3; case GL_ADD: return 2; case SH_INTERPOLATE: return 4; case SH_SUBTRACT: return 5; default: return 1; }
}
static int mapsrc(GLint s)
{
    switch(s) { case SH_CONSTANT: return 1; case SH_PRIMARY_COLOR: return 2; case SH_PREVIOUS: return 3; default: return 0; }
}
static int mapop(GLint o)
{
    switch(o) { case GL_ONE_MINUS_SRC_COLOR: return 1; case GL_SRC_ALPHA: return 2; case GL_ONE_MINUS_SRC_ALPHA: return 3; default: return 0; }
}

void shim_glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    if(target != GL_TEXTURE_ENV) return;
    texenvstate &e = envs[activeunit];
    if(pname == GL_TEXTURE_ENV_MODE) e.mode = mapmode(param);
    else if(pname == SH_COMBINE_RGB) e.crgb = mapfunc(param);
    else if(pname == SH_COMBINE_ALPHA) e.calpha = mapfunc(param);
    else if(pname == SH_RGB_SCALE) e.scale[0] = (GLfloat)param;
    else if(pname == SH_ALPHA_SCALE) e.scale[1] = (GLfloat)param;
    else if(pname >= SH_SOURCE0_RGB && pname <= SH_SOURCE0_RGB + 2) e.srgb[pname - SH_SOURCE0_RGB] = mapsrc(param);
    else if(pname >= SH_SOURCE0_ALPHA && pname <= SH_SOURCE0_ALPHA + 2) e.salpha[pname - SH_SOURCE0_ALPHA] = mapsrc(param);
    else if(pname >= SH_OPERAND0_RGB && pname <= SH_OPERAND0_RGB + 2) e.orgb[pname - SH_OPERAND0_RGB] = mapop(param);
    else if(pname >= SH_OPERAND0_ALPHA && pname <= SH_OPERAND0_ALPHA + 2) e.oalpha[pname - SH_OPERAND0_ALPHA] = mapop(param);
    dirty = true;
}
void shim_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    if(target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_COLOR) return;
    memcpy(envs[activeunit].color, params, 4*sizeof(GLfloat));
    dirty = true;
}

// fog (linear only: that is all the engine uses) and alpha test
static bool fogon = false, alphaon = false;
static GLfloat fogstart = 0, fogend = 1, fogcolor[4] = { 0, 0, 0, 0 };
static int alphafunc = 7; // GL_ALWAYS - GL_NEVER
static GLfloat alpharef = 0;

void shim_glFogi(GLenum pname, GLint param)
{
    if(pname == GL_FOG_START) fogstart = (GLfloat)param;
    else if(pname == GL_FOG_END) fogend = (GLfloat)param;
    dirty = true;
}
void shim_glFogf(GLenum pname, GLfloat param)
{
    if(pname == GL_FOG_START) fogstart = param;
    else if(pname == GL_FOG_END) fogend = param;
    dirty = true;
}
void shim_glFogfv(GLenum pname, const GLfloat *params)
{
    if(pname == GL_FOG_COLOR) memcpy(fogcolor, params, 4*sizeof(GLfloat));
    dirty = true;
}
void shim_glAlphaFunc(GLenum func, GLfloat ref) { alphafunc = (int)func - 0x0200; alpharef = ref; dirty = true; }

static void initstate()
{
    m_identity(mvstack.m[0]); m_identity(projstack.m[0]);
    mvstack.top = projstack.top = 0;
    for(int i = 0; i < NUNITS; i++)
    {
        m_identity(texstack[i].m[0]); texstack[i].top = 0;
        texenvstate &e = envs[i];
        e.mode = 0; e.crgb = e.calpha = 1;
        e.srgb[0] = 0; e.srgb[1] = 3; e.srgb[2] = 1;
        e.salpha[0] = 0; e.salpha[1] = 3; e.salpha[2] = 1;
        e.orgb[0] = 0; e.orgb[1] = 0; e.orgb[2] = 2;
        e.oalpha[0] = e.oalpha[1] = e.oalpha[2] = 2;
        e.scale[0] = e.scale[1] = 1;
        memset(e.color, 0, sizeof(e.color));
        tex2d[i] = false;
        curtc[i][0] = curtc[i][1] = curtc[i][2] = 0; curtc[i][3] = 1;
    }
    memset(&carr_vtx, 0, sizeof(carr_vtx)); memset(&carr_col, 0, sizeof(carr_col)); memset(carr_tc, 0, sizeof(carr_tc));
    matmode = GL_MODELVIEW; activeunit = clientunit = 0;
    fogon = alphaon = false; alphafunc = 7; alpharef = 0;
    dirty = true;
}

// ---------------------------------------------------------------- 2. uber shader (GLES2 backend)

static const char *vs_src =
    "attribute vec4 a_pos;\n"
    "attribute vec4 a_col;\n"
    "attribute vec4 a_tc0;\n"
    "attribute vec4 a_tc1;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_tm0;\n"
    "uniform mat4 u_tm1;\n"
    "uniform vec4 u_mvz;\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_tc0;\n"
    "varying vec4 v_tc1;\n"
    "varying float v_fogz;\n"
    "void main()\n"
    "{\n"
    "    gl_Position = u_mvp * a_pos;\n"
    "    v_col = a_col;\n"
    "    v_tc0 = u_tm0 * a_tc0;\n"
    "    v_tc1 = u_tm1 * a_tc1;\n"
    "    v_fogz = abs(dot(u_mvz, a_pos));\n"
    "}\n";

static const char *fs_head =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec4 v_col;\n"
    "varying vec4 v_tc0;\n"
    "varying vec4 v_tc1;\n"
    "varying float v_fogz;\n"
    "uniform sampler2D u_s0;\n"
    "uniform sampler2D u_s1;\n"
    "uniform vec4 u_fogc;\n"
    "uniform vec3 u_fogp;\n"   // start, end, enabled
    "uniform ivec2 u_at;\n"    // func, enabled
    "uniform float u_aref;\n"
    "vec4 getsrc(int s, vec4 tex, vec4 cst, vec4 prim, vec4 prev)\n"
    "{\n"
    "    if(s == 0) return tex;\n"
    "    if(s == 1) return cst;\n"
    "    if(s == 2) return prim;\n"
    "    return prev;\n"
    "}\n"
    "vec3 oprgb(int o, vec4 c)\n"
    "{\n"
    "    if(o == 0) return c.rgb;\n"
    "    if(o == 1) return vec3(1.0) - c.rgb;\n"
    "    if(o == 2) return vec3(c.a);\n"
    "    return vec3(1.0 - c.a);\n"
    "}\n"
    "float opa(int o, vec4 c)\n"
    "{\n"
    "    if(o == 1 || o == 3) return 1.0 - c.a;\n"
    "    return c.a;\n"
    "}\n"
    "vec3 comb3(int f, vec3 a0, vec3 a1, vec3 a2)\n"
    "{\n"
    "    if(f == 0) return a0;\n"
    "    if(f == 1) return a0 * a1;\n"
    "    if(f == 2) return a0 + a1;\n"
    "    if(f == 3) return a0 + a1 - vec3(0.5);\n"
    "    if(f == 4) return a0 * a2 + a1 * (vec3(1.0) - a2);\n"
    "    return a0 - a1;\n"
    "}\n"
    "float comb1(int f, float a0, float a1, float a2)\n"
    "{\n"
    "    if(f == 0) return a0;\n"
    "    if(f == 1) return a0 * a1;\n"
    "    if(f == 2) return a0 + a1;\n"
    "    if(f == 3) return a0 + a1 - 0.5;\n"
    "    if(f == 4) return a0 * a2 + a1 * (1.0 - a2);\n"
    "    return a0 - a1;\n"
    "}\n";

// '@' is replaced with the texture unit number (GLSL ES 1.00 forbids dynamic sampler/uniform-array indexing)
static const char *fs_unit =
    "uniform ivec4 u_cm@;\n"   // mode, combine rgb func, combine alpha func, texture enabled
    "uniform vec4 u_col@;\n"
    "uniform ivec4 u_sr@;\n"
    "uniform ivec4 u_or@;\n"
    "uniform ivec4 u_sa@;\n"
    "uniform ivec4 u_oa@;\n"
    "uniform vec2 u_sc@;\n"
    "vec4 env@(vec4 prev, vec4 prim)\n"
    "{\n"
    "    if(u_cm@.w == 0) return prev;\n"
    "    vec4 t = texture2D(u_s@, v_tc@.xy / v_tc@.w);\n"
    "    int m = u_cm@.x;\n"
    "    if(m == 0) return prev * t;\n"
    "    if(m == 1) return t;\n"
    "    if(m == 2) return vec4(prev.rgb + t.rgb, prev.a * t.a);\n"
    "    if(m == 3) return vec4(mix(prev.rgb, t.rgb, t.a), prev.a);\n"
    "    if(m == 5) return vec4(mix(prev.rgb, u_col@.rgb, t.rgb), prev.a * t.a);\n"
    "    vec4 c = u_col@;\n"
    "    vec3 r0 = oprgb(u_or@.x, getsrc(u_sr@.x, t, c, prim, prev));\n"
    "    vec3 r1 = oprgb(u_or@.y, getsrc(u_sr@.y, t, c, prim, prev));\n"
    "    vec3 r2 = oprgb(u_or@.z, getsrc(u_sr@.z, t, c, prim, prev));\n"
    "    vec3 rgb = comb3(u_cm@.y, r0, r1, r2) * u_sc@.x;\n"
    "    float a0 = opa(u_oa@.x, getsrc(u_sa@.x, t, c, prim, prev));\n"
    "    float a1 = opa(u_oa@.y, getsrc(u_sa@.y, t, c, prim, prev));\n"
    "    float a2 = opa(u_oa@.z, getsrc(u_sa@.z, t, c, prim, prev));\n"
    "    float a = comb1(u_cm@.z, a0, a1, a2) * u_sc@.y;\n"
    "    return clamp(vec4(rgb, a), 0.0, 1.0);\n"
    "}\n";

static const char *fs_main =
    "void main()\n"
    "{\n"
    "    vec4 prim = v_col;\n"
    "    vec4 c = env0(prim, prim);\n"
    "    c = env1(c, prim);\n"
    "    if(u_at.y != 0)\n"
    "    {\n"
    "        int f = u_at.x;\n"
    "        bool pass = true;\n"
    "        if(f == 0) pass = false;\n"
    "        else if(f == 1) pass = c.a < u_aref;\n"
    "        else if(f == 2) pass = c.a == u_aref;\n"
    "        else if(f == 3) pass = c.a <= u_aref;\n"
    "        else if(f == 4) pass = c.a > u_aref;\n"
    "        else if(f == 5) pass = c.a != u_aref;\n"
    "        else if(f == 6) pass = c.a >= u_aref;\n"
    "        if(!pass) discard;\n"
    "    }\n"
    "    if(u_fogp.z > 0.5)\n"
    "    {\n"
    "        float range = max(u_fogp.y - u_fogp.x, 0.0001);\n"
    "        float f = clamp((u_fogp.y - v_fogz) / range, 0.0, 1.0);\n"
    "        c.rgb = mix(u_fogc.rgb, c.rgb, f);\n"
    "    }\n"
    "    gl_FragColor = c;\n"
    "}\n";

static std::string unitsrc(char digit)
{
    std::string s(fs_unit);
    for(size_t i = 0; i < s.size(); i++) if(s[i] == '@') s[i] = digit;
    return s;
}

static GLuint prog = 0;
static struct
{
    GLint mvp, tm[NUNITS], mvz, fogc, fogp, at, aref;
    GLint s[NUNITS], cm[NUNITS], col[NUNITS], sr[NUNITS], orr[NUNITS], sa[NUNITS], oa[NUNITS], sc[NUNITS];
} U;

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if(!ok)
    {
        char log[2048]; GLsizei len = 0;
        glGetShaderInfoLog(sh, sizeof(log) - 1, &len, log); log[len] = 0;
        fprintf(stderr, "glshim: %s shader compile failed:\n%s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLint uloc(const char *base, int unit = -1)
{
    char name[64];
    if(unit < 0) snprintf(name, sizeof(name), "%s", base);
    else snprintf(name, sizeof(name), "%s%d", base, unit);
    return glGetUniformLocation(prog, name);
}

bool shim_init()
{
    initstate();

    std::string fs = std::string(fs_head) + unitsrc('0') + unitsrc('1') + fs_main;
    GLuint vsh = compile(GL_VERTEX_SHADER, vs_src), fsh = compile(GL_FRAGMENT_SHADER, fs.c_str());
    if(!vsh || !fsh) return false;

    prog = glCreateProgram();
    glAttachShader(prog, vsh);
    glAttachShader(prog, fsh);
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_col");
    glBindAttribLocation(prog, 2, "a_tc0");
    glBindAttribLocation(prog, 3, "a_tc1");
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if(!ok)
    {
        char log[2048]; GLsizei len = 0;
        glGetProgramInfoLog(prog, sizeof(log) - 1, &len, log); log[len] = 0;
        fprintf(stderr, "glshim: program link failed:\n%s\n", log);
        return false;
    }
    glDeleteShader(vsh);
    glDeleteShader(fsh);

    glUseProgram(prog);
    U.mvp = uloc("u_mvp"); U.mvz = uloc("u_mvz"); U.fogc = uloc("u_fogc"); U.fogp = uloc("u_fogp");
    U.at = uloc("u_at"); U.aref = uloc("u_aref");
    for(int i = 0; i < NUNITS; i++)
    {
        U.tm[i] = uloc("u_tm", i); U.s[i] = uloc("u_s", i); U.cm[i] = uloc("u_cm", i); U.col[i] = uloc("u_col", i);
        U.sr[i] = uloc("u_sr", i); U.orr[i] = uloc("u_or", i); U.sa[i] = uloc("u_sa", i); U.oa[i] = uloc("u_oa", i); U.sc[i] = uloc("u_sc", i);
        glUniform1i(U.s[i], i);
    }
    dirty = true;
    return true;
}

static void apply_state()
{
    if(!dirty) return;
    dirty = false;

    GLfloat mvp[16];
    m_mul(mvp, projstack.m[projstack.top], mvstack.m[mvstack.top]);
    glUniformMatrix4fv(U.mvp, 1, GL_FALSE, mvp);
    const GLfloat *mv = mvstack.m[mvstack.top];
    glUniform4f(U.mvz, mv[2], mv[6], mv[10], mv[14]);
    for(int i = 0; i < NUNITS; i++)
    {
        const texenvstate &e = envs[i];
        glUniformMatrix4fv(U.tm[i], 1, GL_FALSE, texstack[i].m[texstack[i].top]);
        glUniform4i(U.cm[i], e.mode, e.crgb, e.calpha, tex2d[i] ? 1 : 0);
        glUniform4f(U.col[i], e.color[0], e.color[1], e.color[2], e.color[3]);
        glUniform4i(U.sr[i], e.srgb[0], e.srgb[1], e.srgb[2], 0);
        glUniform4i(U.orr[i], e.orgb[0], e.orgb[1], e.orgb[2], 0);
        glUniform4i(U.sa[i], e.salpha[0], e.salpha[1], e.salpha[2], 0);
        glUniform4i(U.oa[i], e.oalpha[0], e.oalpha[1], e.oalpha[2], 0);
        glUniform2f(U.sc[i], e.scale[0], e.scale[1]);
    }
    glUniform4fv(U.fogc, 1, fogcolor);
    glUniform3f(U.fogp, fogstart, fogend, fogon ? 1.0f : 0.0f);
    glUniform2i(U.at, alphafunc, alphaon ? 1 : 0);
    glUniform1f(U.aref, alpharef);
}

// ---------------------------------------------------------------- 3. draw paths

struct attribsrc { bool on; GLint size; GLenum type; GLboolean norm; GLsizei stride; const void *ptr; };

static void setup_attribs(const attribsrc *a)
{
    for(int i = 0; i < 4; i++)
    {
        if(a[i].on)
        {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, a[i].size, a[i].type, a[i].norm, a[i].stride, a[i].ptr);
        }
        else
        {
            glDisableVertexAttribArray(i);
            if(i == 0) glVertexAttrib4f(0, 0, 0, 0, 1);
            else if(i == 1) glVertexAttrib4fv(1, curcol);
            else glVertexAttrib4fv(i, curtc[i - 2]);
        }
    }
}

static int typesize(GLenum t)
{
    switch(t) { case GL_BYTE: case GL_UNSIGNED_BYTE: return 1; case GL_SHORT: case GL_UNSIGNED_SHORT: return 2; default: return 4; }
}

// vertex data comes from the client arrays, starting at vertex `first`
static void setup_client_attribs(GLint first)
{
    attribsrc a[4];
    const clientarray *src[4] = { &carr_vtx, &carr_col, &carr_tc[0], &carr_tc[1] };
    for(int i = 0; i < 4; i++)
    {
        const clientarray &c = *src[i];
        a[i].on = c.on && c.ptr;
        a[i].size = c.size;
        a[i].type = c.type;
        a[i].norm = (i == 1 && c.type == GL_UNSIGNED_BYTE) ? GL_TRUE : GL_FALSE;
        a[i].stride = c.stride;
        GLsizei stride = c.stride ? c.stride : c.size * typesize(c.type);
        a[i].ptr = (const char *)c.ptr + (size_t)first * stride;
    }
    setup_attribs(a);
}

// 0,1,2, 0,2,3 | 4,5,6, 4,6,7 ...
static std::vector<GLushort> quadidx;
static const GLushort *quad_indices(int nverts)
{
    int nquads = nverts / 4;
    if((int)quadidx.size() < nquads * 6)
    {
        size_t old = quadidx.size() / 6;
        quadidx.resize((size_t)nquads * 6);
        for(size_t q = old; q < (size_t)nquads; q++)
        {
            GLushort b = (GLushort)(q * 4);
            GLushort *p = &quadidx[q * 6];
            p[0] = b; p[1] = b + 1; p[2] = b + 2; p[3] = b; p[4] = b + 2; p[5] = b + 3;
        }
    }
    return &quadidx[0];
}

static int quadlimit(int nverts)
{
    if(nverts > 65532) { fprintf(stderr, "glshim: quad batch of %d vertices truncated to 65532\n", nverts); return 65532; }
    return nverts;
}

static void draw_arrays_prim(GLenum mode, GLint first, GLsizei count)
{
    if(mode == GL_QUADS)
    {
        count = quadlimit(count) & ~3;
        if(count <= 0) return;
        setup_client_attribs(first);
        glDrawElements(GL_TRIANGLES, count / 4 * 6, GL_UNSIGNED_SHORT, quad_indices(count));
    }
    else
    {
        setup_client_attribs(0);
        glDrawArrays(mode == GL_POLYGON ? GL_TRIANGLE_FAN : mode, first, count);
    }
}

static void draw_elements_prim(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    setup_client_attribs(0);
    if(mode == GL_QUADS)
    {
        count &= ~3;
        std::vector<GLushort> tri;
        tri.reserve((size_t)count / 4 * 6);
        for(int q = 0; q < count; q += 4)
        {
            GLuint v[4];
            for(int k = 0; k < 4; k++)
            {
                switch(type)
                {
                    case GL_UNSIGNED_BYTE: v[k] = ((const GLubyte *)indices)[q + k]; break;
                    case GL_UNSIGNED_INT: v[k] = ((const GLuint *)indices)[q + k]; break;
                    default: v[k] = ((const GLushort *)indices)[q + k]; break;
                }
            }
            static const int order[6] = { 0, 1, 2, 0, 2, 3 };
            for(int k = 0; k < 6; k++) tri.push_back((GLushort)v[order[k]]);
        }
        if(!tri.empty()) glDrawElements(GL_TRIANGLES, (GLsizei)tri.size(), GL_UNSIGNED_SHORT, &tri[0]);
    }
    else glDrawElements(mode == GL_POLYGON ? GL_TRIANGLE_FAN : mode, count, type, indices);
}

void shim_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if(!carr_vtx.on || !carr_vtx.ptr || count <= 0) return;
    apply_state();
    draw_arrays_prim(mode, first, count);
}

void shim_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    if(!carr_vtx.on || !carr_vtx.ptr || count <= 0) return;
    apply_state();
    draw_elements_prim(mode, count, type, indices);
}

// immediate mode
struct immvert { GLfloat pos[3], col[4], tc0[4], tc1[4]; };
static immvert *immbuf = NULL;
static int immcap = 0, immcount = 0;
static GLenum immmode = 0;
static bool inbegin = false;

void shim_glBegin(GLenum mode)
{
    if(inbegin) fprintf(stderr, "glshim: nested glBegin\n");
    inbegin = true;
    immmode = mode;
    immcount = 0;
}

void shim_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if(!inbegin) return;
    if(immcount >= immcap)
    {
        immcap = immcap ? immcap * 2 : 4096;
        immbuf = (immvert *)realloc(immbuf, immcap * sizeof(immvert));
    }
    immvert &v = immbuf[immcount++];
    v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
    memcpy(v.col, curcol, sizeof(curcol));
    memcpy(v.tc0, curtc[0], sizeof(v.tc0));
    memcpy(v.tc1, curtc[1], sizeof(v.tc1));
}

void shim_glEnd()
{
    if(!inbegin) return;
    inbegin = false;
    int n = immcount;
    if(n <= 0) return;
    apply_state();

    attribsrc a[4];
    a[0].on = true; a[0].size = 3; a[0].type = GL_FLOAT; a[0].norm = GL_FALSE; a[0].stride = sizeof(immvert); a[0].ptr = immbuf[0].pos;
    a[1] = a[0]; a[1].size = 4; a[1].ptr = immbuf[0].col;
    a[2] = a[0]; a[2].size = 4; a[2].ptr = immbuf[0].tc0;
    a[3] = a[0]; a[3].size = 4; a[3].ptr = immbuf[0].tc1;
    setup_attribs(a);

    switch(immmode)
    {
        case GL_QUADS:
        {
            n = quadlimit(n) & ~3;
            if(n > 0) glDrawElements(GL_TRIANGLES, n / 4 * 6, GL_UNSIGNED_SHORT, quad_indices(n));
            break;
        }
        case GL_QUAD_STRIP:
        {
            std::vector<GLushort> tri;
            for(int i = 0; i + 3 < n; i += 2)
            {
                GLushort t[6] = { (GLushort)i, (GLushort)(i + 1), (GLushort)(i + 3), (GLushort)i, (GLushort)(i + 3), (GLushort)(i + 2) };
                tri.insert(tri.end(), t, t + 6);
            }
            if(!tri.empty()) glDrawElements(GL_TRIANGLES, (GLsizei)tri.size(), GL_UNSIGNED_SHORT, &tri[0]);
            break;
        }
        case GL_POLYGON: glDrawArrays(GL_TRIANGLE_FAN, 0, n); break;
        default: glDrawArrays(immmode, 0, n); break;
    }
}

// ---------------------------------------------------------------- pass-through wrappers for state GLES2 restricts

void shim_glEnable(GLenum cap)
{
    switch(cap)
    {
        case GL_TEXTURE_2D: tex2d[activeunit] = true; dirty = true; return;
        case GL_FOG: fogon = true; dirty = true; return;
        case GL_ALPHA_TEST: alphaon = true; dirty = true; return;
        case GL_BLEND: case GL_DEPTH_TEST: case GL_CULL_FACE: case GL_STENCIL_TEST: case GL_SCISSOR_TEST:
        case GL_POLYGON_OFFSET_FILL: case GL_DITHER: case GL_SAMPLE_ALPHA_TO_COVERAGE:
            glEnable(cap); return;
        default: return; // legacy caps (GL_LINE_SMOOTH, GL_NORMALIZE, ...) do not exist in ES2
    }
}

void shim_glDisable(GLenum cap)
{
    switch(cap)
    {
        case GL_TEXTURE_2D: tex2d[activeunit] = false; dirty = true; return;
        case GL_FOG: fogon = false; dirty = true; return;
        case GL_ALPHA_TEST: alphaon = false; dirty = true; return;
        case GL_BLEND: case GL_DEPTH_TEST: case GL_CULL_FACE: case GL_STENCIL_TEST: case GL_SCISSOR_TEST:
        case GL_POLYGON_OFFSET_FILL: case GL_DITHER: case GL_SAMPLE_ALPHA_TO_COVERAGE:
            glDisable(cap); return;
        default: return;
    }
}

const GLubyte *shim_glGetString(GLenum name)
{
    if(name != GL_EXTENSIONS) return glGetString(name);
    // advertise what the shim emulates, so the engine's extension probes (rendergl.cpp) take those paths
    static std::string exts;
    if(exts.empty())
    {
        const char *real = (const char *)glGetString(GL_EXTENSIONS);
        exts = real ? real : "";
        exts += " GL_ARB_multitexture GL_EXT_texture_env_combine GL_EXT_multi_draw_arrays GL_EXT_draw_range_elements"
                " GL_ATI_separate_stencil GL_EXT_stencil_wrap";
    }
    return (const GLubyte *)exts.c_str();
}

void shim_glGetFloatv(GLenum pname, GLfloat *params)
{
    switch(pname)
    {
        case GL_MODELVIEW_MATRIX: memcpy(params, mvstack.m[mvstack.top], 16*sizeof(GLfloat)); break;
        case GL_PROJECTION_MATRIX: memcpy(params, projstack.m[projstack.top], 16*sizeof(GLfloat)); break;
        case GL_CURRENT_COLOR: memcpy(params, curcol, sizeof(curcol)); break;
        default: glGetFloatv(pname, params); break;
    }
}

void shim_glGetIntegerv(GLenum pname, GLint *params)
{
    if(pname == GL_MAX_TEXTURE_UNITS_ARB) *params = NUNITS;
    else glGetIntegerv(pname, params);
}

void shim_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    if(param == GL_CLAMP) param = GL_CLAMP_TO_EDGE;
    glTexParameteri(target, pname, param);
}

// ES2 requires internalformat == format and has no BGR(A); convert and strip the sized formats
void shim_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels)
{
    (void)internalformat;
    std::vector<GLubyte> tmp;
    if((format == GL_BGR || format == GL_BGRA) && pixels && type == GL_UNSIGNED_BYTE)
    {
        int bpp = format == GL_BGR ? 3 : 4;
        tmp.assign((const GLubyte *)pixels, (const GLubyte *)pixels + (size_t)w * h * bpp);
        for(size_t i = 0; i + 2 < tmp.size(); i += bpp) { GLubyte t = tmp[i]; tmp[i] = tmp[i + 2]; tmp[i + 2] = t; }
        pixels = &tmp[0];
    }
    if(format == GL_BGR) format = GL_RGB;
    else if(format == GL_BGRA) format = GL_RGBA;
    glTexImage2D(target, level, (GLint)format, w, h, border, format, type, pixels);
}

// TODO: ES2 cannot read textures back without an FBO and the size is not queryable. Only used by the
// "mapshot" screenshot path (main.cpp), so it is a no-op for now.
void shim_glGetTexImage(GLenum, GLint, GLenum, GLenum, void *)
{
    static bool warned = false;
    if(!warned) { warned = true; fprintf(stderr, "glshim: glGetTexImage is not supported\n"); }
}

// ---------------------------------------------------------------- 4. extension entry points the engine looks up by name

static void APIENTRY p_ActiveTexture(GLenum t) { activeunit = unitidx(t); glActiveTexture(t); }
static void APIENTRY p_ClientActiveTexture(GLenum t) { clientunit = unitidx(t); }
static void APIENTRY p_MultiTexCoord2f(GLenum t, GLfloat s, GLfloat tt) { shim_glMultiTexCoord(t, s, tt, 0); }
static void APIENTRY p_MultiTexCoord3f(GLenum t, GLfloat s, GLfloat tt, GLfloat r) { shim_glMultiTexCoord(t, s, tt, r); }

static void APIENTRY p_MultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    for(GLsizei i = 0; i < primcount; i++) shim_glDrawArrays(mode, first[i], count[i]);
}
static void APIENTRY p_MultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices, GLsizei primcount)
{
    for(GLsizei i = 0; i < primcount; i++) shim_glDrawElements(mode, count[i], type, indices[i]);
}
static void APIENTRY p_DrawRangeElements(GLenum mode, GLuint, GLuint, GLsizei count, GLenum type, const void *indices)
{
    shim_glDrawElements(mode, count, type, indices);
}
static void APIENTRY p_StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) { glStencilOpSeparate(face, sfail, dpfail, dppass); }
static void APIENTRY p_StencilFuncSeparate(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask)
{
    glStencilFuncSeparate(GL_FRONT, frontfunc, ref, mask);
    glStencilFuncSeparate(GL_BACK, backfunc, ref, mask);
}

void *shim_getprocaddress(const char *name)
{
    struct { const char *n; void *p; } table[] =
    {
        { "glActiveTextureARB", (void *)p_ActiveTexture },
        { "glClientActiveTextureARB", (void *)p_ClientActiveTexture },
        { "glMultiTexCoord2fARB", (void *)p_MultiTexCoord2f },
        { "glMultiTexCoord3fARB", (void *)p_MultiTexCoord3f },
        { "glMultiDrawArraysEXT", (void *)p_MultiDrawArrays },
        { "glMultiDrawElementsEXT", (void *)p_MultiDrawElements },
        { "glDrawRangeElementsEXT", (void *)p_DrawRangeElements },
        { "glStencilOpSeparateATI", (void *)p_StencilOpSeparate },
        { "glStencilFuncSeparateATI", (void *)p_StencilFuncSeparate },
    };
    for(size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++) if(!strcmp(table[i].n, name)) return table[i].p;
    return NULL;
}

#endif // USE_GLES2_SHIM
