#ifndef CHEEMSIT_GUI_VK_VULKAN_MODEL_HPP
#define CHEEMSIT_GUI_VK_VULKAN_MODEL_HPP
/*
class transform_t
{
public:
    virtual ~transform_t() = default;

    matrix4x4 world_matrix() const;
    void rotate(double angle, vector3 axis);

    vector3 forward_vector() const {return rotation * axis::forward;}
    vector3 backard_vector() const {return rotation * axis::backward;}
    vector3 left_vector() const {return rotation * axis::left;}
    vector3 right_vector() const {return rotation * axis::right;}
    vector3 up_vector() const {return rotation * axis::up;}
    vector3 down_vector() const {return rotation * axis::down;}

    vector3 location{0, 0, 0};
    quaternion rotation{1, 0, 0, 0};
    vector3 scale{1, 1, 1};
};

struct vertex_input_t
{
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
    vk::PipelineVertexInputStateCreateFlagBits flags = {};
};

struct model_push_constants_t
{
    glm::mat4x4 model;
};

struct material_t
{
    material_t(std::string name) : material_name(std::move(name)){}
    std::string material_name;

    vk::PipelineLayout pipeline_layout;
    vk::Pipeline pipeline;
};

struct model_t
{
    struct vertex_t
    {
        glm::vec3 position;
        glm::vec3 color;
    };

    static vertex_input_t input_description();

    model_t(std::string name) : model_name(std::move(name)){}

    void load_from_obj(std::string_view filename);

    std::string model_name;

    std::vector<vertex_t> vertices;
    allocated_buffer_t vertex_buffer;
};

class color_model_t
{
public:
    struct vertex_t
    {
        vertex_t() = default;
        vertex_t(glm::vec3 in_pos, glm::vec3 in_normal, glm::vec4 in_color)
            : position(in_pos), normal(in_normal), color(in_color)
        {
        }

        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 color;
    };

    static vertex_input_t input_description();

    void load_obj(std::string_view filename);
    void load_ply(std::string_view filename);

    void upload(vma::Allocator allocator);

    void destroy(vma::Allocator allocator);

    std::vector<vertex_t> vertices;

    allocated_buffer_t vertex_buffer;
    allocated_buffer_t staging_buffer;
};

class line_model_t
{
public:
    struct vertex_t
    {
        vertex_t() = default;
        vertex_t(glm::vec3 in_pos, glm::vec3 in_color)
                : position(in_pos), color(in_color)
        {
        }

        glm::vec3 position;
        glm::vec3 color;
    };

    static vertex_input_t input_description();

    void upload(vma::Allocator allocator);
    void destroy(vma::Allocator allocator);

    std::vector<vertex_t> vertices;
    allocated_buffer_t vertex_buffer;
};

class render_object_t : public transform_t, public auto_tick_t<render_object_t>
{
public:
    void tick(double delta_time);

    material_t* material;
    color_model_t* model;
};
*/
#endif //CHEEMSIT_GUI_VK_VULKAN_MODEL_HPP
