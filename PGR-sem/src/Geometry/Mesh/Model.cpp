module;
#include <gl_core_4_4.h>
#include <limits>
#include <vector>
module Model;
import Logger.gl;

Model::~Model() {
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
}

//TODO refactor
void Model::uploadGeometry(const void* vertexData, int vertexBytes,
    const unsigned int* indices, int numIndices) {
    
    // Resset existing buffers
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo);      vbo = 0; }
    if (ebo) { glDeleteBuffers(1, &ebo);      ebo = 0; }
    
    indexCount = numIndices;


    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    
    glBufferData(GL_ARRAY_BUFFER, vertexBytes, vertexData, GL_STATIC_DRAW);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, numIndices * sizeof(unsigned int), indices, GL_STATIC_DRAW);

    // 0=position (vec3), 1=normal (vec3), 2=texcoord (vec2), 3=tangent+sign (vec4)
    constexpr int stride = 12 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));

    glBindVertexArray(0);
}


void Model::drawGeometry() const {
    if (!vao) {
        logWarning("Model::drawGeometry: no geometry to draw");
        return;
    }
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
}


void Model::setRenderMode(RenderMode mode) {
    if (mode == RenderMode::Auto) {
        renderMode = material && material->isTransparent() ? RenderMode::Forward : RenderMode::Deferred;
    } else
        renderMode = mode;
}

BoundingBox Model::getWorldBounds() const {
    const glm::vec3& lo = localBounds.min;
    const glm::vec3& hi = localBounds.max;

    const glm::vec3 corners[8] = {
        glm::vec3(lo.x, lo.y, lo.z), glm::vec3(hi.x, lo.y, lo.z),
        glm::vec3(lo.x, hi.y, lo.z), glm::vec3(hi.x, hi.y, lo.z),
        glm::vec3(lo.x, lo.y, hi.z), glm::vec3(hi.x, lo.y, hi.z),
        glm::vec3(lo.x, hi.y, hi.z), glm::vec3(hi.x, hi.y, hi.z),
    };

    constexpr float fmax = std::numeric_limits<float>::max();
    glm::vec3 bmin( fmax,  fmax,  fmax);
    glm::vec3 bmax(-fmax, -fmax, -fmax);

    for (const auto& c : corners) {
        glm::vec3 wc = glm::vec3(modelMatrix * glm::vec4(c, 1.0f));
        bmin = glm::min(bmin, wc);
        bmax = glm::max(bmax, wc);
    }
    return { bmin, bmax };
}
