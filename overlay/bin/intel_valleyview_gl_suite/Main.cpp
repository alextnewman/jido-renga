// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <Application.h>
#include <GLView.h>
#include <OS.h>
#include <Window.h>

#include <GL/gl.h>
#include <GL/glext.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "PiglitCases.h"

extern "C" {
GLuint APIENTRY glCreateShader(GLenum type);
void APIENTRY glShaderSource(GLuint shader, GLsizei count,
	const GLchar* const* strings, const GLint* lengths);
void APIENTRY glCompileShader(GLuint shader);
void APIENTRY glGetShaderiv(GLuint shader, GLenum name, GLint* value);
void APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei size,
	GLsizei* length, GLchar* log);
void APIENTRY glDeleteShader(GLuint shader);
GLuint APIENTRY glCreateProgram(void);
void APIENTRY glAttachShader(GLuint program, GLuint shader);
void APIENTRY glLinkProgram(GLuint program);
void APIENTRY glGetProgramiv(GLuint program, GLenum name, GLint* value);
void APIENTRY glGetProgramInfoLog(GLuint program, GLsizei size,
	GLsizei* length, GLchar* log);
void APIENTRY glDeleteProgram(GLuint program);
void APIENTRY glUseProgram(GLuint program);
void APIENTRY glGenBuffers(GLsizei count, GLuint* buffers);
void APIENTRY glDeleteBuffers(GLsizei count, const GLuint* buffers);
void APIENTRY glBindBuffer(GLenum target, GLuint buffer);
void APIENTRY glBufferData(GLenum target, GLsizeiptr size,
	const void* data, GLenum usage);
GLint APIENTRY glGetAttribLocation(GLuint program, const GLchar* name);
void APIENTRY glEnableVertexAttribArray(GLuint index);
void APIENTRY glDisableVertexAttribArray(GLuint index);
void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
	GLboolean normalized, GLsizei stride, const void* pointer);
}


namespace {

enum CaseId {
	kClear,
	kExplicitVbo,
	kClientArrays,
	kFixedVbo,
	kImmediateTriangle,
	kImmediateQuadStrip,
	kDrawArraysStart,
	kDrawArraysStartList,
	kDisplayListBeginEnd,
	kImmediate8,
	kImmediate32,
	kImmediate162,
	kDepthFunctions,
	kLighting,
	kTexture,
	kLines
};


class SuiteView : public BGLView {
public:
	SuiteView(BRect frame)
		:
		BGLView(frame, "ValleyView GL suite", B_FOLLOW_ALL, B_WILL_DRAW,
			BGL_RGB | BGL_DOUBLE | BGL_ALPHA | BGL_DEPTH),
		fRan(false),
		fProgram(0),
		fVertexBuffer(0),
		fPositionAttribute(-1),
		fColorAttribute(-1)
	{
	}

	virtual void AttachedToWindow()
	{
		BGLView::AttachedToWindow();
		LockGL();
		printf("jr_gl_suite renderer=%s version=%s\n",
			glGetString(GL_RENDERER), glGetString(GL_VERSION));
		UnlockGL();
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		if (fRan)
			return;
		fRan = true;
		_RunSuite();
	}

private:
	GLuint _CompileShader(GLenum type, const char* source)
	{
		GLuint shader = glCreateShader(type);
		glShaderSource(shader, 1, &source, NULL);
		glCompileShader(shader);
		GLint compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled == GL_TRUE)
			return shader;
		char log[1024] = {};
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		fprintf(stderr, "jr_gl_suite shader_error=%s\n", log);
		glDeleteShader(shader);
		return 0;
	}

	bool _InitializeExplicitPipeline()
	{
		if (fProgram != 0)
			return true;
		static const char* kVertexShader =
			"#version 120\n"
			"attribute vec2 position;\n"
			"attribute vec3 color;\n"
			"varying vec3 interpolatedColor;\n"
			"void main() {\n"
			"  gl_Position = vec4(position, 0.0, 1.0);\n"
			"  interpolatedColor = color;\n"
			"}\n";
		static const char* kFragmentShader =
			"#version 120\n"
			"varying vec3 interpolatedColor;\n"
			"void main() {\n"
			"  gl_FragColor = vec4(interpolatedColor, 1.0);\n"
			"}\n";
		const GLuint vertex = _CompileShader(GL_VERTEX_SHADER, kVertexShader);
		const GLuint fragment = _CompileShader(GL_FRAGMENT_SHADER,
			kFragmentShader);
		if (vertex == 0 || fragment == 0) {
			if (vertex != 0)
				glDeleteShader(vertex);
			if (fragment != 0)
				glDeleteShader(fragment);
			return false;
		}
		fProgram = glCreateProgram();
		glAttachShader(fProgram, vertex);
		glAttachShader(fProgram, fragment);
		glLinkProgram(fProgram);
		glDeleteShader(vertex);
		glDeleteShader(fragment);
		GLint linked = GL_FALSE;
		glGetProgramiv(fProgram, GL_LINK_STATUS, &linked);
		if (linked != GL_TRUE) {
			char log[1024] = {};
			glGetProgramInfoLog(fProgram, sizeof(log), NULL, log);
			fprintf(stderr, "jr_gl_suite link_error=%s\n", log);
			glDeleteProgram(fProgram);
			fProgram = 0;
			return false;
		}

		const GLfloat vertices[] = {
			 0.00f, -0.80f, 1.0f, 0.0f, 0.0f,
			-0.80f,  0.80f, 0.0f, 1.0f, 0.0f,
			 0.80f,  0.80f, 0.0f, 0.0f, 1.0f
		};
		glGenBuffers(1, &fVertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, fVertexBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
			GL_STATIC_DRAW);
		fPositionAttribute = glGetAttribLocation(fProgram, "position");
		fColorAttribute = glGetAttribLocation(fProgram, "color");
		return fVertexBuffer != 0 && fPositionAttribute >= 0
			&& fColorAttribute >= 0;
	}

	void _ResetState()
	{
		glUseProgram(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_FOG);
		glDisable(GL_LIGHTING);
		glDisable(GL_LIGHT0);
		glDisable(GL_NORMALIZE);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_TEXTURE_2D);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glShadeModel(GL_SMOOTH);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	void _DrawExplicitVbo()
	{
		if (!_InitializeExplicitPipeline())
			return;
		glUseProgram(fProgram);
		glBindBuffer(GL_ARRAY_BUFFER, fVertexBuffer);
		glEnableVertexAttribArray(fPositionAttribute);
		glEnableVertexAttribArray(fColorAttribute);
		glVertexAttribPointer(fPositionAttribute, 2, GL_FLOAT, GL_FALSE,
			5 * sizeof(GLfloat), NULL);
		glVertexAttribPointer(fColorAttribute, 3, GL_FLOAT, GL_FALSE,
			5 * sizeof(GLfloat),
			reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glDisableVertexAttribArray(fPositionAttribute);
		glDisableVertexAttribArray(fColorAttribute);
		glUseProgram(0);
	}

	void _DrawFixedVbo()
	{
		if (!_InitializeExplicitPipeline())
			return;
		glUseProgram(0);
		glBindBuffer(GL_ARRAY_BUFFER, fVertexBuffer);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glVertexPointer(2, GL_FLOAT, 5 * sizeof(GLfloat), NULL);
		glColorPointer(3, GL_FLOAT, 5 * sizeof(GLfloat),
			reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void _DrawCase(CaseId id)
	{
		switch (id) {
			case kClear:
				break;
			case kExplicitVbo:
				_DrawExplicitVbo();
				break;
			case kClientArrays:
				glsuite::DrawClientArrays();
				break;
			case kFixedVbo:
				_DrawFixedVbo();
				break;
			case kImmediateTriangle:
				glsuite::DrawImmediateTriangle();
				break;
			case kImmediateQuadStrip:
				glsuite::DrawImmediateQuadStrips(1);
				break;
			case kDrawArraysStart:
				glsuite::DrawDrawArraysStart(false);
				break;
			case kDrawArraysStartList:
				glsuite::DrawDrawArraysStart(true);
				break;
			case kDisplayListBeginEnd:
				glsuite::DrawDisplayListBeginEnd();
				break;
			case kImmediate8:
				glsuite::DrawImmediateQuadStrips(8);
				break;
			case kImmediate32:
				glsuite::DrawImmediateQuadStrips(32);
				break;
			case kImmediate162:
				glsuite::DrawImmediateQuadStrips(162);
				break;
			case kDepthFunctions:
				glsuite::DrawDepthFunctions();
				break;
			case kLighting:
				glsuite::DrawLighting();
				break;
			case kTexture:
				glsuite::DrawTexture();
				break;
			case kLines:
				glsuite::DrawLines();
				break;
		}
	}

	GLenum _DrainErrors()
	{
		GLenum first = GL_NO_ERROR;
		for (GLenum error = glGetError(); error != GL_NO_ERROR;
			error = glGetError()) {
			if (first == GL_NO_ERROR)
				first = error;
		}
		return first;
	}

	void _RunCase(const char* name, CaseId id, unsigned index)
	{
		setenv("VALLEYVIEW_GPU_CASE", name, 1);
		const bigtime_t started = system_time();
		printf("jr_gl_case begin name=%s index=%u\n", name, index);
		LockGL();
		_DrainErrors();
		_ResetState();
		glViewport(0, 0, Bounds().IntegerWidth() + 1,
			Bounds().IntegerHeight() + 1);
		glClearColor(0.015f * (index % 7), 0.01f * (index % 5),
			0.02f * (index % 3), 1.0f);
		glClearDepth(1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glFinish();
		const GLenum clearError = _DrainErrors();
		_DrawCase(id);
		glFinish();
		const GLenum drawError = _DrainErrors();
		SwapBuffers();
		UnlockGL();
		printf("jr_gl_case end name=%s index=%u clear_error=%#x"
			" draw_error=%#x elapsed_us=%" B_PRIdBIGTIME "\n",
			name, index, clearError, drawError, system_time() - started);
	}

	void _RunSuite()
	{
		static const struct {
			const char* name;
			CaseId id;
		} cases[] = {
			{"clear", kClear},
			{"explicit-vbo-glsl", kExplicitVbo},
			{"client-arrays-fixed", kClientArrays},
			{"vbo-fixed", kFixedVbo},
			{"immediate-triangle", kImmediateTriangle},
			{"immediate-quad-strip", kImmediateQuadStrip},
			{"draw-arrays-start", kDrawArraysStart},
			{"draw-arrays-start-list", kDrawArraysStartList},
			{"display-list-begin-end", kDisplayListBeginEnd},
			{"immediate-quad-strip-8", kImmediate8},
			{"immediate-quad-strip-32", kImmediate32},
			{"immediate-quad-strip-162", kImmediate162},
			{"depth-functions", kDepthFunctions},
			{"fixed-lighting", kLighting},
			{"immediate-texture", kTexture},
			{"immediate-lines", kLines},
			{"explicit-vbo-recovery", kExplicitVbo}
		};
		printf("jr_gl_suite begin cases=%zu\n", sizeof(cases) / sizeof(cases[0]));
		for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]);
				index++) {
			_RunCase(cases[index].name, cases[index].id, index);
		}
		setenv("VALLEYVIEW_GPU_CASE", "suite-complete", 1);
		printf("jr_gl_suite end cases=%zu\n", sizeof(cases) / sizeof(cases[0]));
	}

	bool fRan;
	GLuint fProgram;
	GLuint fVertexBuffer;
	GLint fPositionAttribute;
	GLint fColorAttribute;
};


class SuiteWindow : public BWindow {
public:
	SuiteWindow()
		:
		BWindow(BRect(80, 80, 720, 560), "ValleyView GL compatibility suite",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new SuiteView(Bounds()));
	}
};


class SuiteApplication : public BApplication {
public:
	SuiteApplication()
		:
		BApplication("application/x-vnd.JidoRenga-ValleyViewGLSuite")
	{
	}

	virtual void ReadyToRun()
	{
		SuiteWindow* window = new SuiteWindow();
		window->Show();
	}
};

}


int
main()
{
	setenv("VALLEYVIEW_GPU_DEBUG", "1", 1);
	SuiteApplication application;
	application.Run();
	return 0;
}
