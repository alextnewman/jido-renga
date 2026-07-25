// SPDX-FileCopyrightText: 2009 Intel Corporation
// SPDX-FileCopyrightText: 2011 Marek Olšák
// SPDX-FileCopyrightText: 2018 VMware, Inc.
// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "PiglitCases.h"

#include <GL/gl.h>

#include <math.h>


namespace glsuite {

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


void
DrawTexture()
{
	static const GLubyte pixels[] = {
		255, 32, 32, 255, 32, 255, 32, 255,
		32, 32, 255, 255, 255, 255, 32, 255
	};
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, pixels);
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
	glDeleteTextures(1, &texture);
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
