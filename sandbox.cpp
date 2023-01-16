#include <iostream>
#include <chrono>
#include <thread>
#include <assert.h>

#include <SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>

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
		loop = g_main_loop_new(nullptr, FALSE);
		g_mutex_init(&app_lock);
	}
	~Application()
	{
		g_mutex_clear(&app_lock);
		if(gl_context)
			gst_object_unref(GST_OBJECT(std::exchange(gl_context, nullptr)));
		if(gl_display)
			gst_object_unref(GST_OBJECT(std::exchange(gl_display, nullptr)));
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
			std::cout << "Error: " << error->message << std::endl;
			g_error_free(error);
			if(debug_message)
			{
				std::cout << debug_message << std::endl;
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
			std::cout << "Warning: " << error->message << std::endl;
			g_error_free(error);
			if(debug_message)
			{
				std::cout << debug_message << std::endl;
				g_free(debug_message);
			}
		}
	}
	void bus_eos(GstBus* bus, GstMessage* message)
	{
		g_assert_nonnull(bus);
		g_assert_nonnull(message);
		g_print("%s, %s: ...\n", gst_element_get_name(GST_ELEMENT(message->src)), GST_MESSAGE_TYPE_NAME(message));
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
			g_print("%s, %s: ...\n", gst_element_get_name(GST_ELEMENT(message->src)), GST_MESSAGE_TYPE_NAME(message));
			break;
		}
	}

	gboolean sink_client_reshape(GstElement* sink, GstGLContext* context, GLuint width, GLuint height)
	{
		g_print("%d: width %u, height %u\n", __LINE__, width, height);
		glViewport(0, 0, width, height);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		return TRUE;
	}
	gboolean sink_client_draw(GstElement* sink, GstGLContext* context, GstSample* sample)
	{
		GstCaps* caps = gst_sample_get_caps(sample);
		GstVideoInfo info;
		gst_video_info_from_caps(&info, caps); // Does not have to be per sample
		GstVideoFrame frame;
		GstBuffer* buffer = gst_sample_get_buffer(sample);
		verify(gst_video_frame_map(&frame, &info, buffer, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_GL)));

		g_mutex_lock(&app_lock);
		app_rendered = FALSE;
		this->frame = &frame;
		g_idle_add_full(G_PRIORITY_HIGH, G_SOURCE_FUNC(+[] (gpointer user_data) -> gboolean { return reinterpret_cast<Application*>(user_data)->render(); }), this, nullptr);
		while(!app_rendered && !app_quit)
			g_cond_wait(&app_cond, &app_lock);
		g_mutex_unlock(&app_lock);

		gst_video_frame_unmap (&frame);
		return TRUE;
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
				auto const texture = *reinterpret_cast<guint*>(frame->data[0]);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear The Screen And The Depth Buffer
				glLoadIdentity();									// Reset The View

				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, texture);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				//glColor3f(0.4f, 0.4f, 1.0f); // set color to a blue shade.
				glBegin(GL_QUADS);
				glTexCoord3f(0.0f, 1.0f, 0.0f);
				glVertex3f(-0.6f, -0.6f, 0.0f); // Top Left
				glTexCoord3f(1.0f, 1.0f, 0.0f);
				glVertex3f(+0.6f, -0.6f, 0.0f); // Top Right
				glTexCoord3f(1.0f, 0.0f, 0.0f);
				glVertex3f(+0.6f, +0.6f, 0.0f); // Bottom Right
				glTexCoord3f(0.0f, 0.0f, 0.0f);
				glVertex3f(-0.6f, +0.6f, 0.0f); // Bottom Left
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

int main(int argc, char** argv)
{
	gst_init(&argc, &argv);

	verify(SDL_Init(SDL_INIT_VIDEO) >= 0);

	SDL_version Version;
	SDL_VERSION(&Version);
	std::cout << "Version: " << static_cast<int>(Version.major) << "." << static_cast<int>(Version.minor) << " patch " << static_cast<int>(Version.patch) << std::endl;
	std::cout << "Current Video Driver: " << SDL_GetCurrentVideoDriver() << std::endl;

	SDL_DisplayMode DisplayMode;
	SDL_GetDesktopDisplayMode(0, &DisplayMode);
	std::cout << "Desktop Display Mode: 0x" << std::hex << DisplayMode.format << ", " << std::dec << DisplayMode.w << " x " << DisplayMode.h << ", refresh_rate " << DisplayMode.refresh_rate << std::endl;

	Application Application;

	Application.Window = SDL_CreateWindow("Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DisplayMode.w * 0.70, DisplayMode.h * 0.70, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
	assert(Application.Window);
	std::cout << "Window: ID " << SDL_GetWindowID(Application.Window) << ", Flags 0x" << SDL_GetWindowFlags(Application.Window) << std::endl;

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
	Application.Context = SDL_GL_CreateContext(Application.Window);
	assert(Application.Context);

	SDL_SysWMinfo WindowInfo { Version, SDL_SYSWM_X11 };
	verify(SDL_GetWindowWMInfo(Application.Window, &WindowInfo) == SDL_TRUE);
	std::cout << "Window: subsystem " << static_cast<int>(WindowInfo.subsystem) << std::endl; // SDL_SYSWM_X11
	assert(WindowInfo.info.x11.display);

	{
		Application.gl_display = reinterpret_cast<GstGLDisplay*>(gst_gl_display_x11_new_with_display(WindowInfo.info.x11.display));
		g_assert_nonnull(Application.gl_display);
		auto const handle = glXGetCurrentContext();
		g_assert_nonnull(handle);
		static GstGLPlatform constexpr const g_platform = GST_GL_PLATFORM_GLX;
		GstGLAPI const api = gst_gl_context_get_current_gl_api (g_platform, nullptr, nullptr);
		g_assert_true(api == GST_GL_API_OPENGL);
		std::cout << "GStreamer GL:  " << reinterpret_cast<void const*>(handle) << ", g_platform " << static_cast<int>(g_platform) << ", api 0x" << std::hex << static_cast<unsigned int>(api) << std::endl;
		Application.gl_context = gst_gl_context_new_wrapped(Application.gl_display, reinterpret_cast<guintptr>(handle), g_platform, api); // https://gstreamer.freedesktop.org/documentation/gl/gstglapi.html#gst_gl_platform_from_string
		g_assert_nonnull(Application.gl_context);
		verify(gst_gl_context_activate(Application.gl_context, TRUE));
		GError* error = nullptr;
		verify(gst_gl_context_fill_info(Application.gl_context, &error));
		if(error && error->message)
			std::cout << "GStreamer Error: " << error->message << std::endl;
	}

	std::string const pipeline_text = 
		"gltestsrc ! "
		//"videotestsrc ! video/x-raw, width=640, height=480, framerate=(fraction)30/1 ! gleffects effect=0 ! "
		//"glimagesink name=sink";
		"fakesink name=sink sync=1";
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
	g_signal_connect(G_OBJECT(bus), "message::eos", G_CALLBACK(+[](GstBus* bus, GstMessage* message, gpointer user_data) { reinterpret_cast<::Application*>(user_data)->bus_eos(bus, message); }), &Application);
	gst_bus_enable_sync_message_emission(bus);
	g_signal_connect(G_OBJECT(bus), "sync-message", G_CALLBACK(+[](GstBus* bus, GstMessage* message, gpointer user_data) { reinterpret_cast<::Application*>(user_data)->bus_sync_message(bus, message); }), &Application);
	GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");

	//g_signal_connect(G_OBJECT(sink), "client-reshape", G_CALLBACK(+[](GstElement* sink, GstGLContext* context, GLuint width, GLuint height, gpointer user_data) -> gboolean { return reinterpret_cast<::Application*>(user_data)->sink_client_reshape(sink, context, width, height); }), &Application);
	//g_signal_connect(G_OBJECT(sink), "client-draw", G_CALLBACK(+[](GstElement* sink, GstGLContext* context, GstSample* sample, gpointer user_data) -> gboolean { return reinterpret_cast<::Application*>(user_data)->sink_client_draw(sink, context, sample); }), &Application);

	g_object_set(G_OBJECT(sink), "signal-handoffs", TRUE, nullptr);
	g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement* element, GstBuffer* buffer, GstPad* pad, gpointer user_data) { reinterpret_cast<::Application*>(user_data)->sink_handoff(element, buffer, pad); }), &Application);

	gst_object_unref(sink);

	SDL_GL_MakeCurrent(Application.Window, Application.Context);
	SDL_GL_SetSwapInterval(1);

	int Width = 0, Height = 0;
	SDL_GL_GetDrawableSize(Application.Window, &Width, &Height);
	std::cout << "Drawable Size: " << Width << " x " << Height << std::endl;

	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

	g_timeout_add(100, G_SOURCE_FUNC(+[](gpointer user_data) -> gboolean { return reinterpret_cast<::Application*>(user_data)->timeout(); }), &Application);
	g_main_loop_run(Application.loop);

/*
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
*/

	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
	gst_bus_remove_signal_watch(bus);
	gst_object_unref(GST_OBJECT(std::exchange(bus, nullptr)));
	gst_object_unref(GST_OBJECT(std::exchange(pipeline, nullptr)));

	gst_gl_context_activate(Application.gl_context, FALSE);

	SDL_GL_MakeCurrent(Application.Window, nullptr);
	SDL_GL_DeleteContext(Application.Context);
	SDL_DestroyWindow(Application.Window);

	SDL_Quit();
	return 0;
}
