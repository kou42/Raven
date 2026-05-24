#pragma once

#include <string>
#include "../../Core/Base.h"

namespace Raven
{

class Texture
{

public:

    static Ref<Texture> Create(const std::string& path);

    Texture(const std::string& path);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;

    unsigned int GetID() const;
    
private:
    unsigned int m_ID;
    int m_Width;
    int m_Height;
    int m_Channels;

};

}