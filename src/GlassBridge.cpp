#include "plugin.hpp"
#include <vector>
#include <mutex>
#include <algorithm>
#include <cmath>

struct BridgePoint {
    float x = 0.5f;
    float y = 0.5f;
    float intensity = 0.5f;
    float size = 2.0f;
    float persistence = 0.5f;
    float spread = 0.0f;
    float hue = 0.0f;
    float age = 0.0f;
    bool connectLine = false;
    bool active = true;
};

struct GlassBridge : Module {
    enum ParamIds {
        X_SCALE_PARAM,
        Y_SCALE_PARAM,
        Z_ENERGY_PARAM,
        DOT_AMOUNT_PARAM,
        LINE_AMOUNT_PARAM,
        PARTICLE_AMOUNT_PARAM,

        SIZE_AMOUNT_PARAM,
        PERSISTENCE_AMOUNT_PARAM,
        SPREAD_AMOUNT_PARAM,
        COLOR_RESPONSE_PARAM,
        PARTICLE_BLEND_PARAM,
        GLOW_AMOUNT_PARAM,

        SAMPLE_MODE_PARAM,
        PARTICLE_SOURCE_PARAM,

        CLEAR_PARAM,
        FREEZE_PARAM,

        NUM_PARAMS
    };

    enum InputIds {
        X_INPUT,
        Y_INPUT,
        Z_INPUT,
        MOD1_INPUT,
        MOD2_INPUT,
        MOD3_INPUT,
        CLOCK_INPUT,
        RESET_INPUT,
        FREEZE_INPUT,
        NUM_INPUTS
    };

    enum OutputIds {
        NUM_OUTPUTS
    };

    enum LightIds {
        CLEAR_LIGHT,
        FREEZE_LIGHT,
        NUM_LIGHTS
    };

    enum SampleMode {
        SAMPLE_CONTINUOUS = 0,
        SAMPLE_CLOCKED = 1,
        SAMPLE_TRIGGERED = 2
    };

    enum ParticleSourceMode {
        PARTICLE_POINT = 0,
        PARTICLE_LINE = 1,
        PARTICLE_BOTH = 2
    };

    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger freezeInputTrigger;
    dsp::SchmittTrigger clearButtonTrigger;
    dsp::SchmittTrigger freezeButtonTrigger;

    bool freezeLatched = false;

    static constexpr int MAX_POINTS = 4096;
    static constexpr int MAX_PARTICLES = 8192;
    static constexpr float VISUAL_UPDATE_HZ = 120.0f;
    static constexpr float SNAPSHOT_UPDATE_HZ = 30.0f;

    std::vector<BridgePoint> livePoints;
    std::vector<BridgePoint> liveParticles;
    int livePointWriteIndex = 0;
    int liveParticleWriteIndex = 0;
    int livePointCount = 0;
    int liveParticleCount = 0;

    std::vector<BridgePoint> renderPoints;
    std::vector<BridgePoint> renderParticles;
    std::mutex renderMutex;

    float visualAccumulator = 0.0f;
    float snapshotAccumulator = 0.0f;
    float sampleAccumulator = 0.0f;

    GlassBridge() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(X_SCALE_PARAM, 0.1f, 4.0f, 1.0f, "X Scale");
        configParam(Y_SCALE_PARAM, 0.1f, 4.0f, 1.0f, "Y Scale");
        configParam(Z_ENERGY_PARAM, 0.0f, 2.0f, 1.0f, "Z Energy");
        configParam(DOT_AMOUNT_PARAM, 0.0f, 1.0f, 0.9f, "Dot Amount");
        configParam(LINE_AMOUNT_PARAM, 0.0f, 1.0f, 0.5f, "Line Amount");
        configParam(PARTICLE_AMOUNT_PARAM, 0.0f, 1.0f, 0.4f, "Particle Amount");

        configParam(SIZE_AMOUNT_PARAM, 0.0f, 2.0f, 1.0f, "Size Amount");
        configParam(PERSISTENCE_AMOUNT_PARAM, 0.0f, 1.0f, 0.85f, "Persistence Amount");
        configParam(SPREAD_AMOUNT_PARAM, 0.0f, 2.0f, 1.0f, "Spread Amount");
        configParam(COLOR_RESPONSE_PARAM, 0.0f, 2.0f, 1.0f, "Color Response");
        configParam(PARTICLE_BLEND_PARAM, 0.0f, 1.0f, 0.5f, "Particle Blend");
        configParam(GLOW_AMOUNT_PARAM, 0.0f, 2.0f, 1.0f, "Glow Amount");

        configSwitch(SAMPLE_MODE_PARAM, 0.f, 2.f, 0.f, "Sample Mode", {"Continuous", "Clocked", "Triggered"});
        configSwitch(PARTICLE_SOURCE_PARAM, 0.f, 2.f, 2.f, "Particle Source", {"Point", "Line", "Both"});

        configButton(CLEAR_PARAM, "Clear");
        configButton(FREEZE_PARAM, "Freeze");

        configInput(X_INPUT, "X");
        configInput(Y_INPUT, "Y");
        configInput(Z_INPUT, "Z");
        configInput(MOD1_INPUT, "MOD 1");
        configInput(MOD2_INPUT, "MOD 2");
        configInput(MOD3_INPUT, "MOD 3");
        configInput(CLOCK_INPUT, "Clock / Trigger");
        configInput(RESET_INPUT, "Reset");
        configInput(FREEZE_INPUT, "Freeze");

        livePoints.resize(MAX_POINTS);
        liveParticles.resize(MAX_PARTICLES);
    }

    static float clamp01(float v) {
        return std::max(0.f, std::min(1.f, v));
    }

    static float normBipolar(float v) {
        return clamp01((v + 5.f) / 10.f);
    }

    static float normUnipolar(float v) {
        return clamp01(v / 10.f);
    }

    static NVGcolor hsvToRgb(float h, float s, float v, float a = 1.0f) {
        h = std::fmod(std::fmod(h, 1.f) + 1.f, 1.f);
        s = clamp01(s);
        v = clamp01(v);

        float r = 0.f, g = 0.f, b = 0.f;
        float i = std::floor(h * 6.f);
        float f = h * 6.f - i;
        float p = v * (1.f - s);
        float q = v * (1.f - f * s);
        float t = v * (1.f - (1.f - f) * s);

        switch ((int)i % 6) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }

        return nvgRGBAf(r, g, b, a);
    }

    bool isFrozen() {
        return freezeLatched || (inputs[FREEZE_INPUT].isConnected() && inputs[FREEZE_INPUT].getVoltage() >= 1.f);
    }

    void clearAll() {
        livePointWriteIndex = 0;
        liveParticleWriteIndex = 0;
        livePointCount = 0;
        liveParticleCount = 0;

        for (auto& p : livePoints) {
            p.active = false;
            p.age = 999.f;
        }
        for (auto& p : liveParticles) {
            p.active = false;
            p.age = 999.f;
        }

        std::lock_guard<std::mutex> lock(renderMutex);
        renderPoints.clear();
        renderParticles.clear();
    }

    void pushLivePoint(const BridgePoint& p) {
        livePoints[livePointWriteIndex] = p;
        livePoints[livePointWriteIndex].active = true;
        livePointWriteIndex = (livePointWriteIndex + 1) % MAX_POINTS;
        if (livePointCount < MAX_POINTS)
            livePointCount++;
    }

    void pushLiveParticle(const BridgePoint& p) {
        liveParticles[liveParticleWriteIndex] = p;
        liveParticles[liveParticleWriteIndex].active = true;
        liveParticleWriteIndex = (liveParticleWriteIndex + 1) % MAX_PARTICLES;
        if (liveParticleCount < MAX_PARTICLES)
            liveParticleCount++;
    }

    BridgePoint makePointFromInputs() {
        float xScale = params[X_SCALE_PARAM].getValue();
        float yScale = params[Y_SCALE_PARAM].getValue();
        float zEnergy = params[Z_ENERGY_PARAM].getValue();

        float xV = inputs[X_INPUT].isConnected() ? inputs[X_INPUT].getVoltage() * xScale : 0.f;
        float yV = inputs[Y_INPUT].isConnected() ? inputs[Y_INPUT].getVoltage() * yScale : 0.f;
        float zV = inputs[Z_INPUT].isConnected() ? inputs[Z_INPUT].getVoltage() * zEnergy : 5.f;

        float mod1 = inputs[MOD1_INPUT].isConnected() ? normUnipolar(inputs[MOD1_INPUT].getVoltage()) : 0.5f;
        float mod2 = inputs[MOD2_INPUT].isConnected() ? normUnipolar(inputs[MOD2_INPUT].getVoltage()) : 0.5f;
        float mod3 = inputs[MOD3_INPUT].isConnected() ? normUnipolar(inputs[MOD3_INPUT].getVoltage()) : 0.5f;

        float sizeAmt = params[SIZE_AMOUNT_PARAM].getValue();
        float persistenceAmt = params[PERSISTENCE_AMOUNT_PARAM].getValue();
        float spreadAmt = params[SPREAD_AMOUNT_PARAM].getValue();
        float colorAmt = params[COLOR_RESPONSE_PARAM].getValue();

        BridgePoint p;
        p.x = normBipolar(xV);
        p.y = normBipolar(yV);
        p.intensity = clamp01(normUnipolar(zV));
        p.size = 1.0f + (mod1 * 8.0f * sizeAmt);
        p.persistence = clamp01(0.1f + mod2 * persistenceAmt);
        p.spread = mod3 * 0.04f * spreadAmt;

        float motionHue = std::fabs(xV * 0.031f) + std::fabs(yV * 0.047f);
        float zHue = p.intensity * 0.5f;
        float chaosHue = mod3 * colorAmt;
        p.hue = std::fmod(motionHue + zHue + chaosHue, 1.0f);

        p.age = 0.f;
        p.connectLine = params[LINE_AMOUNT_PARAM].getValue() > 0.01f;
        p.active = true;
        return p;
    }

    void spawnParticles(const BridgePoint& p, int count, bool enabled) {
        if (!enabled || count <= 0)
            return;

        for (int i = 0; i < count; ++i) {
            BridgePoint particle = p;
            float t = (float)i / std::max(1, count);
            float angle = t * 6.28318530718f + p.hue * 6.28318530718f;
            float radius = 0.002f + p.spread * (0.002f + 0.01f * t);

            particle.x = clamp01(p.x + std::cos(angle) * radius);
            particle.y = clamp01(p.y + std::sin(angle) * radius);
            particle.size = std::max(0.5f, p.size * (0.15f + 0.45f * (1.f - t)));
            particle.intensity = clamp01(p.intensity * (0.4f + 0.6f * (1.f - t)));
            particle.age = 0.f;
            particle.connectLine = false;
            particle.active = true;

            pushLiveParticle(particle);
        }
    }

    void appendVisualSample() {
        BridgePoint p = makePointFromInputs();
        pushLivePoint(p);

        int particleSource = (int)std::round(params[PARTICLE_SOURCE_PARAM].getValue());
        bool particleFromPoint = (particleSource == PARTICLE_POINT || particleSource == PARTICLE_BOTH);
        bool particleEnabled = params[PARTICLE_AMOUNT_PARAM].getValue() > 0.01f;

        int count = (int)std::round(
            params[PARTICLE_AMOUNT_PARAM].getValue() *
            params[PARTICLE_BLEND_PARAM].getValue() *
            (2.f + 18.f * p.intensity)
        );

        spawnParticles(p, count, particleEnabled && particleFromPoint);

        bool particleFromLine = (particleSource == PARTICLE_LINE || particleSource == PARTICLE_BOTH);
        if (particleFromLine && particleEnabled && params[LINE_AMOUNT_PARAM].getValue() > 0.01f) {
            int extra = (int)std::round(
                params[PARTICLE_AMOUNT_PARAM].getValue() *
                params[PARTICLE_BLEND_PARAM].getValue() *
                (1.f + 8.f * p.intensity)
            );
            spawnParticles(p, extra, true);
        }
    }

    void ageLiveBuffers(float dt) {
        float persistenceDecay = 0.1f + (1.f - params[PERSISTENCE_AMOUNT_PARAM].getValue()) * 2.5f;
        float pointAgeStep = dt * persistenceDecay;
        float particleAgeStep = dt * persistenceDecay * 1.8f;

        for (int i = 0; i < livePointCount; ++i) {
            BridgePoint& p = livePoints[i];
            if (!p.active)
                continue;
            p.age += pointAgeStep;
            if (p.age > 1.4f)
                p.active = false;
        }

        for (int i = 0; i < liveParticleCount; ++i) {
            BridgePoint& p = liveParticles[i];
            if (!p.active)
                continue;
            p.age += particleAgeStep;
            if (p.age > 1.0f)
                p.active = false;
        }
    }

    void snapshotRenderBuffers() {
        std::vector<BridgePoint> pointsCopy;
        std::vector<BridgePoint> particlesCopy;
        pointsCopy.reserve(livePointCount);
        particlesCopy.reserve(liveParticleCount);

        for (int i = 0; i < livePointCount; ++i) {
            const BridgePoint& p = livePoints[i];
            if (p.active)
                pointsCopy.push_back(p);
        }

        for (int i = 0; i < liveParticleCount; ++i) {
            const BridgePoint& p = liveParticles[i];
            if (p.active)
                particlesCopy.push_back(p);
        }

        std::lock_guard<std::mutex> lock(renderMutex);
        renderPoints.swap(pointsCopy);
        renderParticles.swap(particlesCopy);
    }

    void process(const ProcessArgs& args) override {
        lights[CLEAR_LIGHT].setBrightness(0.f);
        lights[FREEZE_LIGHT].setBrightness(freezeLatched ? 1.f : 0.f);

        if (clearButtonTrigger.process(params[CLEAR_PARAM].getValue())) {
            clearAll();
            lights[CLEAR_LIGHT].setBrightness(1.f);
        }

        if (freezeButtonTrigger.process(params[FREEZE_PARAM].getValue())) {
            freezeLatched = !freezeLatched;
        }

        if (freezeInputTrigger.process(inputs[FREEZE_INPUT].isConnected() ? inputs[FREEZE_INPUT].getVoltage() : 0.f)) {
            freezeLatched = !freezeLatched;
        }

        if (resetTrigger.process(inputs[RESET_INPUT].isConnected() ? inputs[RESET_INPUT].getVoltage() : 0.f)) {
            clearAll();
            lights[CLEAR_LIGHT].setBrightness(1.f);
        }

        if (isFrozen()) {
            snapshotAccumulator += args.sampleTime;
            if (snapshotAccumulator >= (1.0f / SNAPSHOT_UPDATE_HZ)) {
                snapshotAccumulator = 0.f;
                snapshotRenderBuffers();
            }
            return;
        }

        visualAccumulator += args.sampleTime;
        snapshotAccumulator += args.sampleTime;
        sampleAccumulator += args.sampleTime;

        const float visualStep = 1.0f / VISUAL_UPDATE_HZ;

        while (visualAccumulator >= visualStep) {
            visualAccumulator -= visualStep;
            ageLiveBuffers(visualStep);
        }

        bool shouldAppend = false;
        int sampleMode = (int)std::round(params[SAMPLE_MODE_PARAM].getValue());

        if (sampleMode == SAMPLE_CONTINUOUS) {
            if (sampleAccumulator >= visualStep) {
                sampleAccumulator = 0.f;
                shouldAppend = true;
            }
        }
        else if (sampleMode == SAMPLE_CLOCKED) {
            if (clockTrigger.process(inputs[CLOCK_INPUT].isConnected() ? inputs[CLOCK_INPUT].getVoltage() : 0.f)) {
                shouldAppend = true;
            }
        }
        else if (sampleMode == SAMPLE_TRIGGERED) {
            if (clockTrigger.process(inputs[CLOCK_INPUT].isConnected() ? inputs[CLOCK_INPUT].getVoltage() : 0.f)) {
                shouldAppend = true;
            }
        }

        if (shouldAppend) {
            appendVisualSample();
        }

        if (snapshotAccumulator >= (1.0f / SNAPSHOT_UPDATE_HZ)) {
            snapshotAccumulator = 0.f;
            snapshotRenderBuffers();
        }
    }
};

struct GlassBridgeDisplay : Widget {
    GlassBridge* module = nullptr;

    struct Viewport {
        float minX = 0.f;
        float maxX = 1.f;
        float minY = 0.f;
        float maxY = 1.f;
    };

    float camMinX = 0.f;
    float camMaxX = 1.f;
    float camMinY = 0.f;
    float camMaxY = 1.f;
    bool cameraInitialized = false;

    static float clampSafe(float v, float lo, float hi) {
        return std::max(lo, std::min(hi, v));
    }

    void drawBackground(const DrawArgs& args) {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 8.f);
        nvgFillColor(args.vg, nvgRGB(6, 10, 16));
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 1.f, 1.f, box.size.x - 2.f, box.size.y - 2.f, 7.f);
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStrokeColor(args.vg, nvgRGB(40, 70, 90));
        nvgStroke(args.vg);

        for (int i = 1; i < 8; ++i) {
            float x = box.size.x * ((float)i / 8.f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, x, 0.f);
            nvgLineTo(args.vg, x, box.size.y);
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStrokeColor(args.vg, nvgRGBA(40, 90, 120, 30));
            nvgStroke(args.vg);
        }

        for (int i = 1; i < 4; ++i) {
            float y = box.size.y * ((float)i / 4.f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 0.f, y);
            nvgLineTo(args.vg, box.size.x, y);
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStrokeColor(args.vg, nvgRGBA(40, 90, 120, 28));
            nvgStroke(args.vg);
        }
    }

    Viewport computeTargetViewport(const std::vector<BridgePoint>& points, const std::vector<BridgePoint>& particles) {
        Viewport vp;

        bool found = false;
        float minX = 1e9f;
        float maxX = -1e9f;
        float minY = 1e9f;
        float maxY = -1e9f;

        auto accumulate = [&](const std::vector<BridgePoint>& src) {
            for (const auto& p : src) {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
                found = true;
            }
        };

        accumulate(points);
        accumulate(particles);

        if (!found) {
            vp.minX = 0.f;
            vp.maxX = 1.f;
            vp.minY = 0.f;
            vp.maxY = 1.f;
            return vp;
        }

        float width = std::max(0.02f, maxX - minX);
        float height = std::max(0.02f, maxY - minY);
        float cx = 0.5f * (minX + maxX);
        float cy = 0.5f * (minY + maxY);

        float paddingFactor = 0.12f;
        width *= (1.f + paddingFactor * 2.f);
        height *= (1.f + paddingFactor * 2.f);

        float screenAspect = box.size.x / box.size.y;
        float dataAspect = width / height;

        if (dataAspect > screenAspect) {
            height = width / screenAspect;
        }
        else {
            width = height * screenAspect;
        }

        vp.minX = cx - width * 0.5f;
        vp.maxX = cx + width * 0.5f;
        vp.minY = cy - height * 0.5f;
        vp.maxY = cy + height * 0.5f;

        return vp;
    }

    void updateCamera(const Viewport& target) {
        if (!cameraInitialized) {
            camMinX = target.minX;
            camMaxX = target.maxX;
            camMinY = target.minY;
            camMaxY = target.maxY;
            cameraInitialized = true;
            return;
        }

        const float smooth = 0.14f;
        camMinX += (target.minX - camMinX) * smooth;
        camMaxX += (target.maxX - camMaxX) * smooth;
        camMinY += (target.minY - camMinY) * smooth;
        camMaxY += (target.maxY - camMaxY) * smooth;
    }

    Vec mapPoint(float x, float y) {
        float width = std::max(0.001f, camMaxX - camMinX);
        float height = std::max(0.001f, camMaxY - camMinY);

        float nx = (x - camMinX) / width;
        float ny = (y - camMinY) / height;

        nx = clampSafe(nx, -0.25f, 1.25f);
        ny = clampSafe(ny, -0.25f, 1.25f);

        return Vec(
            nx * box.size.x,
            (1.f - ny) * box.size.y
        );
    }

    void drawLineLayer(const DrawArgs& args, const std::vector<BridgePoint>& points) {
        if (!module)
            return;

        float lineAmt = module->params[GlassBridge::LINE_AMOUNT_PARAM].getValue();
        float glowAmt = module->params[GlassBridge::GLOW_AMOUNT_PARAM].getValue();
        if (lineAmt <= 0.01f || points.size() < 2)
            return;

        bool first = true;
        float lastX = 0.f;
        float lastY = 0.f;
        float lastHue = 0.f;
        float lastA = 0.f;

        for (const auto& p : points) {
            Vec pos = mapPoint(p.x, p.y);
            float x = pos.x;
            float y = pos.y;
            float a = std::max(0.f, 1.f - p.age) * lineAmt * (0.2f + 0.8f * p.intensity);

            if (!first && p.connectLine) {
                NVGcolor c = GlassBridge::hsvToRgb(
                    std::fmod((lastHue + p.hue) * 0.5f, 1.f),
                    0.7f + 0.3f * std::min(1.f, glowAmt),
                    0.7f + 0.3f * p.intensity,
                    std::min(1.f, (lastA + a) * 0.6f)
                );

                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, lastX, lastY);
                nvgLineTo(args.vg, x, y);
                nvgStrokeWidth(args.vg, 1.0f + 2.5f * lineAmt);
                nvgStrokeColor(args.vg, c);
                nvgStroke(args.vg);
            }

            first = false;
            lastX = x;
            lastY = y;
            lastHue = p.hue;
            lastA = a;
        }
    }

    void drawDotLayer(const DrawArgs& args, const std::vector<BridgePoint>& points) {
        if (!module)
            return;

        float dotAmt = module->params[GlassBridge::DOT_AMOUNT_PARAM].getValue();
        float glowAmt = module->params[GlassBridge::GLOW_AMOUNT_PARAM].getValue();
        if (dotAmt <= 0.01f)
            return;

        for (const auto& p : points) {
            Vec pos = mapPoint(p.x, p.y);
            float x = pos.x;
            float y = pos.y;
            float alpha = std::max(0.f, 1.f - p.age) * dotAmt * (0.25f + 0.75f * p.intensity);
            float radius = p.size * (0.35f + 0.65f * dotAmt);

            NVGcolor c = GlassBridge::hsvToRgb(
                p.hue,
                0.75f + 0.25f * std::min(1.f, glowAmt),
                0.8f + 0.2f * p.intensity,
                std::min(1.f, alpha)
            );

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, x, y, radius + glowAmt * 1.5f);
            nvgFillColor(args.vg, nvgRGBAf(c.r, c.g, c.b, alpha * 0.15f));
            nvgFill(args.vg);

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, x, y, std::max(0.7f, radius));
            nvgFillColor(args.vg, c);
            nvgFill(args.vg);
        }
    }

    void drawParticleLayer(const DrawArgs& args, const std::vector<BridgePoint>& particles) {
        if (!module)
            return;

        float particleAmt = module->params[GlassBridge::PARTICLE_AMOUNT_PARAM].getValue();
        if (particleAmt <= 0.01f)
            return;

        for (const auto& p : particles) {
            Vec pos = mapPoint(p.x, p.y);
            float x = pos.x;
            float y = pos.y;
            float alpha = std::max(0.f, 1.f - p.age) * particleAmt * p.intensity;
            float radius = std::max(0.6f, p.size);

            NVGcolor c = GlassBridge::hsvToRgb(
                std::fmod(p.hue + 0.08f, 1.f),
                0.85f,
                1.0f,
                std::min(1.f, alpha)
            );

            nvgBeginPath(args.vg);
            nvgCircle(args.vg, x, y, radius);
            nvgFillColor(args.vg, c);
            nvgFill(args.vg);
        }
    }

    void draw(const DrawArgs& args) override {
        drawBackground(args);

        if (!module)
            return;

        std::vector<BridgePoint> points;
        std::vector<BridgePoint> particles;
        {
            std::lock_guard<std::mutex> lock(module->renderMutex);
            points = module->renderPoints;
            particles = module->renderParticles;
        }

        Viewport target = computeTargetViewport(points, particles);
        updateCamera(target);

        drawLineLayer(args, points);
        drawParticleLayer(args, particles);
        drawDotLayer(args, points);
    }
};

struct GlassBridgeWidget : ModuleWidget {
    GlassBridgeWidget(GlassBridge* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/GlassBridge.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        GlassBridgeDisplay* display = createWidget<GlassBridgeDisplay>(mm2px(Vec(24.0, 14.0)));
        display->box.size = mm2px(Vec(195.0, 76.0));
        display->module = module;
        addChild(display);

        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(12.0, 22.0)), module, GlassBridge::X_SCALE_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(12.0, 40.0)), module, GlassBridge::Y_SCALE_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(12.0, 58.0)), module, GlassBridge::Z_ENERGY_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(12.0, 76.0)), module, GlassBridge::DOT_AMOUNT_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(12.0, 94.0)), module, GlassBridge::LINE_AMOUNT_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(12.0, 112.0)), module, GlassBridge::PARTICLE_AMOUNT_PARAM));

        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(228.0, 22.0)), module, GlassBridge::SIZE_AMOUNT_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(228.0, 40.0)), module, GlassBridge::PERSISTENCE_AMOUNT_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(228.0, 58.0)), module, GlassBridge::SPREAD_AMOUNT_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(228.0, 76.0)), module, GlassBridge::COLOR_RESPONSE_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(228.0, 94.0)), module, GlassBridge::PARTICLE_BLEND_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(228.0, 112.0)), module, GlassBridge::GLOW_AMOUNT_PARAM));

        addParam(createParamCentered<CKSSThree>(mm2px(Vec(27.0, 106.0)), module, GlassBridge::SAMPLE_MODE_PARAM));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(213.0, 106.0)), module, GlassBridge::PARTICLE_SOURCE_PARAM));

        addParam(createParamCentered<LEDButton>(mm2px(Vec(27.0, 118.0)), module, GlassBridge::CLEAR_PARAM));
        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(27.0, 118.0)), module, GlassBridge::CLEAR_LIGHT));

        addParam(createParamCentered<LEDButton>(mm2px(Vec(213.0, 118.0)), module, GlassBridge::FREEZE_PARAM));
        addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(213.0, 118.0)), module, GlassBridge::FREEZE_LIGHT));

        float y = 121.0f;
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.0, y)), module, GlassBridge::X_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(69.0, y)), module, GlassBridge::Y_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(90.0, y)), module, GlassBridge::Z_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(111.0, y)), module, GlassBridge::MOD1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(132.0, y)), module, GlassBridge::MOD2_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(153.0, y)), module, GlassBridge::MOD3_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(174.0, y)), module, GlassBridge::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(195.0, y)), module, GlassBridge::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(213.0, y)), module, GlassBridge::FREEZE_INPUT));
    }
};

Model* modelGlassBridge = createModel<GlassBridge, GlassBridgeWidget>("GlassBridge");
