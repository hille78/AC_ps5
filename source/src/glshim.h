// glshim.h: emulates the fixed-function OpenGL 1.x subset used by AssaultCube on top of OpenGL ES 2.0.
// Enabled with -DUSE_GLES2_SHIM. Implementation in glshim.cpp (shared emulation + GLES2 backend).
#ifndef __GLSHIM_H__
#define __GLSHIM_H__

#ifdef USE_GLES2_SHIM

#include <SDL_opengles2.h>

#ifndef APIENTRY
    #define APIENTRY GL_APIENTRY
#endif

// ---- legacy enums that GLES2 does not define (values from desktop GL) ----
#ifndef GL_MODELVIEW
    #define GL_MODELVIEW 0x1700
    #define GL_PROJECTION 0x1701
#endif
#ifndef GL_TEXTURE
    #define GL_TEXTURE 0x1702
#endif
#ifndef GL_QUADS
    #define GL_QUADS 0x0007
    #define GL_QUAD_STRIP 0x0008
    #define GL_POLYGON 0x0009
#endif
#ifndef GL_FOG
    #define GL_FOG 0x0B60
    #define GL_FOG_DENSITY 0x0B62
    #define GL_FOG_START 0x0B63
    #define GL_FOG_END 0x0B64
    #define GL_FOG_MODE 0x0B65
    #define GL_FOG_COLOR 0x0B66
    #define GL_FOG_HINT 0x0C54
#endif
#ifndef GL_ALPHA_TEST
    #define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_LINE_SMOOTH_HINT
    #define GL_LINE_SMOOTH_HINT 0x0C52
#endif
#ifndef GL_NICEST
    #define GL_NICEST 0x1102
#endif
#ifndef GL_SMOOTH
    #define GL_SMOOTH 0x1D01
    #define GL_FLAT 0x1D00
#endif
#ifndef GL_VERTEX_ARRAY
    #define GL_VERTEX_ARRAY 0x8074
    #define GL_NORMAL_ARRAY 0x8075
    #define GL_COLOR_ARRAY 0x8076
    #define GL_TEXTURE_COORD_ARRAY 0x8078
#endif
#ifndef GL_TEXTURE_ENV
    #define GL_TEXTURE_ENV 0x2300
    #define GL_TEXTURE_ENV_MODE 0x2200
    #define GL_TEXTURE_ENV_COLOR 0x2201
    #define GL_MODULATE 0x2100
    #define GL_DECAL 0x2101
    #define GL_REPLACE 0x1E01
    #define GL_ADD 0x0104
#endif
#ifndef GL_COMBINE
    #define GL_COMBINE 0x8570
#endif
#ifndef GL_LINE
    #define GL_POINT 0x1B00
    #define GL_LINE 0x1B01
    #define GL_FILL 0x1B02
#endif
#ifndef GL_COMPILE
    #define GL_COMPILE 0x1300
#endif
#ifndef GL_CURRENT_COLOR
    #define GL_CURRENT_COLOR 0x0B00
#endif
#ifndef GL_MODELVIEW_MATRIX
    #define GL_MODELVIEW_MATRIX 0x0BA6
    #define GL_PROJECTION_MATRIX 0x0BA7
#endif
#ifndef GL_MAX_TEXTURE_UNITS_ARB
    #define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif
#endif
#ifndef GL_LINE_SMOOTH
    #define GL_LINE_SMOOTH 0x0B20
#endif
#ifndef GL_RGB8
    #define GL_RGB8 0x8051
    #define GL_RGBA8 0x8058
    #define GL_RGB5 0x8050
    #define GL_RGBA4 0x8056
#endif
#ifndef GL_BGR
    #define GL_BGR 0x80E0
    #define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP
    #define GL_CLAMP 0x2900 /* mapped to CLAMP_TO_EDGE by shim_glTexParameteri */
#endif

// ---- shim API ----
// Call once after the GL context is current. Returns false if shader setup failed.
bool shim_init();
void *shim_getprocaddress(const char *name);

const GLubyte *shim_glGetString(GLenum name);
void shim_glEnable(GLenum cap);
void shim_glDisable(GLenum cap);
void shim_glGetFloatv(GLenum pname, GLfloat *params);
void shim_glGetIntegerv(GLenum pname, GLint *params);
void shim_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels);
void shim_glTexParameteri(GLenum target, GLenum pname, GLint param);
void shim_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels);
void shim_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void shim_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);

void shim_glMatrixMode(GLenum mode);
void shim_glLoadIdentity();
void shim_glLoadMatrixf(const GLfloat *m);
void shim_glMultMatrixf(const GLfloat *m);
void shim_glPushMatrix();
void shim_glPopMatrix();
void shim_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void shim_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void shim_glScalef(GLfloat x, GLfloat y, GLfloat z);
void shim_glOrtho(double l, double r, double b, double t, double n, double f);
void shim_glFrustum(double l, double r, double b, double t, double n, double f);

void shim_glBegin(GLenum mode);
void shim_glEnd();
void shim_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void shim_glTexCoord2f(GLfloat s, GLfloat t);
void shim_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void shim_glMultiTexCoord(GLenum unit, GLfloat s, GLfloat t, GLfloat r);

void shim_glEnableClientState(GLenum array);
void shim_glDisableClientState(GLenum array);
void shim_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr);
void shim_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr);
void shim_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr);

void shim_glTexEnvi(GLenum target, GLenum pname, GLint param);
void shim_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void shim_glFogi(GLenum pname, GLint param);
void shim_glFogf(GLenum pname, GLfloat param);
void shim_glFogfv(GLenum pname, const GLfloat *params);
void shim_glAlphaFunc(GLenum func, GLfloat ref);

#ifndef GLSHIM_IMPL
    // Redirect the legacy calls. glActiveTexture_/glMultiDraw*_/glDrawRangeElements_ etc. are reached
    // through shim_getprocaddress, so they need no macro.
    #define glGetString shim_glGetString
    #define glEnable shim_glEnable
    #define glDisable shim_glDisable
    #define glGetFloatv shim_glGetFloatv
    #define glGetIntegerv shim_glGetIntegerv
    #define glTexImage2D shim_glTexImage2D
    #define glTexParameteri shim_glTexParameteri
    #define glGetTexImage shim_glGetTexImage
    #define glDrawArrays shim_glDrawArrays
    #define glDrawElements shim_glDrawElements

    #define glMatrixMode shim_glMatrixMode
    #define glLoadIdentity shim_glLoadIdentity
    #define glLoadMatrixf shim_glLoadMatrixf
    #define glMultMatrixf shim_glMultMatrixf
    #define glPushMatrix shim_glPushMatrix
    #define glPopMatrix shim_glPopMatrix
    #define glTranslatef shim_glTranslatef
    #define glRotatef shim_glRotatef
    #define glScalef shim_glScalef
    #define glOrtho shim_glOrtho
    #define glFrustum shim_glFrustum

    #define glBegin shim_glBegin
    #define glEnd shim_glEnd
    #define glVertex2f(x, y) shim_glVertex3f((x), (y), 0.0f)
    #define glVertex2d(x, y) shim_glVertex3f((GLfloat)(x), (GLfloat)(y), 0.0f)
    #define glVertex3f shim_glVertex3f
    #define glVertex3fv(v) shim_glVertex3f((v)[0], (v)[1], (v)[2])
    #define glTexCoord2f shim_glTexCoord2f
    #define glTexCoord2i(s, t) shim_glTexCoord2f((GLfloat)(s), (GLfloat)(t))
    #define glColor3f(r, g, b) shim_glColor4f((r), (g), (b), 1.0f)
    #define glColor3d(r, g, b) shim_glColor4f((GLfloat)(r), (GLfloat)(g), (GLfloat)(b), 1.0f)
    #define glColor4f shim_glColor4f
    #define glColor3ub(r, g, b) shim_glColor4f((r)/255.0f, (g)/255.0f, (b)/255.0f, 1.0f)
    #define glColor4ub(r, g, b, a) shim_glColor4f((r)/255.0f, (g)/255.0f, (b)/255.0f, (a)/255.0f)
    #define glColor4ubv(v) shim_glColor4f((v)[0]/255.0f, (v)[1]/255.0f, (v)[2]/255.0f, (v)[3]/255.0f)

    #define glEnableClientState shim_glEnableClientState
    #define glDisableClientState shim_glDisableClientState
    #define glVertexPointer shim_glVertexPointer
    #define glColorPointer shim_glColorPointer
    #define glTexCoordPointer shim_glTexCoordPointer
    #define glNormalPointer(type, stride, ptr) ((void)0)

    #define glTexEnvi shim_glTexEnvi
    #define glTexEnvfv shim_glTexEnvfv
    #define glFogi shim_glFogi
    #define glFogf shim_glFogf
    #define glFogfv shim_glFogfv
    #define glAlphaFunc shim_glAlphaFunc

    // no GLES2 equivalent; harmless to drop
    #define glShadeModel(m) ((void)0)
    #define glHint(target, mode) ((void)0)
    #define glPolygonMode(face, mode) ((void)0)
    #define glClearDepth(d) glClearDepthf((GLfloat)(d))

    // display lists are not supported: vertmodel.h must not build them (see mdldlist guard there)
    #define glGenLists(n) 0
    #define glDeleteLists(a, b) ((void)0)
    #define glNewList(a, b) ((void)0)
    #define glEndList() ((void)0)
    #define glCallList(a) ((void)0)
#endif // GLSHIM_IMPL

#endif // USE_GLES2_SHIM
#endif // __GLSHIM_H__
