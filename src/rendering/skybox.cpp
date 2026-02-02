#include "skybox.h"
#include <utils/shaders/shader_utils.h>
#include "../core/resources.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace Skybox {
    static GLuint skyboxVAO = 0;
    static GLuint skyboxVBO = 0;
    static GLuint skyboxShader = 0;
    static float timeOfDay = 0.5f; // Noon by default

    // Cube vertices for skybox
    static float skyboxVertices[] = {
        // positions          
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };

    bool Initialize() {
        // Load skybox shader
        if (!LoadShaderProgram(Resources::Shaders::Skybox::VERTEX, 
                              Resources::Shaders::Skybox::FRAGMENT, 
                              skyboxShader)) {
            std::cerr << "Failed to load skybox shader" << std::endl;
            return false;
        }

        // Create VAO and VBO
        glGenVertexArrays(1, &skyboxVAO);
        glGenBuffers(1, &skyboxVBO);
        glBindVertexArray(skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);

        std::cout << "Skybox initialized successfully" << std::endl;
        return true;
    }

    void Cleanup() {
        if (skyboxVAO) {
            glDeleteVertexArrays(1, &skyboxVAO);
            glDeleteBuffers(1, &skyboxVBO);
        }
        if (skyboxShader) {
            glDeleteProgram(skyboxShader);
        }
    }

    void Render(const glm::mat4& view, const glm::mat4& projection) {
        // Change depth function so depth test passes when values are equal to depth buffer's content
        glDepthFunc(GL_LEQUAL);
        
        glUseProgram(skyboxShader);
        
        // Remove translation from view matrix
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "view"), 1, GL_FALSE, glm::value_ptr(viewNoTranslation));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform1f(glGetUniformLocation(skyboxShader, "timeOfDay"), timeOfDay);

        // Render cube
        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        
        // Set depth function back to default
        glDepthFunc(GL_LESS);
    }

    GLuint GetShader() {
        return skyboxShader;
    }

    void SetTimeOfDay(float time) {
        timeOfDay = time;
    }
}
