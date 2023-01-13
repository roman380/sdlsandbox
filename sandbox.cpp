#include <iostream>
#include <chrono>
#include <thread>
#include <assert.h>

#include <SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>

#include <gst/gst.h>
#include <gst/gl/gl.h>
#include <gst/gl/x11/gstgldisplay_x11.h>

inline void verify(bool Value)
{
	assert(Value);
}
inline void verify(void const* Value)
{
	verify(Value != nullptr);
}

int main(int argc, char** argv)
{
	gst_init(&argc, &argv);

	verify(SDL_Init(SDL_INIT_VIDEO) >= 0);

	SDL_version Version;
	SDL_VERSION(&Version);
	std::cout << "Version: " << static_cast<int>(Version.major) << "." << static_cast<int>(Version.minor) << " patch " << static_cast<int>(Version.patch) << std::endl;

	SDL_DisplayMode DisplayMode;
	SDL_GetDesktopDisplayMode(0, &DisplayMode);
	std::cout << "Desktop Display Mode: 0x" << std::hex << DisplayMode.format << ", " << std::dec << DisplayMode.w << " x " << DisplayMode.h << ", refresh_rate " << DisplayMode.refresh_rate << std::endl;

	SDL_Window* Window = SDL_CreateWindow("Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DisplayMode.w * 0.66, DisplayMode.h * 0.66, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
	assert(Window);
	std::cout << "Window: ID " << SDL_GetWindowID(Window) << ", Flags 0x" << SDL_GetWindowFlags(Window) << std::endl;

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	SDL_GLContext Context = SDL_GL_CreateContext(Window);
	assert(Context);

	SDL_GL_MakeCurrent(Window, Context);
	SDL_GL_SetSwapInterval(1);

	SDL_SysWMinfo Info { };
	if(SDL_GetWindowWMInfo(Window, &Info) == SDL_TRUE)
		std::cout << "Window: subsystem" << static_cast<int>(Info.subsystem) << std::endl;
	//assert(Info.subsystem == SDL_SYSWM_X11);

	int Width = 0, Height = 0;
	SDL_GL_GetDrawableSize(Window, &Width, &Height);
	std::cout << "Drawable Size: " << Width << " x " << Height << std::endl;

	// glimagesink https://gstreamer.freedesktop.org/documentation/opengl/glimagesink.html?gi-language=c#glimagesink-page

	auto const pipeline = gst_parse_launch("gltestsrc ! glimagesink", nullptr);

	// https://github.com/nxp-imx/gst-plugins-base/blob/4b8559690bf7a66745cc65900baccd955b436d3c/tests/examples/gl/sdl/sdlshare2.c#L354

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	SDL_Surface* Surface = SDL_GetWindowSurface(Window);
	Uint32 const Colors[2] 
	{ 
		SDL_MapRGB(Surface->format, 0x88, 0x44, 0x88), 
		SDL_MapRGB(Surface->format, 0x44, 0x11, 0x44),
	};
	unsigned int FrameIndex = 0;

	for(bool Quit = false; !Quit;)
	{
		for(;;)
		{
			SDL_Event Event {};
			if(!SDL_PollEvent(&Event))
				break;
			//std::cout << "SDL Event: " << Event.type << std::endl;
			if(Event.type == SDL_QUIT)
			{
				Quit = true;
				break;
			}
		}

		SDL_FillRect(Surface, nullptr, Colors[FrameIndex++ % std::size(Colors)]);
		SDL_UpdateWindowSurface(Window);

		//SDL_GL_SwapWindow(Window);
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(20ms);
	}

	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(pipeline));

	SDL_GL_MakeCurrent(Window, nullptr);
	SDL_GL_DeleteContext(Context);
	SDL_DestroyWindow(Window);

	SDL_Quit();
	return 0;
}
