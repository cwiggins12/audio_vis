#pragma once

#include <glad/glad.h>
#include <iostream>
#include <cstring>

struct SSBO {
    GLuint id   = 0;
    float* ptr  = nullptr;
    size_t size = 0;

    SSBO() = default;

    // no copying
    SSBO(const SSBO&) = delete;
    SSBO& operator=(const SSBO&) = delete;

    // move
    SSBO(SSBO&& o) noexcept : id(o.id), ptr(o.ptr), size(o.size) {
        o.id = 0; o.ptr = nullptr; o.size = 0;
    }
    SSBO& operator=(SSBO&& o) noexcept {
        if (this != &o) {
            free();
            id = o.id; ptr = o.ptr; size = o.size;
            o.id = 0; o.ptr = nullptr; o.size = 0;
        }
        return *this;
    }

    ~SSBO() {
        free();
    }

    void alloc(size_t bytes) {
        if (bytes == 0) return;
        size = bytes;
        glGenBuffers(1, &id);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        glBufferStorageEXT(GL_SHADER_STORAGE_BUFFER, bytes, nullptr,
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT);
        ptr = (float*)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes,
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        if (!ptr) {
            std::cerr << "SSBO: glMapBufferRange returned nullptr\n";
        }
    }

    void bind(int slot) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, id);
    }

    void write(const float* src, size_t bytes) {
        if (ptr && src && bytes <= size) {
            std::memcpy(ptr, src, bytes);
        }
    }

    void free() {
        if (id) {
            glFinish();
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            glDeleteBuffers(1, &id);
            id   = 0;
            ptr  = nullptr;
            size = 0;
        }
    }

    void resize(size_t bytes) {
        if (bytes == 0) return;
        free();
        alloc(bytes);
    }

    void fill(float value) {
        if (ptr && size > 0) {
            size_t count = size / sizeof(float);
            for (size_t i = 0; i < count; i++) {
                ptr[i] = value;
            }
        }
    }
};
