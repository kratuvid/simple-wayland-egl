#include <print>
#include <cstring>
#include <memory>
#include <ranges>
#include <string_view>
#include <cassert>

#include <glad/glad.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

using namespace std;

// Storage
struct {
	struct {
		wl_display* display;
		wl_registry* registry;

		wl_compositor* compositor;
		xdg_wm_base* wm_base;
	} core;

	struct {
		wl_surface* surface;
		xdg_surface* xsurface;
		xdg_toplevel* xtoplevel;
		wl_callback* redraw_callback;
	} window;
} wl {};

struct {
	int width = 512, height = 512;
	bool is_initial_configured = false;
	bool running = true;
} states;

struct {
	void (*on_resize) ();
	void (*on_redraw) (uint32_t);
} callbacks {};

struct {
	EGLDisplay display;
	wl_egl_window* wl_window;
	EGLSurface surface;
	EGLContext context;
} egl {};

// Listener
void rl_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0)
		wl.core.compositor = (wl_compositor*) wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	else if (strcmp(interface, "xdg_wm_base") == 0)
		wl.core.wm_base = (xdg_wm_base*) wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
}
void rl_global_remove(void* data, wl_registry* registry, uint32_t name) {}

void wbl_ping(void* data, xdg_wm_base* wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

void xsl_configure(void* data, xdg_surface* xsurface, uint32_t serial)
{
	states.is_initial_configured = true;
	xdg_surface_ack_configure(xsurface, serial);
}

void xtl_configure(void* data, xdg_toplevel* xtoplevel, int32_t width, int32_t height, wl_array* states_)
{
	if (!((width == 0 || height == 0) or (width == states.width and height == states.height)))
	{
		states.width = width;
		states.height = height;
		callbacks.on_resize();
	}
}
void xtl_close(void* data, xdg_toplevel* xtoplevel)
{
	states.running = false;
}

const wl_callback_listener* _redraw_callback_listener_ptr = nullptr;
void rcl_done(void* data, wl_callback* callback, uint32_t time)
{
	callbacks.on_redraw(time);

	if (callback)
		wl_callback_destroy(callback);
	wl.window.redraw_callback = wl_surface_frame(wl.window.surface);
	wl_callback_add_listener(wl.window.redraw_callback, _redraw_callback_listener_ptr, nullptr);

	wl_surface_commit(wl.window.surface);
}

const wl_registry_listener registry_listener { .global = rl_global,
	.global_remove = rl_global_remove
};
const xdg_wm_base_listener wm_base_listener {
	.ping = wbl_ping
};
const xdg_surface_listener xsurface_listener {
	.configure = xsl_configure
};
const xdg_toplevel_listener xtoplevel_listener {
	.configure = xtl_configure,
	.close = xtl_close
};
const wl_callback_listener redraw_callback_listener {
	.done = rcl_done
};

void init()
{
}

void destroy()
{
}

void on_resize()
{
	wl_egl_window_resize(egl.wl_window, states.width, states.height, 0, 0);
	
	glViewport(0, 0, states.width, states.height);
	glScissor(0, 0, states.width, states.height);
}

void on_redraw(uint32_t now_time)
{
	float time_secs = now_time * 1e-3f;
	glClearColor(std::abs(std::sin(time_secs)), std::abs(std::sin(time_secs + M_PIf / 4)), std::abs(std::cos(time_secs)), 1);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(egl.display, egl.surface);
}

int main()
{
	_redraw_callback_listener_ptr = &redraw_callback_listener;
	callbacks.on_resize = on_resize;
	callbacks.on_redraw = on_redraw;

	wl.core.display = wl_display_connect(nullptr);
	wl.core.registry = wl_display_get_registry(wl.core.display);

	wl_registry_add_listener(wl.core.registry, &registry_listener, nullptr);
	wl_display_roundtrip(wl.core.display);

	xdg_wm_base_add_listener(wl.core.wm_base, &wm_base_listener, nullptr);
	wl_display_roundtrip(wl.core.display);

	wl.window.surface = wl_compositor_create_surface(wl.core.compositor);
	wl.window.xsurface = xdg_wm_base_get_xdg_surface(wl.core.wm_base, wl.window.surface);
	wl.window.xtoplevel = xdg_surface_get_toplevel(wl.window.xsurface);

	xdg_surface_add_listener(wl.window.xsurface, &xsurface_listener, nullptr);
	xdg_toplevel_add_listener(wl.window.xtoplevel, &xtoplevel_listener, nullptr);
	wl_surface_commit(wl.window.surface);
	while (!states.is_initial_configured)
		wl_display_dispatch(wl.core.display);

	auto query = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	std::println("Pre-extensions: {}", query);

	EGLNativeDisplayType native_display = wl.core.display;
	egl.display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, native_display, nullptr);
	std::println("EGLDisplay: {}", (void*)egl.display);

	eglInitialize(egl.display, nullptr, nullptr);

	query = eglQueryString(egl.display, EGL_VERSION);
	std::println("Version: {}", query);
	query = eglQueryString(egl.display, EGL_VENDOR);
	std::println("Vendor: {}", query);
	query = eglQueryString(egl.display, EGL_CLIENT_APIS);
	std::println("Client APIs: {}", query);
	query = eglQueryString(egl.display, EGL_EXTENSIONS);
	std::println("Extensions: {}", query);

	EGLint num_configs;
	eglGetConfigs(egl.display, nullptr, 0, &num_configs);
	std::println("{} configs are supported", num_configs);

	const EGLint attrib_list[] {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_BUFFER_SIZE, 32,
		EGL_DEPTH_SIZE, 0,
		EGL_STENCIL_SIZE, 0,
		EGL_SAMPLES, 0,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_CONFIG_CAVEAT, EGL_NONE,
		EGL_MAX_SWAP_INTERVAL, 1,
		EGL_NONE
	};
	eglChooseConfig(egl.display, attrib_list, nullptr, 0, &num_configs);
	std::println("{} configs left post filtering", num_configs);

	auto configs = std::make_unique<EGLConfig[]>(num_configs);
	eglChooseConfig(egl.display, attrib_list, configs.get(), num_configs, &num_configs);

#define _ATTRIB_ENTRY(attrib) {attrib, #attrib}
	const std::pair<EGLint, std::string_view> attribs[] {
		_ATTRIB_ENTRY(EGL_CONFIG_ID),
		_ATTRIB_ENTRY(EGL_BUFFER_SIZE),
		_ATTRIB_ENTRY(EGL_RED_SIZE),
		_ATTRIB_ENTRY(EGL_GREEN_SIZE),
		_ATTRIB_ENTRY(EGL_BLUE_SIZE),
		_ATTRIB_ENTRY(EGL_ALPHA_SIZE),
		_ATTRIB_ENTRY(EGL_DEPTH_SIZE),
		_ATTRIB_ENTRY(EGL_STENCIL_SIZE),
		_ATTRIB_ENTRY(EGL_MAX_SWAP_INTERVAL),
		_ATTRIB_ENTRY(EGL_MIN_SWAP_INTERVAL),
		_ATTRIB_ENTRY(EGL_NATIVE_RENDERABLE),
		_ATTRIB_ENTRY(EGL_NATIVE_VISUAL_ID),
		_ATTRIB_ENTRY(EGL_NATIVE_VISUAL_TYPE),
		_ATTRIB_ENTRY(EGL_RENDERABLE_TYPE),
		_ATTRIB_ENTRY(EGL_SAMPLE_BUFFERS),
		_ATTRIB_ENTRY(EGL_SAMPLES),
		_ATTRIB_ENTRY(EGL_SURFACE_TYPE)
	};
#undef _ATTRIB_ENTRY

	std::println("\nMatched configurations' attributes:");
	for (int i : std::views::iota(0, num_configs))
	{
		for (auto& [attrib, name] : attribs)
		{
			EGLint value;
			eglGetConfigAttrib(egl.display, configs[i], attrib, &value);

			std::println("{}: {}, {:#x}", name, value, value);
		}
		std::println();
	}

	EGLConfig config = configs[0];
	configs.reset();

	egl.wl_window = wl_egl_window_create(wl.window.surface, states.width, states.height);

	const EGLAttrib window_attrib_list[] {
		EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
		EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
		EGL_NONE,
	};
	egl.surface = eglCreatePlatformWindowSurface(egl.display, config, egl.wl_window, window_attrib_list);
	eglSurfaceAttrib(egl.display, egl.surface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED);

#define _ATTRIB_ENTRY(attrib) {attrib, #attrib}
	const std::pair<EGLAttrib, std::string_view> window_attribs[] {
		_ATTRIB_ENTRY(EGL_CONFIG_ID),
		_ATTRIB_ENTRY(EGL_WIDTH),
		_ATTRIB_ENTRY(EGL_HEIGHT),
		_ATTRIB_ENTRY(EGL_HORIZONTAL_RESOLUTION),
		_ATTRIB_ENTRY(EGL_VERTICAL_RESOLUTION),
		_ATTRIB_ENTRY(EGL_PIXEL_ASPECT_RATIO),
	};
#undef _ATTRIB_ENTRY

	std::println("Surface attributes:");
	for (auto& [attrib, name] : window_attribs)
	{
		EGLint value;
		eglQuerySurface(egl.display, egl.surface, attrib, &value);

		std::println("{}: {}, {:#x}", name, value, value);
	}

	eglBindAPI(EGL_OPENGL_API);

	const EGLint context_attrib_list[] {
		EGL_CONTEXT_MAJOR_VERSION, 4,
		EGL_CONTEXT_MINOR_VERSION, 6,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
		EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE, EGL_TRUE,
		EGL_NONE
	};
	egl.context = eglCreateContext(egl.display, config, EGL_NO_CONTEXT, context_attrib_list);
	assert(egl.context != EGL_NO_CONTEXT);

	assert(eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context));

	assert(gladLoadGLLoader((GLADloadproc)eglGetProcAddress) != 0);

	const char* gl_query = (const char*) glGetString(GL_VERSION);
	std::println("\nGL_VERSION: {}", gl_query);
	gl_query = (const char*) glGetString(GL_SHADING_LANGUAGE_VERSION);
	std::println("GL_SHADING_LANGUAGE_VERSION: {}", gl_query);
	gl_query = (const char*) glGetString(GL_VENDOR);
	std::println("GL_VENDOR: {}", gl_query);
	gl_query = (const char*) glGetString(GL_RENDERER);
	std::println("GL_RENDERER: {}", gl_query);

	/*
	wl.window.redraw_callback = wl_surface_frame(wl.window.surface);
	wl_surface_add_listener(wl.window.redraw_callback, &redraw_callback_listener, nullptr);
	wl_surface_commit(wl.window.surface);
	*/
	rcl_done(nullptr, nullptr, 0);
	wl_display_roundtrip(wl.core.display);

	init();
	while (states.running and wl_display_dispatch(wl.core.display) != -1);
	destroy();

	eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(egl.display, egl.context);
	eglDestroySurface(egl.display, egl.surface);
	wl_egl_window_destroy(egl.wl_window);
	eglTerminate(egl.display);

	xdg_toplevel_destroy(wl.window.xtoplevel);
	xdg_surface_destroy(wl.window.xsurface);
	wl_surface_destroy(wl.window.surface);
	if (wl.window.redraw_callback)
		wl_callback_destroy(wl.window.redraw_callback);
	xdg_wm_base_destroy(wl.core.wm_base);
	wl_compositor_destroy(wl.core.compositor);
	wl_registry_destroy(wl.core.registry);
	wl_display_disconnect(wl.core.display);
}
