// SPDX-FileCopyrightText: 2009 Intel Corporation
// SPDX-FileCopyrightText: 2011 Marek Olšák
// SPDX-FileCopyrightText: 2018 VMware, Inc.
// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "PiglitCases.h"

#include <GL/gl.h>
#include <GL/glext.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


namespace glsuite {

extern "C" {
void APIENTRY glGenBuffers(GLsizei count, GLuint* buffers);
void APIENTRY glBindBuffer(GLenum target, GLuint buffer);
void APIENTRY glBufferData(GLenum target, ptrdiff_t size, const void* data,
	GLenum usage);
void APIENTRY glBufferSubData(GLenum target, ptrdiff_t offset, ptrdiff_t size,
	const void* data);
void APIENTRY glDeleteBuffers(GLsizei count, const GLuint* buffers);
void APIENTRY glGenFramebuffers(GLsizei count, GLuint* framebuffers);
void APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer);
void APIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment,
	GLenum textureTarget, GLuint texture, GLint level);
GLenum APIENTRY glCheckFramebufferStatus(GLenum target);
void APIENTRY glDeleteFramebuffers(GLsizei count, const GLuint* framebuffers);
void APIENTRY glGenRenderbuffers(GLsizei count, GLuint* renderbuffers);
void APIENTRY glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void APIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
	GLenum internalFormat, GLsizei width, GLsizei height);
void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment,
	GLenum renderbufferTarget, GLuint renderbuffer);
void APIENTRY glDeleteRenderbuffers(GLsizei count,
	const GLuint* renderbuffers);
void APIENTRY glBlitFramebuffer(GLint sourceX0, GLint sourceY0,
	GLint sourceX1, GLint sourceY1, GLint destinationX0, GLint destinationY0,
	GLint destinationX1, GLint destinationY1, GLbitfield mask, GLenum filter);
void APIENTRY glGenQueries(GLsizei count, GLuint* queries);
void APIENTRY glBeginQuery(GLenum target, GLuint query);
void APIENTRY glEndQuery(GLenum target);
void APIENTRY glGetQueryObjectuiv(GLuint query, GLenum name, GLuint* value);
void APIENTRY glDeleteQueries(GLsizei count, const GLuint* queries);
void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
	GLsizei instances);
void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internalFormat,
	GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format,
	GLenum type, const void* pixels);
}

static bool
PixelNear(const GLubyte* pixel, GLubyte red, GLubyte green, GLubyte blue,
	GLubyte tolerance = 3)
{
	return abs(static_cast<int>(pixel[0]) - red) <= tolerance
		&& abs(static_cast<int>(pixel[1]) - green) <= tolerance
		&& abs(static_cast<int>(pixel[2]) - blue) <= tolerance;
}


bool
DrawProfileLimits()
{
	const char* renderer = reinterpret_cast<const char*>(
		glGetString(GL_RENDERER));
	const char* version = reinterpret_cast<const char*>(
		glGetString(GL_VERSION));
	const char* shading = reinterpret_cast<const char*>(
		glGetString(GL_SHADING_LANGUAGE_VERSION));
	int major = 0;
	int minor = 0;
	int shadingMajor = 0;
	int shadingMinor = 0;
	if (version != NULL)
		sscanf(version, "%d.%d", &major, &minor);
	if (shading != NULL)
		sscanf(shading, "%d.%d", &shadingMajor, &shadingMinor);
	GLint texture2d = 0;
	GLint texture3d = 0;
	GLint cube = 0;
	GLint attributes = 0;
	GLint drawBuffers = 0;
	GLint samples = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture2d);
	glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &texture3d);
	glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &cube);
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attributes);
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &drawBuffers);
	glGetIntegerv(GL_MAX_SAMPLES, &samples);
	printf("jr_gl_profile renderer=%s version=%s glsl=%s"
		" limits=%d/%d/%d/%d/%d/%d\n",
		renderer != NULL ? renderer : "(null)",
		version != NULL ? version : "(null)",
		shading != NULL ? shading : "(null)", texture2d, texture3d, cube,
		attributes, drawBuffers, samples);
	return renderer != NULL && strstr(renderer, "Intel") != NULL
		&& (major > 3 || (major == 3 && minor >= 1))
		&& (shadingMajor > 1
			|| (shadingMajor == 1 && shadingMinor >= 40))
		&& texture2d >= 8192 && texture3d >= 2048 && cube >= 8192
		&& attributes >= 16 && drawBuffers >= 8 && samples >= 4;
}


bool
DrawBlend()
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
	glBegin(GL_QUADS);
	glVertex2f(-0.8f, -0.8f);
	glVertex2f(0.8f, -0.8f);
	glVertex2f(0.8f, 0.8f);
	glVertex2f(-0.8f, 0.8f);
	glEnd();
	glColor4f(0.0f, 0.0f, 1.0f, 0.5f);
	glBegin(GL_QUADS);
	glVertex2f(-0.8f, -0.8f);
	glVertex2f(0.8f, -0.8f);
	glVertex2f(0.8f, 0.8f);
	glVertex2f(-0.8f, 0.8f);
	glEnd();
	glDisable(GL_BLEND);
	glFinish();
	GLubyte pixel[4] = {};
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	printf("jr_gl_blend rgba=%u/%u/%u/%u\n", pixel[0], pixel[1],
		pixel[2], pixel[3]);
	return PixelNear(pixel, 64, 0, 128);
}


bool
DrawScissor()
{
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);
	glScissor(32, 32, 192, 128);
	glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);
	glFinish();
	GLubyte inside[4] = {};
	GLubyte outside[4] = {};
	glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inside);
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
	printf("jr_gl_scissor inside=%u/%u/%u outside=%u/%u/%u\n",
		inside[0], inside[1], inside[2], outside[0], outside[1], outside[2]);
	return PixelNear(inside, 255, 0, 0) && PixelNear(outside, 0, 0, 0);
}


bool
DrawReadPixels()
{
	glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	GLubyte pixel[4] = {};
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	printf("jr_gl_readpixels rgba=%u/%u/%u/%u\n", pixel[0], pixel[1],
		pixel[2], pixel[3]);
	return pixel[0] >= 62 && pixel[0] <= 66
		&& pixel[1] >= 126 && pixel[1] <= 130
		&& pixel[2] >= 190 && pixel[2] <= 194 && pixel[3] == 255;
}


bool
DrawTextureMipmaps()
{
	GLubyte base[4 * 4 * 4] = {};
	GLubyte level[2 * 2 * 4];
	GLubyte final[4] = {0, 0, 255, 255};
	for (size_t index = 0; index < sizeof(level); index += 4) {
		level[index + 0] = 0;
		level[index + 1] = 255;
		level[index + 2] = 0;
		level[index + 3] = 255;
	}
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, base);
	glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, level);
	glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA8, 1, 1, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, final);
	GLint widths[3] = {};
	for (GLint level = 0; level < 3; level++) {
		glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH,
			&widths[level]);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(-0.8f, -0.8f);
	glTexCoord2f(1.0f, 0.0f);
	glVertex2f(0.8f, -0.8f);
	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(0.8f, 0.8f);
	glTexCoord2f(0.0f, 1.0f);
	glVertex2f(-0.8f, 0.8f);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	glFinish();
	GLubyte pixel[4] = {};
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	glDeleteTextures(1, &texture);
	printf("jr_gl_mipmaps widths=%d/%d/%d rgba=%u/%u/%u/%u\n",
		widths[0], widths[1], widths[2], pixel[0], pixel[1], pixel[2],
		pixel[3]);
	return widths[0] == 4 && widths[1] == 2 && widths[2] == 1
		&& PixelNear(pixel, 0, 255, 0);
}


bool
DrawCubeMap()
{
	GLubyte pixels[6][2 * 2 * 4] = {};
	for (unsigned face = 0; face < 6; face++) {
		for (size_t index = 0; index < sizeof(pixels[face]); index += 4) {
			pixels[face][index + face % 3] = 255;
			pixels[face][index + 3] = 255;
		}
	}
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	for (unsigned face = 0; face < 6; face++) {
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8,
			2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels[face]);
	}
	GLint width = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0,
		GL_TEXTURE_WIDTH, &width);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_CUBE_MAP);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glBegin(GL_QUADS);
	glTexCoord3f(1.0f, -0.5f, -0.5f);
	glVertex2f(-0.8f, -0.8f);
	glTexCoord3f(1.0f, 0.5f, -0.5f);
	glVertex2f(0.8f, -0.8f);
	glTexCoord3f(1.0f, 0.5f, 0.5f);
	glVertex2f(0.8f, 0.8f);
	glTexCoord3f(1.0f, -0.5f, 0.5f);
	glVertex2f(-0.8f, 0.8f);
	glEnd();
	glDisable(GL_TEXTURE_CUBE_MAP);
	glFinish();
	GLubyte pixel[4] = {};
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	glDeleteTextures(1, &texture);
	printf("jr_gl_cubemap width=%d rgba=%u/%u/%u/%u\n", width, pixel[0],
		pixel[1], pixel[2], pixel[3]);
	return width == 2 && PixelNear(pixel, 255, 0, 0);
}


bool
DrawIndexedElementBuffer()
{
	static const GLfloat vertices[][2] = {
		{0.0f, -0.8f}, {-0.8f, 0.8f}, {0.8f, 0.8f}
	};
	static const GLushort indices[] = {0, 1, 2};
	GLuint buffers[2] = {};
	glGenBuffers(2, buffers);
	glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
		GL_STATIC_DRAW);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, NULL);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColor3f(1.0f, 1.0f, 0.0f);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
	glDisableClientState(GL_VERTEX_ARRAY);
	glFinish();
	GLubyte pixel[4] = {};
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(2, buffers);
	printf("jr_gl_element_buffer rgba=%u/%u/%u/%u\n", pixel[0], pixel[1],
		pixel[2], pixel[3]);
	return PixelNear(pixel, 255, 255, 0);
}


bool
DrawMultisampleFramebuffer()
{
	GLint maxSamples = 0;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
	if (maxSamples < 4)
		return false;
	GLuint framebuffer = 0;
	GLuint renderbuffer = 0;
	GLuint resolveFramebuffer = 0;
	GLuint resolveTexture = 0;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glGenRenderbuffers(1, &renderbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 64, 64);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, renderbuffer);
	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status == GL_FRAMEBUFFER_COMPLETE) {
		glViewport(0, 0, 64, 64);
		glClearColor(0.6f, 0.3f, 0.9f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glGenTextures(1, &resolveTexture);
	glBindTexture(GL_TEXTURE_2D, resolveTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &resolveFramebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFramebuffer);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, resolveTexture, 0);
	const GLenum resolveStatus = glCheckFramebufferStatus(
		GL_DRAW_FRAMEBUFFER);
	GLubyte pixel[4] = {};
	if (status == GL_FRAMEBUFFER_COMPLETE
		&& resolveStatus == GL_FRAMEBUFFER_COMPLETE) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFramebuffer);
		glBlitFramebuffer(0, 0, 64, 64, 0, 0, 64, 64,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_FRAMEBUFFER, resolveFramebuffer);
		glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	}
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &resolveFramebuffer);
	glDeleteTextures(1, &resolveTexture);
	glDeleteRenderbuffers(1, &renderbuffer);
	glDeleteFramebuffers(1, &framebuffer);
	printf("jr_gl_msaa samples=4 status=%#x/%#x rgba=%u/%u/%u/%u\n",
		status, resolveStatus, pixel[0], pixel[1], pixel[2], pixel[3]);
	return status == GL_FRAMEBUFFER_COMPLETE
		&& resolveStatus == GL_FRAMEBUFFER_COMPLETE
		&& PixelNear(pixel, 153, 77, 230);
}


void
DrawImmediateTriangle()
{
	glBegin(GL_TRIANGLES);
	glColor3f(1.0f, 0.0f, 0.0f);
	glVertex2f(0.0f, -0.8f);
	glColor3f(0.0f, 1.0f, 0.0f);
	glVertex2f(-0.8f, 0.8f);
	glColor3f(0.0f, 0.0f, 1.0f);
	glVertex2f(0.8f, 0.8f);
	glEnd();
}


void
DrawImmediateQuadStrips(unsigned count)
{
	for (unsigned index = 0; index < count; index++) {
		const unsigned column = index % 18;
		const unsigned row = (index / 18) % 9;
		const float left = -0.95f + column * 0.105f;
		const float bottom = -0.92f + row * 0.205f;
		const float right = left + 0.08f;
		const float top = bottom + 0.16f;
		const float phase = static_cast<float>(index % 7) / 6.0f;
		glBegin(GL_QUAD_STRIP);
		glColor3f(1.0f - phase, phase, 0.2f);
		glVertex2f(left, bottom);
		glColor3f(0.2f, 1.0f - phase, phase);
		glVertex2f(right, bottom);
		glColor3f(phase, 0.2f, 1.0f - phase);
		glVertex2f(left, top);
		glColor3f(1.0f, phase, 1.0f - phase);
		glVertex2f(right, top);
		glEnd();
	}
}


void
DrawClientArrays()
{
	const GLfloat vertices[] = {
		 0.00f, -0.80f, 1.0f, 0.0f, 0.0f,
		-0.80f,  0.80f, 0.0f, 1.0f, 0.0f,
		 0.80f,  0.80f, 0.0f, 0.0f, 1.0f
	};
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 5 * sizeof(GLfloat), vertices);
	glColorPointer(3, GL_FLOAT, 5 * sizeof(GLfloat), vertices + 2);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void
DrawIndexedUnsignedInt()
{
	static const GLfloat vertices[][2] = {
		{0.0f, -0.8f}, {-0.8f, 0.8f}, {0.8f, 0.8f}
	};
	static const GLuint indices[] = {0, 1, 2};
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glColor3f(0.2f, 0.9f, 0.6f);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, indices);
	glDisableClientState(GL_VERTEX_ARRAY);
}


void
DrawBufferSubData()
{
	static const GLfloat initial[][2] = {
		{-0.2f, -0.8f}, {-0.9f, 0.7f}, {0.5f, 0.5f}
	};
	static const GLfloat replacement = 0.8f;
	GLuint buffer = 0;
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(initial), initial, GL_DYNAMIC_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 4 * sizeof(GLfloat),
		sizeof(replacement), &replacement);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, NULL);
	glColor3f(0.9f, 0.3f, 0.8f);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glDisableClientState(GL_VERTEX_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &buffer);
}


bool
DrawFramebufferObject()
{
	GLuint texture = 0;
	GLuint framebuffer = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, texture, 0);
	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status == GL_FRAMEBUFFER_COMPLETE) {
		glViewport(0, 0, 64, 64);
		glClearColor(0.2f, 0.7f, 0.9f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteTextures(1, &texture);
	printf("jr_gl_fbo status=%#x\n", status);
	return status == GL_FRAMEBUFFER_COMPLETE;
}


bool
DrawOcclusionQuery()
{
	GLuint query = 0;
	GLuint samples = 0;
	glGenQueries(1, &query);
	glBeginQuery(GL_SAMPLES_PASSED, query);
	DrawImmediateTriangle();
	glEndQuery(GL_SAMPLES_PASSED);
	glGetQueryObjectuiv(query, GL_QUERY_RESULT, &samples);
	glDeleteQueries(1, &query);
	printf("jr_gl_query samples=%u\n", samples);
	return samples != 0;
}


void
DrawInstanced()
{
	static const GLfloat vertices[][2] = {
		{-0.25f, -0.8f}, {-0.75f, 0.4f}, {0.25f, 0.4f}
	};
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glColor3f(0.3f, 0.8f, 1.0f);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}


bool
DrawTexture3D()
{
	GLubyte pixels[4 * 4 * 4 * 4];
	for (size_t index = 0; index < sizeof(pixels); index++)
		pixels[index] = static_cast<GLubyte>(index * 17);
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_3D, texture);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 4, 4, 4, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, pixels);
	GLint width = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_WIDTH, &width);
	glDeleteTextures(1, &texture);
	printf("jr_gl_texture3d width=%d\n", width);
	return width == 4;
}


void
DrawDrawArraysStart(bool displayList)
{
	static const GLfloat vertices[][2] = {
		{-0.90f, -0.65f}, {-0.15f, -0.65f},
		{-0.15f,  0.65f}, {-0.90f,  0.65f},
		{ 0.15f, -0.65f}, { 0.90f, -0.65f},
		{ 0.90f,  0.65f}, { 0.15f,  0.65f}
	};
	GLuint first = 0;
	GLuint second = 0;
	glColor3f(0.9f, 0.7f, 0.2f);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	if (displayList) {
		first = glGenLists(1);
		second = glGenLists(1);
		glNewList(first, GL_COMPILE);
	}
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	if (displayList) {
		glEndList();
		glNewList(second, GL_COMPILE);
	}
	glDrawArrays(GL_TRIANGLE_FAN, 4, 4);
	if (displayList) {
		glEndList();
		glCallList(first);
		glCallList(second);
		glDeleteLists(first, 1);
		glDeleteLists(second, 1);
	}
	glDisableClientState(GL_VERTEX_ARRAY);
}


void
DrawDisplayListBeginEnd()
{
	const GLuint list = glGenLists(1);
	glNewList(list, GL_COMPILE);
	DrawImmediateTriangle();
	glEndList();
	glCallList(list);
	glDeleteLists(list, 1);
}


static void
DrawDepthQuad(float left, float bottom, float right, float top, float depth)
{
	const GLfloat vertices[][3] = {
		{left, bottom, depth}, {right, bottom, depth},
		{right, top, depth}, {left, top, depth}
	};
	glVertexPointer(3, GL_FLOAT, 0, vertices);
	glEnableClientState(GL_VERTEX_ARRAY);
	glDrawArrays(GL_QUADS, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}


void
DrawDepthFunctions()
{
	static const GLenum functions[] = {
		GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
		GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS
	};
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_ALWAYS);
	for (unsigned index = 0; index < 8; index++) {
		const float left = -0.95f + index * 0.24f;
		const float right = left + 0.18f;
		glColor3f(0.0f, 0.8f, 0.2f);
		DrawDepthQuad(left, -0.75f, right, 0.75f, 0.0f);
		glDepthFunc(functions[index]);
		glColor3f(0.1f, 0.3f, 1.0f);
		DrawDepthQuad(left, -0.75f, right, 0.75f, 0.5f);
	}
}


void
DrawLighting()
{
	const GLfloat position[] = {0.0f, 0.0f, 3.0f, 0.0f};
	const GLfloat diffuse[] = {1.0f, 0.8f, 0.3f, 1.0f};
	const GLfloat material[] = {0.8f, 0.3f, 0.1f, 1.0f};
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_NORMALIZE);
	glLightfv(GL_LIGHT0, GL_POSITION, position);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, material);
	glBegin(GL_TRIANGLES);
	glNormal3f(0.0f, 0.0f, 1.0f);
	glVertex3f(0.0f, -0.8f, 0.0f);
	glNormal3f(-0.4f, 0.0f, 0.9f);
	glVertex3f(-0.8f, 0.8f, 0.0f);
	glNormal3f(0.4f, 0.0f, 0.9f);
	glVertex3f(0.8f, 0.8f, 0.0f);
	glEnd();
}


static void
DrawTexture(const void* data)
{
	printf("jr_gl_texture step=generate upload=%s\n",
		data != NULL ? "yes" : "no");
	GLuint texture;
	glGenTextures(1, &texture);
	printf("jr_gl_texture step=bind texture=%u\n", texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	printf("jr_gl_texture step=image\n");
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, data);
	printf("jr_gl_texture step=draw\n");
	glEnable(GL_TEXTURE_2D);
	glColor3f(1.0f, 1.0f, 1.0f);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(-0.8f, -0.8f);
	glTexCoord2f(1.0f, 0.0f);
	glVertex2f(0.8f, -0.8f);
	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(0.8f, 0.8f);
	glTexCoord2f(0.0f, 1.0f);
	glVertex2f(-0.8f, 0.8f);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	printf("jr_gl_texture step=queued\n");
}


void
DrawTextureAllocation()
{
	DrawTexture(NULL);
}


void
DrawTextureUpload()
{
	static const GLubyte pixels[] = {
		255, 32, 32, 255, 32, 255, 32, 255,
		32, 32, 255, 255, 255, 255, 32, 255
	};
	DrawTexture(pixels);
}


void
DrawLines()
{
	glLineWidth(2.0f);
	glBegin(GL_LINES);
	for (unsigned index = 0; index < 16; index++) {
		const float offset = -0.9f + index * 0.12f;
		glColor3f(index / 15.0f, 1.0f - index / 15.0f, 0.8f);
		glVertex2f(-0.9f, offset);
		glVertex2f(0.9f, -offset);
	}
	glEnd();
}

}
