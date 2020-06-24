#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stack>
#include <string>
#include <vector>
#include <limits>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <tiny_obj_loader.h>
#include "utils.h"
#include "matrices.h"
#include "wiicpp.h"

// Object data loaded from wavefront model
struct ObjModel
{
    tinyobj::attrib_t                 attrib;
    std::vector<tinyobj::shape_t>     shapes;
    std::vector<tinyobj::material_t>  materials;

    ObjModel(const char* filename, const char* basepath = NULL, bool triangulate = true)
    {
        printf("Loading Model \"%s\"... ", filename);

        std::string err;
        bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filename, basepath, triangulate);

        if (!err.empty())
            fprintf(stderr, "\n%s\n", err.c_str());

        if (!ret)
            throw std::runtime_error("Error loading model");

        printf("OK.\n");
    }
};

// ObjModel building and drawing functions
void BuildTrianglesAndAddToVirtualScene(ObjModel*); // Builds ObjModel as a triangle mesh for rastering
void ComputeNormals(ObjModel* model); // Computes normals for ObjModel in case they do not exist
void DrawVirtualObject(const char* object_name); // Draws an object from g_VirtualScene
void PrintObjModelInfo(ObjModel*); // Prints information about an ObjModel (DEBUG)

// Shader functions
void LoadShadersFromFiles(); // Load vertex and fragment shaders
GLuint LoadShader_Vertex(const char* filename);   // Loads a vertex shader
GLuint LoadShader_Fragment(const char* filename); // Loads a fragment shader
void LoadShader(const char* filename, GLuint shader_id);
GLuint CreateGpuProgram(GLuint vertex_shader_id, GLuint fragment_shader_id); // Creates a GPU program from loaded shaders

// Text rendering functions
void TextRendering_Init();
float TextRendering_LineHeight(GLFWwindow* window);
float TextRendering_CharWidth(GLFWwindow* window);
void TextRendering_PrintString(GLFWwindow* window, const std::string &str, float x, float y, float scale = 1.0f);
void TextRendering_ShowFramesPerSecond(GLFWwindow* window);

// Callback functions for user and operating system interaction
void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
void ErrorCallback(int error, const char* description);
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);

// Wiimote related functions
int ConnectWiimotes();
void ControllerHandlerThread();
void HandleEvent(CWiimote &wm);

// Struct containing data for rendering and object
struct SceneObject
{
    std::string  name;        // Object name
    size_t       first_index; // Index of the first vertex in index[]
    size_t       num_indices; // Amount of vertex indices in index[]
    GLenum       rendering_mode; // Rastering mode (GL_TRIANGLES, GL_TRIANGLE_STRIP, etc.)
    GLuint       vertex_array_object_id; // Vertex Array Object ID with model attributes
};

// Struct containing placement information for an instance of an object
struct PlacedObject
{

    // Angle change speed  10 *  sr / fps
    #define ROTATION_SPEED 10 * 100 / 60
    // Movement speed
    #define MOVEMENT_SPEED 10 * 100 / 60

    std::string obj_name;
    float positionX, positionY, positionZ; // Global object position
    float scaleX, scaleY, scaleZ; // Global object scale
    glm::quat quaternion; // Orientation quaternion

    void SetOrientation(float yaw, float roll, float pitch)
    {
        quaternion = glm::quat(glm::vec3(yaw,roll,pitch));
    }

    void UpdateOrientation(float yaw, float roll, float pitch, float delta_t)
    {
        yaw   = yaw   * ROTATION_SPEED * delta_t;
        roll  = roll  * ROTATION_SPEED * delta_t;
        pitch = pitch * ROTATION_SPEED * delta_t;
        glm::quat new_quaternion = glm::quat(glm::vec3(yaw,roll,pitch));
        quaternion = quaternion * new_quaternion;
    }   

    void SetPosition(float x, float y, float z)
    {
        positionX = x;
        positionY = y;
        positionZ = z;
    }

    void UpdatePosition(float x, float y, float z, float delta_t)
    {
        x = x * MOVEMENT_SPEED * delta_t;
        y = y * MOVEMENT_SPEED * delta_t;
        z = z * MOVEMENT_SPEED * delta_t;
        SetPosition(positionX + x, positionY + y, positionZ + z);
    }

};

// Struct containing data for the camera controlled by the user
struct Camera
{
    // Camera rotation angles
    float cameraTheta, cameraPhi;
    // Camera position
    glm::vec4 camera_position;
    // Camera up vector (Points up on global Y)
    glm::vec4 camera_up_vector;
    // Camera View Vector
    glm::vec4 camera_view_vector;
    // Camera speed
    float camera_speed;
};

// Struct containing data for the real wiimote sensors
struct WiiData
{

    // Window sizes for moving average smoothing
    #define GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE 8
    #define ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE 8

    CWii wii; // Wii instance
    int connectedWiimotes; // Connected wiimote count
    float gyroReadings[GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE][3]; // Last gyroscope readings
    int gyroReadingsIndex;
    float accelReadings[ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE][3]; // Last accelerometer readings
    int accelReadingsIndex;

    WiiData()
    {
        // Initialize readings with 0
        for (int i=0; i < GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE; ++i)
            for (int j=0; j < 3; ++j)
                gyroReadings[i][j] = 0.0f;
        
        for (int i=0; i < ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE; ++ i)
            for (int j=0; j < 3; ++j)
                accelReadings[i][j] = 0.0f;

        // Initialize reading index and wiimote count to 0
        gyroReadingsIndex  = 0;
        accelReadingsIndex = 0;
        connectedWiimotes  = 0;
    }

    // Update gyroscope readings with provided values
    void UpdateGyro(float yaw, float roll, float pitch)
    {
        // Save readings as last
        gyroReadings[gyroReadingsIndex][0] = yaw;
        gyroReadings[gyroReadingsIndex][1] = roll;
        gyroReadings[gyroReadingsIndex][2] = pitch;

        // Update readings index
        if (++gyroReadingsIndex == GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE) gyroReadingsIndex = 0;
    }

    // Returns the average yaw roll and pitch as provided by the moving average smoothing
    void GetAvgGyroValues(float* yaw, float* roll, float* pitch)
    {
        // Reset received variables
        *yaw   = 0;
        *roll  = 0;
        *pitch = 0;

        // Calculate average
        for (int i=0; i < GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE; ++i)
        {
            *yaw   += gyroReadings[i][0];
            *roll  += gyroReadings[i][1];
            *pitch += gyroReadings[i][2];
        }

        *yaw   = *yaw   / GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE;
        *roll  = *roll  / GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE;
        *pitch = *pitch / GYROSCOPE_MOVING_AVERAGE_WINDOW_SIZE;
    }

    // Removes the gravity component from the acceleration vector
    void RemoveGravityAccel(float* x, float* y, float* z)
    {
        // TODO
    }

    // Update accelerometer readings with prrovided values
    void UpdateAccel(float x, float y, float z)
    {
        // Remove gravity component
        RemoveGravityAccel(&x,&y,&z);

        // Save readings as last
        accelReadings[accelReadingsIndex][0] = x;
        accelReadings[accelReadingsIndex][1] = y;
        accelReadings[accelReadingsIndex][2] = z;
        
        // Update readings index
        if (++accelReadingsIndex == ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE) accelReadingsIndex = 0;
    }   

    // Returns the average x y and z accelerations as provided by the moving average smoothing
    void GetAvgAccelValues(float* x, float* y, float* z)
    {
        // Reset received variables
        *x = 0;
        *y = 0;
        *z = 0;

        for (int i = 0; i < ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE; ++i)
        {
            *x += accelReadings[i][0];
            *y += accelReadings[i][1];
            *z += accelReadings[i][2];
        }

        *x = *x / ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE;
        *y = *y / ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE;
        *z = *z / ACCELEROMETER_MOVING_AVERAGE_WINDOW_SIZE;
    }
};

// Object map (name : SceneObject)
std::map<std::string, SceneObject> g_VirtualScene;

// Screen Ratio (Width / Height)
float g_ScreenRatio = 1.0f;

// Mouse buttons status
bool g_LeftMouseButtonPressed = false;
bool g_RightMouseButtonPressed = false;
bool g_MiddleMouseButtonPressed = false;

// Shader program variables
GLuint vertex_shader_id;
GLuint fragment_shader_id;
GLuint program_id = 0;
GLint model_uniform;
GLint view_uniform;
GLint projection_uniform;
GLint object_id_uniform;

// Time variables
static float previous_time = glfwGetTime();
static float current_time  = glfwGetTime();

// Instance control variables

// Camera Instance
static struct Camera g_Camera = {.cameraTheta = M_PI_2,
                          .cameraPhi = M_PI_4,
                          .camera_position = glm::vec4(20.0f,20.0f,0.0f,1.0f),
                          .camera_up_vector = glm::vec4(0.0f,1.0f,0.0f,0.0f),
                          .camera_view_vector = glm::vec4(0.0f,0.0f,0.0f,0.0f),
                          .camera_speed = 0.4};

// Wiimote real object instance
static WiiData g_Wii;

// Wiimote vritual object instance
static struct PlacedObject placed_wiimote = {.obj_name = "wiimote",
                            .positionX = 0.0f, .positionY = 0.0f, .positionZ = 0.0f,
                            .scaleX = 1.0f, .scaleY = 1.1f, .scaleZ = 1.0f,
                            .quaternion = glm::quat(glm::vec3(0.0f,0.0f,M_PI_2))};