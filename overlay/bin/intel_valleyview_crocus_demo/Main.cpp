// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <Application.h>
#include <GLView.h>
#include <Window.h>

#include <GL/gl.h>

#include <stdio.h>


namespace {

class CrocusView : public BGLView {
public:
	CrocusView(BRect frame)
		:
		BGLView(frame, "Crocus P1", B_FOLLOW_ALL, B_WILL_DRAW,
			BGL_RGB | BGL_DOUBLE | BGL_ALPHA)
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
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glBegin(GL_TRIANGLES);
		glColor3f(1.0f, 0.0f, 0.0f);
		glVertex2f(0.0f, -0.85f);
		glColor3f(0.0f, 1.0f, 0.0f);
		glVertex2f(-0.85f, 0.8f);
		glColor3f(0.0f, 0.0f, 1.0f);
		glVertex2f(0.85f, 0.8f);
		glEnd();
		glFlush();
		SwapBuffers();
		UnlockGL();
	}
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
