#define GL_GLEXT_PROTOTYPES
#include <GL/glut.h>
#include <GL/glext.h>
#include <stdio.h>
#include "shader.h"
#include "blocks.h"

static GLuint shaderProgram;
static GLint locLightDir;
static GLint locAmbient;

static const char *vertSrc =
    "#version 120\n"
    "attribute vec3 inPos;\n"
    "attribute vec3 inNormal;\n"
    "attribute vec3 inColor;\n"
    "varying vec3 vNormal;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "  vNormal = inNormal;\n"
    "  vColor = inColor;\n"
    "  gl_Position = gl_ModelViewProjectionMatrix * vec4(inPos, 1.0);\n"
    "}\n";

static const char *fragSrc =
    "#version 120\n"
    "varying vec3 vNormal;\n"
    "varying vec3 vColor;\n"
    "uniform vec3 uLightDir;\n"
    "uniform float uAmbient;\n"
    "void main() {\n"
    "  float diff = max(dot(normalize(vNormal), uLightDir), 0.0);\n"
    "  float light = uAmbient + (1.0 - uAmbient) * diff;\n"
    "  gl_FragColor = vec4(vColor * light, 1.0);\n"
    "}\n";

static GLuint compileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
        return 0;
    }
    return s;
}

void initShaders(void)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) return;

    shaderProgram = glCreateProgram();
    glBindAttribLocation(shaderProgram, 0, "inPos");
    glBindAttribLocation(shaderProgram, 1, "inNormal");
    glBindAttribLocation(shaderProgram, 2, "inColor");
    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);

    GLint ok;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(shaderProgram, sizeof(log), NULL, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        return;
    }

    locLightDir = glGetUniformLocation(shaderProgram, "uLightDir");
    locAmbient = glGetUniformLocation(shaderProgram, "uAmbient");
}

void setLightingUniforms(void)
{
    if (!shaderProgram) return;
    glUseProgram(shaderProgram);
    glUniform3f(locLightDir, LIGHT_DIR_X, LIGHT_DIR_Y, LIGHT_DIR_Z);
    glUniform1f(locAmbient, 0.25f);
}
