/*
 * Copyright (c) 2012, Courtney Cavin
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
	zdl_flags_t flags = ZDL_FLAG_NONE;
	ZDL::Window *window = new ZDL::Window(320, 240, flags);
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
				if (event.key.sym == ZDL_KEYSYM_R) {
					flags ^= ZDL_FLAG_NORESIZE;
					window->setFlags(flags);
				}
				if (event.key.sym == ZDL_KEYSYM_Q)
					done = 1;
				if (event.key.sym == ZDL_KEYSYM_F) {
					flags ^= ZDL_FLAG_FULLSCREEN;
					window->setFlags(flags);
					window->showCursor(!(flags & ZDL_FLAG_FULLSCREEN));
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
				w = event.reconfigure.width;
				h = event.reconfigure.height;
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
