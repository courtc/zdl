#ifdef _WIN32
#include <windows.h>
#define delay(x) Sleep(x/1000)
#else
#include <unistd.h>
#define delay(x) usleep(x)
#endif

#include <GL/gl.h>
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
				if (event.key.unicode != 0)
					fprintf(stderr, "%c", event.key.unicode & 0x7f);
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
				//fprintf(stderr, "motion (%d,%d)\n", event.motion.d_x, event.motion.d_y);
				break;
			case ZDL_EVENT_EXIT:
				done = 1;
				break;
			case ZDL_EVENT_RECONFIGURE:
				window->getSize(&w, &h);
				fprintf(stderr, "resize (%d,%d)\n", w, h);
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
			
			float fw = (float)w, fh = (float)h;
			glVertex2f(0.0f,0.0f);
			glVertex2f(fw,   0.0f);
			glVertex2f(fw,  10.0f);
			glVertex2f(0.0f,10.0f);

			glColor3f(1.0f,0.0f,1.0f);
			glVertex2f(0.0f,fh);
			glVertex2f(fw,  fh);
			glVertex2f(fw,  fh-10.0f);
			glVertex2f(0.0f,fh-10.f);


		glEnd();

		delay(16666);
		window->swap();
	}

	delete window;
	return 0;
}
