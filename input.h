#pragma once
#include <GLFW/glfw3.h>
#include <Vector.h>

namespace input {
    inline Vector3 get_move(GLFWwindow* window) {
        Vector3 v = {0, 0, 0};
        if (glfwGetKey(window, GLFW_KEY_S))          { v.y -= 1; }
        if (glfwGetKey(window, GLFW_KEY_W))          { v.y += 1; }
        if (glfwGetKey(window, GLFW_KEY_A))          { v.x -= 1; }
        if (glfwGetKey(window, GLFW_KEY_D))          { v.x += 1; }
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) { v.z -= 1; }
        if (glfwGetKey(window, GLFW_KEY_SPACE))      { v.z += 1; }
        if (v) { v.Normalize(); }
        return v;
    }

    inline Vector3 get_rotation(GLFWwindow* window) {
        Vector3 v = {0, 0, 0};
        if (glfwGetKey(window, GLFW_KEY_UP))    { v.x += 1; }
        if (glfwGetKey(window, GLFW_KEY_DOWN))  { v.x -= 1; }
        if (glfwGetKey(window, GLFW_KEY_RIGHT)) { v.z -= 1; }
        if (glfwGetKey(window, GLFW_KEY_LEFT))  { v.z += 1; }
        if (glfwGetKey(window, GLFW_KEY_Q))     { v.y -= 1; }
        if (glfwGetKey(window, GLFW_KEY_E))     { v.y += 1; }
        return v;
    }
}
