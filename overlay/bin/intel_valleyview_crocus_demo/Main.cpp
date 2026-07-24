// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <Application.h>
#include <GLView.h>
#include <Window.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <stdio.h>
#include <stddef.h>

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

class CrocusView : public BGLView {
public:
	CrocusView(BRect frame)
		:
		BGLView(frame, "Crocus P1", B_FOLLOW_ALL, B_WILL_DRAW,
			BGL_RGB | BGL_DOUBLE | BGL_ALPHA),
		fProgram(0),
		fVertexBuffer(0),
		fPositionAttribute(-1),
		fColorAttribute(-1)
	{
	}

	void AttachedToWindow() override
	{
		BGLView::AttachedToWindow();
		MakeFocus();
		_Render();
	}

	void Draw(BRect) override
	{
		_Render();
	}

	void FrameResized(float width, float height) override
	{
		BGLView::FrameResized(width, height);
		_Render();
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
		fprintf(stderr, "crocus_demo shader failure: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}

	bool _InitializePipeline()
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
			fprintf(stderr, "crocus_demo link failure: %s\n", log);
			glDeleteProgram(fProgram);
			fProgram = 0;
			return false;
		}

		const GLfloat vertices[] = {
			 0.00f, -0.85f, 1.0f, 0.0f, 0.0f,
			-0.85f,  0.80f, 0.0f, 1.0f, 0.0f,
			 0.85f,  0.80f, 0.0f, 0.0f, 1.0f
		};
		glGenBuffers(1, &fVertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, fVertexBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
			GL_STATIC_DRAW);
		fPositionAttribute = glGetAttribLocation(fProgram, "position");
		fColorAttribute = glGetAttribLocation(fProgram, "color");
		if (fVertexBuffer != 0 && fPositionAttribute >= 0
			&& fColorAttribute >= 0 && _CheckGl("pipeline setup"))
			return true;
		if (fVertexBuffer != 0)
			glDeleteBuffers(1, &fVertexBuffer);
		glDeleteProgram(fProgram);
		fProgram = 0;
		fVertexBuffer = 0;
		return false;
	}

	bool _CheckGl(const char* operation)
	{
		bool succeeded = true;
		for (GLenum error = glGetError(); error != GL_NO_ERROR;
			error = glGetError()) {
			fprintf(stderr, "crocus_demo %s GL error: 0x%04x\n",
				operation, error);
			succeeded = false;
		}
		return succeeded;
	}

	void _Render()
	{
		LockGL();
		const GLubyte* renderer = glGetString(GL_RENDERER);
		const GLubyte* version = glGetString(GL_VERSION);
		printf("crocus_demo renderer=%s version=%s\n",
			renderer != NULL ? reinterpret_cast<const char*>(renderer)
				: "unavailable",
			version != NULL ? reinterpret_cast<const char*>(version)
				: "unavailable");

		const BRect bounds = Bounds();
		glViewport(0, 0, bounds.IntegerWidth() + 1,
			bounds.IntegerHeight() + 1);
		glDisable(GL_DEPTH_TEST);
		glClearColor(0.12f, 0.04f, 0.16f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (_InitializePipeline()) {
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
			_CheckGl("draw");
		}
		glFinish();
		SwapBuffers();
		UnlockGL();
	}

	GLuint fProgram;
	GLuint fVertexBuffer;
	GLint fPositionAttribute;
	GLint fColorAttribute;
};


class CrocusWindow : public BWindow {
public:
	CrocusWindow()
		:
		BWindow(BRect(80, 80, 680, 580), "ValleyView Crocus P1",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new CrocusView(Bounds()));
		Show();
	}
};


class CrocusApplication : public BApplication {
public:
	CrocusApplication()
		:
		BApplication("application/x-vnd.JidoRenga-CrocusP1")
	{
	}

	void ReadyToRun() override
	{
		new CrocusWindow();
	}
};

} // namespace


int
main()
{
	CrocusApplication application;
	application.Run();
	return 0;
}
