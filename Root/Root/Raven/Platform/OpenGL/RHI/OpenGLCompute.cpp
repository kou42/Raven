#include "Raven/Platform/OpenGL/RHI/OpenGLCompute.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <iostream>
#include <limits>

namespace Raven
{
namespace
{

GLint GetInteger(GLenum name)
{
    GLint value = 0;
    glGetIntegerv(name, &value);
    return value;
}

RHIComputeCapabilities QueryCapabilities()
{
    RHIComputeCapabilities caps{};
    if (glfwGetCurrentContext() == nullptr || GLAD_GL_VERSION_4_3 == 0 ||
        glDispatchCompute == nullptr || glMemoryBarrier == nullptr ||
        glBindImageTexture == nullptr || glCopyBufferSubData == nullptr ||
        glFenceSync == nullptr || glTexStorage3D == nullptr ||
        GetInteger(GL_MAJOR_VERSION) * 10 + GetInteger(GL_MINOR_VERSION) < 43)
    {
        return caps;
    }
    caps.Supported = true;
    for (GLuint axis = 0; axis < 3; ++axis)
    {
        GLint groups = 0;
        GLint localSize = 0;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, axis, &groups);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, axis, &localSize);
        caps.MaxGroups[axis] = groups;
        caps.MaxLocalSize[axis] = localSize;
    }
    caps.MaxInvocations = GetInteger(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
    caps.MaxStorageBufferSlots = GetInteger(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS);
    caps.MaxStorageImageSlots = GetInteger(GL_MAX_IMAGE_UNITS);
    caps.MaxStorageTexture3DSize = GetInteger(GL_MAX_3D_TEXTURE_SIZE);
    GLint64 bufferBytes = 0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &bufferBytes);
    caps.MaxStorageBufferBytes = bufferBytes;
    return caps;
}

bool Fits(const RHIComputeExtent& requested, const RHIComputeExtent& limits)
{
    for (std::size_t axis = 0; axis < requested.size(); ++axis)
    {
        if (requested[axis] == 0 || requested[axis] > limits[axis])
        {
            return false;
        }
    }
    return true;
}

// 共有トークンでEncoderの所有範囲を識別します。GLオブジェクトは、生成時の
// Contextがカレントの間に解放する必要があります。
struct ComputeOwner {};

struct OpenGLPipeline final : RHIComputePipeline
{
    ~OpenGLPipeline() override { glDeleteProgram(Program); }
    Ref<ComputeOwner> Owner;
    GLuint Program = 0;
    std::vector<RHIComputeBindingLayout> Layout;
};

struct OpenGLResource final : RHIComputeResource
{
    ~OpenGLResource() override
    {
        if (Type == RHIComputeBindingType::StorageBuffer)
        {
            glDeleteBuffers(1, &Name);
        }
        else
        {
            glDeleteTextures(1, &Name);
        }
    }
    Ref<ComputeOwner> Owner;
    RHIComputeBindingType Type = RHIComputeBindingType::StorageBuffer;
    GLuint Name = 0;
    std::size_t Size = 0;
};

struct OpenGLReadback final : RHIComputeReadback
{
    ~OpenGLReadback() override
    {
        if (Fence != nullptr)
        {
            glDeleteSync(Fence);
        }
        glDeleteBuffers(1, &Staging);
    }

    RHIComputeReadbackStatus Poll(std::vector<std::byte>& data) override
    {
        if (glfwGetCurrentContext() != Context || Failed || Fence == nullptr)
        {
            return RHIComputeReadbackStatus::Failed;
        }
        // コピーを発行したContextでFlushを許可します。待機時間0なので描画スレッドを止めません。
        const GLenum status = glClientWaitSync(Fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (status == GL_TIMEOUT_EXPIRED)
        {
            return RHIComputeReadbackStatus::Pending;
        }
        if (status == GL_WAIT_FAILED)
        {
            Failed = true;
            return RHIComputeReadbackStatus::Failed;
        }
        std::vector<std::byte> copy(Size);
        const GLint prior = GetInteger(GL_COPY_WRITE_BUFFER_BINDING);
        glBindBuffer(GL_COPY_WRITE_BUFFER, Staging);
        glGetBufferSubData(GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(Size), copy.data());
        glBindBuffer(GL_COPY_WRITE_BUFFER, prior);
        if (glGetError() != GL_NO_ERROR)
        {
            Failed = true;
            return RHIComputeReadbackStatus::Failed;
        }
        data = std::move(copy);
        return RHIComputeReadbackStatus::Ready;
    }

    GLFWwindow* Context = nullptr;
    Ref<OpenGLResource> Source;
    GLuint Staging = 0;
    GLsync Fence = nullptr;
    std::size_t Size = 0;
    bool Failed = false;
};

GLuint CompileProgram(const std::string& source)
{
    const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    if (shader == 0)
    {
        return 0;
    }
    const char* code = source.c_str();
    glShaderSource(shader, 1, &code, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(std::max(length, 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        std::cerr << "[OpenGL Compute] Compile failed: " << log << '\n';
        glDeleteShader(shader);
        return 0;
    }
    const GLuint program = glCreateProgram();
    if (program == 0)
    {
        glDeleteShader(shader);
        return 0;
    }
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDetachShader(program, shader);
    glDeleteShader(shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(std::max(length, 1), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        std::cerr << "[OpenGL Compute] Link failed: " << log << '\n';
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

GLenum ImageAccess(RHIComputeAccess access)
{
    switch (access)
    {
    case RHIComputeAccess::Read: return GL_READ_ONLY;
    case RHIComputeAccess::Write: return GL_WRITE_ONLY;
    case RHIComputeAccess::ReadWrite: return GL_READ_WRITE;
    }
    return 0;
}

bool ValidateProgramLayout(GLuint program, const std::vector<RHIComputeBindingLayout>& layout)
{
    std::vector<bool> matched(layout.size(), false);
    const auto match = [&](RHIComputeBindingType type, GLint slot)
    {
        for (std::size_t i = 0; i < layout.size(); ++i)
        {
            if (layout[i].Type == type && slot >= 0 &&
                layout[i].Slot == static_cast<std::uint32_t>(slot) && matched[i] == false)
            {
                matched[i] = true;
                return true;
            }
        }
        return false;
    };
    GLint blocks = 0;
    glGetProgramInterfaceiv(program, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &blocks);
    constexpr GLenum bufferBinding = GL_BUFFER_BINDING;
    for (GLint block = 0; block < blocks; ++block)
    {
        GLint slot = -1;
        glGetProgramResourceiv(program, GL_SHADER_STORAGE_BLOCK, block, 1, &bufferBinding, 1, nullptr, &slot);
        if (match(RHIComputeBindingType::StorageBuffer, slot) == false)
        {
            return false;
        }
    }
    GLint uniforms = 0;
    glGetProgramInterfaceiv(program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &uniforms);
    constexpr GLenum properties[] = {GL_TYPE, GL_ARRAY_SIZE, GL_LOCATION};
    for (GLint uniform = 0; uniform < uniforms; ++uniform)
    {
        GLint values[3]{};
        glGetProgramResourceiv(program, GL_UNIFORM, uniform, 3, properties, 3, nullptr, values);
        if (values[0] != GL_IMAGE_3D || values[1] != 1 || values[2] < 0)
        {
            return false;
        }
        GLint slot = -1;
        glGetUniformiv(program, values[2], &slot);
        if (match(RHIComputeBindingType::StorageImage3D, slot) == false)
        {
            return false;
        }
    }
    return std::all_of(matched.begin(), matched.end(), [](bool found) { return found; });
}

struct OpenGLTimestamp final : RHIComputeTimestamp
{
    GLFWwindow* Context = nullptr;
    GLuint Queries[2]{};
    std::uint64_t Mask = ~std::uint64_t{0};

    ~OpenGLTimestamp() override
    {
        if (glfwGetCurrentContext() == Context)
        {
            glDeleteQueries(2, Queries);
        }
    }

    RHIComputeReadbackStatus Poll(double& nanoseconds) override
    {
        if (glfwGetCurrentContext() != Context)
        {
            return RHIComputeReadbackStatus::Failed;
        }
        GLint ready = GL_FALSE;
        glGetQueryObjectiv(Queries[1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready != GL_TRUE)
        {
            return RHIComputeReadbackStatus::Pending;
        }
        GLuint64 start = 0;
        GLuint64 end = 0;
        glGetQueryObjectui64v(Queries[0], GL_QUERY_RESULT, &start);
        glGetQueryObjectui64v(Queries[1], GL_QUERY_RESULT, &end);
        nanoseconds = static_cast<double>((end - start) & Mask);
        return RHIComputeReadbackStatus::Ready;
    }
};

class OpenGLComputeCommandEncoder final : public RHIComputeCommandEncoder
{
public:
    explicit OpenGLComputeCommandEncoder(RHIComputeCapabilities caps)
        : m_Context(glfwGetCurrentContext()), m_Caps(caps)
    {
    }

    Ref<RHIComputePipeline> CreatePipeline(const RHIComputePipelineSpecification& spec) override
    {
        if (IsCurrent() == false || spec.OpenGLSource.empty() ||
            Fits(spec.LocalSize, m_Caps.MaxLocalSize) == false ||
            std::uint64_t{spec.LocalSize[0]} * spec.LocalSize[1] * spec.LocalSize[2] > m_Caps.MaxInvocations)
        {
            return nullptr;
        }
        for (std::size_t i = 0; i < spec.Layout.size(); ++i)
        {
            const auto& binding = spec.Layout[i];
            std::uint32_t limit = 0;
            switch (binding.Type)
            {
            case RHIComputeBindingType::StorageBuffer: limit = m_Caps.MaxStorageBufferSlots; break;
            case RHIComputeBindingType::StorageImage3D: limit = m_Caps.MaxStorageImageSlots; break;
            }
            const auto prior = spec.Layout.begin() + i;
            if (binding.Slot >= limit || ImageAccess(binding.Access) == 0 ||
                std::any_of(spec.Layout.begin(), prior,
                    [&binding](const auto& entry) { return entry.Slot == binding.Slot; }))
            {
                return nullptr;
            }
        }
        auto result = CreateRef<OpenGLPipeline>();
        result->Owner = m_Owner;
        result->Layout = spec.Layout;
        result->Program = CompileProgram(spec.OpenGLSource);
        if (result->Program == 0)
        {
            return nullptr;
        }
        GLint size[3]{};
        glGetProgramiv(result->Program, GL_COMPUTE_WORK_GROUP_SIZE, size);
        if (RHIComputeExtent{static_cast<std::uint32_t>(size[0]),
            static_cast<std::uint32_t>(size[1]), static_cast<std::uint32_t>(size[2])} != spec.LocalSize ||
            ValidateProgramLayout(result->Program, spec.Layout) == false)
        {
            std::cerr << "[OpenGL Compute] Workgroup size or binding layout mismatch\n";
            return nullptr;
        }
        ++m_Statistics.Pipelines;
        return result;
    }

    bool MeasureGpuWork(const FrameRecorder& work, Ref<RHIComputeTimestamp>& timestamp) override
    {
        timestamp.reset();
        if (IsCurrent() == false || glGetQueryiv == nullptr || glGenQueries == nullptr ||
            glDeleteQueries == nullptr || glGetQueryObjectiv == nullptr || glQueryCounter == nullptr ||
            glGetQueryObjectui64v == nullptr)
        {
            return work(*this);
        }
        GLint bits = 0;
        glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
        if (bits <= 0)
        {
            return work(*this);
        }
        auto result = CreateRef<OpenGLTimestamp>();
        result->Context = m_Context;
        result->Mask = bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
        glGenQueries(2, result->Queries);
        if (result->Queries[0] == 0 || result->Queries[1] == 0)
        {
            return work(*this);
        }
        glQueryCounter(result->Queries[0], GL_TIMESTAMP);
        const bool success = work(*this);
        glQueryCounter(result->Queries[1], GL_TIMESTAMP);
        glFlush();
        if (success)
        {
            timestamp = std::move(result);
        }
        return success;
    }

    Ref<RHIComputeResource> CreateStorageBuffer(std::size_t size, const void* initialData) override
    {
        if (IsCurrent() == false || size == 0 || size > m_Caps.MaxStorageBufferBytes ||
            size > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()))
        {
            return nullptr;
        }
        auto resource = CreateRef<OpenGLResource>();
        resource->Owner = m_Owner;
        resource->Size = size;
        const GLint prior = GetInteger(GL_COPY_WRITE_BUFFER_BINDING);
        glGenBuffers(1, &resource->Name);
        glBindBuffer(GL_COPY_WRITE_BUFFER, resource->Name);
        glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(size), initialData, GL_DYNAMIC_COPY);
        glBindBuffer(GL_COPY_WRITE_BUFFER, prior);
        if (resource->Name == 0 || glGetError() != GL_NO_ERROR)
        {
            return nullptr;
        }
        ++m_Statistics.Buffers;
        m_Statistics.BufferBytes += size;
        return resource;
    }

    Ref<RHIComputeResource> CreateStorageImage3D(const RHIComputeExtent& size) override
    {
        const auto limit = m_Caps.MaxStorageTexture3DSize;
        if (IsCurrent() == false || Fits(size, {limit, limit, limit}) == false)
        {
            return nullptr;
        }
        auto resource = CreateRef<OpenGLResource>();
        resource->Owner = m_Owner;
        resource->Type = RHIComputeBindingType::StorageImage3D;
        const GLint prior = GetInteger(GL_TEXTURE_BINDING_3D);
        glGenTextures(1, &resource->Name);
        glBindTexture(GL_TEXTURE_3D, resource->Name);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_R32F, size[0], size[1], size[2]);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, prior);
        if (resource->Name == 0 || glGetError() != GL_NO_ERROR)
        {
            return nullptr;
        }
        ++m_Statistics.Images;
        m_Statistics.ImageBytes += static_cast<std::uint64_t>(size[0]) * size[1] * size[2] * sizeof(float);
        return resource;
    }

    bool Dispatch(const Ref<RHIComputePipeline>& pipeline, std::span<const RHIComputeBinding> bindings,
        const RHIComputeExtent& groups) override
    {
        const auto native = std::dynamic_pointer_cast<OpenGLPipeline>(pipeline);
        if (IsCurrent() == false || native == nullptr || native->Owner != m_Owner ||
            bindings.size() != native->Layout.size() || Fits(groups, m_Caps.MaxGroups) == false)
        {
            return false;
        }
        std::vector<Ref<OpenGLResource>> resources;
        resources.reserve(bindings.size());
        for (const auto& layout : native->Layout)
        {
            const auto binding = std::find_if(bindings.begin(), bindings.end(),
                [&layout](const auto& entry) { return entry.Slot == layout.Slot; });
            if (binding == bindings.end())
            {
                return false;
            }
            const auto resource = std::dynamic_pointer_cast<OpenGLResource>(binding->Resource);
            if (resource == nullptr || resource->Owner != m_Owner || resource->Type != layout.Type)
            {
                return false;
            }
            // 初期実装では同じResourceを単一Dispatchの複数Slotへ割り当てません。
            if (std::find(resources.begin(), resources.end(), resource) != resources.end())
            {
                return false;
            }
            resources.push_back(resource);
        }
        const GLint program = GetInteger(GL_CURRENT_PROGRAM);
        const GLint storageBuffer = GetInteger(GL_SHADER_STORAGE_BUFFER_BINDING);
        glUseProgram(native->Program);
        for (std::size_t i = 0; i < resources.size(); ++i)
        {
            Bind(native->Layout[i], resources[i]->Name);
        }
        const bool bound = glGetError() == GL_NO_ERROR;
        if (bound == true)
        {
            glDispatchCompute(groups[0], groups[1], groups[2]);
        }
        // Storageのindexed bindingはCompute専用とし、Graphicsが共有するProgramと
        // 汎用Buffer bindingを復元します。
        for (const auto& layout : native->Layout)
        {
            Bind(layout, 0);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, storageBuffer);
        glUseProgram(program);
        const bool restored = glGetError() == GL_NO_ERROR;
        if (bound && restored)
        {
            ++m_Statistics.Dispatches;
            return true;
        }
        return false;
    }

    bool Barrier(RHIComputeConsumer consumer) override
    {
        if (IsCurrent() == false)
        {
            return false;
        }
        GLbitfield bits = 0;
        switch (consumer)
        {
        case RHIComputeConsumer::Storage:
            bits = GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
            break;
        case RHIComputeConsumer::TextureSampling: bits = GL_TEXTURE_FETCH_BARRIER_BIT; break;
        case RHIComputeConsumer::BufferReadback: bits = GL_BUFFER_UPDATE_BARRIER_BIT; break;
        }
        if (bits == 0)
        {
            return false;
        }
        glMemoryBarrier(bits);
        return glGetError() == GL_NO_ERROR;
    }

    Ref<RHIComputeReadback> RequestBufferReadback(const Ref<RHIComputeResource>& resource,
        std::size_t offset, std::size_t size) override
    {
        auto source = std::dynamic_pointer_cast<OpenGLResource>(resource);
        if (IsCurrent() == false || source == nullptr || source->Owner != m_Owner ||
            source->Type != RHIComputeBindingType::StorageBuffer ||
            size == 0 || offset > source->Size || size > source->Size - offset)
        {
            return nullptr;
        }
        const GLint priorRead = GetInteger(GL_COPY_READ_BUFFER_BINDING);
        const GLint priorWrite = GetInteger(GL_COPY_WRITE_BUFFER_BINDING);
        auto result = CreateRef<OpenGLReadback>();
        result->Context = m_Context;
        result->Size = size;
        result->Source = source;
        glGenBuffers(1, &result->Staging);
        glBindBuffer(GL_COPY_READ_BUFFER, source->Name);
        glBindBuffer(GL_COPY_WRITE_BUFFER, result->Staging);
        glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_STREAM_READ);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
            static_cast<GLintptr>(offset), 0, static_cast<GLsizeiptr>(size));
        result->Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glBindBuffer(GL_COPY_READ_BUFFER, priorRead);
        glBindBuffer(GL_COPY_WRITE_BUFFER, priorWrite);
        if (result->Fence == nullptr || glGetError() != GL_NO_ERROR)
        {
            return nullptr;
        }
        ++m_Statistics.Readbacks;
        m_Statistics.ReadbackBytes += size;
        return result;
    }

private:
    bool IsCurrent() const { return glfwGetCurrentContext() == m_Context; }

    static void Bind(const RHIComputeBindingLayout& layout, GLuint name)
    {
        if (layout.Type == RHIComputeBindingType::StorageBuffer)
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, layout.Slot, name);
        }
        else
        {
            glBindImageTexture(layout.Slot, name, 0, GL_TRUE, 0, ImageAccess(layout.Access), GL_R32F);
        }
    }

    GLFWwindow* m_Context;
    RHIComputeCapabilities m_Caps;
    Ref<ComputeOwner> m_Owner = CreateRef<ComputeOwner>();
};

} // namespace

RHIComputeCapabilities OpenGLRHIDevice::QueryComputeCapabilities() const
{
    return QueryCapabilities();
}

Ref<RHIComputeCommandEncoder> OpenGLRHIDevice::CreateComputeCommandEncoder()
{
    const auto caps = QueryCapabilities();
    return caps.Supported ? CreateRef<OpenGLComputeCommandEncoder>(caps) : nullptr;
}

std::uint32_t GetOpenGLComputeTextureID(const Ref<RHIComputeResource>& resource)
{
    const auto native = std::dynamic_pointer_cast<OpenGLResource>(resource);
    return native != nullptr && native->Type == RHIComputeBindingType::StorageImage3D ? native->Name : 0;
}

std::uint32_t GetOpenGLComputeBufferID(const Ref<RHIComputeResource>& resource)
{
    const auto native = std::dynamic_pointer_cast<OpenGLResource>(resource);
    return native != nullptr && native->Type == RHIComputeBindingType::StorageBuffer ? native->Name : 0;
}

} // namespace Raven
