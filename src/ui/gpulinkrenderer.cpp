#include "gpulinktest.h"
#include "gpulinksample.h"
#include <QVersionNumber>
#include <QVulkanInstance>
#include <QVulkanFunctions>
#include <QVulkanWindow>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
QString text(const char *source) { return QCoreApplication::translate("GpuLinkTest", source); }
constexpr int deviceLimit = 64;
constexpr int extensionLimit = 1024;
constexpr int sampleIntervalMs = 250;
constexpr int testDurationMs = 15000;

// Resolve the complete PCI identity. Never infer it from a device name or vendor ID.
bool isTarget(QVulkanInstance &instance, VkPhysicalDevice physical, const QString &bdf)
{
    auto *f = instance.functions();
    uint32_t count = 0;
    if (f->vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS
        || count == 0 || count > extensionLimit) return false;
    QVector<VkExtensionProperties> extensions(static_cast<int>(count));
    if (f->vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()) != VK_SUCCESS
        || count > static_cast<uint32_t>(extensions.size())) return false;
    bool hasPci = false;
    for (uint32_t i = 0; i < count; ++i)
        hasPci |= std::strcmp(extensions[static_cast<int>(i)].extensionName, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME) == 0;
    if (!hasPci) return false;
    auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(instance.getInstanceProcAddr("vkGetPhysicalDeviceProperties2"));
    if (!query) return false;
    VkPhysicalDevicePCIBusInfoPropertiesEXT pci {};
    pci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 properties {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &pci;
    query(physical, &properties);
    if (properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) return false;
    return QStringLiteral("%1:%2:%3.%4").arg(pci.pciDomain, 4, 16, QLatin1Char('0'))
        .arg(pci.pciBus, 2, 16, QLatin1Char('0')).arg(pci.pciDevice, 2, 16, QLatin1Char('0'))
        .arg(pci.pciFunction, 1, 16, QLatin1Char('0')) == bdf;
}

class LoadWindow;
class Renderer final : public QVulkanWindowRenderer {
public:
    explicit Renderer(LoadWindow *window) : window_(window) {}
    void initResources() override;
    void releaseResources() override;
    void startNextFrame() override;
    void logicalDeviceLost() override;
    void physicalDeviceLost() override;
private:
    VkShaderModule shader(const QString &path);
    LoadWindow *window_;
    QVulkanDeviceFunctions *f_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    QElapsedTimer elapsed_;
};
class LoadWindow final : public QVulkanWindow {
public:
    QString bdf;
    std::function<void(const QString &)> failed;
    std::function<void()> submitted;
    QVulkanWindowRenderer *createRenderer() override { return new Renderer(this); }
};

VkShaderModule Renderer::shader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return VK_NULL_HANDLE;
    const QByteArray bytes = file.read(1024 * 1024 + 1);
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || bytes.size() % 4) return VK_NULL_HANDLE;
    // QByteArray does not promise alignment appropriate for SPIR-V words.
    QVector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.constData(), static_cast<size_t>(bytes.size()));
    VkShaderModuleCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = static_cast<size_t>(bytes.size());
    info.pCode = words.constData();
    VkShaderModule module = VK_NULL_HANDLE;
    if (f_->vkCreateShaderModule(window_->device(), &info, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

void Renderer::initResources()
{
    // Recheck the actual selected Qt device before recording any workload. This
    // also catches enumeration order changes or a Qt fallback to another device.
    if (!isTarget(*window_->vulkanInstance(), window_->physicalDevice(), window_->bdf)) {
        window_->failed(text("The renderer could not verify the selected PCI GPU."));
        return;
    }
    f_ = window_->vulkanInstance()->deviceFunctions(window_->device());
    VkPushConstantRange push {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout {};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (f_->vkCreatePipelineLayout(window_->device(), &layout, nullptr, &layout_) != VK_SUCCESS) {
        window_->failed(text("Could not create the GPU workload layout."));
        return;
    }
    const VkShaderModule vertex = shader(QStringLiteral(":/gpulink/workload.vert.spv"));
    const VkShaderModule fragment = shader(QStringLiteral(":/gpulink/workload.frag.spv"));
    VkPipelineShaderStageCreateInfo stages[2] {};
    for (auto &stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    VkPipelineVertexInputStateCreateInfo input {};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly {};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport {};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth {};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState attachment {};
    attachment.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blend {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState states[] {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo pipeline {};
    pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &input;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pDepthStencilState = &depth;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = layout_;
    pipeline.renderPass = window_->defaultRenderPass();
    const bool ok = vertex && fragment && f_->vkCreateGraphicsPipelines(window_->device(), VK_NULL_HANDLE,
        1, &pipeline, nullptr, &pipeline_) == VK_SUCCESS;
    if (vertex) f_->vkDestroyShaderModule(window_->device(), vertex, nullptr);
    if (fragment) f_->vkDestroyShaderModule(window_->device(), fragment, nullptr);
    if (!ok) window_->failed(text("Could not create the GPU workload pipeline."));
    elapsed_.start();
}
void Renderer::releaseResources()
{
    if (f_) {
        if (pipeline_) f_->vkDestroyPipeline(window_->device(), pipeline_, nullptr);
        if (layout_) f_->vkDestroyPipelineLayout(window_->device(), layout_, nullptr);
    }
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    f_ = nullptr;
}
void Renderer::logicalDeviceLost() { window_->failed(text("The Vulkan device was lost; test stopped.")); }
void Renderer::physicalDeviceLost() { window_->failed(text("The GPU became unavailable; test stopped.")); }
void Renderer::startNextFrame()
{
    if (!pipeline_ || !f_) { window_->frameReady(); return; }
    const QSize extent = window_->swapChainImageSize();
    // Cap shader work even on HiDPI desktops; the remaining area is cleared.
    const uint32_t width = static_cast<uint32_t>(std::min(extent.width(), 640));
    const uint32_t height = static_cast<uint32_t>(std::min(extent.height(), 360));
    VkClearValue clear[2] {};
    clear[0].color.float32[3] = 1.0f;
    clear[1].depthStencil.depth = 1.0f;
    VkRenderPassBeginInfo pass {};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = window_->defaultRenderPass();
    pass.framebuffer = window_->currentFramebuffer();
    pass.renderArea.extent = {static_cast<uint32_t>(extent.width()), static_cast<uint32_t>(extent.height())};
    pass.clearValueCount = 2;
    pass.pClearValues = clear;
    const auto command = window_->currentCommandBuffer();
    f_->vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    f_->vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport viewport {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    VkRect2D scissor {{0, 0}, {width, height}};
    f_->vkCmdSetViewport(command, 0, 1, &viewport);
    f_->vkCmdSetScissor(command, 0, 1, &scissor);
    const float values[4] {static_cast<float>(width), static_cast<float>(height), elapsed_.elapsed() / 1000.0f, 0};
    f_->vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(values), values);
    f_->vkCmdDraw(command, 3, 1, 0, 0);
    f_->vkCmdEndRenderPass(command);
    window_->frameReady();
    window_->submitted();
}

QByteArray readAttribute(int directory, const char *name, int *error = nullptr)
{
    const int fd = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (error) *error = errno;
        return {};
    }
    char bytes[129];
    const ssize_t size = read(fd, bytes, sizeof(bytes));
    const int readError = size < 0 ? errno : (size == 0 || size > 128 ? EPROTO : 0);
    close(fd);
    if (error) *error = readError;
    if (readError) return {};
    return QByteArray(bytes, static_cast<int>(size)).trimmed();
}
QString formatLink(double speed, int width)
{
    return text("%1 GT/s ×%2").arg(QString::number(speed, 'g', 5)).arg(width);
}
} // namespace

int runGpuLinkTest(const QString &path, const QString &incarnation)
{
    QString error;
    QString summary;
    bool observed = false;
    // The child has no privileged mode and never loads a user-supplied shader.
    if (geteuid() == 0) error = text("Run the application as an ordinary user to test the GPU link.");
    const QString bdf = QFileInfo(path).fileName();
    const QRegularExpression address(QStringLiteral("^[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-7]$"));
    if (!path.startsWith(QStringLiteral("/sys/devices/")) || QFileInfo(path).canonicalFilePath() != path
        || !address.match(bdf).hasMatch()) error = text("Invalid PCI GPU path.");
    const int directory = error.isEmpty() ? open(path.toUtf8().constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat identity {};
    if (error.isEmpty()) {
        const QString expected = incarnation.section(QLatin1Char(':'), 0, 1);
        if (directory < 0 || fstat(directory, &identity) != 0
            || QStringLiteral("%1:%2").arg(static_cast<qulonglong>(identity.st_dev))
                .arg(static_cast<qulonglong>(identity.st_ino)) != expected)
            error = text("The selected device was removed or replaced.");
        bool valid = false;
        int classError = 0;
        const uint deviceClass = readAttribute(directory, "class", &classError).toUInt(&valid, 0);
        if (error.isEmpty() && classError) error = text("Could not read the selected device's PCI class.");
        if (error.isEmpty() && (!valid || deviceClass >> 16 != 3)) error = text("The selected device is not a PCI display adapter.");
    }
    if (error.isEmpty()) {
        QVulkanInstance instance;
        instance.setApiVersion(QVersionNumber(1, 1));
        if (!instance.create()) error = text("Vulkan 1.1 could not be initialized in this desktop session.");
        if (error.isEmpty()) {
            uint32_t count = 0;
            auto *f = instance.functions();
            QVector<VkPhysicalDevice> devices;
            if (f->vkEnumeratePhysicalDevices(instance.vkInstance(), &count, nullptr) == VK_SUCCESS && count > 0 && count <= deviceLimit) {
                devices.resize(static_cast<int>(count));
                if (f->vkEnumeratePhysicalDevices(instance.vkInstance(), &count, devices.data()) != VK_SUCCESS
                    || count > static_cast<uint32_t>(devices.size())) devices.clear();
                else devices.resize(static_cast<int>(count));
            }
            int selected = -1;
            for (int i = 0; i < devices.size(); ++i) {
                if (isTarget(instance, devices[i], bdf)) {
                    if (selected >= 0) { selected = -1; break; }
                    selected = i;
                }
            }
            if (selected < 0) error = text("No unique Vulkan GPU reports this PCI address. The driver must expose VK_EXT_pci_bus_info.");
            else {
                QDialog dialog;
                dialog.setWindowTitle(text("GPU PCIe link test"));
                auto *layout = new QVBoxLayout(&dialog);
                VkPhysicalDeviceProperties properties {};
                f->vkGetPhysicalDeviceProperties(devices[selected], &properties);
                auto *heading = new QLabel(text("%1 (%2)").arg(QString::fromUtf8(properties.deviceName), bdf), &dialog);
                heading->setTextFormat(Qt::PlainText);
                layout->addWidget(heading);
                auto *notice = new QLabel(text("Applying a graphics load for up to %1 seconds. Power use and fan noise may increase.\n"
                    "This observes the link; it does not measure throughput or guarantee maximum speed.").arg(testDurationMs / 1000), &dialog);
                notice->setWordWrap(true);
                layout->addWidget(notice);
                auto *window = new LoadWindow;
                window->bdf = bdf;
                window->setVulkanInstance(&instance);
                window->setPhysicalDeviceIndex(selected);
                auto *container = QWidget::createWindowContainer(window, &dialog);
                container->setFixedSize(480, 270);
                layout->addWidget(container);
                auto *live = new QLabel(text("Starting renderer…"), &dialog);
                live->setTextFormat(Qt::PlainText);
                layout->addWidget(live);
                auto *best = new QLabel(text("Highest observed: unavailable"), &dialog);
                best->setTextFormat(Qt::PlainText);
                layout->addWidget(best);
                auto *credit = new QLabel(text("Procedural waves — QLinuxDeviceManager contributors\nGPLv3 (application license)"), &dialog);
                credit->setTextFormat(Qt::PlainText);
                credit->setWordWrap(true);
                layout->addWidget(credit);
                auto *stop = new QPushButton(text("Stop"), &dialog);
                layout->addWidget(stop);
                QObject::connect(stop, &QPushButton::clicked, &dialog, &QDialog::reject);
                std::atomic<qint64> lastFrame {-1};
                QElapsedTimer clock;
                clock.start();
                QThread *sampler = nullptr;
                GpuLinkSample peak;
                QString peakTime;
                bool ended = false;
                window->failed = [&](const QString &why) {
                    if (ended) return;
                    ended = true;
                    error = why;
                    QTimer::singleShot(0, &dialog, &QDialog::reject);
                };
                window->submitted = [&] {
                    lastFrame.store(clock.elapsed());
                    if (sampler || ended) return;
                    QTimer::singleShot(testDurationMs, &dialog, &QDialog::accept);
                    sampler = QThread::create([&] {
                        // Read in one bounded worker. Kernel reads can still stall;
                        // the parent process enforces the overall deadline.
                        while (!QThread::currentThread()->isInterruptionRequested()) {
                            struct stat now {};
                            if (stat(path.toUtf8().constData(), &now) != 0 || now.st_dev != identity.st_dev || now.st_ino != identity.st_ino) {
                                QMetaObject::invokeMethod(&dialog, [&] { window->failed(text("The selected GPU was removed or replaced.")); }, Qt::QueuedConnection);
                                return;
                            }
                            int speedError = 0, widthError = 0;
                            const QByteArray speedText = readAttribute(directory, "current_link_speed", &speedError);
                            const QByteArray widthText = readAttribute(directory, "current_link_width", &widthError);
                            if (speedError || widthError) {
                                const int code = speedError ? speedError : widthError;
                                const QString why = code == EACCES || code == EPERM
                                    ? text("Permission denied reading the GPU PCIe link.")
                                    : code == ENOENT || code == ENODEV
                                        ? text("The GPU PCIe link attributes are no longer available.")
                                        : text("Error reading the GPU PCIe link (errno %1).").arg(code);
                                QMetaObject::invokeMethod(&dialog, [&, why] { window->failed(why); }, Qt::QueuedConnection);
                                return;
                            }
                            GpuLinkSample sample;
                            if (!parseGpuLinkSample(speedText, widthText, sample)) {
                                QMetaObject::invokeMethod(&dialog, [&] { window->failed(text("The kernel PCIe link reading is unavailable or unreadable.")); }, Qt::QueuedConnection);
                                return;
                            }
                            // Bracket both reads to discard an observed retraining
                            // transition. Sysfs provides no atomic speed/width pair.
                            if (readAttribute(directory, "current_link_speed") != speedText
                                || readAttribute(directory, "current_link_width") != widthText) {
                                QThread::msleep(sampleIntervalMs);
                                continue;
                            }
                            if (stat(path.toUtf8().constData(), &now) != 0 || now.st_dev != identity.st_dev || now.st_ino != identity.st_ino) {
                                QMetaObject::invokeMethod(&dialog, [&] { window->failed(text("The selected GPU was removed or replaced.")); }, Qt::QueuedConnection);
                                return;
                            }
                            const qint64 frame = lastFrame.load();
                            if (frame >= 0 && clock.elapsed() - frame <= 1000) {
                                const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODate);
                                QMetaObject::invokeMethod(&dialog, [&, sample, stamp] {
                                    if (ended) return;
                                    observed = true;
                                    live->setText(text("Current link during rendering: %1").arg(formatLink(sample.speed, sample.width)));
                                    if (sample.exceeds(peak)) {
                                        peak = sample;
                                        peakTime = stamp;
                                        best->setText(text("Highest observed: %1").arg(formatLink(sample.speed, sample.width)));
                                    }
                                }, Qt::QueuedConnection);
                            }
                            QThread::msleep(sampleIntervalMs);
                        }
                    });
                    sampler->start();
                };
                QTimer frames;
                frames.setInterval(16);
                QObject::connect(&frames, &QTimer::timeout, &dialog, [window] {
                    if (window->isExposed()) window->requestUpdate();
                });
                frames.start();
                QTimer::singleShot(5000, &dialog, [&] {
                    if (!sampler) window->failed(text("The GPU renderer did not start within five seconds."));
                });
                dialog.exec();
                ended = true;
                frames.stop();
                if (sampler) {
                    sampler->requestInterruption();
                    sampler->wait();
                    delete sampler;
                }
                if (error.isEmpty() && observed)
                    summary = text("Highest observed during graphics load: %1 (%2). Maximum not guaranteed.")
                        .arg(formatLink(peak.speed, peak.width), peakTime);
                else if (error.isEmpty()) error = text("Test stopped before any link readings were collected.");
                // Destroy renderer and its Vulkan resources while captured state
                // and the instance are still alive; do not persist a GPU context.
                delete container;
            }
        }
    }
    if (error.isEmpty() && observed) {
        struct stat finalIdentity {};
        if (stat(path.toUtf8().constData(), &finalIdentity) != 0 || finalIdentity.st_dev != identity.st_dev || finalIdentity.st_ino != identity.st_ino)
            error = text("The selected GPU was removed or replaced.");
    }
    if (directory >= 0) close(directory);
    const QJsonObject result {{QStringLiteral("ok"), error.isEmpty() && observed},
        {QStringLiteral("summary"), error.isEmpty() ? summary : error}};
    const QByteArray report = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(report.constData(), 1, static_cast<size_t>(report.size()), stdout);
    std::fflush(stdout);
    return error.isEmpty() ? 0 : 1;
}
