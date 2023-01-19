#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <assert.h>

#include <SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_video_hook.h>

#include <GL/glx.h>

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

struct Application
{
	Application()
	{
		SDL_version Version;
		SDL_VERSION(&Version);
		std::cout << "Version: " << static_cast<int>(Version.major) << "." << static_cast<int>(Version.minor) << " patch " << static_cast<int>(Version.patch) << std::endl;
		std::cout << "Current Video Driver: " << SDL_GetCurrentVideoDriver() << std::endl;

		SDL_DisplayMode DisplayMode;
		SDL_GetDesktopDisplayMode(0, &DisplayMode);
		std::cout << "Desktop Display Mode: 0x" << std::hex << DisplayMode.format << std::dec << ", " << std::dec << DisplayMode.w << " x " << DisplayMode.h << ", refresh_rate " << DisplayMode.refresh_rate << std::endl;

		assert(!Current);
		Current = this;
		SDL_VideoHook VideoHook;
		VideoHook.After_CreateWindow = +[] (SDL_Window* Window) { Current->AfterCreateWindow(Window); };
		VideoHook.After_GL_CreateContext = +[] (SDL_Window* Window, SDL_GLContext Context) { Current->AfterCreateContext(Window, Context); };
		VideoHook.Before_GL_SwapWindow = +[] (SDL_Window* Window) { Current->BeforeSwapWindow(Window); };
		SDL_SetVideoHook(&VideoHook);

		Window = SDL_CreateWindow("Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DisplayMode.w * 0.70, DisplayMode.h * 0.70, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
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
		Context = SDL_GL_CreateContext(Window);
		assert(Context);

		{
			SDL_SysWMinfo WindowInfo { Version, SDL_SYSWM_X11 };
			verify(SDL_GetWindowWMInfo(Window, &WindowInfo) == SDL_TRUE);
			std::cout << "Window: subsystem " << static_cast<int>(WindowInfo.subsystem) << std::endl; // SDL_SYSWM_X11
			assert(WindowInfo.info.x11.display);
			
			gl_display = reinterpret_cast<GstGLDisplay*>(gst_gl_display_x11_new_with_display(WindowInfo.info.x11.display));
			g_assert_nonnull(gl_display);
			auto const handle = glXGetCurrentContext();
			g_assert_nonnull(handle);
			static GstGLPlatform constexpr const g_platform = GST_GL_PLATFORM_GLX;
			GstGLAPI const api = gst_gl_context_get_current_gl_api (g_platform, nullptr, nullptr);
			g_assert_true(api == GST_GL_API_OPENGL);
			std::cout << "GStreamer GL:  " << reinterpret_cast<void const*>(handle) << ", g_platform " << static_cast<int>(g_platform) << ", api 0x" << std::hex << static_cast<unsigned int>(api) << std::dec << std::endl;
			gl_context = gst_gl_context_new_wrapped(gl_display, reinterpret_cast<guintptr>(handle), g_platform, api); // https://gstreamer.freedesktop.org/documentation/gl/gstglapi.html#gst_gl_platform_from_string
			g_assert_nonnull(gl_context);
			verify(gst_gl_context_activate(gl_context, TRUE));
			GError* error = nullptr;
			verify(gst_gl_context_fill_info(gl_context, &error));
			if(error && error->message)
				std::cout << "GStreamer Error: " << error->message << std::endl;
		}

		loop = g_main_loop_new(nullptr, FALSE);
		g_mutex_init(&app_lock);
	}
	~Application()
	{
		g_mutex_clear(&app_lock);
		assert(loop);
		g_main_loop_unref(std::exchange(loop, nullptr));

		if(gl_context)
		{
			gst_gl_context_activate(gl_context, FALSE);
			gst_object_unref(GST_OBJECT(std::exchange(gl_context, nullptr)));
		}
		if(gl_display)
			gst_object_unref(GST_OBJECT(std::exchange(gl_display, nullptr)));

		assert(Window && Context);
		SDL_GL_MakeCurrent(Window, nullptr);
		SDL_GL_DeleteContext(Context);
		SDL_DestroyWindow(Window);
	}

	void AfterCreateWindow(SDL_Window* Window)
	{
		std::cout << "After SDL_CreateWindow: " << reinterpret_cast<void const*>(Window) << std::endl;
	}
	void AfterCreateContext(SDL_Window* Window, SDL_GLContext Context)
	{
		std::cout << "After SDL_GL_CreateContext: " << reinterpret_cast<void const*>(Window) << ", Context " << Context << std::endl;
	}
	void BeforeSwapWindow(SDL_Window* Window)
	{
		//std::cout << "After SDL_GL_SwapWindow: " << reinterpret_cast<void const*>(Window) << std::endl;
	}

	void bus_error(GstBus* bus, GstMessage* message)
	{
		g_assert_nonnull(bus);
		g_assert_nonnull(message);
		g_print("%s, %s: ...\n", gst_element_get_name(GST_ELEMENT(message->src)), GST_MESSAGE_TYPE_NAME(message));
		if(GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
		{
			gchar* debug_message = nullptr;
			GError* error = nullptr;
			gst_message_parse_error(message, &error, &debug_message);
			g_print("Warning: %s\n", error->message);
			g_error_free(error);
			if(debug_message)
			{
				g_print("%s\n", debug_message);
				g_free(debug_message);
			}
		}
	}
	void bus_warning(GstBus* bus, GstMessage* message)
	{
		g_assert_nonnull(bus);
		g_assert_nonnull(message);
		g_print("%s, %s: ...\n", gst_element_get_name(GST_ELEMENT(message->src)), GST_MESSAGE_TYPE_NAME(message));
		if(GST_MESSAGE_TYPE (message) == GST_MESSAGE_WARNING)
		{
			gchar* debug_message = nullptr;
			GError* error = nullptr;
			gst_message_parse_warning(message, &error, &debug_message);
			g_print("Warning: %s\n", error->message);
			g_error_free(error);
			if(debug_message)
			{
				g_print("%s\n", debug_message);
				g_free(debug_message);
			}
		}
	}
	void bus_sync_message(GstBus* bus, GstMessage* message)
	{
		g_assert_nonnull(bus);
		g_assert_nonnull(message);
		switch(GST_MESSAGE_TYPE(message))
		{
		case GST_MESSAGE_NEED_CONTEXT:
			{
				gchar const* context_type = nullptr;
				gst_message_parse_context_type(message, &context_type);
				g_print("%s, %s: %s\n", gst_element_get_name(GST_ELEMENT(message->src)), GST_MESSAGE_TYPE_NAME(message), context_type);
				if(g_strcmp0(context_type, GST_GL_DISPLAY_CONTEXT_TYPE) == 0) // gst.gl.GLDisplay
				{
					g_assert_nonnull(gl_display);
					GstContext* display_context = gst_context_new(context_type, TRUE);
					gst_context_set_gl_display(display_context, gl_display);
					gst_element_set_context(GST_ELEMENT(message->src), display_context);
					gst_context_unref(display_context);
				}
				else if(g_strcmp0(context_type, "gst.gl.app_context") == 0)
				{
					g_assert_nonnull(gl_context);
					GstContext* app_context = gst_context_new(context_type, TRUE);
					GstStructure* structure = gst_context_writable_structure(app_context);
					gst_structure_set(structure, "context", GST_TYPE_GL_CONTEXT, gl_context, nullptr);
					gst_element_set_context(GST_ELEMENT(message->src), app_context);
					gst_context_unref(app_context);
				}
				break;
			}
		default:
			g_print("%s, %s\n", gst_element_get_name(GST_ELEMENT(message->src)), GST_MESSAGE_TYPE_NAME(message));
			break;
		}
	}

	void sink_handoff(GstElement* element, GstBuffer* buffer, GstPad* pad)
	{
		GstCaps* caps = gst_pad_get_current_caps(pad);
		GstVideoInfo info;
		gst_video_info_from_caps(&info, caps); // Does not have to be per sample
		GstVideoFrame frame;
		verify(gst_video_frame_map(&frame, &info, buffer, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_GL)));
		gst_caps_unref(caps);

		g_mutex_lock(&app_lock);
		app_rendered = FALSE;
		this->frame = &frame;
		g_idle_add_full(G_PRIORITY_HIGH, G_SOURCE_FUNC(+[] (gpointer user_data) -> gboolean { return reinterpret_cast<Application*>(user_data)->render(); }), this, nullptr);
		while(!app_rendered && !app_quit)
			g_cond_wait(&app_cond, &app_lock);
		g_mutex_unlock(&app_lock);

		gst_video_frame_unmap (&frame);
	}

	static std::string Format(char const* TextFormat, ...)
	{
		char Text[4 << 10];
		va_list Arguments;
		va_start(Arguments, TextFormat);
		vsprintf(Text, TextFormat, Arguments);
		va_end(Arguments);
		return Text;
	}
	static std::string Join(std::vector<std::string> const& Vector, char const* Separator)
	{
		std::ostringstream Stream;
		for(size_t Index = 0; Index < Vector.size(); Index++)
		{
			Stream << Vector[Index];
			if(Index + 1 < Vector.size())
				Stream << Separator;
		}
		return Stream.str();
	}
	static std::string FormatValues(float const* Values, size_t ValueCount, char const* ValueFormat, char const* Separator)
	{
		std::vector<std::string> Vector;
		Vector.reserve(ValueCount);
		for(size_t Index = 0; Index < ValueCount; Index++)
			Vector.emplace_back(Format(ValueFormat, Values[Index]));
		return Join(Vector, Separator);
	}
	
	gboolean timeout()
	{
		for(;;)
		{
			SDL_Event Event {};
			if(!SDL_PollEvent(&Event))
				break;
			//std::cout << "SDL Event: " << Event.type << std::endl;
			if(Event.type == SDL_KEYDOWN)
			{
				if (Event.key.keysym.sym == SDLK_ESCAPE)	
				{
					app_quit = TRUE;
					g_main_loop_quit(loop);
					return G_SOURCE_REMOVE;
				}
			}
			if(Event.type == SDL_QUIT)
			{
				app_quit = TRUE;
				g_main_loop_quit(loop);
				return G_SOURCE_REMOVE;
			}
		}
		return G_SOURCE_CONTINUE;
	}
	gboolean render()
	{
		g_mutex_lock(&app_lock);
		if(!app_quit)
		{
			SDL_GL_MakeCurrent(Window, Context);
			if(glXGetCurrentContext())
			{
				auto const Texture = *reinterpret_cast<guint*>(frame->data[0]);

				// NOTE: OpenGL® 2.1, GLX, and GLU Reference Pages https://registry.khronos.org/OpenGL-Refpages/gl2.1/

				static std::once_flag g_OutputStaticParameters;
				std::call_once(g_OutputStaticParameters, [&]
				{
					GLint MatrixMode;
					glGetIntegerv(GL_MATRIX_MODE, &MatrixMode); // GL_MODELVIEW 0x1700
					std::cout << "GL_MATRIX_MODE " << std::hex << MatrixMode << std::dec << std::endl;
					if(MatrixMode == GL_MODELVIEW)
					{
						GLfloat Matrix[16];
						glGetFloatv(GL_MODELVIEW_MATRIX, Matrix);
						std::cout << "GL_MODELVIEW_MATRIX " << FormatValues(Matrix, std::size(Matrix), "%.2f", " ") << std::endl;
					}
					GLfloat Viewport[4];
					glGetFloatv(GL_VIEWPORT, Viewport);
					GLfloat MaximalViewportDimension[2];
					glGetFloatv(GL_MAX_VIEWPORT_DIMS, MaximalViewportDimension);
					std::cout << "GL_VIEWPORT " << FormatValues(Viewport, std::size(Viewport), "%.1f", " ") << ", GL_MAX_VIEWPORT_DIMS " << FormatValues(MaximalViewportDimension, std::size(MaximalViewportDimension), "%.1f", " ") << std::endl;
					GLfloat ScissorBox[4];
					glGetFloatv(GL_SCISSOR_BOX, ScissorBox);
					std::cout << "GL_SCISSOR_TEST " << static_cast<bool>(glIsEnabled(GL_SCISSOR_TEST)) << ", GL_SCISSOR_BOX " << FormatValues(ScissorBox, std::size(ScissorBox), "%.1f", " ") << std::endl;
					std::cout << "GL_DEPTH_TEST " << static_cast<bool>(glIsEnabled(GL_DEPTH_TEST)) << std::endl;
				});

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear The Screen And The Depth Buffer
				glLoadIdentity();									// Reset The View

				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, Texture);

				GLint TextureWidth, TextureHeight, TextureFormat;
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &TextureWidth);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &TextureHeight);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &TextureFormat); // GL_RGBA8 0x8058
				std::cout << Texture << ", " << TextureWidth << " x " << TextureHeight << ", TextureFormat " << std::hex << TextureFormat << std::dec << std::endl;

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				//glColor3f(0.4f, 0.4f, 1.0f); // set color to a blue shade.

				GLfloat const A = 0.90f;
				GLfloat const B = 0.00f;
				glBegin(GL_QUADS);
				glTexCoord3f(0.0f, 1.0f, 0.0f);
				glVertex3f(-A, -A, B); // Top Left
				glTexCoord3f(1.0f, 1.0f, 0.0f);
				glVertex3f(+A, -A, B); // Top Right
				glTexCoord3f(1.0f, 0.0f, 0.0f);
				glVertex3f(+A, +A, B); // Bottom Right
				glTexCoord3f(0.0f, 0.0f, 0.0f);
				glVertex3f(-A, +A, B); // Bottom Left
				glEnd();

				glBindTexture(GL_TEXTURE_2D, 0);

				SDL_GL_SwapWindow(Window);
			}
			SDL_GL_MakeCurrent(Window, nullptr);
		}
		app_rendered = TRUE;
		g_cond_signal(&app_cond);
		g_mutex_unlock(&app_lock);
		return G_SOURCE_REMOVE;
	}

	static Application* Current;
	SDL_Window* Window = nullptr;
	SDL_GLContext Context = nullptr;
	GMainLoop* loop = nullptr;
	GstGLDisplay* gl_display = nullptr;
	GstGLContext* gl_context = nullptr;
	GMutex app_lock;
	GCond app_cond;
	GstVideoFrame* frame = nullptr;
	gboolean app_rendered = FALSE;
	gboolean app_quit = FALSE;
};

Application* Application::Current = nullptr;

int main(int argc, char** argv)
{
	gst_init(&argc, &argv);
	verify(SDL_Init(SDL_INIT_VIDEO) >= 0);

	{
		Application Application;

		std::string const pipeline_text = "gltestsrc ! fakesink name=sink sync=1";
		GError* error = nullptr;
		GstPipeline* pipeline = GST_PIPELINE(gst_parse_launch(pipeline_text.c_str(), &error));
		if(error && error->message)
			std::cout << "GStreamer Error: " << error->message << std::endl;
		g_assert_nonnull(pipeline);
		g_assert_null(error);

		GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
		g_assert_nonnull(bus);
		gst_bus_add_signal_watch(bus);
		g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(+[](GstBus* bus, GstMessage* message, gpointer user_data) { reinterpret_cast<::Application*>(user_data)->bus_error(bus, message); }), &Application);
		g_signal_connect(G_OBJECT(bus), "message::warning", G_CALLBACK(+[](GstBus* bus, GstMessage* message, gpointer user_data) { reinterpret_cast<::Application*>(user_data)->bus_warning(bus, message); }), &Application);
		gst_bus_enable_sync_message_emission(bus);
		g_signal_connect(G_OBJECT(bus), "sync-message", G_CALLBACK(+[](GstBus* bus, GstMessage* message, gpointer user_data) { reinterpret_cast<::Application*>(user_data)->bus_sync_message(bus, message); }), &Application);
		{
			GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
			g_object_set(G_OBJECT(sink), "signal-handoffs", TRUE, nullptr);
			g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement* element, GstBuffer* buffer, GstPad* pad, gpointer user_data) { reinterpret_cast<::Application*>(user_data)->sink_handoff(element, buffer, pad); }), &Application);
			gst_object_unref(sink);
		}

		SDL_GL_MakeCurrent(Application.Window, Application.Context);
		SDL_GL_SetSwapInterval(1);

		int Width = 0, Height = 0;
		SDL_GL_GetDrawableSize(Application.Window, &Width, &Height);
		std::cout << "Drawable Size: " << Width << " x " << Height << std::endl;

		gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
		gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

		g_timeout_add(100, G_SOURCE_FUNC(+[](gpointer user_data) -> gboolean { return reinterpret_cast<::Application*>(user_data)->timeout(); }), &Application);
		g_main_loop_run(Application.loop);

		gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
		gst_bus_remove_signal_watch(bus);
		gst_object_unref(GST_OBJECT(std::exchange(bus, nullptr)));
		gst_object_unref(GST_OBJECT(std::exchange(pipeline, nullptr)));
	}

	SDL_Quit();
	gst_deinit();
	return 0;
}
