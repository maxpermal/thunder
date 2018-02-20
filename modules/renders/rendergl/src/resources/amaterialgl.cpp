#include "resources/amaterialgl.h"

#include "agl.h"

#include "apipeline.h"
#include "resources/text.h"
#include "resources/atexturegl.h"

#include "components/staticmesh.h"

#include <file.h>
#include <log.h>

#include <regex>
#include <sstream>

#include <timer.h>

const regex include("^[ ]*#[ ]*include[ ]+[\"<](.*)[\">][^?]*");
const regex pragma("^[ ]*#[ ]*pragma[ ]+(.*)[^?]*");

const char *gPost       = "PostEffect.frag";
const char *gSurface    = "Surface.frag";
const char *gLight      = "DirectLight.frag";
const char *gVertex     = "BasePass.vert";

const string gEmbedded(".embedded/");

AMaterialGL::AMaterialGL() :
        Material() {

}

AMaterialGL::~AMaterialGL() {

}

void AMaterialGL::loadUserData(const AVariantMap &data) {
    Material::loadUserData(data);

    auto it = data.find("Shader");
    if(it == data.end()) {
        return ;
    }

    addPragma("material", (*it).second.toString());

    ProgramMap fragments;
    // Number of shader pairs calculation
    switch(m_MaterialType) {
        case PostProcess: {
            m_DoubleSided   = true;
            m_Tangent       = false;
            m_BlendMode     = Opaque;
            m_LightModel    = Unlit;
            m_Surfaces      = Static;

            fragments[0]    = buildShader(Fragment, loadIncludes(gEmbedded + gPost), gEmbedded + gPost);
        } break;
        case LightFunction: {
            m_DoubleSided   = true;
            m_Tangent       = false;
            m_BlendMode     = Opaque;
            m_LightModel    = Unlit;
            m_Surfaces      = Static;

            fragments[0]    = buildShader(Fragment, loadIncludes(gEmbedded + gLight), gEmbedded + gPost);
        } break;
        default: { // Surface type
            string define;
            switch(m_BlendMode) {
                case Additive: {
                    define  = "#define BLEND_ADDITIVE 1";
                } break;
                case Translucent: {
                    define  = "#define BLEND_TRANSLUCENT 1";
                } break;
                default: {
                    define  = "#define BLEND_OPAQUE 1";
                } break;
            }
            switch(m_LightModel) {
                case Lit: {
                    define += "\n#define MODEL_LIT 1";
                } break;
                case Subsurface: {
                    define += "\n#define MODEL_SUBSURFACE 1";
                } break;
                default: {
                    define += "\n#define MODEL_UNLIT 1";
                } break;
            }
            if(m_Tangent) {
                define += "\n#define TANGENT 1";
            }

            fragments[0]        = buildShader(Fragment, loadIncludes(gEmbedded + gSurface, define), gEmbedded + gPost);
            define += "\n#define SIMPLE 1";
            fragments[Simple]   = buildShader(Fragment, loadIncludes(gEmbedded + gSurface, define), gEmbedded + gPost);
            // if cast shadows
            fragments[Depth]    = buildShader(Fragment, loadIncludes(gEmbedded + gSurface, define + "\n#define DEPTH 1"), gEmbedded + gPost);
        } break;
    }

    if(m_Surfaces & Static) {
        for(auto it : fragments) {
            m_Programs[Static | it.first]  = buildProgram(it.second, "#define TYPE_STATIC 1");
        }
    }
    if(m_Surfaces & Skinned) {
        for(auto it : fragments) {
            m_Programs[Skinned | it.first]  = buildProgram(it.second, "#define TYPE_SKINNED 1");
        }
    }
    if(m_Surfaces & Billboard) {
        for(auto it : fragments) {
            m_Programs[Billboard | it.first]    = buildProgram(it.second, "#define TYPE_BILLBOARD 1");
        }
    }
    if(m_Surfaces & Oriented) {
        for(auto it : fragments) {
            m_Programs[Oriented | it.first]     = buildProgram(it.second, "#define TYPE_AXISALIGNED 1");
        }
    }

    for(auto program : m_Programs) {
        uint8_t t   = 0;
        for(auto it : m_Textures) {
            int location    = glGetUniformLocation(program.second, it.first.c_str());
            if(location > -1) {
                glProgramUniform1i(program.second, location, t);
            }
            t++;
        }
    }
}

uint32_t AMaterialGL::getProgram(uint16_t type) const {
    auto it = m_Programs.find(type);
    if(it != m_Programs.end()) {
        return it->second;
    }
    return 0;
}

bool AMaterialGL::bind(APipeline &pipeline, uint8_t layer, uint16_t type) {
    uint8_t b   = blendMode();

    if((layer & IDrawObjectGL::DEFAULT || layer & IDrawObjectGL::SHADOWCAST || layer & IDrawObjectGL::RAYCAST) &&
       (b == Material::Additive || b == Material::Translucent)) {
        return false;
    }
    if(layer & IDrawObjectGL::TRANSLUCENT && b == Material::Opaque) {
        return false;
    }

    switch(layer) {
        case IDrawObjectGL::RAYCAST:    {
            type   |= AMaterialGL::Simple;
        } break;
        case IDrawObjectGL::SHADOWCAST: {
            type   |= AMaterialGL::Depth;
        } break;
        default: break;
    }
    uint32_t program    = getProgram(type);
    if(!program) {
        return false;
    }

    pipeline.setShaderParams(program);

    int location    = -1;
    location        = glGetUniformLocation(program, "_time");
    if(location > -1) {
        glProgramUniform1f(program, location, Timer::time());
    }
    // Push uniform values to shader
    for(const auto &it : mUniforms) {
        location    = glGetUniformLocation(program, it.first.c_str());
        if(location > -1) {
            const AVariant &data= it.second;
            switch(data.type()) {
                case AMetaType::VECTOR2:    glProgramUniform2fv(program, location, 1, data.toVector2().v); break;
                case AMetaType::VECTOR3:    glProgramUniform3fv(program, location, 1, data.toVector3().v); break;
                case AMetaType::VECTOR4:    glProgramUniform4fv(program, location, 1, data.toVector4().v); break;
                default:                    glProgramUniform1f (program, location, data.toDouble()); break;
            }
        }
    }

    if(!isDoubleSided()) {
        glEnable    ( GL_CULL_FACE );
        glCullFace  ( GL_BACK );
    }

    uint8_t blend   = blendMode();
    if(blend != Material::Opaque) {
        glEnable    ( GL_BLEND );
        if(blend == Material::Translucent) {
            glBlendFunc ( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA ); // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
        } else {
            glBlendFunc ( GL_SRC_ALPHA, GL_ONE );
        }
        glBlendEquation(GL_FUNC_ADD);
    }

    glEnable(GL_TEXTURE_2D);
    uint8_t t   = 0;//(layer & IDrawObjectGL::DEFAULT) ? 0 : 1;
    for(auto it : m_Textures) {
        int location    = glGetUniformLocation(program, it.first.c_str());
        if(location > -1) {
            glProgramUniform1i(program, location, t);
        }

        ATextureGL *texture = static_cast<ATextureGL *>(it.second);
        glActiveTexture(GL_TEXTURE0 + t);
        if(texture) {
            texture->bind();
        }
        t++;
    }

    glUseProgram(program);

    return true;
}

void AMaterialGL::unbind(uint8_t layer) {
    uint8_t t   = 0;//(layer & IDrawObjectGL::DEFAULT) ? 0 : 1;
    for(auto it : m_Textures) {
        ATextureGL *texture = static_cast<ATextureGL *>(it.second);
        glActiveTexture(GL_TEXTURE0 + t);
        if(texture) {
            texture->unbind();
        }
        t++;
    }

    glDisable(GL_TEXTURE_2D);

    glUseProgram(0);

    uint8_t blend   = blendMode();
    if(blend == Material::Additive || blend == Material::Translucent) {
        glDisable   ( GL_BLEND );
    }

    glDisable( GL_CULL_FACE );
}

void AMaterialGL::clear() {
    Material::clear();

    m_Pragmas.clear();
    addPragma("version", "#version 450");

    for(auto it : m_Programs) {
        glDeleteProgram(it.second);
    }
    m_Programs.clear();
}

uint32_t AMaterialGL::buildShader(uint8_t type, const string &src, const string &path) {
    uint32_t shader;
    switch(type) {
        case Vertex:    shader  = glCreateShader(GL_VERTEX_SHADER);   break;
        case Geometry:  shader  = glCreateShader(GL_GEOMETRY_SHADER); break;
        default:        shader  = glCreateShader(GL_FRAGMENT_SHADER); break;
    }
    const char *data    = src.c_str();

    if(shader) {
        glShaderSource  (shader, 1, &data, NULL);
        glCompileShader (shader);
        bool result = true;
        result     &= checkShader(shader, path);
        if(result) {
            return shader;
        }
        glDeleteShader(shader);
    }
    return 0;
}

uint32_t AMaterialGL::buildProgram(uint32_t fragment, const string &define) {
    uint32_t vertex     = buildShader(Vertex, loadIncludes(gEmbedded + gVertex, define));

    if(fragment && vertex) {
        uint32_t program    = glCreateProgram();
        if(program) {
            glAttachShader  (program, vertex);
            glAttachShader  (program, fragment);

            glLinkProgram   (program);
            checkShader(program, string(), true);

            glDetachShader  (program, fragment);
            glDetachShader  (program, vertex);

            uint8_t t   = 0;
            for(auto it : m_Textures) {
                int location    = glGetUniformLocation(program, it.first.c_str());
                if(location > -1) {
                    glProgramUniform1i(program, location, (t + 1));
                }
                t++;
            }
            return program;
        }
    }
    return 0;
}

bool AMaterialGL::checkShader(uint32_t shader, const string &path, bool link) {
    int value   = 0;

    if(!link) {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &value);
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &value);
    }

    if(value != GL_TRUE) {
        if(!link) {
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &value);
        } else {
            glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &value);
        }
        if(value) {
            char *buff  = new char[value + 1];
            if(!link) {
                glGetShaderInfoLog(shader, value, NULL, buff);
            } else {
                glGetProgramInfoLog(shader, value, NULL, buff);
            }
            Log(Log::ERR) << "[ Render::ShaderGL ]" << path.c_str() << "\n[ Said ]" << buff;
            delete []buff;
        } else {
            //m_Valid = false;
        }
        return false;
    }
    return true;
}

void AMaterialGL::addPragma(const string &key, const string &value) {
    m_Pragmas[key]  = m_Pragmas[key].append(value).append("\r\n");
}

string AMaterialGL::parseData(const string &data, const string &define) {
    stringstream input;
    stringstream output;
    input << data;

    string line;
    while( getline(input, line) ) {
        smatch matches;
        if(regex_match(line, matches, include)) {
            output << loadIncludes(string(matches[1]), define) << endl;
        } else if(regex_match(line, matches, pragma)) {
            if(matches[1] == "flags") {
                output << define << endl;
            } else {
                auto it = m_Pragmas.find(matches[1]);
                if(it != m_Pragmas.end()) {
                    output << m_Pragmas[matches[1]] << endl;
                }
            }
        } else {
            output << line << endl;
        }
    }
    return output.str();
}

string AMaterialGL::loadIncludes(const string &path, const string &define) {
    Text *text  = Engine::loadResource<Text>(path);
    if(text) {
        return parseData(text->text(), define);
    }

    return string();
}