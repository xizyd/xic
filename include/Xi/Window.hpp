#ifndef XI_WINDOW_HPP
#define XI_WINDOW_HPP

#include "Input.hpp"
#include "Texture.hpp"

namespace Xi {
class Window {
public:
  Texture *texture = nullptr; // The camera's output texture
  String title = "Xi";
  i32 width = 800, height = 600;
  bool shouldClose = false;
  Array<InputControl> controls;

  Window() {}
  virtual ~Window() {}
  virtual void update() = 0;
};
} // namespace Xi

#ifdef __has_include
#if __has_include(<GLFW/glfw3.h>)
#include <GLFW/glfw3.h>
#define GLFW_AVAILABLE 1
#else
#define GLFW_AVAILABLE 0
#endif
#else
#warning                                                                       \
    "Could not detect if GLFW is available to use... try defining GLFW_AVAILABLE yourself..."
#define GLFW_AVAILABLE 0
#endif

#ifdef GLFW_AVAILABLE
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
// Undefining X11 macros that conflict with modern C++
#undef Window
#undef Success
#undef Always
#endif

namespace Xi {
class GLFWWindowImpl : public Window {
  GLFWwindow *_win = nullptr;
  SwapContext swp;

public:
  GLFWWindowImpl() {
#if PLATFORM_LINUX
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit())
      return;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    _win = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(_win, this);

    // Handle Resizing
    glfwSetFramebufferSizeCallback(
        _win, [](GLFWwindow *w, int width, int height) {
          // Retrieve the C++ instance pointer
          auto *s = static_cast<GLFWWindowImpl *>(glfwGetWindowUserPointer(w));

          if (s != nullptr) {
            s->width = width;
            s->height = height;
            // Access the swap context through the instance
            s->swp.resize(width, height);
          }
        });

    // Input handling logic remains similar
    glfwSetKeyCallback(_win, [](GLFWwindow *w, int k, int s, int a, int m) {
      auto *me = (GLFWWindowImpl *)glfwGetWindowUserPointer(w);
      for (usz i = 0; i < me->controls.length; ++i) {
        if (me->controls[i].code == k) {
          me->controls[i].down = (a == GLFW_PRESS);
          me->controls[i].held = (a != GLFW_RELEASE);
          return;
        }
      }
      if (a == GLFW_PRESS) {
        InputControl c;
        c.code = k;
        c.type = InputType::Key;
        c.down = true;
        c.held = true;
        c.value = 1.0f;
        me->controls.push(c);
      }
    });

    swp.setDisp(glfwGetX11Display());
    swp.setWin((void *)glfwGetX11Window(_win));
  }

  ~GLFWWindowImpl() {
    if (_win)
      glfwDestroyWindow(_win);
    glfwTerminate();
  }

  void update() override {
    // Reset per-frame input states
    for (usz i = 0; i < controls.length; ++i) {
      controls[i].down = false;
      controls[i].up = (glfwGetKey(_win, controls[i].code) == GLFW_RELEASE &&
                        controls[i].held);
    }

    glfwPollEvents();
    shouldClose = glfwWindowShouldClose(_win);

    void *windowRTV = swp.getRTV();
    void *windowDSV = swp.getDSV();

    if (windowRTV && texture) {
      texture->width = width;
      texture->height = height;
      auto *sceneSRV = texture->getView();

      if (sceneSRV) {
        gContext.bindResources(windowRTV, windowDSV, width, height);
        swp.drawFullscreen(sceneSRV);
      }

      swp.present();
    }
  }
};

Window *createGLFWScreen() { return new GLFWWindowImpl(); }
} // namespace Xi
#endif
#endif