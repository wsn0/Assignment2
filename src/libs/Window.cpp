#include "Window.h"

#include <cstdio>

Window::Window() = default;

Window::Window(GLint windowWidth, GLint windowHeight, GLint majorVersion, GLint minorVersion)
    : glfwMajorVersion(majorVersion),
      glfwMinorVersion(minorVersion),
      width(windowWidth),
      height(windowHeight)
{
}

Window::~Window()
{
    if (mainWindow != nullptr)
    {
        glfwDestroyWindow(mainWindow);
        mainWindow = nullptr;
    }

    glfwTerminate();
}

int Window::initialise()
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "GLFW initialisation failed.\n");
        return 1;
    }

    // Request a modern OpenGL core context. The starter uses OpenGL 3.3.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, glfwMajorVersion);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, glfwMinorVersion);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    // Required by macOS for OpenGL 3.2+ core contexts.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    mainWindow = glfwCreateWindow(width, height, "OpenGL Starter", nullptr, nullptr);

    if (mainWindow == nullptr)
    {
        std::fprintf(stderr, "GLFW window creation failed.\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(mainWindow);
    glfwSwapInterval(1);

    // GLEW must be initialised after an OpenGL context becomes current.
    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK)
    {
        std::fprintf(stderr, "GLEW initialisation failed: %s\n", glewGetErrorString(glewStatus));
        glfwDestroyWindow(mainWindow);
        mainWindow = nullptr;
        glfwTerminate();
        return 1;
    }

    // Some GLEW/core-profile combinations may leave a harmless GL_INVALID_ENUM.
    glGetError();

    glEnable(GL_DEPTH_TEST);

    glfwGetFramebufferSize(mainWindow, &bufferWidth, &bufferHeight);
    glViewport(0, 0, bufferWidth, bufferHeight);

    // Keep the viewport correct on window resize and high-DPI displays.
    glfwSetFramebufferSizeCallback(mainWindow, [](GLFWwindow*, int newWidth, int newHeight)
    {
        glViewport(0, 0, newWidth, newHeight);
    });

    return 0;
}

