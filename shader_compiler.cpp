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

#include "spirv_msl.hpp"

bool isNumber(const std::string & s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

std::vector<uint32_t> readFile(const char* path) {
	FILE *file = fopen(path, "rb");
	if (!file) {
		std::cerr << "Failed to open file: " << path << std::endl;

		return {};
	}

	fseek(file, 0, SEEK_END);
	long len = ftell(file) / sizeof(uint32_t);
	rewind(file);

	std::vector<uint32_t> fileData(len);
	if (fread(fileData.data(), sizeof(uint32_t), len, file) != size_t(len))
		fileData.clear();

	fclose(file);

	return fileData;
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

void writeFile(const char* path, const char* string) {
	FILE *file = fopen(path, "w");
	if (!file) {
		std::cerr << "Failed to write file: " << path << std::endl;
	}

	fprintf(file, "%s", string);
	fclose(file);
}

struct PushConstant {
    //std::string name;
    uint32_t outBufferBinding;
};

struct BufferBinding {
    //std::string name;
    uint32_t inSet;
    uint32_t inBinding;
    uint32_t outBufferBinding;
};

struct SampledImageBinding {
    //std::string name;
    uint32_t inSet;
    uint32_t inBinding;
    uint32_t outTextureBinding;
    uint32_t outSamplerBinding;
};

struct ImageBinding {
    //std::string name;
    uint32_t inSet;
    uint32_t inBinding;
    uint32_t outTextureBinding;
};

nh::json compileSpirvToMSL(std::string tempDir, std::string spirvSrcFile) {
    std::vector<uint32_t> spirvBinary = readFile(spirvSrcFile.c_str());
    auto movedSpirvBinary = std::move(spirvBinary);

    //MSL
	spirv_cross::CompilerMSL msl(movedSpirvBinary);

	// Set some options.
	spirv_cross::CompilerMSL::Options options = msl.get_msl_options();
    //options.platform = spirv_cross::CompilerMSL::Options::Platform::macOS;
    options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(3, 0);
    options.use_framebuffer_fetch_subpasses = true;
	msl.set_msl_options(options);

	std::string mslSource = msl.compile();

    writeFile((tempDir + "/temp.metal").c_str(), mslSource.c_str());
    //std::cout << "METAL SOURCE:\n\n" << mslSource << "\n\n\n\n" << std::endl;

    //GLSL
    /*
	spirv_cross::CompilerGLSL glsl(movedSpirvBinary);
    
    spirv_cross::CompilerGLSL::Options options;
    options.version = 410;
    options.emit_push_constant_as_uniform_buffer = true;
    options.enable_420pack_extension = false;
    //Does not work on Apple devices for some reason
    //options.separate_shader_objects = true;
    glsl.set_common_options(options);

	std::string glslSource = glsl.compile();

    writeFile((tempDir + "/temp.glsl").c_str(), glslSource.c_str());
    */

    //Bindings
    PushConstant* pushConstant = nullptr;
    std::vector<BufferBinding> bufferBindings;
    std::vector<SampledImageBinding> sampledImageBindings;
    std::vector<ImageBinding> imageBindings;

	spirv_cross::ShaderResources resources = msl.get_shader_resources();

	for (auto& resource : resources.push_constant_buffers) {
        //auto& glslResource = glsl.get_shader_resources().push_constant_buffers[0];
        //std::cout << glsl.get_name(glslResource.id) << std::endl;
        pushConstant = new PushConstant{
            //glslResource.name,
            msl.get_automatic_msl_resource_binding(resource.id)
        };
    }

    for (auto& resource : resources.uniform_buffers) {
        BufferBinding uniformBufferBinding{
            //resource.name,
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

        bufferBindings.push_back(uniformBufferBinding);
    }

    for (auto& resource : resources.storage_buffers) {
        BufferBinding storageSpaceBufferBinding{
            msl.get_decoration(resource.id, spv::DecorationDescriptorSet),
            msl.get_decoration(resource.id, spv::DecorationBinding),
            msl.get_automatic_msl_resource_binding(resource.id)
        };

        bufferBindings.push_back(storageSpaceBufferBinding);
    }

	for (auto& resource : resources.sampled_images) {
        SampledImageBinding sampledImageBinding{
            //resource.name,
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

	for (auto& resource : resources.storage_images) {
        ImageBinding imageBinding{
            msl.get_decoration(resource.id, spv::DecorationDescriptorSet),
            msl.get_decoration(resource.id, spv::DecorationBinding),
            msl.get_automatic_msl_resource_binding(resource.id)
        };

        imageBindings.push_back(imageBinding);
	}

    for (auto& resource : resources.subpass_inputs) {
        ImageBinding imageBinding{
            //resource.name,
            msl.get_decoration(resource.id, spv::DecorationDescriptorSet),
            msl.get_decoration(resource.id, spv::DecorationBinding),
            msl.get_automatic_msl_resource_binding(resource.id)
        };

        imageBindings.push_back(imageBinding);
    }

    //Write the shader resource file
    nh::json shaderJSON;

    shaderJSON["stage"] = "unknown";

    if (pushConstant != nullptr) {
        //JSON["pushConstant"]["name"] = pushConstant->name;
        shaderJSON["pushConstant"]["bufferBinding"] = pushConstant->outBufferBinding;
    }
    
    shaderJSON["maxSet"] = "-1";

#define UPDATE_DESCRIPTOR_SET_MAX_BINDING(inSet, inBinding) \
auto& descriptorSets = shaderJSON["descriptorSets"]; \
std::string maxSet = "0"; \
if (descriptorSets.contains("maxSet")) \
    maxSet = descriptorSets["maxSet"]; \
shaderJSON["maxSet"] = std::to_string(std::max((uint32_t)stoi(maxSet), inSet)); \
auto& descriptorSet = descriptorSets[std::to_string(inSet)]; \
std::string maxBinding = "0"; \
if (descriptorSet.contains("maxBinding")) \
    maxBinding = descriptorSet["maxBinding"]; \
descriptorSet["maxBinding"] = std::to_string(std::max((uint32_t)stoi(maxBinding), inBinding)); \
auto& binding = descriptorSet["bindings"][std::to_string(inBinding)];

    for (auto& bufferBinding : bufferBindings) {
        UPDATE_DESCRIPTOR_SET_MAX_BINDING(bufferBinding.inSet, bufferBinding.inBinding);
        //binding["name"] = bufferBinding.name;
        binding["descriptorType"] = "buffer";
        binding["bufferBinding"] = bufferBinding.outBufferBinding;
    }

    for (auto& sampledImageBinding : sampledImageBindings) {
        UPDATE_DESCRIPTOR_SET_MAX_BINDING(sampledImageBinding.inSet, sampledImageBinding.inBinding);
        //binding["name"] = sampledImageBinding.name;
        binding["descriptorType"] = "combinedImageSampler";
        binding["textureBinding"] = sampledImageBinding.outTextureBinding;
        binding["samplerBinding"] = sampledImageBinding.outSamplerBinding;
    }

    for (auto& imageBinding : imageBindings) {
        UPDATE_DESCRIPTOR_SET_MAX_BINDING(imageBinding.inSet, imageBinding.inBinding);
        //binding["name"] = imageBinding.name;
        binding["descriptorType"] = "image";
        binding["textureBinding"] = imageBinding.outTextureBinding;
    }

    return shaderJSON;
}

const std::string INPUT_ATTACHMENT_INDEX_NAME = "input_attachment_index";
const std::string COLOR_ATTACHMENT_INDEX_NAME = "color_attachment_index";
const std::string DEPTH_ATTACHMENT_NAME = "depth_attachment";
const std::string LOCATION_NAME = "location";

struct MappedAttachment {
    uint32_t index;
    size_t pos;
};

struct PreprocessedSource {
    std::string source;
    std::vector<MappedAttachment> mappedAttachments;
};

PreprocessedSource preprocessGlslShader(std::string filename) {
    PreprocessedSource source;
    source.source = readFileStr(filename.c_str());

    //Gather and remove all the aditional information from the shader
    size_t pos = 0;
    while (true) {
        pos = source.source.find(LOCATION_NAME, pos);

        if (pos == std::string::npos)
            break;

        pos = source.source.find_first_not_of(" =", pos + LOCATION_NAME.size());
        std::string locationIndexStr = source.source.substr(pos, 1);
        if (!isNumber(locationIndexStr))
            continue;
        uint32_t locationIndex = std::stoi(locationIndexStr);

        //std::cout << "Color attachment: " << locationIndex << std::endl;

        pos++;
        size_t removePos = pos, removeSize = 0;
        pos = source.source.find_first_not_of(", ", pos);
        if (source.source.substr(pos, COLOR_ATTACHMENT_INDEX_NAME.size()) == COLOR_ATTACHMENT_INDEX_NAME) {
            pos = source.source.find_first_not_of(" =", pos + COLOR_ATTACHMENT_INDEX_NAME.size());
            removeSize = pos - removePos + 1;
            uint32_t colorAttachmentIndex = std::stoi(source.source.substr(pos, 1));

            //std::cout << "Color attachment index: " << colorAttachmentIndex << std::endl;

            source.source.replace(removePos, removeSize, "");
            source.mappedAttachments.push_back({colorAttachmentIndex, removePos - 1});
        }
    }

    pos = 0;
    while (true) {
        pos = source.source.find(INPUT_ATTACHMENT_INDEX_NAME, pos);

        if (pos == std::string::npos)
            break;
        
        pos = source.source.find_first_not_of(" =", pos + INPUT_ATTACHMENT_INDEX_NAME.size());
        std::string inputAttachmentIndexStr = source.source.substr(pos, 1);
        if (!isNumber(inputAttachmentIndexStr))
            continue;
        uint32_t inputAttachmentIndex = std::stoi(inputAttachmentIndexStr);

        //std::cout << "Input attachment: " << inputAttachmentIndex << std::endl;

        pos++;
        size_t removePos = pos, removeSize = 0;
        pos = source.source.find_first_not_of(", ", pos);
        if (source.source.substr(pos, COLOR_ATTACHMENT_INDEX_NAME.size()) == COLOR_ATTACHMENT_INDEX_NAME) {
            pos = source.source.find_first_not_of(" =", pos + COLOR_ATTACHMENT_INDEX_NAME.size());
            removeSize = pos - removePos + 1;
            uint32_t colorAttachmentIndex = std::stoi(source.source.substr(pos, 1));

            //std::cout << "Color attachment index: " << colorAttachmentIndex << std::endl;

            source.mappedAttachments.push_back({colorAttachmentIndex, removePos - 1});
        } else if (source.source.substr(pos, DEPTH_ATTACHMENT_NAME.size()) == DEPTH_ATTACHMENT_NAME) {
            //removeSize = pos - removePos + DEPTH_ATTACHMENT_NAME.size();
            throw std::runtime_error("Depth attachment is not currently supported as an input attachment. To use it with the Vulkan backend, just make it a color attachment with random index");
        } else {
            throw std::runtime_error("Input attachment must have a color or depth attachment associated with it");
        }
        source.source.replace(removePos, removeSize, "");
    }

    return source;
}

void defineGlslMacro(std::string& source, std::string macroName) {
    size_t pos = source.find("#version");
    pos = source.find("\n", pos);
    source.insert(pos + 1, "#define " + macroName + "\n");
}

void mapGlslAttachmentForMsl(PreprocessedSource& source) {
    for (auto& mappedAttachment : source.mappedAttachments) {
        source.source.replace(mappedAttachment.pos, 1, std::to_string(mappedAttachment.index));
        //std::cout << "INDEX: " << mappedAttachment.index << " : " << mappedAttachment.pos << std::endl;
    }
    //std::cout << "SOURCE:\n" << source.source << std::endl;
}

nh::json filesJSON;

std::string compilerPath = "/Users/samuliak/VulkanSDK/1.3.236.0/macOS/bin/glslc";

std::string includeSource =
"#extension GL_AMD_gpu_shader_half_float: enable\n"
"#define float2 vec2\n"
"#define float3 vec3\n"
"#define float4 vec4\n"
"#define float3x3 mat3\n"
"#define float4x4 mat4\n"
"#define half float16_t\n"
"#define half2 f16vec2\n"
"#define half3 f16vec3\n"
"#define half4 f16vec4\n"
"#define half3x3 f16mat3\n"
"#define half4x4 f16mat4";

void compileShaders(std::string tempDir, std::string sourceDir, std::string compiledDir) {
    struct stat result;
    if (stat(sourceDir.c_str(), &result) == 0) {
        bool compiled = false;
        auto dirIterator = std::filesystem::directory_iterator(sourceDir);
        for (auto& dirEntry : dirIterator) {
            std::string path = std::filesystem::current_path().string() + "/" + dirEntry.path().string();

            if (stat(path.c_str(), &result) == 0) {
                auto modTime = result.st_mtime;
                //std::cout << modTime << std::endl;
                if (filesJSON[path] != modTime) {
                    filesJSON[path] = modTime;
                    std::string filename = dirEntry.path().filename().string();
                    std::string filenameStem = dirEntry.path().stem().string();
                    std::string filenameExt = dirEntry.path().extension();
                    std::cout << "Compiling '" << filename << "'" << std::endl;
                    std::string spirvCompiledFile1 = tempDir + "/temp1.spv";
                    std::string spirvCompiledFile2 = tempDir + "/temp2.spv";
                    std::string glslSourceFile1 = tempDir + "/temp1." + filenameExt;
                    std::string glslSourceFile2 = tempDir + "/temp2." + filenameExt;

                    PreprocessedSource glslSource1 = preprocessGlslShader(sourceDir + "/" + filename);
                    PreprocessedSource glslSource2 = glslSource1;
                    mapGlslAttachmentForMsl(glslSource2);
                    defineGlslMacro(glslSource1.source, "LV_BACKEND_VULKAN");
                    defineGlslMacro(glslSource2.source, "LV_BACKEND_METAL");
                    writeFile(glslSourceFile1.c_str(), glslSource1.source.c_str());
                    writeFile(glslSourceFile2.c_str(), glslSource2.source.c_str());

                    system((compilerPath + " " + glslSourceFile1 + " -o " + spirvCompiledFile1).c_str());
                    system((compilerPath + " " + glslSourceFile2 + " -o " + spirvCompiledFile2).c_str());

                    //std::string metalSourcePath = metalSourceDir + "/" + filenameStem + ".metal";
                    //std::string openglSourcePath = openglSourceDir + "/" + filenameStem + ".glsl";
                    nh::json shaderJSON = compileSpirvToMSL(tempDir, spirvCompiledFile2);
                    std::string airPath = tempDir + "/temp.air";
                    std::string metallibPath = tempDir + "/temp.metallib";

                    system(("xcrun -sdk macosx metal -gline-tables-only -frecord-sources -c " + tempDir + "/temp.metal -o " + airPath).c_str());
                    system(("xcrun -sdk macosx metallib " + airPath + " -o " + metallibPath).c_str());

                    std::string spirvFile = readFileStr(spirvCompiledFile1.c_str());
                    std::string metallibFile = readFileStr(metallibPath.c_str());
                    //shaderJSON[".spirv"] = spirvFile;
                    //shaderJSON[".metallib"] = metallibFile;

                    std::ofstream out(compiledDir + "/" + filenameStem + ".json");
                    out << std::setw(4) << shaderJSON << "\nsection.spv" << spirvFile << "section.metallib" << metallibFile;
                    out.close();

                    compiled = true;
                }
            }
        }
        if (!compiled) {
            std::cout << "Nothing to do for '" << sourceDir << "'" << std::endl;
        }
    } else {
        std::cout << "No such file or directory '" << sourceDir << "'" << std::endl;
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
    filesJSON = nh::json::parse(text);

    std::string tempDir = directory + "/.temp";
    mkdir(tempDir.c_str(), 0700);

    std::ofstream includeSourceOut(tempDir + "/lava_common.glsl");
    includeSourceOut << includeSource;
    includeSourceOut.close();

    compileShaders(tempDir, directory + "/source/vertex", directory + "/compiled/vertex");
    compileShaders(tempDir, directory + "/source/fragment", directory + "/compiled/fragment");
    compileShaders(tempDir, directory + "/source/compute", directory + "/compiled/compute");

    std::ofstream out(filesPath);
    out << std::setw(4) << filesJSON << std::endl;
    out.close();

    return 0;
}
