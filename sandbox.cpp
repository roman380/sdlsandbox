#include <iostream>
#include <chrono>
#include <thread>
#include <assert.h>
#include <SDL.h>

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
	SDL_GLContext Context = nullptr;

	verify(SDL_Init(SDL_INIT_VIDEO) >= 0);

	SDL_DisplayMode DisplayMode;
	SDL_GetDesktopDisplayMode(0, &DisplayMode);
	std::cout << "Desktop Display Mode: " << DisplayMode.format << ", " << DisplayMode.w << " x " << DisplayMode.h << ", refresh_rate " << DisplayMode.refresh_rate << std::endl;

	SDL_Window* Window = SDL_CreateWindow("Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DisplayMode.w * 0.66, DisplayMode.h * 0.66, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    auto CreateContext = [&](int MajorVersion, int MinorVersion)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, MajorVersion);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, MinorVersion);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        Context = SDL_GL_CreateContext(Window);
    };

    assert(!Context);
    CreateContext(3, 2);
    assert(!Context);

    SDL_GL_MakeCurrent(Window, Context);
    SDL_GL_SetSwapInterval(1);

	int Width = 0, Height = 0;
	SDL_GL_GetDrawableSize(Window, &Width, &Height);
	std::cout << "Drawable Size: " << Width << " x " << Height << std::endl;

	for(bool Quit = false; !Quit;)
	{
		for(;;)
		{
			SDL_Event Event {};
			if(!SDL_PollEvent(&Event))
				break;
			std::cout << "SDL Event: " << Event.type << std::endl;
			if(Event.type == SDL_QUIT)
			{
				Quit = true;
				break;
			}
		}
		SDL_GL_SwapWindow(Window);
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(20ms);
	}

	SDL_GL_MakeCurrent(Window, nullptr);
	SDL_GL_DeleteContext(Context);
	SDL_DestroyWindow(Window);

	SDL_Quit();
	return 0;
}
