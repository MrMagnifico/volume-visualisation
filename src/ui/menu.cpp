#include "menu.h"
#include "render/renderer.h"
#include <filesystem>
#include <fmt/format.h>
#include <imgui.h>
#include <iostream>
#include <nfd.h>
#include <string>
#include <glm/gtc/type_ptr.hpp>

namespace ui {

const std::filesystem::path DATA_PATH = RESOURCES_DIR;
std::string currentFileName = "FLD File";

Menu::Menu(const glm::ivec2& baseRenderResolution)
    : m_baseRenderResolution(baseRenderResolution)
{
    m_renderConfig.renderResolution = m_baseRenderResolution;
}

void Menu::setLoadVolumeCallback(LoadVolumeCallback&& callback)
{
    m_optLoadVolumeCallback = std::move(callback);
}

void Menu::setRenderConfigChangedCallback(RenderConfigChangedCallback&& callback)
{
    m_optRenderConfigChangedCallback = std::move(callback);
}

void Menu::setInterpolationModeChangedCallback(InterpolationModeChangedCallback&& callback)
{
    m_optInterpolationModeChangedCallback = std::move(callback);
}

render::RenderConfig Menu::renderConfig() const
{
    return m_renderConfig;
}

volume::InterpolationMode Menu::interpolationMode() const
{
    return m_interpolationMode;
}

void Menu::setBaseRenderResolution(const glm::ivec2& baseRenderResolution)
{
    m_baseRenderResolution = baseRenderResolution;
    m_renderConfig.renderResolution = glm::ivec2(glm::vec2(m_baseRenderResolution) * m_resolutionScale);
    callRenderConfigChangedCallback();
}

// This function handles a part of the volume loading where we create the widget histograms, set some config values
//  and set the menu volume information
void Menu::setLoadedVolume(const volume::Volume& volume, const volume::GradientVolume& gradientVolume)
{
    m_tfWidget      = TransferFunctionWidget(volume);
    m_tf2DWidget    = TransferFunction2DWidget(volume, gradientVolume);
    m_leWidget      = LightEditorWidget(volume);

    m_tfWidget->updateRenderConfig(m_renderConfig);
    m_tf2DWidget->updateRenderConfig(m_renderConfig);
    m_leWidget->updateRenderConfig(m_renderConfig);

    const glm::ivec3 dim = volume.dims();
    m_volumeInfo = fmt::format("Volume info:\n{}\nDimensions: ({}, {}, {})\nVoxel value range: {} - {}\n",
        volume.fileName(), dim.x, dim.y, dim.z, volume.minimum(), volume.maximum());
    m_volumeMax = int(volume.maximum());
    m_volumeLoaded = true;
}

// This function draws the menu
void Menu::drawMenu(const glm::ivec2& pos, const glm::ivec2& size, std::chrono::duration<double> renderTime)
{
    static bool open = 1;
    ImGui::Begin("VolVis", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowPos(ImVec2(float(pos.x), float(pos.y)));
    ImGui::SetWindowSize(ImVec2(float(size.x), float(size.y)));

    ImGui::BeginTabBar("VolVisTabs");
    showLoadVolTab();
    if (m_volumeLoaded) {
        const auto renderConfigBefore = m_renderConfig;
        const auto interpolationModeBefore = m_interpolationMode;

        showRayCastTab(renderTime);
        showTransFuncTab();
        show2DTransFuncTab();
        showLightEditorTab();

        if (m_renderConfig != renderConfigBefore)
            callRenderConfigChangedCallback();
        if (m_interpolationMode != interpolationModeBefore)
            callInterpolationModeChangedCallback();
    }

    ImGui::EndTabBar();
    ImGui::End();
}

// This renders the Load Volume tab, which shows a "Load" button and some volume information
void Menu::showLoadVolTab()
{
    if (ImGui::BeginTabItem("Load")) {
        // Create drop-down with all data files
        if (ImGui::BeginCombo("Volumes", currentFileName.c_str())) {
            for (const auto &file : std::filesystem::directory_iterator(DATA_PATH)) {
                const std::filesystem::path &fileName = file.path().filename();
                bool isSelected = fileName.string() == currentFileName;
                if (ImGui::Selectable(fileName.string().c_str(), isSelected)) { currentFileName = fileName.string(); }
                if (isSelected) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }

        // Create load button
        if (ImGui::Button("Load volume")) {
            // Check if an actual file has been selected
            if (currentFileName != "FLD File" && m_optLoadVolumeCallback) {
                (*m_optLoadVolumeCallback)(DATA_PATH / currentFileName);
            }
        }
        ImGui::EndTabItem();
    }
    if (m_volumeLoaded) {ImGui::Text("%s", m_volumeInfo.c_str());}
}

void Menu::showIsoOptions() {
    ImGui::Text("Iso shading parameters");
    ImGui::ColorEdit3("Iso Colour", glm::value_ptr(m_renderConfig.isoColor));
    ImGui::DragFloat("Iso Value", &m_renderConfig.isoValue, 0.1f, 0.0f, float(m_volumeMax));
}

void Menu::showEdgeDetectionOptions() {
    ImGui::Text("Edge detection");
    ImGui::Checkbox("Enable", &m_renderConfig.edgeDetection);
    if (m_renderConfig.edgeDetection) { ImGui::SliderFloat("Threshold", &m_renderConfig.edgeThreshold, 0.0f, 6.0f); }
    if (m_renderConfig.edgeDetection) { ImGui::ColorEdit4("Edge color", glm::value_ptr(m_renderConfig.edgeColor)); }
}

// This renders the options for configuring Gooch shading
void Menu::showGoochOptions() {
    ImGui::Text("Gooch shading coefficients");
    ImGui::SliderFloat("kBlue",             &m_renderConfig.blueCoeff,                      0.0f, 1.0f);
    ImGui::SliderFloat("kYellow",           &m_renderConfig.yellowCoeff,                    0.0f, 1.0f);
    ImGui::SliderFloat("Cool Diffuse",      &m_renderConfig.coolDiffuseCoeff,               0.0f, 1.0f);
    ImGui::SliderFloat("Warm Diffuse",      &m_renderConfig.warmDiffuseCoeff,               0.0f, 1.0f);
}

// This renders the RayCast tab, where the user can set the render mode, interpolation mode and other
//  render-related settings
void Menu::showRayCastTab(std::chrono::duration<double> renderTime)
{
    if (ImGui::BeginTabItem("Raycaster")) {
        const std::string renderText = fmt::format("rendering time: {}ms\nrendering resolution: ({}, {})\n",
            std::chrono::duration_cast<std::chrono::milliseconds>(renderTime).count(), m_renderConfig.renderResolution.x, m_renderConfig.renderResolution.y);
        ImGui::Text("%s", renderText.c_str());
        ImGui::NewLine();

        int* pRenderModeInt = reinterpret_cast<int*>(&m_renderConfig.renderMode);
        ImGui::Text("Render Mode:");
        ImGui::RadioButton("Slicer", pRenderModeInt, int(render::RenderMode::RenderSlicer));
        ImGui::RadioButton("MIP", pRenderModeInt, int(render::RenderMode::RenderMIP));
        ImGui::RadioButton("IsoSurface Rendering", pRenderModeInt, int(render::RenderMode::RenderIso));
        ImGui::RadioButton("Compositing", pRenderModeInt, int(render::RenderMode::RenderComposite));
        ImGui::RadioButton("2D Transfer Function", pRenderModeInt, int(render::RenderMode::RenderTF2D));

        ImGui::NewLine();

        if (m_renderConfig.renderMode == render::RenderMode::RenderIso) { showIsoOptions(); }

        ImGui::NewLine();
        ImGui::Separator();

        int* pShadingModeInt = reinterpret_cast<int*>(&m_renderConfig.shadingMode);
        ImGui::Text("Shading:");
        ImGui::RadioButton("None", pShadingModeInt, int(render::ShadingMode::ShadingNone));
        ImGui::RadioButton("Phong", pShadingModeInt, int(render::ShadingMode::ShadingPhong));
        ImGui::RadioButton("Gooch", pShadingModeInt, int(render::ShadingMode::ShadingGooch));
        if (m_renderConfig.shadingMode == render::ShadingMode::ShadingGooch) { showGoochOptions(); }

        ImGui::NewLine();
        ImGui::Separator();

        showEdgeDetectionOptions();

        ImGui::NewLine();
        ImGui::Separator();

        ImGui::DragFloat("Resolution scale", &m_resolutionScale, 0.0025f, 0.25f, 2.0f);
        m_renderConfig.renderResolution = glm::ivec2(glm::vec2(m_baseRenderResolution) * m_resolutionScale);

        ImGui::NewLine();

        int* pInterpolationModeInt = reinterpret_cast<int*>(&m_interpolationMode);
        ImGui::Text("Interpolation:");
        ImGui::RadioButton("Nearest Neighbour", pInterpolationModeInt, int(volume::InterpolationMode::NearestNeighbour));
        ImGui::RadioButton("Linear", pInterpolationModeInt, int(volume::InterpolationMode::Linear));
        ImGui::RadioButton("TriCubic", pInterpolationModeInt, int(volume::InterpolationMode::Cubic));

        ImGui::EndTabItem();
    }
}

// This renders the 1D Transfer Function Widget.
void Menu::showTransFuncTab()
{
    if (ImGui::BeginTabItem("Transfer function")) {
        m_tfWidget->draw();
        m_tfWidget->updateRenderConfig(m_renderConfig);
        ImGui::EndTabItem();
    }
}

// This renders the 2D Transfer Function Widget.
void Menu::show2DTransFuncTab()
{
    if (ImGui::BeginTabItem("2D transfer function")) {
        m_tf2DWidget->draw();
        m_tf2DWidget->updateRenderConfig(m_renderConfig);
        ImGui::EndTabItem();
    }
}

// This renders the Lights Editor Widget
void Menu::showLightEditorTab() {
    if (ImGui::BeginTabItem("Lights")) {
        m_leWidget->draw();
        m_leWidget->updateRenderConfig(m_renderConfig);
        ImGui::EndTabItem();
    }
}

void Menu::callRenderConfigChangedCallback() const
{
    if (m_optRenderConfigChangedCallback)
        (*m_optRenderConfigChangedCallback)(m_renderConfig);
}

void Menu::callInterpolationModeChangedCallback() const
{
    if (m_optInterpolationModeChangedCallback)
        (*m_optInterpolationModeChangedCallback)(m_interpolationMode);
}

}
