// Exercises production sampling with spatially varying flow and texture detail.
// These image regressions complement the original ideal-field/motion tests.
#define main original_pip_flow_gpu_test_main
#include "pip_flow_gpu_test.cpp"
#undef main

static Pixel wall(float x, float y)
{
    const float fine = 0.035f * std::sin(x * 0.71f + y * 0.23f) +
        0.025f * std::cos(y * 0.51f - x * 0.17f);
    const float coarse = 0.05f * std::sin(x * 0.031f) * std::cos(y * 0.019f);
    const float mortar = std::fmod(y, 17.f) < 1.f ? -0.035f : 0.f;
    return {0.28f + fine + coarse + mortar, 0.37f + fine * 0.7f + coarse,
        0.21f + fine * 0.4f + coarse * 0.5f, 1.f};
}

static Pixel foreground(float x, float y)
{
    const float texture = 0.055f * std::sin(x * 0.31f + y * 0.17f) +
        0.045f * std::cos(y * 0.27f - x * 0.11f);
    return {0.64f + texture, 0.18f + texture * 0.6f, 0.12f + texture * 0.3f, 1.f};
}

static float maximumError(const Pixel& actual, const Pixel& expected)
{
    float maximum = 0.f;
    for (size_t channel = 0; channel < 3; ++channel) {
        require(std::isfinite(actual[channel]), "Nonfinite production lens output");
        maximum = std::max(maximum, std::abs(actual[channel] - expected[channel]));
    }
    return maximum;
}

int main(int argc, char** argv)
{
    try {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        require(argc >= 2 && argc <= 3, "Usage: pip_block_artifact_gpu_test.exe <engine-root> [lens-shader-root]");
        const fs::path root = fs::absolute(argv[1]);
        const fs::path lensRoot = argc == 3 ? fs::absolute(argv[2]) : root / "gamedata/shaders/r3";
        const fs::path outputRoot = root / "_build/v142-validation";
        fs::create_directories(outputRoot);
        const fs::path wrapper = outputRoot / "lens_block_wrapper.ps";
        std::ofstream source(wrapper);
        source << "Texture2D<float4> s_second_vp, s_prev_frame, s_position;\n"
            "SamplerState smp_base;\n"
            "float4 screen_res, scope_lense_imaging, s3ds_param_3, markswitch_current;\n"
            "float4x4 scope_lense_reproject;\n"
            "#include \"anthology_pip_temporal.h\"\n"
            "float4 main(float4 pos:SV_Position,float2 uv:TEXCOORD0):SV_Target { return sample_second_vp(uv); }\n";
        source.close();
        Includes includes({lensRoot, root / "gamedata/shaders/r3", root / "_build/shader_validation/v140/r3"});
        Gpu gpu;
        Shader lens(gpu.device.Get(), wrapper, includes);
        constexpr UINT width = 256, height = 192, flowWidth = width / 4, flowHeight = height / 4;
        const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        lens.constant("scope_lense_reproject", identity);
        lens.constant("screen_res", std::array<float, 4>{float(width), float(height), 1.f / width, 1.f / height});
        lens.constant("scope_lense_camera", std::array<float, 4>{0, 0, 0, 0});
        lens.constant("scope_lense_main_view", std::array<float, 4>{1, 1, 0.5f, 0.5f});
        lens.constant("s3ds_param_3", std::array<float, 4>{0, 0, 0, 0});
        lens.constant("markswitch_current", std::array<float, 4>{1, 0, 0, 0});
        lens.constant("scope_lense_imaging", std::array<float, 4>{0, 0, 0, 0});
        lens.constant("scope_lense_motion", std::array<float, 4>{0.5f, 1, 0.0165f, 0});
        size_t failures = 0;
        auto expectation = [&](bool passed, const char* message) {
            if (!passed) { ++failures; std::fprintf(stderr, "CHECK FAILED: %s\n", message); }
        };
        auto render = [&](Texture& captured, Texture& mainImage, Texture& depth, Texture& mainPosition, Texture& flow) {
            auto output = gpu.texture(width, height);
            lens.texture(gpu.context.Get(), "s_second_vp", captured);
            if (lens.hasTexture("s_prev_frame")) lens.texture(gpu.context.Get(), "s_prev_frame", mainImage);
            lens.texture(gpu.context.Get(), "s_second_vp_depth", depth);
            lens.texture(gpu.context.Get(), "s_position", mainPosition);
            lens.texture(gpu.context.Get(), "s_second_vp_flow", flow);
            gpu.draw(lens, output);
            return gpu.read(output);
        };
        if (fs::exists(root / "gamedata/shaders/r3/svp_scene_capture.ps")) {
            Shader capture(gpu.device.Get(), root / "gamedata/shaders/r3/svp_scene_capture.ps", includes);
            for (const bool reduced : {false, true}) {
                const UINT inputWidth = reduced ? 37u : width, inputHeight = reduced ? 23u : height;
                std::vector<Pixel> input(size_t(inputWidth) * inputHeight);
                for (UINT y = 0; y < inputHeight; ++y) for (UINT x = 0; x < inputWidth; ++x)
                    input[size_t(y) * inputWidth + x] = {float(x) / (inputWidth - 1), float(y) / (inputHeight - 1),
                        0.5f + 0.2f * std::sin(float(x) * 0.31f + float(y) * 0.17f), 1.f};
                auto from = gpu.texture(inputWidth, inputHeight, input);
                auto to = gpu.texture(width, height);
                capture.texture(gpu.context.Get(), "s_svp_scene", from);
                gpu.draw(capture, to);
                const auto captured = gpu.read(to);
                size_t incorrect = 0;
                float maximum = 0.f;
                for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
                    const float sx = (float(x) + 0.5f) / width * inputWidth - 0.5f;
                    const float sy = (float(y) + 0.5f) / height * inputHeight - 0.5f;
                    const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                    const float fx = sx - std::floor(sx), fy = sy - std::floor(sy);
                    Pixel expected{};
                    for (int row = 0; row <= 1; ++row) for (int column = 0; column <= 1; ++column) {
                        const UINT sampleX = UINT(std::clamp(ix + column, 0, int(inputWidth) - 1));
                        const UINT sampleY = UINT(std::clamp(iy + row, 0, int(inputHeight) - 1));
                        const float weight = (column ? fx : 1.f - fx) * (row ? fy : 1.f - fy);
                        for (size_t c = 0; c < 4; ++c) expected[c] += input[size_t(sampleY) * inputWidth + sampleX][c] * weight;
                    }
                    const float error = maximumError(captured[size_t(y) * width + x], expected);
                    maximum = std::max(maximum, error);
                    if (error > (reduced ? 0.0005f : 0.00003f)) ++incorrect;
                }
                std::printf("production pre-LUT scene capture %ux%u to%ux%u: checked=%zu mismatches=%zu max_filter_error=%.6f\n",
                    inputWidth, inputHeight, width, height, captured.size(), incorrect, maximum);
                expectation(incorrect == 0, "Scene capture changed color, cropped a reduced render target, or lost image quadrants");
            }
        }
        for (UINT scenario = 0; scenario < 3; ++scenario) {
            std::vector<Pixel> cached(size_t(width) * height), live(cached.size());
            for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
                const size_t i = size_t(y) * width + x;
                cached[i] = wall(float(x), float(y));
                // Simulates the v141 final-PiP / pre-LUT-main stage mismatch.
                live[i] = {cached[i][0] * 0.65f + 0.07f, cached[i][1] * 0.78f + 0.03f,
                    cached[i][2] * 1.2f + 0.04f, 1.f};
            }
            std::vector<Pixel> vectors(size_t(flowWidth) * flowHeight);
            for (UINT y = 0; y < flowHeight; ++y) for (UINT x = 0; x < flowWidth; ++x) {
                const bool sentinel = ((x / 3 + y / 2) % 2) != 0;
                Pixel value = {0, 0, 20, sentinel ? -1.f : 1.f};
                if (scenario == 1) value = {(float(int(x % 7) - 3) * 2.f) / width,
                    float(int(y % 5) - 2) / height, 20, sentinel ? -1.f : 0.f};
                if (scenario == 2) value[3] = sentinel ? -0.55f : float((x * 13 + y * 7) % 11) / 10.f;
                vectors[size_t(y) * flowWidth + x] = value;
            }
            auto capturedTexture = gpu.texture(width, height, cached);
            auto mainTexture = gpu.texture(width, height, live);
            auto depth = gpu.texture(width, height, std::vector<Pixel>(cached.size(), {20, 0, 0, 0}));
            auto mainPosition = gpu.texture(width, height,
                std::vector<Pixel>(cached.size(), {0, 0, scenario == 2 ? 20.3f : 20.f, 0}));
            auto flow = gpu.texture(flowWidth, flowHeight, vectors);
            const auto result = render(capturedTexture, mainTexture, depth, mainPosition, flow);
            float maxError = 0.f, maxExcessEdge = 0.f;
            size_t mismatches = 0, badBlockEdges = 0, checks = 0;
            // Perspective division can cross D3D11's finite subtexel filtering
            // precision even for an algebraically unchanged zero-translation UV.
            const float colorTolerance = scenario == 2 ? 0.0002f : 0.00003f;
            const float edgeTolerance = scenario == 2 ? 0.0002f : 0.00005f;
            for (UINT y = 8; y < height - 8; ++y) for (UINT x = 8; x < width - 8; ++x) {
                const size_t i = size_t(y) * width + x;
                const float error = maximumError(result[i], cached[i]);
                maxError = std::max(maxError, error);
                ++checks;
                if (error > colorTolerance) ++mismatches;
                for (const size_t j : {i - 1, i - width}) {
                    float excess = 0.f;
                    for (size_t c = 0; c < 3; ++c)
                        excess = std::max(excess, std::abs((result[i][c] - result[j][c]) - (cached[i][c] - cached[j][c])));
                    maxExcessEdge = std::max(maxExcessEdge, excess);
                    if ((x % 4 == 0 || y % 4 == 0) && excess > edgeTolerance) ++badBlockEdges;
                }
            }
            std::printf("static textured scenario%u: checked=%zu changed_pixels=%zu max_error=%.6f bad_quarter_grid_edges=%zu max_excess_edge=%.6f\n",
                scenario, checks, mismatches, maxError, badBlockEdges, maxExcessEdge);
            expectation(mismatches == 0, "Uncertain/mixed quarter-resolution flow imported main radiometry or distorted stationary captured texture");
            expectation(badBlockEdges == 0, "Quarter-resolution confidence mask created visible block edges on stationary texture");
        }

        // A textured moving foreground crosses a static textured wall. Different
        // surfaces have different depth and nonuniform flow confidence. Sources
        // share the same color stage, as the corrected production pipeline must.
        std::vector<Pixel> captured(size_t(width) * height), live(captured.size()), captureDepth(captured.size()), mainPosition(captured.size());
        auto onObject = [](UINT x, UINT y, float offset) { return float(x) >= 80.f + offset && float(x) < 148.f + offset && y >= 55 && y < 139; };
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            const bool oldObject = onObject(x, y, 0), newObject = onObject(x, y, 4);
            captured[i] = oldObject ? foreground(float(x), float(y)) : wall(float(x), float(y));
            live[i] = newObject ? foreground(float(x) - 4.f, float(y)) : wall(float(x), float(y));
            captureDepth[i] = {oldObject ? 5.f : 20.f, 0, 0, 0};
            mainPosition[i] = {0, 0, newObject ? 5.f : 20.f, 0};
        }
        std::vector<Pixel> movingFlow(size_t(flowWidth) * flowHeight);
        for (UINT y = 0; y < flowHeight; ++y) for (UINT x = 0; x < flowWidth; ++x) {
            const bool object = onObject(x * 4 + 2, y * 4 + 2, 0);
            const float confidence = object ? 0.45f + 0.5f * float((x * 3 + y * 5) % 9) / 8.f : ((x + y) % 3 == 0 ? -1.f : 1.f);
            movingFlow[size_t(y) * flowWidth + x] = {object ? 8.f / width : 0, 0, object ? 5.f : 20.f, confidence};
        }
        auto capturedTexture = gpu.texture(width, height, captured);
        auto liveTexture = gpu.texture(width, height, live);
        auto depthTexture = gpu.texture(width, height, captureDepth);
        auto mainDepthTexture = gpu.texture(width, height, mainPosition);
        auto flowTexture = gpu.texture(flowWidth, flowHeight, movingFlow);
        const auto movingResult = render(capturedTexture, liveTexture, depthTexture, mainDepthTexture, flowTexture);
        size_t backgroundErrors = 0, innerErrors = 0, silhouetteErrors = 0;
        float maxInteriorError = 0.f;
        for (UINT y = 8; y < height - 8; ++y) for (UINT x = 8; x < width - 8; ++x) {
            const size_t i = size_t(y) * width + x;
            const float error = maximumError(movingResult[i], live[i]);
            const bool farBackground = x < 64 || x > 168 || y < 40 || y > 152;
            const bool objectInterior = x >= 100 && x < 136 && y >= 72 && y < 124;
            const bool changedSilhouette = onObject(x, y, 0) != onObject(x, y, 4);
            if (farBackground && error > 0.00003f) ++backgroundErrors;
            if (objectInterior) { maxInteriorError = std::max(maxInteriorError, error); if (error > 0.003f) ++innerErrors; }
            if (changedSilhouette && error > 0.035f) ++silhouetteErrors;
        }
        std::printf("nonuniform moving textured target: background_errors=%zu interior_errors=%zu max_interior_error=%.6f silhouette_errors=%zu\n",
            backgroundErrors, innerErrors, maxInteriorError, silhouetteErrors);
        expectation(backgroundErrors == 0, "Moving target flow changed distant static detailed background");
        expectation(innerErrors == 0, "Spatial confidence variations tore textured foreground interior");
        expectation(silhouetteErrors < 40, "Confirmed disocclusion/moving silhouette failed aligned live-color or valid reprojection fallback");

        // Obtain the nonuniform vector field from the real three-level production
        // pyramid/flow shaders, then inspect local image/seam errors, not just a
        // region-averaged optical-flow endpoint or an ideal constant field.
        Shader pyramidShader(gpu.device.Get(), root / "gamedata/shaders/r3/svp_temporal_pyramid.ps", includes);
        Shader flowShader(gpu.device.Get(), root / "gamedata/shaders/r3/svp_temporal_flow.ps", includes);
        std::vector<Pixel> future(live.size()), futurePosition(live.size());
        std::vector<Pixel> currentDepth(live.size());
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            const bool object = onObject(x, y, 6);
            future[i] = object ? foreground(float(x) - 6.f, float(y)) : wall(float(x), float(y));
            futurePosition[i] = {0, 0, object ? 5.f : 20.f, 0};
            currentDepth[i] = {mainPosition[i][2], 0, 0, 0};
        }
        auto currentDepthTexture = gpu.texture(width, height, currentDepth);
        auto futureTexture = gpu.texture(width, height, future);
        auto futurePositionTexture = gpu.texture(width, height, futurePosition);
        auto emptySeed = gpu.texture(1, 1, {{0, 0, 0, 0}});
        std::array<std::array<Texture, 3>, 2> pyramid;
        for (UINT level = 0; level < 3; ++level) for (UINT history = 0; history < 2; ++history) {
            const UINT divisor = 4u << level;
            pyramid[history][level] = gpu.texture(width / divisor, height / divisor);
            Texture& input = level ? pyramid[history][level - 1] : history ? capturedTexture : liveTexture;
            const float kernel = level ? 0.5f : 1.f;
            pyramidShader.constant("svp_pyramid_step", std::array<float, 4>{kernel / input.width, kernel / input.height, 0, 0});
            pyramidShader.texture(gpu.context.Get(), "s_svp_pyramid_source", input);
            gpu.draw(pyramidShader, pyramid[history][level]);
        }
        flowShader.constant("svp_capture_projection", std::array<float, 4>{1, 1, 0, 0});
        flowShader.constant("svp_capture_to_previous", std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0,
            0, 0, 1.01f, 1, 0, 0, -0.1f, 0});
        std::array<Texture, 3> levels;
        for (UINT level = 0; level < 3; ++level) {
            const UINT divisor = 16u >> level;
            levels[level] = gpu.texture(width / divisor, height / divisor);
            flowShader.constant("svp_flow_res", std::array<float, 4>{float(divisor) / width, float(divisor) / height, float(level), 1});
            flowShader.texture(gpu.context.Get(), "s_svp_current", pyramid[0][2 - level]);
            flowShader.texture(gpu.context.Get(), "s_svp_previous", pyramid[1][2 - level]);
            flowShader.texture(gpu.context.Get(), "s_svp_depth", currentDepthTexture);
            flowShader.texture(gpu.context.Get(), "s_svp_previous_depth", depthTexture);
            flowShader.texture(gpu.context.Get(), "s_svp_flow_seed", level ? levels[level - 1] : emptySeed);
            gpu.draw(flowShader, levels[level]);
        }
        const auto actualFlow = gpu.read(levels.back());
        size_t acceptedSamples = 0, rejectedSamples = 0, staticFlowErrors = 0, interiorAccepted = 0, interiorWrong = 0;
        float minimumMotion = 10000, maximumMotion = -10000;
        for (UINT y = 2; y < flowHeight - 2; ++y) for (UINT x = 2; x < flowWidth - 2; ++x) {
            const Pixel& value = actualFlow[size_t(y) * flowWidth + x];
            if (value[3] > 0.2f) ++acceptedSamples; else ++rejectedSamples;
            if (x >= 25 && x < 34 && y >= 18 && y < 31) {
                minimumMotion = std::min(minimumMotion, value[0] * width);
                maximumMotion = std::max(maximumMotion, value[0] * width);
                if (value[3] > 0.2f) {
                    ++interiorAccepted;
                    if (std::hypot(value[0] * width - 4.f, value[1] * height) > 1.25f) ++interiorWrong;
                }
            }
            if ((x < 16 || x > 42 || y < 10 || y > 38) && value[3] > 0.2f &&
                std::hypot(value[0] * width, value[1] * height) > 0.5f) ++staticFlowErrors;
        }
        const auto actualResult = render(liveTexture, futureTexture, currentDepthTexture, futurePositionTexture, levels.back());
        std::vector<Pixel> coordinateCapture(live.size()), coordinateMain(live.size());
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            coordinateCapture[i] = {(float(x) + 0.5f) / width, (float(y) + 0.5f) / height, 0, 1};
            coordinateMain[i] = {(float(x) + 0.5f) / width, (float(y) + 0.5f) / height, 1, 1};
        }
        auto coordinateCaptureTexture = gpu.texture(width, height, coordinateCapture);
        auto coordinateMainTexture = gpu.texture(width, height, coordinateMain);
        // RGB does not affect the lookup decision. Encoding coordinates exposes
        // the real selected tap without duplicating shader candidate selection.
        const auto selected = render(coordinateCaptureTexture, coordinateMainTexture, currentDepthTexture, futurePositionTexture, levels.back());
        std::vector<float> interiorErrors, heldInteriorErrors, gridEdgeErrors;
        size_t actualBackgroundErrors = 0, severeForegroundErrors = 0, actualSilhouetteErrors = 0;
        size_t uncoveredErrors = 0, enteringErrors = 0;
        for (UINT y = 8; y < height - 8; ++y) for (UINT x = 8; x < width - 8; ++x) {
            const size_t i = size_t(y) * width + x;
            const float error = maximumError(actualResult[i], future[i]);
            if ((x < 64 || x > 168 || y < 40 || y > 152) && error > 0.00003f) ++actualBackgroundErrors;
            if (x >= 100 && x < 136 && y >= 72 && y < 124) {
                interiorErrors.push_back(error);
                heldInteriorErrors.push_back(maximumError(live[i], future[i]));
                if (error > 0.06f) ++severeForegroundErrors;
                if (x % 4 == 0 || y % 4 == 0) for (const size_t j : {i - 1, i - width}) {
                    float edgeError = 0;
                    for (size_t c = 0; c < 3; ++c)
                        edgeError = std::max(edgeError, std::abs((actualResult[i][c] - actualResult[j][c]) - (future[i][c] - future[j][c])));
                    gridEdgeErrors.push_back(edgeError);
                }
            }
            if ((onObject(x, y, 4) != onObject(x, y, 6)) && error > 0.035f) {
                ++actualSilhouetteErrors;
                if (onObject(x, y, 6)) ++enteringErrors; else ++uncoveredErrors;
                if (actualSilhouetteErrors <= 12) {
                    const UINT sx = UINT(std::clamp(int(selected[i][0] * width), 0, int(width) - 1));
                    const UINT sy = UINT(std::clamp(int(selected[i][1] * height), 0, int(height) - 1));
                    const Pixel& sourceFlow = actualFlow[size_t(sy / 4) * flowWidth + sx / 4];
                    const Pixel& localFlow = actualFlow[size_t(y / 4) * flowWidth + x / 4];
                    std::printf("  silhouette(%u,%u) staticZ=%.1f currentZ=%.1f source=(%.3f,%.3f) sourceZ=%.1f uses_main=%.1f source_flow=(%.3f,%.3f,w%.3f) local_flow=(%.3f,%.3f,w%.3f) actualRGB=(%.4f,%.4f,%.4f) expectedRGB=(%.4f,%.4f,%.4f)\n",
                        x, y, currentDepth[i][0], futurePosition[i][2], selected[i][0] * width - 0.5f,
                        selected[i][1] * height - 0.5f, currentDepth[size_t(sy) * width + sx][0], selected[i][2],
                        sourceFlow[0] * width, sourceFlow[1] * height, sourceFlow[3], localFlow[0] * width, localFlow[1] * height, localFlow[3],
                        actualResult[i][0], actualResult[i][1], actualResult[i][2], future[i][0], future[i][1], future[i][2]);
                }
            }
        }
        auto percentile = [](std::vector<float> values, float percentileValue) {
            require(!values.empty(), "Empty image-regression sample region");
            std::sort(values.begin(), values.end());
            return values[std::min(values.size() - 1, size_t(float(values.size() - 1) * percentileValue))];
        };
        const float error95 = percentile(interiorErrors, 0.95f), heldError95 = percentile(heldInteriorErrors, 0.95f);
        const float edge99 = percentile(gridEdgeErrors, 0.99f);
        std::printf("real production flow+textured lens: accepted=%zu rejected=%zu interior_vector_range=(%.4f,%.4f) interior_wrong/accepted=%zu/%zu static_flow_errors=%zu background_errors=%zu interior_p95=%.6f held_p95=%.6f grid_edge_p99=%.6f severe_pixels=%zu silhouette_errors=%zu(uncovered=%zu,entering=%zu)\n",
            acceptedSamples, rejectedSamples, minimumMotion, maximumMotion, interiorWrong, interiorAccepted, staticFlowErrors, actualBackgroundErrors,
            error95, heldError95, edge99, severeForegroundErrors, actualSilhouetteErrors, uncoveredErrors, enteringErrors);
        expectation(acceptedSamples > 0 && rejectedSamples > 0 && maximumMotion - minimumMotion > 0.01f,
            "Actual motion test did not exercise spatially varying confidence/vector field");
        expectation(staticFlowErrors == 0 && actualBackgroundErrors == 0, "Production flow/lens created motion or seams on static textured wall");
        expectation(error95 < heldError95 * 0.7f, "Production moving-texture 95th percentile error did not improve over held capture");
        expectation(edge99 < 0.025f && severeForegroundErrors == 0, "Production nonuniform flow generated foreground block seams or severe local deformation");
        expectation(actualSilhouetteErrors < 40, "Production flow/lens left stale foreground at confirmed disocclusion");

        // Real optics crop the wide world view; it is not the identity mapping
        // used by the original silhouette fixture. The C++ binding oracle checks
        // world-vs-HUD projection selection; this checks the production shader's
        // spatial depth/color mapping with a zoomed crop and nonzero jitter.
        const float pi = 3.14159265358979323846f;
        const float cropScale = std::tan(10.f * pi / 180.f) / std::tan(41.5f * pi / 180.f);
        const float centerX = 0.52f + 0.375f / width, centerY = 0.49f - 0.25f / height;
        lens.constant("scope_lense_main_view", std::array<float, 4>{cropScale, cropScale, centerX, centerY});
        std::vector<Pixel> cropCapture(size_t(width) * height), cropMain(cropCapture.size()), cropPositions(cropCapture.size());
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            cropCapture[i] = wall(float(x), float(y));
            cropMain[i] = foreground(float(x) * 1.7f, float(y) * 1.3f);
            const float u = (float(x) + 0.5f) / width, v = (float(y) + 0.5f) / height;
            const bool window = std::abs(u - centerX) < 0.028f && std::abs(v - centerY) < 0.033f;
            cropPositions[i] = {0, 0, window ? 20.f : 5.f, 0};
        }
        auto cropCaptureTexture = gpu.texture(width, height, cropCapture);
        auto cropMainTexture = gpu.texture(width, height, cropMain);
        auto cropDepthTexture = gpu.texture(width, height, std::vector<Pixel>(cropCapture.size(), {5, 0, 0, 0}));
        auto cropPositionTexture = gpu.texture(width, height, cropPositions);
        auto cropFlowTexture = gpu.texture(flowWidth, flowHeight, std::vector<Pixel>(size_t(flowWidth) * flowHeight, {0, 0, 5, -1}));
        const auto cropResult = render(cropCaptureTexture, cropMainTexture, cropDepthTexture, cropPositionTexture, cropFlowTexture);
        auto bilinear = [&](const std::vector<Pixel>& pixels, float u, float v) {
            const float tx = u * width - 0.5f, ty = v * height - 0.5f;
            const int ix = int(std::floor(tx)), iy = int(std::floor(ty));
            const float fx = tx - std::floor(tx), fy = ty - std::floor(ty);
            Pixel result{};
            for (int row = 0; row <= 1; ++row) for (int column = 0; column <= 1; ++column) {
                const UINT x = UINT(std::clamp(ix + column, 0, int(width) - 1));
                const UINT y = UINT(std::clamp(iy + row, 0, int(height) - 1));
                const float weight = (column ? fx : 1.f - fx) * (row ? fy : 1.f - fy);
                for (size_t c = 0; c < 4; ++c) result[c] += pixels[size_t(y) * width + x][c] * weight;
            }
            return result;
        };
        size_t cropInsideChecks = 0, cropOutsideChecks = 0, cropInsideErrors = 0, cropOutsideErrors = 0;
        float cropMaximumError = 0;
        for (UINT y = 8; y < height - 8; ++y) for (UINT x = 8; x < width - 8; ++x) {
            const size_t i = size_t(y) * width + x;
            const float u = ((float(x) + 0.5f) / width - 0.5f) * cropScale + centerX;
            const float v = ((float(y) + 0.5f) / height - 0.5f) * cropScale + centerY;
            const bool deepInside = std::abs(u - centerX) < 0.028f - 2.f / width && std::abs(v - centerY) < 0.033f - 2.f / height;
            const bool farOutside = std::abs(u - centerX) > 0.028f + 2.f / width || std::abs(v - centerY) > 0.033f + 2.f / height;
            if (deepInside) {
                ++cropInsideChecks;
                const float error = maximumError(cropResult[i], bilinear(cropMain, u, v));
                cropMaximumError = std::max(cropMaximumError, error);
                if (error > 0.0002f) ++cropInsideErrors;
            }
            if (farOutside) { ++cropOutsideChecks; if (maximumError(cropResult[i], cropCapture[i]) > 0.00003f) ++cropOutsideErrors; }
        }
        std::printf("83deg-world/20deg-lens crop with jitter: scale=%.6f confirmed_disocclusion_errors=%zu/%zu max_filter_error=%.6f same_surface_errors=%zu/%zu\n",
            cropScale, cropInsideErrors, cropInsideChecks, cropMaximumError, cropOutsideErrors, cropOutsideChecks);
        expectation(cropInsideChecks > 1000 && cropOutsideChecks > 10000, "Insufficient nonidentity crop/depth fixture coverage");
        expectation(cropInsideErrors == 0 && cropOutsideErrors == 0, "Production lens cropped depth/color inconsistently or let sentinel mask override unchanged surface");
        for (UINT imagingMode = 0; imagingMode < 3; ++imagingMode) {
            std::array<float, 4> flags{};
            flags[imagingMode] = 1;
            lens.constant("scope_lense_imaging", flags);
            const auto guarded = render(cropCaptureTexture, cropMainTexture, cropDepthTexture, cropPositionTexture, cropFlowTexture);
            size_t paletteErrors = 0;
            for (UINT y = 8; y < height - 8; ++y) for (UINT x = 8; x < width - 8; ++x) {
                const size_t i = size_t(y) * width + x;
                if (maximumError(guarded[i], cropCapture[i]) > 0.0002f) ++paletteErrors;
            }
            std::printf("nonmatching imaging-stage guard axis%u: inspected=42240 palette_errors=%zu\n", imagingMode, paletteErrors);
            expectation(paletteErrors == 0, "NVG/thermal/HDR capture imported incompatible main-view color at disocclusion");
        }
        require(failures == 0, "Production lens block-artifact image regressions failed");
        std::puts("PASS: production lens pixel/seam regressions, mixed quarter-resolution fields and textured motion/depth silhouette.");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
