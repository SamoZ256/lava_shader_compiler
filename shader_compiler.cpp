#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef WIN32
#include <unistd.h>
#endif

#ifdef WIN32
#define stat _stat
#endif

#include <json/json.h>

namespace nh = nlohmann;

#include "spirv_cross/spirv_msl.hpp"

std::vector<uint32_t> readFile(const char* path) {

	FILE *file = fopen(path, "rb");
	if (!file) {
		std::cerr << "Failed to open file: " << path << std::endl;

		return {};
	}

	fseek(file, 0, SEEK_END);
	long len = ftell(file) / sizeof(uint32_t);
	rewind(file);

	std::vector<uint32_t> spirv(len);
	if (fread(spirv.data(), sizeof(uint32_t), len, file) != size_t(len))
		spirv.clear();

	fclose(file);
	return spirv;
}

void writeFile(const char* path, const char* string) {
	FILE *file = fopen(path, "w");
	if (!file) {
		std::cerr << "Failed to write file: " << path << std::endl;
	}

	fprintf(file, "%s", string);
	fclose(file);
}

std::string readFileStr(const char* filename) {
    std::string content;
    std::ifstream file;
    // ensure ifstream objects can throw exceptions:
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        // open files
        file.open(filename);
        std::stringstream stream;
        // read file's buffer contents into streams
        stream << file.rdbuf();
        // close file handlers
        file.close();
        // convert stream into string
        content = stream.str();
    }
    catch(std::ifstream::failure e) {
        std::cout << "Error: could not open file '" << filename << "'" << std::endl;
    }

    return content;
}

struct PushConstant {
    std::string name;
    uint32_t outBufferBinding;
};

struct UniformBufferBinding {
    std::string name;
    uint32_t inSet;
    uint32_t inBinding;
    uint32_t outBufferBinding;
};

struct SampledImageBinding {
    std::string name;
    uint32_t inSet;
    uint32_t inBinding;
    uint32_t outTextureBinding;
    uint32_t outSamplerBinding;
};

void compileSpirvToMSL(std::string spirvSrcFile, std::string mslDstFile, std::string glslDstFile, std::string shaderBundleFile) {
    std::vector<uint32_t> spirvBinary = readFile(spirvSrcFile.c_str());
    auto movedSpirvBinary = std::move(spirvBinary);

    //MSL
	spirv_cross::CompilerMSL msl(movedSpirvBinary);

	// Set some options.
	//spirv_cross::CompilerMSL::Options options;
    //options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 0);
	//msl.set_msl_options(options);

	std::string mslSource = msl.compile();

    writeFile(mslDstFile.c_str(), mslSource.c_str());

    //GLSL
	spirv_cross::CompilerGLSL glsl(movedSpirvBinary);
    
    spirv_cross::CompilerGLSL::Options options;
    options.version = 410;
    options.emit_push_constant_as_uniform_buffer = true;
    options.enable_420pack_extension = false;
    //Does not work on Apple devices for some reason
    //options.separate_shader_objects = true;
    glsl.set_common_options(options);

	std::string glslSource = glsl.compile();

    writeFile(glslDstFile.c_str(), glslSource.c_str());

    //Bindings
    PushConstant* pushConstant = nullptr;
    std::vector<UniformBufferBinding> uniformBufferBindings;
    std::vector<SampledImageBinding> sampledImageBindings;

	spirv_cross::ShaderResources resources = msl.get_shader_resources();

	for (auto& resource : resources.push_constant_buffers) {
        auto& glslResource = glsl.get_shader_resources().push_constant_buffers[0];
        std::cout << glsl.get_name(glslResource.id) << std::endl;
        pushConstant = new PushConstant{
            glslResource.name,
            msl.get_automatic_msl_resource_binding(resource.id)
        };
    }

    for (auto& resource : resources.uniform_buffers) {
        UniformBufferBinding uniformBufferBinding{
            resource.name,
            msl.get_decoration(resource.id, spv::DecorationDescriptorSet),
            msl.get_decoration(resource.id, spv::DecorationBinding),
            msl.get_automatic_msl_resource_binding(resource.id)
        };

        /*
        spirv_cross::MSLResourceBinding resBinding;
        resBinding.stage = spv::ExecutionModelFragment;
        resBinding.desc_set = set;
        resBinding.binding = binding;
        resBinding.msl_buffer = uniformBufferBinding.outBufferBinding;

        msl.add_msl_resource_binding(resBinding);
        */

        uniformBufferBindings.push_back(uniformBufferBinding);
    }

	for (auto& resource : resources.sampled_images) {
        SampledImageBinding sampledImageBinding{
            resource.name,
            msl.get_decoration(resource.id, spv::DecorationDescriptorSet),
            msl.get_decoration(resource.id, spv::DecorationBinding),
            msl.get_automatic_msl_resource_binding(resource.id),
            msl.get_automatic_msl_resource_binding_secondary(resource.id)
        };
        
        /*
        spirv_cross::MSLResourceBinding resBinding;
        resBinding.stage = spv::ExecutionModelFragment;
        resBinding.desc_set = set;
        resBinding.binding = binding;
        resBinding.msl_texture = sampledImageBinding.outTextureBinding;
        resBinding.msl_sampler = sampledImageBinding.outSamplerBinding;

        msl.add_msl_resource_binding(resBinding);
        */

        sampledImageBindings.push_back(sampledImageBinding);
	}

    //Write the shader resource file
    nh::json JSON;

    JSON["stage"] = "unknown";

    if (pushConstant != nullptr) {
        JSON["pushConstant"]["name"] = pushConstant->name;
        JSON["pushConstant"]["bufferBinding"] = pushConstant->outBufferBinding;
    }
    
    JSON["maxSet"] = "-1";

#define UPDATE_DESCRIPTOR_SET_MAX_BINDING(inSet, inBinding) \
auto& descriptorSets = JSON["descriptorSets"]; \
std::string maxSet = "0"; \
if (descriptorSets.contains("maxSet")) \
    maxSet = descriptorSets["maxSet"]; \
JSON["maxSet"] = std::to_string(std::max((uint32_t)stoi(maxSet), inSet)); \
auto& descriptorSet = descriptorSets[std::to_string(inSet)]; \
std::string maxBinding = "0"; \
if (descriptorSet.contains("maxBinding")) \
    maxBinding = descriptorSet["maxBinding"]; \
descriptorSet["maxBinding"] = std::to_string(std::max((uint32_t)stoi(maxBinding), inBinding)); \
auto& binding = descriptorSet["bindings"][std::to_string(inBinding)];

    for (auto& uniformBufferBinding : uniformBufferBindings) {
        UPDATE_DESCRIPTOR_SET_MAX_BINDING(uniformBufferBinding.inSet, uniformBufferBinding.inBinding);
        binding["name"] = uniformBufferBinding.name;
        binding["descriptorType"] = "uniformBuffer";
        binding["bufferBinding"] = uniformBufferBinding.outBufferBinding;
    }

    for (auto& sampledImageBinding : sampledImageBindings) {
        UPDATE_DESCRIPTOR_SET_MAX_BINDING(sampledImageBinding.inSet, sampledImageBinding.inBinding);
        binding["name"] = sampledImageBinding.name;
        binding["descriptorType"] = "combinedImageSampler";
        binding["textureBinding"] = sampledImageBinding.outTextureBinding;
        binding["samplerBinding"] = sampledImageBinding.outSamplerBinding;
    }

    std::ofstream out(shaderBundleFile);
    out << std::setw(4) << JSON << std::endl;
}

nh::json JSON;

std::string compilerPath = "/Users/samuliak/VulkanSDK/1.3.236.0/macOS/bin/glslc";

void compileShaders(std::string vulkanSourceDir, std::string vulkanCompDir, std::string metalSourceDir, std::string metalCompDir, std::string openglSourceDir, std::string shaderBundlesDir) {
    bool compiled = false;
    auto dirIterator = std::filesystem::directory_iterator(vulkanSourceDir);
    for (auto& dirEntry : dirIterator) {
        std::string path = std::filesystem::current_path().string() + "/" + dirEntry.path().string();

        struct stat result;
        if (stat(path.c_str(), &result) == 0) {
            auto modTime = result.st_mtime;
            //std::cout << modTime << std::endl;
            if (JSON[path] != modTime) {
                JSON[path] = modTime;
                std::string filename = dirEntry.path().filename().string();
                std::string filenameStem = dirEntry.path().stem().string();
                std::cout << "Compiling '" << filename << "'" << std::endl;
                std::string vulkanCompPath = vulkanCompDir + "/" + filenameStem + ".spv";
                system((compilerPath + " " + vulkanSourceDir + "/" + filename + " -o " + vulkanCompPath).c_str());
                std::string metalSourcePath = metalSourceDir + "/" + filenameStem + ".metal";
                std::string openglSourcePath = openglSourceDir + "/" + filenameStem + ".glsl";
                //system((spirvCrossPath + " " + vulkanCompPath + " --output " + metalSourcePath + " --msl").c_str()); // --msl-force-native-arrays
                compileSpirvToMSL(vulkanCompPath, metalSourcePath, openglSourcePath, shaderBundlesDir + "/" + filenameStem + ".json");
                std::string metalCompPath = metalCompDir + "/" + filenameStem + ".air";
                system(("xcrun -sdk macosx metal -gline-tables-only -frecord-sources -c " + metalSourcePath + " -o " + metalCompPath).c_str());
                system(("xcrun -sdk macosx metallib " + metalCompPath + " -o " + metalCompDir + "/" + filenameStem + ".metallib").c_str());
                compiled = true;
            }
        }
    }
    if (!compiled) {
        std::cout << "Nothing to do for '" << vulkanSourceDir << "'" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    //std::cout << argc << std::endl;
    if (argc == 1) {
        std::cout << "You must enter a valid shader directory" << std::endl;
        return 0;
    } else if (argc > 2) {
        std::cout << "You must enter exactly 1 argument" << std::endl;
        return 0;
    }
    
    std::string directory(argv[1]);
    std::string filesPath = std::string(argv[0]);
    filesPath = filesPath.substr(0, filesPath.find_last_of("/")) + "/files.json";

    std::string text = readFileStr(filesPath.c_str());
    JSON = nh::json::parse(text);

    compileShaders(directory + "/vulkan/source/vertex", directory + "/vulkan/compiled/vertex", directory + "/metal/source/vertex", directory + "/metal/compiled/vertex", directory + "/opengl/source/vertex", directory + "/shader_bundles/vertex");
    compileShaders(directory + "/vulkan/source/fragment", directory + "/vulkan/compiled/fragment", directory + "/metal/source/fragment", directory + "/metal/compiled/fragment", directory + "/opengl/source/fragment", directory + "/shader_bundles/fragment");
    compileShaders(directory + "/vulkan/source/compute", directory + "/vulkan/compiled/compute", directory + "/metal/source/compute", directory + "/metal/compiled/compute", directory + "/opengl/source/compute", directory + "/shader_bundles/compute");

    std::ofstream out(filesPath);
    out << std::setw(4) << JSON << std::endl;

    return 0;
}
