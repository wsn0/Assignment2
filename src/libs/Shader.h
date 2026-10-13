#ifndef SHADER____H
#define SHADER____H

#include <GL/glew.h>

#include <filesystem>
#include <string>

class Shader
{
public:
    Shader();
    ~Shader();

    void CreateFromString(const char* vertexCode, const char* fragmentCode);
    void CreateFromFiles(const std::filesystem::path& vertexLocation,
                         const std::filesystem::path& fragmentLocation);
    std::string ReadFile(const std::filesystem::path& fileLocation);

    void UseShader();
    void ClearShader();

    GLint GetUniformLocation(const char* uniformName) const
    {
        return glGetUniformLocation(shader, uniformName);
    }

private:
    GLuint shader;

    void CompileShaders(const char* vertexCode, const char* fragmentCode);
    bool AddShader(GLuint theProgram, const char* shaderCode, GLenum shaderType);
};

#endif

