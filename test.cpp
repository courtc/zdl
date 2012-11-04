#include <GL/gl.h>
#include <unistd.h>
#include <stdio.h>

#include "zdl.h"

int main(int argc, char **argv)
{
	ZDL::Window *window = new ZDL::Window(320, 240, false);
	bool fullscreen = false;
	int done = 0;
	int w, h;

	window->setTitle(argv[0]);
	window->getSize(&w, &h);
	glViewport(0, 0, w, h);

	while (!done) {
		struct zdl_event event;

		while (window->pollEvent(&event) == 0) {
			switch (event.type) {
			case ZDL_EVENT_KEYPRESS:
				if (event.key.scancode != 0)
					fprintf(stderr, "%c", event.key.scancode);
				if (event.key.sym == ZDL_KEYSYM_Q)
					done = 1;
				if (event.key.sym == ZDL_KEYSYM_F) {
					fullscreen = !fullscreen;
					window->setFullscreen(fullscreen);
					window->showCursor(!fullscreen);
					window->getSize(&w, &h);
					glViewport(0, 0, w, h);
				}
				if (event.key.sym == ZDL_KEYSYM_ESCAPE)
					done = 1;
				break;
			case ZDL_EVENT_MOTION:
				break;
			case ZDL_EVENT_EXIT:
				done = 1;
				break;
			case ZDL_EVENT_RECONFIGURE:
				window->getSize(&w, &h);
				glViewport(0, 0, w, h);
				break;
			default:
				break;
			}
		}

		glClearColor(0.0,0.0,0.0,0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0f,w,h,0.0f,1.0f,-1.0f);

		//glMatrixMode(GL_MODELVIEW);
		//glLoadIdentity();

		glBegin(GL_QUADS);
			glColor3f(1.0f,0.0f,0.0f);

			glVertex2f(0.0,0.0);
			glVertex2f(w,  0.0);
			glVertex2f(w,  10);
			glVertex2f(0.0,10);

			glColor3f(1.0f,0.0f,1.0f);
			glVertex2f(0.0,h);
			glVertex2f(w,  h);
			glVertex2f(w,  h-10);
			glVertex2f(0.0,h-10);


		glEnd();

		usleep(16666);
		window->swap();
	}

	delete window;
	return 0;
}
