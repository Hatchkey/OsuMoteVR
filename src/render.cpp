#include "render.h"

// =========================================================================================
//                             APPLICATION ENTRYPOINT
//==========================================================================================

int main(int argc, char* argv[])
{

    // Start thread for managing wiimote sensor update events
    std::thread controller_manager(ControllerHandlerThread);

    // Connect to wiimotes
    g_Wii.connectedWiimotes = ConnectWiimotes();
    if (!g_Wii.connectedWiimotes)
    {
        glfwTerminate();
        fprintf(stderr, "ERROR: ConnectWiimotes() failed.\n");
        controller_manager.join();
        std::exit(EXIT_FAILURE);
    }

    // Initialize GLFW
    int success = glfwInit();
    if (!success)
    {
        fprintf(stderr, "ERROR: glfwInit() failed.\n");
        std::exit(EXIT_FAILURE);
    }

    // Set error callback
    glfwSetErrorCallback(ErrorCallback);

    // Set OpenGL 3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    // Set core profile (Modern functions)
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create window
    GLFWwindow* window;
    window = glfwCreateWindow(800, 600, "Render", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        fprintf(stderr, "ERROR: glfwCreateWindow() failed.\n");
        std::exit(EXIT_FAILURE);
    }

    // Set input callback functions
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);

    // Set current context to window
    glfwMakeContextCurrent(window);

    // Load OpenGL 3.3 functions
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);

    // Set window resize callback
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    FramebufferSizeCallback(window, 800, 600);

    // Print GPU info
    const GLubyte *vendor      = glGetString(GL_VENDOR);
    const GLubyte *renderer    = glGetString(GL_RENDERER);
    const GLubyte *glversion   = glGetString(GL_VERSION);
    const GLubyte *glslversion = glGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("GPU: %s, %s, OpenGL %s, GLSL %s\n", vendor, renderer, glversion, glslversion);

    // Load vertex and fragment shaders
    LoadShadersFromFiles();

    // Object 1 - Wiimote

    // Load object
    ObjModel wiimoteModel("../../data/wiimote.obj");
    ComputeNormals(&wiimoteModel);
    BuildTrianglesAndAddToVirtualScene(&wiimoteModel);

    if ( argc > 1 )
    {
        ObjModel model(argv[1]);
        BuildTrianglesAndAddToVirtualScene(&model);
    }

    // Initialize text rendering
    TextRendering_Init();

    // Enable Z-buffer
    glEnable(GL_DEPTH_TEST);

    // Enable Back-face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Main window loop
    while (!glfwWindowShouldClose(window))
    {
        // Framebuffer background
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

        // Reset Z-Buffer and paint pixels
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Use created shader program
        glUseProgram(program_id);

        // Update view vector
        g_Camera.camera_view_vector = (Matrix_Rotate_Y(g_Camera.cameraTheta)*Matrix_Rotate_X(-g_Camera.cameraPhi))*glm::vec4(0.0f,0.0f,-1.0f,0.0f);
        // View matrix
        glm::mat4 view = Matrix_Camera_View(g_Camera.camera_position, g_Camera.camera_view_vector, g_Camera.camera_up_vector);

        // Cutoff planes
        float nearplane = -0.1f;  // Posição do "near plane"
        float farplane  = -200.0f; // Posição do "far plane"

        // Projection matrix
        float field_of_view  = 3.141592 / 3.0f;
        glm::mat4 projection = Matrix_Perspective(field_of_view, g_ScreenRatio, nearplane, farplane);
        glm::mat4 model = Matrix_Identity();

        // Send view and projection matrix to GPU
        glUniformMatrix4fv(view_uniform       , 1 , GL_FALSE , glm::value_ptr(view));
        glUniformMatrix4fv(projection_uniform , 1 , GL_FALSE , glm::value_ptr(projection));

        // Draw objects

        // Wiimote
        #define WIIMOTE 1
        
        // Get rotation matrix based on object orientation quaternion
        glm::mat4 RotationMatrix = glm::toMat4(placed_wiimote.quaternion);

        model = Matrix_Translate(placed_wiimote.positionX, placed_wiimote.positionY, placed_wiimote.positionZ)
        * RotationMatrix
        * Matrix_Scale(placed_wiimote.scaleX,placed_wiimote.scaleY,placed_wiimote.scaleZ);

        glUniformMatrix4fv(model_uniform, 1 , GL_FALSE , glm::value_ptr(model));
        glUniform1i(object_id_uniform, WIIMOTE);
        DrawVirtualObject("wiimote");

        // Write FPS Coutner
        TextRendering_ShowFramesPerSecond(window);

        // Write orientation quaternion for the wiimote object
        char buffer[128];
        int numchars = snprintf(buffer,128,"Orientation = [ %.2f, %.2f, %.2f, %.2f ]",
                placed_wiimote.quaternion.x,
                placed_wiimote.quaternion.y,
                placed_wiimote.quaternion.z,
                placed_wiimote.quaternion.w);
        TextRendering_PrintString(window, buffer, (numchars + 1)*TextRendering_CharWidth(window) - 1.0f, 1.0f-TextRendering_LineHeight(window), 1.0f);

        // Swap buffers (Show all that was rendered above)
        glfwSwapBuffers(window);

        // Poll system for user input events
        glfwPollEvents();

    }

    // Stop operating system resource usage
    glfwTerminate();

    // Wait for controller event handler thread to exit
    controller_manager.join();
    
    return 0;
}

// =========================================================================================
//                           OBJECT BUILDING AND DRAWING
//==========================================================================================

// Draws an object stored in g_VirtualScene
void DrawVirtualObject(const char* object_name)
{
    // Enable VAO (Use vertex attributes stored in VAO)
    glBindVertexArray(g_VirtualScene[object_name].vertex_array_object_id);

    // Draw Object
    glDrawElements(
        g_VirtualScene[object_name].rendering_mode,
        g_VirtualScene[object_name].num_indices,
        GL_UNSIGNED_INT,
        (void*)(g_VirtualScene[object_name].first_index * sizeof(GLuint))
    );

    // Disable VAO to stop next operations from editing it
    glBindVertexArray(0);
}

// Load vertex and fragment Shaders
void LoadShadersFromFiles()
{
    vertex_shader_id = LoadShader_Vertex("../../src/shader_vertex.glsl");
    fragment_shader_id = LoadShader_Fragment("../../src/shader_fragment.glsl");

    // Delete old GPU program (if exists)
    if ( program_id != 0 )
        glDeleteProgram(program_id);

    // Create GPU program using loaded shaders
    program_id = CreateGpuProgram(vertex_shader_id, fragment_shader_id);

    // Search for vertex shader address
    model_uniform           = glGetUniformLocation(program_id, "model");      // "model" matrix variable
    view_uniform            = glGetUniformLocation(program_id, "view");       // "view" matrix variable (vertex shader)
    projection_uniform      = glGetUniformLocation(program_id, "projection"); // "projection" matrix variable (vertex shader)
    object_id_uniform       = glGetUniformLocation(program_id, "object_id");  // "object_id" variable (fragment shader)
}

// Compute normals for an ObjModel if they were not specified
void ComputeNormals(ObjModel* model)
{
    if ( !model->attrib.normals.empty() )
        return;

    // Compute triangle normals
    // Compute vertex models using Gouraud method

    size_t num_vertices = model->attrib.vertices.size() / 3;

    std::vector<int> num_triangles_per_vertex(num_vertices, 0);
    std::vector<glm::vec4> vertex_normals(num_vertices, glm::vec4(0.0f,0.0f,0.0f,0.0f));

    for (size_t shape = 0; shape < model->shapes.size(); ++shape)
    {
        size_t num_triangles = model->shapes[shape].mesh.num_face_vertices.size();

        for (size_t triangle = 0; triangle < num_triangles; ++triangle)
        {
            assert(model->shapes[shape].mesh.num_face_vertices[triangle] == 3);

            glm::vec4  vertices[3];
            for (size_t vertex = 0; vertex < 3; ++vertex)
            {
                tinyobj::index_t idx = model->shapes[shape].mesh.indices[3*triangle + vertex];
                const float vx = model->attrib.vertices[3*idx.vertex_index + 0];
                const float vy = model->attrib.vertices[3*idx.vertex_index + 1];
                const float vz = model->attrib.vertices[3*idx.vertex_index + 2];
                vertices[vertex] = glm::vec4(vx,vy,vz,1.0);
            }

            const glm::vec4  a = vertices[0];
            const glm::vec4  b = vertices[1];
            const glm::vec4  c = vertices[2];

            const glm::vec4  n = crossproduct((a - b),(a - c));

            for (size_t vertex = 0; vertex < 3; ++vertex)
            {
                tinyobj::index_t idx = model->shapes[shape].mesh.indices[3*triangle + vertex];
                num_triangles_per_vertex[idx.vertex_index] += 1;
                vertex_normals[idx.vertex_index] += n;
                model->shapes[shape].mesh.indices[3*triangle + vertex].normal_index = idx.vertex_index;
            }
        }
    }

    model->attrib.normals.resize( 3*num_vertices );

    for (size_t i = 0; i < vertex_normals.size(); ++i)
    {
        glm::vec4 n = vertex_normals[i] / (float)num_triangles_per_vertex[i];
        n /= norm(n);
        model->attrib.normals[3*i + 0] = n.x;
        model->attrib.normals[3*i + 1] = n.y;
        model->attrib.normals[3*i + 2] = n.z;
    }
}

// Build triangles for an ObjModel for future rastering
void BuildTrianglesAndAddToVirtualScene(ObjModel* model)
{
    GLuint vertex_array_object_id;
    glGenVertexArrays(1, &vertex_array_object_id);
    glBindVertexArray(vertex_array_object_id);

    std::vector<GLuint> indices;
    std::vector<float>  model_coefficients;
    std::vector<float>  normal_coefficients;
    std::vector<float>  texture_coefficients;

    for (size_t shape = 0; shape < model->shapes.size(); ++shape)
    {
        size_t first_index = indices.size();
        size_t num_triangles = model->shapes[shape].mesh.num_face_vertices.size();

        for (size_t triangle = 0; triangle < num_triangles; ++triangle)
        {
            assert(model->shapes[shape].mesh.num_face_vertices[triangle] == 3);

            for (size_t vertex = 0; vertex < 3; ++vertex)
            {
                tinyobj::index_t idx = model->shapes[shape].mesh.indices[3*triangle + vertex];

                indices.push_back(first_index + 3*triangle + vertex);

                const float vx = model->attrib.vertices[3*idx.vertex_index + 0];
                const float vy = model->attrib.vertices[3*idx.vertex_index + 1];
                const float vz = model->attrib.vertices[3*idx.vertex_index + 2];

                model_coefficients.push_back( vx ); // X
                model_coefficients.push_back( vy ); // Y
                model_coefficients.push_back( vz ); // Z
                model_coefficients.push_back( 1.0f ); // W

                if ( idx.normal_index != -1 )
                {
                    const float nx = model->attrib.normals[3*idx.normal_index + 0];
                    const float ny = model->attrib.normals[3*idx.normal_index + 1];
                    const float nz = model->attrib.normals[3*idx.normal_index + 2];
                    normal_coefficients.push_back( nx ); // X
                    normal_coefficients.push_back( ny ); // Y
                    normal_coefficients.push_back( nz ); // Z
                    normal_coefficients.push_back( 0.0f ); // W
                }

                if ( idx.texcoord_index != -1 )
                {
                    const float u = model->attrib.texcoords[2*idx.texcoord_index + 0];
                    const float v = model->attrib.texcoords[2*idx.texcoord_index + 1];
                    texture_coefficients.push_back( u );
                    texture_coefficients.push_back( v );
                }
            }
        }

        size_t last_index = indices.size() - 1;

        SceneObject theobject;
        theobject.name           = model->shapes[shape].name;
        theobject.first_index    = first_index;
        theobject.num_indices    = last_index - first_index + 1;
        theobject.rendering_mode = GL_TRIANGLES;
        theobject.vertex_array_object_id = vertex_array_object_id;

        g_VirtualScene[model->shapes[shape].name] = theobject;
    }

    GLuint VBO_model_coefficients_id;
    glGenBuffers(1, &VBO_model_coefficients_id);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_model_coefficients_id);
    glBufferData(GL_ARRAY_BUFFER, model_coefficients.size() * sizeof(float), NULL, GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, model_coefficients.size() * sizeof(float), model_coefficients.data());
    GLuint location = 0;
    GLint  number_of_dimensions = 4;
    glVertexAttribPointer(location, number_of_dimensions, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(location);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if ( !normal_coefficients.empty() )
    {
        GLuint VBO_normal_coefficients_id;
        glGenBuffers(1, &VBO_normal_coefficients_id);
        glBindBuffer(GL_ARRAY_BUFFER, VBO_normal_coefficients_id);
        glBufferData(GL_ARRAY_BUFFER, normal_coefficients.size() * sizeof(float), NULL, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, normal_coefficients.size() * sizeof(float), normal_coefficients.data());
        location = 1;
        number_of_dimensions = 4;
        glVertexAttribPointer(location, number_of_dimensions, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(location);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if ( !texture_coefficients.empty() )
    {
        GLuint VBO_texture_coefficients_id;
        glGenBuffers(1, &VBO_texture_coefficients_id);
        glBindBuffer(GL_ARRAY_BUFFER, VBO_texture_coefficients_id);
        glBufferData(GL_ARRAY_BUFFER, texture_coefficients.size() * sizeof(float), NULL, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, texture_coefficients.size() * sizeof(float), texture_coefficients.data());
        location = 2;
        number_of_dimensions = 2;
        glVertexAttribPointer(location, number_of_dimensions, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(location);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    GLuint indices_id;
    glGenBuffers(1, &indices_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_id);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), NULL, GL_STATIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices.size() * sizeof(GLuint), indices.data());

    glBindVertexArray(0);
}

// =========================================================================================
//                            SHADER BUILDING AND LOADING
//==========================================================================================

// Load vertex shader from GLSL file
GLuint LoadShader_Vertex(const char* filename)
{
    // Create shader ID
    GLuint vertex_shader_id = glCreateShader(GL_VERTEX_SHADER);

    // Load and compile shader
    LoadShader(filename, vertex_shader_id);

    // Return generated ID
    return vertex_shader_id;
}

// Load fragment shader from GLSL file
GLuint LoadShader_Fragment(const char* filename)
{
    // Create shader ID
    GLuint fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER);

    // Load and compile shader
    LoadShader(filename, fragment_shader_id);

    // Return generated ID
    return fragment_shader_id;
}

// Load and compile GPU code
void LoadShader(const char* filename, GLuint shader_id)
{
    std::ifstream file;
    try {
        file.exceptions(std::ifstream::failbit);
        file.open(filename);
    } catch ( std::exception& e ) {
        fprintf(stderr, "ERROR: Cannot open file \"%s\".\n", filename);
        std::exit(EXIT_FAILURE);
    }
    std::stringstream shader;
    shader << file.rdbuf();
    std::string str = shader.str();
    const GLchar* shader_string = str.c_str();
    const GLint   shader_string_length = static_cast<GLint>( str.length() );

    // Load shader
    glShaderSource(shader_id, 1, &shader_string, &shader_string_length);

    // Compile shader
    glCompileShader(shader_id);

    // Check for errors/warnings
    GLint compiled_ok;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &compiled_ok);

    GLint log_length = 0;
    glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &log_length);

    GLchar* log = new GLchar[log_length];
    glGetShaderInfoLog(shader_id, log_length, &log_length, log);

    // Print errors
    if ( log_length != 0 )
    {
        std::string  output;

        if ( !compiled_ok )
        {
            output += "ERROR: OpenGL compilation of \"";
            output += filename;
            output += "\" failed.\n";
            output += "== Start of compilation log\n";
            output += log;
            output += "== End of compilation log\n";
        }
        else
        {
            output += "WARNING: OpenGL compilation of \"";
            output += filename;
            output += "\".\n";
            output += "== Start of compilation log\n";
            output += log;
            output += "== End of compilation log\n";
        }

        fprintf(stderr, "%s", output.c_str());
    }

    delete [] log;
}

// Creates a GPU program using vertex and fragment shader
GLuint CreateGpuProgram(GLuint vertex_shader_id, GLuint fragment_shader_id)
{
    // Create GPU program ID
    GLuint program_id = glCreateProgram();

    // Attach both shaders
    glAttachShader(program_id, vertex_shader_id);
    glAttachShader(program_id, fragment_shader_id);

    // Link shaders
    glLinkProgram(program_id);

    // Check for errors
    GLint linked_ok = GL_FALSE;
    glGetProgramiv(program_id, GL_LINK_STATUS, &linked_ok);

    // Print errors
    if ( linked_ok == GL_FALSE )
    {
        GLint log_length = 0;
        glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &log_length);

        GLchar* log = new GLchar[log_length];

        glGetProgramInfoLog(program_id, log_length, &log_length, log);

        std::string output;

        output += "ERROR: OpenGL linking of program failed.\n";
        output += "== Start of link log\n";
        output += log;
        output += "\n== End of link log\n";

        delete [] log;

        fprintf(stderr, "%s", output.c_str());
    }

    // Delete shader objects
    glDeleteShader(vertex_shader_id);
    glDeleteShader(fragment_shader_id);

    // Return created ID
    return program_id;
}

// =========================================================================================
//                                    CALLBACKS
//==========================================================================================

// Window resize callback
void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);

    g_ScreenRatio = (float)width / height;
}

// Last cursor position
double g_LastCursorPosX, g_LastCursorPosY;

// Mouse button callback
void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        glfwGetCursorPos(window, &g_LastCursorPosX, &g_LastCursorPosY);
        g_LeftMouseButtonPressed = true;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        g_LeftMouseButtonPressed = false;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        glfwGetCursorPos(window, &g_LastCursorPosX, &g_LastCursorPosY);
        g_RightMouseButtonPressed = true;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
    {
        g_RightMouseButtonPressed = false;
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS)
    {
        glfwGetCursorPos(window, &g_LastCursorPosX, &g_LastCursorPosY);
        g_MiddleMouseButtonPressed = true;
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE)
    {
        g_MiddleMouseButtonPressed = false;
    }
}

// Cursor movement callback
void CursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    // Move camera
    if (g_LeftMouseButtonPressed)
    {
        // Mouse movement (Screen coordinates)
        float dx = xpos - g_LastCursorPosX;
        float dy = ypos - g_LastCursorPosY;

        // Update camera angles using mouse movement
        g_Camera.cameraTheta -= 0.01f*dx;
        g_Camera.cameraPhi   += 0.01f*dy;

        // Prevent overflow and underflow of spheric coordinates
        float phimax = 3.141592f/2;
        float phimin = -phimax;

        if (g_Camera.cameraPhi > phimax)
            g_Camera.cameraPhi = phimax;

        if (g_Camera.cameraPhi < phimin)
            g_Camera.cameraPhi = phimin;

        // Update global variables
        g_LastCursorPosX = xpos;
        g_LastCursorPosY = ypos;
    }
}

// Key Callback
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mod)
{
    // Close window on ESC press
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);

    // Reload shaders on R press
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        LoadShadersFromFiles();
        fprintf(stdout,"Shaders reloaded!\n");
        fflush(stdout);
    }
    
    // Reset model on Space press
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        placed_wiimote.SetOrientation(0.0f,0.0f,M_PI_2);
        placed_wiimote.SetPosition(0.0f,0.0f,0.0f);
    }


    // Projection vectors for free camera
    glm::vec4 view_projection = g_Camera.camera_view_vector;
    glm::vec4 side_vector = crossproduct(g_Camera.camera_up_vector,-g_Camera.camera_view_vector)/norm(crossproduct(g_Camera.camera_up_vector,-g_Camera.camera_view_vector));

    // Move camera if WASD was pressed
    if (key == GLFW_KEY_W && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        g_Camera.camera_position = g_Camera.camera_position + g_Camera.camera_speed*view_projection;
    }

    if (key == GLFW_KEY_A && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        g_Camera.camera_position = g_Camera.camera_position - g_Camera.camera_speed*side_vector;
    }

    if (key == GLFW_KEY_S && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        g_Camera.camera_position = g_Camera.camera_position - g_Camera.camera_speed*view_projection;
    }

    if (key == GLFW_KEY_D && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        g_Camera.camera_position = g_Camera.camera_position + g_Camera.camera_speed*side_vector;
    }
}

// Error callback
void ErrorCallback(int error, const char* description)
{
    fprintf(stderr, "ERROR: GLFW: %s\n", description);
}

// =========================================================================================
//                                        WIIMOTE
//==========================================================================================

// Connect to the wiimotes via bluetooth
int ConnectWiimotes()
{
    int index = 0;
    std::vector<CWiimote>::iterator i;

    // Wiimote vector
    std::vector<CWiimote>& wiimotes = g_Wii.wii.FindAndConnect();

    // Check for sucessful connections
    if (!wiimotes.size())
        return 0;
    else
    {
        for (index = 0, i = wiimotes.begin(); i != wiimotes.end(); ++i, ++index)
        {
            CWiimote & wiimote = *i;

            // Set LEDS and motion plus
            wiimote.SetLEDs(CWiimote::LED_1);
            wiimote.SetMotionSensingMode(CWiimote::ON);
            wiimote.EnableMotionPlus(CWiimote::ON);
            wiimote.Accelerometer.SetAccelThreshold(0);
        }

        return wiimotes.size();
    }
}

// Receives events from the controller and processes themm
void ControllerHandlerThread()
{
    // Wait for wiimotes to connect
    while (g_Wii.connectedWiimotes == 0)
    {
        // DEBUG message
        //fprintf(stderr,"[ControllerHandlerThread]: Waiting for wiimotes to connect\n");
    }

    // Get wiimotes
    std::vector<CWiimote>& wiimotes = g_Wii.wii.GetWiimotes();
    std::vector<CWiimote>::iterator i;

    int exit = 0, reload_wiimotes = 0;
    while(!exit && g_Wii.connectedWiimotes > 0)
    {
        // Update current time
        current_time = glfwGetTime();

        // Check for refresh
        if (reload_wiimotes)
        {
            wiimotes = g_Wii.wii.GetWiimotes();
            reload_wiimotes = 0;
        }

        // Poll for events
        if (g_Wii.wii.Poll())
        {
            for (i = wiimotes.begin(); i != wiimotes.end(); ++i)
            {
                CWiimote & wiimote = *i;
                switch(wiimote.GetEvent())
                {
                    case CWiimote::EVENT_EVENT:
                        HandleEvent(wiimote);
                        break;
                    case CWiimote::EVENT_DISCONNECT:
                        exit = 1;
                        break;
                    case CWiimote::EVENT_UNEXPECTED_DISCONNECT:
                        reload_wiimotes = 1;
                        break;
                    default:
                        break;
                }
            }
        }

        // Update time counter
        previous_time = current_time;
    }
}

// Handles a sensor update event
void HandleEvent(CWiimote &wm)
{
    // Handle Gyroscope

    // Get pitch, roll and yaw rates
    float roll_rate, pitch_rate, yaw_rate;
    wm.ExpansionDevice.MotionPlus.Gyroscope.GetRates(roll_rate, pitch_rate, yaw_rate);

    // Update gyroscope
    g_Wii.UpdateGyro(yaw_rate, roll_rate, pitch_rate);

    // Get average gyroscope rates
    g_Wii.GetAvgGyroValues(&yaw_rate,&roll_rate,&pitch_rate);

    // Convert from degrees to radians
    yaw_rate   =    yaw_rate * M_PI / 180.0f;
    roll_rate  =  -roll_rate * M_PI / 180.0f;
    pitch_rate =  pitch_rate * M_PI / 180.0f;

    // Update model orientation
    placed_wiimote.UpdateOrientation(yaw_rate, roll_rate, pitch_rate, current_time  - previous_time);

    // Handle accelerometer

    // Get acceleration vector
    float accel_x, accel_y, accel_z;
    wm.Accelerometer.GetGravityVector(accel_z,accel_x,accel_y);

    // Update accelerometer
    g_Wii.UpdateAccel(accel_x, accel_y, accel_z);

    // Get average accelerometer rates
    g_Wii.GetAvgAccelValues(&accel_x, &accel_y, &accel_z);

    // TODO Process these values

    // Update model position
    //placed_wiimote.UpdatePosition(accel_x,accel_y,accel_z, current_time - previous_time);
}

// =========================================================================================
//                                         DEBUG
//==========================================================================================

// Show FPS
void TextRendering_ShowFramesPerSecond(GLFWwindow* window)
{

    static float old_seconds = (float)glfwGetTime();
    static int   ellapsed_frames = 0;
    static char  buffer[20] = "?? fps";
    static int   numchars = 7;

    ellapsed_frames += 1;

    float seconds = (float)glfwGetTime();

    float ellapsed_seconds = seconds - old_seconds;

    if ( ellapsed_seconds > 1.0f )
    {
        numchars = snprintf(buffer, 20, "%.2f fps", ellapsed_frames / ellapsed_seconds);

        old_seconds = seconds;
        ellapsed_frames = 0;
    }

    float lineheight = TextRendering_LineHeight(window);
    float charwidth = TextRendering_CharWidth(window);

    TextRendering_PrintString(window, buffer, 1.0f-(numchars + 1)*charwidth, 1.0f-lineheight, 1.0f);
}

// Print all information about a model (DEBUG)
void PrintObjModelInfo(ObjModel* model)
{
  const tinyobj::attrib_t                & attrib    = model->attrib;
  const std::vector<tinyobj::shape_t>    & shapes    = model->shapes;
  const std::vector<tinyobj::material_t> & materials = model->materials;

  printf("# of vertices  : %d\n", (int)(attrib.vertices.size() / 3));
  printf("# of normals   : %d\n", (int)(attrib.normals.size() / 3));
  printf("# of texcoords : %d\n", (int)(attrib.texcoords.size() / 2));
  printf("# of shapes    : %d\n", (int)shapes.size());
  printf("# of materials : %d\n", (int)materials.size());

  for (size_t v = 0; v < attrib.vertices.size() / 3; v++) {
    printf("  v[%ld] = (%f, %f, %f)\n", static_cast<long>(v),
           static_cast<const double>(attrib.vertices[3 * v + 0]),
           static_cast<const double>(attrib.vertices[3 * v + 1]),
           static_cast<const double>(attrib.vertices[3 * v + 2]));
  }

  for (size_t v = 0; v < attrib.normals.size() / 3; v++) {
    printf("  n[%ld] = (%f, %f, %f)\n", static_cast<long>(v),
           static_cast<const double>(attrib.normals[3 * v + 0]),
           static_cast<const double>(attrib.normals[3 * v + 1]),
           static_cast<const double>(attrib.normals[3 * v + 2]));
  }

  for (size_t v = 0; v < attrib.texcoords.size() / 2; v++) {
    printf("  uv[%ld] = (%f, %f)\n", static_cast<long>(v),
           static_cast<const double>(attrib.texcoords[2 * v + 0]),
           static_cast<const double>(attrib.texcoords[2 * v + 1]));
  }

  // For each shape
  for (size_t i = 0; i < shapes.size(); i++) {
    printf("shape[%ld].name = %s\n", static_cast<long>(i),
           shapes[i].name.c_str());
    printf("Size of shape[%ld].indices: %lu\n", static_cast<long>(i),
           static_cast<unsigned long>(shapes[i].mesh.indices.size()));

    size_t index_offset = 0;

    assert(shapes[i].mesh.num_face_vertices.size() ==
           shapes[i].mesh.material_ids.size());

    printf("shape[%ld].num_faces: %lu\n", static_cast<long>(i),
           static_cast<unsigned long>(shapes[i].mesh.num_face_vertices.size()));

    // For each face
    for (size_t f = 0; f < shapes[i].mesh.num_face_vertices.size(); f++) {
      size_t fnum = shapes[i].mesh.num_face_vertices[f];

      printf("  face[%ld].fnum = %ld\n", static_cast<long>(f),
             static_cast<unsigned long>(fnum));

      // For each vertex in the face
      for (size_t v = 0; v < fnum; v++) {
        tinyobj::index_t idx = shapes[i].mesh.indices[index_offset + v];
        printf("    face[%ld].v[%ld].idx = %d/%d/%d\n", static_cast<long>(f),
               static_cast<long>(v), idx.vertex_index, idx.normal_index,
               idx.texcoord_index);
      }

      printf("  face[%ld].material_id = %d\n", static_cast<long>(f),
             shapes[i].mesh.material_ids[f]);

      index_offset += fnum;
    }

    printf("shape[%ld].num_tags: %lu\n", static_cast<long>(i),
           static_cast<unsigned long>(shapes[i].mesh.tags.size()));
    for (size_t t = 0; t < shapes[i].mesh.tags.size(); t++) {
      printf("  tag[%ld] = %s ", static_cast<long>(t),
             shapes[i].mesh.tags[t].name.c_str());
      printf(" ints: [");
      for (size_t j = 0; j < shapes[i].mesh.tags[t].intValues.size(); ++j) {
        printf("%ld", static_cast<long>(shapes[i].mesh.tags[t].intValues[j]));
        if (j < (shapes[i].mesh.tags[t].intValues.size() - 1)) {
          printf(", ");
        }
      }
      printf("]");

      printf(" floats: [");
      for (size_t j = 0; j < shapes[i].mesh.tags[t].floatValues.size(); ++j) {
        printf("%f", static_cast<const double>(
                         shapes[i].mesh.tags[t].floatValues[j]));
        if (j < (shapes[i].mesh.tags[t].floatValues.size() - 1)) {
          printf(", ");
        }
      }
      printf("]");

      printf(" strings: [");
      for (size_t j = 0; j < shapes[i].mesh.tags[t].stringValues.size(); ++j) {
        printf("%s", shapes[i].mesh.tags[t].stringValues[j].c_str());
        if (j < (shapes[i].mesh.tags[t].stringValues.size() - 1)) {
          printf(", ");
        }
      }
      printf("]");
      printf("\n");
    }
  }

  for (size_t i = 0; i < materials.size(); i++) {
    printf("material[%ld].name = %s\n", static_cast<long>(i),
           materials[i].name.c_str());
    printf("  material.Ka = (%f, %f ,%f)\n",
           static_cast<const double>(materials[i].ambient[0]),
           static_cast<const double>(materials[i].ambient[1]),
           static_cast<const double>(materials[i].ambient[2]));
    printf("  material.Kd = (%f, %f ,%f)\n",
           static_cast<const double>(materials[i].diffuse[0]),
           static_cast<const double>(materials[i].diffuse[1]),
           static_cast<const double>(materials[i].diffuse[2]));
    printf("  material.Ks = (%f, %f ,%f)\n",
           static_cast<const double>(materials[i].specular[0]),
           static_cast<const double>(materials[i].specular[1]),
           static_cast<const double>(materials[i].specular[2]));
    printf("  material.Tr = (%f, %f ,%f)\n",
           static_cast<const double>(materials[i].transmittance[0]),
           static_cast<const double>(materials[i].transmittance[1]),
           static_cast<const double>(materials[i].transmittance[2]));
    printf("  material.Ke = (%f, %f ,%f)\n",
           static_cast<const double>(materials[i].emission[0]),
           static_cast<const double>(materials[i].emission[1]),
           static_cast<const double>(materials[i].emission[2]));
    printf("  material.Ns = %f\n",
           static_cast<const double>(materials[i].shininess));
    printf("  material.Ni = %f\n", static_cast<const double>(materials[i].ior));
    printf("  material.dissolve = %f\n",
           static_cast<const double>(materials[i].dissolve));
    printf("  material.illum = %d\n", materials[i].illum);
    printf("  material.map_Ka = %s\n", materials[i].ambient_texname.c_str());
    printf("  material.map_Kd = %s\n", materials[i].diffuse_texname.c_str());
    printf("  material.map_Ks = %s\n", materials[i].specular_texname.c_str());
    printf("  material.map_Ns = %s\n",
           materials[i].specular_highlight_texname.c_str());
    printf("  material.map_bump = %s\n", materials[i].bump_texname.c_str());
    printf("  material.map_d = %s\n", materials[i].alpha_texname.c_str());
    printf("  material.disp = %s\n", materials[i].displacement_texname.c_str());
    printf("  <<PBR>>\n");
    printf("  material.Pr     = %f\n", materials[i].roughness);
    printf("  material.Pm     = %f\n", materials[i].metallic);
    printf("  material.Ps     = %f\n", materials[i].sheen);
    printf("  material.Pc     = %f\n", materials[i].clearcoat_thickness);
    printf("  material.Pcr    = %f\n", materials[i].clearcoat_thickness);
    printf("  material.aniso  = %f\n", materials[i].anisotropy);
    printf("  material.anisor = %f\n", materials[i].anisotropy_rotation);
    printf("  material.map_Ke = %s\n", materials[i].emissive_texname.c_str());
    printf("  material.map_Pr = %s\n", materials[i].roughness_texname.c_str());
    printf("  material.map_Pm = %s\n", materials[i].metallic_texname.c_str());
    printf("  material.map_Ps = %s\n", materials[i].sheen_texname.c_str());
    printf("  material.norm   = %s\n", materials[i].normal_texname.c_str());
    std::map<std::string, std::string>::const_iterator it(
        materials[i].unknown_parameter.begin());
    std::map<std::string, std::string>::const_iterator itEnd(
        materials[i].unknown_parameter.end());

    for (; it != itEnd; it++) {
      printf("  material.%s = %s\n", it->first.c_str(), it->second.c_str());
    }
    printf("\n");
  }
}

// set makeprg=cd\ ..\ &&\ make\ run\ >/dev/null
// vim: set spell spelllang=pt_br :