#include "OpenGLShader.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace Raven
{

// コメントアウト事前処理
std::string RemoveComments(const std::string& src)
{
    std::string result;
    result.reserve(src.size());

    bool inSingleLineComment = false;
    bool inMultiLineComment = false;

    for (size_t i = 0; i < src.size(); ++i)
    {
        if (!inSingleLineComment && !inMultiLineComment)
        {
            // //
            if (i + 1 < src.size() &&
                src[i] == '/' &&
                src[i + 1] == '/')
            {
                inSingleLineComment = true;
                ++i;
                continue;
            }

            // /*
            if (i + 1 < src.size() &&
                src[i] == '/' &&
                src[i + 1] == '*')
            {
                inMultiLineComment = true;
                ++i;
                continue;
            }

            result += src[i];
        }
        else if (inSingleLineComment)
        {
            if (src[i] == '\n')
            {
                inSingleLineComment = false;
                result += '\n';
            }
        }
        else if (inMultiLineComment)
        {
            if (i + 1 < src.size() &&
                src[i] == '*' &&
                src[i + 1] == '/')
            {
                inMultiLineComment = false;
                ++i;
            }
        }
    }

    return result;
}

static GLuint ShaderTypeFromString(const std::string& type)
{
    if (type == "vertex")   return GL_VERTEX_SHADER;
    if (type == "fragment" || type == "pixel") return GL_FRAGMENT_SHADER;

    std::cerr << "Unknown shader type: " << type << std::endl;
    return 0;
}

OpenGLShader::OpenGLShader(const std::string& filepath)
{
#if 0
    auto [vertexSrc, fragmentSrc] = ParseShaderFile(filepath);
    m_RendererID = CreateProgram(vertexSrc, fragmentSrc);
#else
    std::string source = ReadFile(filepath);
    auto shaderSources = PreProcess(source);

    Compile(shaderSources);
#endif
}

OpenGLShader::OpenGLShader(const std::string& vertexFilePath, const std::string& fragFilePath)
{
#if 0
    auto [vertexSrc, fragmentSrc] = ParseShaderFile(filepath);
    m_RendererID = CreateProgram(vertexSrc, fragmentSrc);
#else
    std::string vertexSource = ReadFile(vertexFilePath);
    std::string flagSource = ReadFile(fragFilePath);
    auto shaderSources = PreProcess(vertexSource, flagSource);

    Compile(shaderSources);
#endif
}

OpenGLShader::~OpenGLShader()
{
    glDeleteProgram(m_RendererID);
}

void OpenGLShader::Bind() const
{
    glUseProgram(m_RendererID);
}

void OpenGLShader::Unbind() const
{
    glUseProgram(0);
}

std::unordered_map<GLuint, std::string> OpenGLShader::PreProcess(const std::string& vertexFilePath, const std::string& fragFilePath)
{
    enum ShaderType {
        SHADER_TYPE_VERTEX,
        SHADER_TYPE_FRAGMENT,
        SHADER_TYPE_MAX,
    };

    const std::string* pSources[SHADER_TYPE_MAX] = { &vertexFilePath, &fragFilePath };
    GLint shaderType[SHADER_TYPE_MAX] = { GL_VERTEX_SHADER ,GL_FRAGMENT_SHADER };

    std::unordered_map<GLuint, std::string> shaderSources;

    for (uint8_t i = 0; i < SHADER_TYPE_MAX; i++) {

        if (!pSources[i]) {
            continue;
        }

        std::string cleanSource = RemoveComments(*pSources[i]);

        shaderSources[shaderType[i]] = cleanSource;
    }

    return shaderSources;
}

std::unordered_map<GLuint, std::string> OpenGLShader::PreProcess(const std::string& source)
{
    std::unordered_map<GLuint, std::string> shaderSources;

    std::string cleanSource = RemoveComments(source);

    const char* typeToken = "#type";
    size_t typeTokenLength = strlen(typeToken);

    size_t pos = cleanSource.find(typeToken, 0);

    while (pos != std::string::npos)
    {
        size_t eol = cleanSource.find_first_of("\r\n", pos);
        if (eol == std::string::npos)
        {
            std::cerr << "Syntax error: #type line has no endline\n";
            break;
        }

        size_t begin = pos + typeTokenLength + 1;
        std::string type = cleanSource.substr(begin, eol - begin);

        GLuint shaderType = ShaderTypeFromString(type);
        if (shaderType == 0) {
            break;
        }
        size_t nextLinePos = cleanSource.find_first_not_of("\r\n", eol);
        if (nextLinePos == std::string::npos)
        {
            std::cerr << "Syntax error: shader source missing after #type " << type << "\n";
            break;
        }

        size_t nextTypePos = cleanSource.find(typeToken, nextLinePos);

        shaderSources[shaderType] =
            cleanSource.substr(
                nextLinePos,
                nextTypePos == std::string::npos
                ? std::string::npos
                : nextTypePos - nextLinePos
            );

        pos = nextTypePos;
    }

    return shaderSources;
}

std::string OpenGLShader::ReadFile(const std::string& filepath)
{
    std::ifstream file(filepath);

    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();

    std::string result = ss.str();

    std::cout << "size = " << result.size() << std::endl;

    return ss.str();
}

std::pair<std::string, std::string> OpenGLShader::ParseShaderFile(const std::string& filepath)
{
    std::string source = ReadFile(filepath);

    const char* vertexToken = "#type vertex";
    const char* fragmentToken = "#type fragment";

    size_t vertexPos = source.find(vertexToken);
    size_t fragmentPos = source.find(fragmentToken);

    if (vertexPos == std::string::npos || fragmentPos == std::string::npos)
    {
        std::cerr << "Shader file must contain #type vertex and #type fragment" << std::endl;
        return {};
    }

    size_t vertexStart = source.find('\n', vertexPos) + 1;
    size_t fragmentStart = source.find('\n', fragmentPos) + 1;

    std::string vertexSource =
        source.substr(vertexStart, fragmentPos - vertexStart);

    std::string fragmentSource =
        source.substr(fragmentStart);

    return { vertexSource, fragmentSource };
}

void OpenGLShader::Compile(const std::unordered_map<GLuint, std::string>& shaderSources)
{
    GLuint program = glCreateProgram();

    std::vector<GLuint> shaderIDs;
    shaderIDs.reserve(shaderSources.size());

    for (auto& [type, source] : shaderSources)
    {
        GLuint shader = glCreateShader(type);

        const GLchar* sourceCStr = source.c_str();
        glShaderSource(shader, 1, &sourceCStr, nullptr);

        glCompileShader(shader);

        GLint isCompiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);

        if (isCompiled == GL_FALSE)
        {
            GLint maxLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(maxLength);
            glGetShaderInfoLog(shader, maxLength, &maxLength, infoLog.data());

            glDeleteShader(shader);

            std::cerr << "Shader compilation failed:\n"
                << infoLog.data() << std::endl;
            return;
        }

        glAttachShader(program, shader);
        shaderIDs.push_back(shader);
    }

    glLinkProgram(program);

    GLint isLinked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &isLinked);

    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());

        glDeleteProgram(program);

        for (GLuint id : shaderIDs)
            glDeleteShader(id);

        std::cerr << "Shader link failed:\n"
            << infoLog.data() << std::endl;
        return;
    }

    for (GLuint id : shaderIDs)
    {
        glDetachShader(program, id);
        glDeleteShader(id);
    }

    m_RendererID = program;
}

unsigned int OpenGLShader::CompileShader(unsigned int type, const std::string& source)
{
    unsigned int shader = glCreateShader(type);

    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        int length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        std::string message(length, ' ');
        glGetShaderInfoLog(shader, length, &length, message.data());

        std::cerr << "Shader compile error:\n" << message << std::endl;

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

unsigned int OpenGLShader::CreateProgram(
    const std::string& vertexSource,
    const std::string& fragmentSource)
{
    unsigned int program = glCreateProgram();

    unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);

    unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success)
    {
        int length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        std::string message(length, ' ');
        glGetProgramInfoLog(program, length, &length, message.data());

        std::cerr << "Shader link error:\n" << message << std::endl;

        glDeleteProgram(program);
        return 0;
    }

    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

void OpenGLShader::SetInt(const std::string& name, int value)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform1i(location, value);
}

void OpenGLShader::SetFloat(const std::string& name, float value)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform1f(location, value);
}

void OpenGLShader::SetFloat3(const std::string& name, float x, float y, float z)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform3f(location, x, y, z);
}

void OpenGLShader::SetFloat4(const std::string& name, float x, float y, float z, float w)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform4f(location, x, y, z, w);
}

}