#pragma once

#include <glad/glad.h>
#include "config/globals.hpp"

class UBO {
public:
    GLuint id = 0;

    UBO() {
        glGenBuffers(1, &id);
        glBindBuffer(GL_UNIFORM_BUFFER, id);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(Globals), nullptr, GL_DYNAMIC_DRAW);
        //remember to change this is you want multiple, just make a function that
        //takes a GLuint binding point that replaces 0 and just calls the line below
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, id);
    }

    ~UBO() { if (id) glDeleteBuffers(1, &id); }

    // No copy, no move
    UBO(const UBO&) = delete;
    UBO& operator=(const UBO&) = delete;
    UBO(UBO&& o) = delete;
    UBO& operator=(UBO&& o) = delete;

    void update(const Globals& data) {
        glBindBuffer(GL_UNIFORM_BUFFER, id);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Globals), &data);
    }
};

