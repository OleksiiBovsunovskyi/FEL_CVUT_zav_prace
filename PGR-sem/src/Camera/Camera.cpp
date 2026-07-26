module;
#include <glm/gtc/matrix_transform.hpp>
module Camera;
import Model;
import boundingBox;

Camera::Camera(const glm::vec3& pos, const glm::vec3& tgt, const glm::vec3& upDir,
               float fov, float aspect, float near, float far)
    : position(pos), target(tgt), up(upDir),
      fov(fov), aspect(aspect), nearPlane(near), farPlane(far) {}


glm::vec3 Camera::getPosition() const { return position; }
glm::vec3 Camera::getTarget() const { return target; }
glm::vec3 Camera::getUp() const { return up; }
float Camera::getFov() const { return fov; }
float Camera::getAspect() const { return aspect; }
float Camera::getNearPlane() const { return nearPlane; }
float Camera::getFarPlane() const { return farPlane; }

void Camera::setPosition(const glm::vec3& pos) { position = pos; }
void Camera::setTarget(const glm::vec3& tgt) { target = tgt; }
void Camera::setUp(const glm::vec3& upDir) { up = upDir; }
void Camera::setFov(float f) { fov = f; }
void Camera::setAspect(float a) { aspect = a; }
void Camera::setNearPlane(float n) { nearPlane = n; }
void Camera::setFarPlane(float f) { farPlane = f; }


glm::mat4 Camera::getView() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::getProjection() const {
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}
bool Camera::isInsideFrustum(const Model* mesh) const {
    glm::mat4 vp = getProjection() * getView();
    BoundingBox box = mesh->getWorldBounds(); // world-space AABB

    // Extract 6 frustum planes
    glm::vec4 planes[6];
    for (int i = 0; i < 3; i++) {
        planes[i * 2]     = glm::vec4(vp[0][3] + vp[0][i], vp[1][3] + vp[1][i], vp[2][3] + vp[2][i], vp[3][3] + vp[3][i]);
        planes[i * 2 + 1] = glm::vec4(vp[0][3] - vp[0][i], vp[1][3] - vp[1][i], vp[2][3] - vp[2][i], vp[3][3] - vp[3][i]);
    }

    glm::vec3 min = box.min;
    glm::vec3 max = box.max;

    for (auto & plane : planes) {
        // Normalize the plane
        float len = glm::length(glm::vec3(plane));
        plane /= len;

        // pos vertex
        glm::vec3 p;
        p.x = (plane.x >= 0) ? max.x : min.x;
        p.y = (plane.y >= 0) ? max.y : min.y;
        p.z = (plane.z >= 0) ? max.z : min.z;

        if (glm::dot(glm::vec3(plane), p) + plane.w < 0.0f)
            return false;
    }

    return true;
}