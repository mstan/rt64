//
// RT64
//

#pragma once

#include "rt64_shader_common.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#include "re-spirv/re-spirv.h"

#include "rhi/rt64_render_interface.h"
#include "shared/rt64_blender.h"
#include "shared/rt64_color_combiner.h"
#include "shared/rt64_other_mode.h"

#include "rt64_shader_library.h"
#include "rt64_shader_compiler.h"

// Require the use of sample locations for MSAA.
#define SAMPLE_LOCATIONS_REQUIRED 1

namespace RT64 {
    struct OptimizerCacheSPIRV {
        respv::Shader rasterVS;
        respv::Shader rasterVSFlat;
        respv::Shader rasterPS;
        respv::Shader rasterPSMS;
        respv::Shader rasterPSDepth;
        respv::Shader rasterPSDepthMS;
        respv::Shader rasterPSFlatDepth;
        respv::Shader rasterPSFlatDepthMS;
        respv::Shader rasterPSFlat;
        respv::Shader rasterPSFlatMS;

        void initialize();
    };

    struct PipelineCreation {
        RenderDevice *device;
        const RenderPipelineLayout *pipelineLayout;
        const RenderShader *vertexShader;
        const RenderShader *pixelShader;
        bool alphaBlend;
        bool culling;
        bool NoN;
        bool zCmp;
        bool zUpd;
        bool cvgAdd;
        bool usesHDR;
        // Conservative rasterization closes sub-pixel gaps between adjacent
        // emulated TRIANGLES (e.g. fragment-57 HUD Vtx grids). It must NOT be
        // applied to N64 rectangles: a rect is one self-contained quad whose
        // two triangles share an exact diagonal that standard rasterization
        // already covers once; conservative raster covers that diagonal from
        // BOTH triangles, double-blending it on alpha-blended rects and
        // producing a corner-to-corner seam on fullscreen transition fades.
        // Rect-projection draws are routed to a variant with this false.
        bool conservativeRaster = true;
        std::vector<RenderSpecConstant> specConstants;
        RenderMultisampling multisampling;
    };

    struct RasterShaderText {
        std::string vertexShader;
        std::string pixelShader;
    };

    struct RasterShader {
        ShaderDescription desc;
        RenderDevice *device;
        std::unique_ptr<RenderPipeline> pipeline;

        RasterShader(RenderDevice *device, const ShaderDescription &desc, const RenderPipelineLayout *pipelineLayout, RenderShaderFormat shaderFormat, const RenderMultisampling &multisampling, 
            const ShaderCompiler *shaderCompiler, const OptimizerCacheSPIRV *optimizerCacheSPIRV);

        ~RasterShader();
        static RasterShaderText generateShaderText(const ShaderDescription &desc, bool multisampling);
        static std::unique_ptr<RenderPipeline> createPipeline(const PipelineCreation &c);
        static RenderMultisampling generateMultisamplingPattern(uint32_t sampleCount, bool sampleLocationsSupported);
    };

    struct RasterShaderUber {
        static const uint64_t RasterVSLibraryHash;
        static const uint64_t RasterPSLibraryHash;

        // Indexed by pipelineStateIndex(zCmp, zUpd, cvgAdd, conservative):
        // bits 0-2 are the depth/coverage state, bit 3 is conservative raster.
        // The non-conservative half (bit 3 = 0) is used for rect draws.
        std::unique_ptr<RenderPipeline> pipelines[16];
        std::unique_ptr<RenderPipeline> postBlendDitherNoiseAddPipeline;
        std::unique_ptr<RenderPipeline> postBlendDitherNoiseSubPipeline;
        std::unique_ptr<RenderPipeline> postBlendDitherNoiseSubNegativePipeline;
        std::mutex firstPipelineMutex;
        std::condition_variable firstPipelineCondition;
        bool pipelinesCreated = false;
        std::unique_ptr<RenderPipelineLayout> pipelineLayout;
        std::vector<std::vector<PipelineCreation>> pipelineThreadCreations;
        std::vector<std::unique_ptr<std::thread>> pipelineThreads;
        std::unique_ptr<RenderShader> vertexShader;
        std::unique_ptr<RenderShader> pixelShader;

        RasterShaderUber(RenderDevice *device, RenderShaderFormat shaderFormat, const RenderMultisampling &multisampling, const ShaderLibrary *shaderLibrary, uint32_t threadCount);
        ~RasterShaderUber();
        void threadCreatePipelines(uint32_t threadIndex);
        void waitForPipelineCreation();
        uint32_t pipelineStateIndex(bool zCmp, bool zUpd, bool cvgAdd, bool conservative) const;
        const RenderPipeline *getPipeline(bool zCmp, bool zUpd, bool cvgAdd, bool conservative) const;
    };
};