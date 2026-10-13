#ifndef WINDOW____H
#define WINDOW____H

#include <GL/glew.h>
#include <GLFW/glfw3.h>

class Window
{
public:
    Window();
    Window(GLint windowWidth, GLint windowHeight, GLint majorVersion, GLint minorVersion);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    int initialise();

    GLint getBufferWidth() const { return bufferWidth; }
    GLint getBufferHeight() const { return bufferHeight; }

    bool getShouldClose() const
    {
        return mainWindow == nullptr || glfwWindowShouldClose(mainWindow);
    }

    void swapBuffers()
    {
        if (mainWindow != nullptr)
            glfwSwapBuffers(mainWindow);
    }

    GLFWwindow* getWindow() const { return mainWindow; }

private:
    GLFWwindow* mainWindow = nullptr;
    GLint glfwMajorVersion = 3;
    GLint glfwMinorVersion = 3;
    GLint width = 800;
    GLint height = 600;
    GLint bufferWidth = 0;
    GLint bufferHeight = 0;
};

#endif

