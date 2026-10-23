#include "gpulinktest.h"
#include "gpulinksample.h"
#include <QVersionNumber>
#include <QVulkanInstance>
#include <QVulkanFunctions>
#include <QImage>
#include <QPixmap>
#include <mutex>
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
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
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

constexpr uint32_t renderWidth = 640;
constexpr uint32_t renderHeight = 360;
constexpr int previewIntervalMs = 16;

// All device, queue and resource access belongs to the rendering worker. The
// instance is created before the worker starts and destroyed only after it joins.
class Renderer final {
public:
    Renderer(QVulkanInstance &instance, VkPhysicalDevice physical, const QString &bdf)
        : instance_(instance), physical_(physical), bdf_(bdf) {}
    ~Renderer();
    bool initialize();
    VkResult render(float seconds, bool preview, QImage &image);
private:
    bool createTarget();
    bool allocate(const VkMemoryRequirements &requirements, VkMemoryPropertyFlags flags, VkDeviceMemory &memory);
    VkShaderModule shader(const QString &path);
    QVulkanInstance &instance_;
    VkPhysicalDevice physical_;
    QString bdf_;
    VkDevice device_ = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *f_ = nullptr;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkImage target_ = VK_NULL_HANDLE;
    VkDeviceMemory targetMemory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkRenderPass pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    void *mapped_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

bool Renderer::allocate(const VkMemoryRequirements &requirements, VkMemoryPropertyFlags flags, VkDeviceMemory &memory)
{
    // Bounded application-owned allocations; driver-internal allocation is not
    // covered by this limit. The image and readback are each under 1 MiB of pixels.
    if (!requirements.size || requirements.size > 64 * 1024 * 1024) return false;
    VkPhysicalDeviceMemoryProperties properties {};
    instance_.functions()->vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if (!(requirements.memoryTypeBits & (1u << i))
            || (properties.memoryTypes[i].propertyFlags & flags) != flags) continue;
        VkMemoryAllocateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        info.allocationSize = requirements.size;
        info.memoryTypeIndex = i;
        return f_->vkAllocateMemory(device_, &info, nullptr, &memory) == VK_SUCCESS;
    }
    return false;
}

bool Renderer::createTarget()
{
    VkFormatProperties format {};
    instance_.functions()->vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R8G8B8A8_UNORM, &format);
    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((format.optimalTilingFeatures & needed) != needed) return false;
    VkImageCreateInfo image {};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {renderWidth, renderHeight, 1};
    image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (f_->vkCreateImage(device_, &image, nullptr, &target_) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements {};
    f_->vkGetImageMemoryRequirements(device_, target_, &requirements);
    if (!allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, targetMemory_)
        || f_->vkBindImageMemory(device_, target_, targetMemory_, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo view {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = target_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (f_->vkCreateImageView(device_, &view, nullptr, &view_) != VK_SUCCESS) return false;
    VkAttachmentDescription color {};
    color.format = image.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependencies[2] {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo pass {};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 1;
    pass.pAttachments = &color;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 2;
    pass.pDependencies = dependencies;
    if (f_->vkCreateRenderPass(device_, &pass, nullptr, &pass_) != VK_SUCCESS) return false;
    VkFramebufferCreateInfo framebuffer {};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = pass_;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &view_;
    framebuffer.width = renderWidth;
    framebuffer.height = renderHeight;
    framebuffer.layers = 1;
    if (f_->vkCreateFramebuffer(device_, &framebuffer, nullptr, &framebuffer_) != VK_SUCCESS) return false;
    VkBufferCreateInfo buffer {};
    buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer.size = renderWidth * renderHeight * 4;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (f_->vkCreateBuffer(device_, &buffer, nullptr, &readback_) != VK_SUCCESS) return false;
    f_->vkGetBufferMemoryRequirements(device_, readback_, &requirements);
    if (!allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, readbackMemory_)
        || f_->vkBindBufferMemory(device_, readback_, readbackMemory_, 0) != VK_SUCCESS
        || f_->vkMapMemory(device_, readbackMemory_, 0, VK_WHOLE_SIZE, 0, &mapped_) != VK_SUCCESS) return false;
    return true;
}

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
    if (f_->vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

bool Renderer::initialize()
{
    if (!isTarget(instance_, physical_, bdf_)) return false;
    auto *functions = instance_.functions();
    uint32_t count = 0;
    functions->vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, nullptr);
    if (!count || count > 256) return false;
    QVector<VkQueueFamilyProperties> queues(static_cast<int>(count));
    functions->vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, queues.data());
    if (count > static_cast<uint32_t>(queues.size())) return false;
    uint32_t family = count;
    for (uint32_t i = 0; i < count; ++i)
        if (queues[static_cast<int>(i)].queueCount && (queues[static_cast<int>(i)].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    if (family == count) return false;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue {};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo device {};
    device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &queue;
    if (functions->vkCreateDevice(physical_, &device, nullptr, &device_) != VK_SUCCESS) return false;
    f_ = instance_.deviceFunctions(device_);
    f_->vkGetDeviceQueue(device_, family, 0, &queue_);
    VkCommandPoolCreateInfo pool {};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = family;
    if (f_->vkCreateCommandPool(device_, &pool, nullptr, &pool_) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo command {};
    command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command.commandPool = pool_;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    if (f_->vkAllocateCommandBuffers(device_, &command, &command_) != VK_SUCCESS) return false;
    VkFenceCreateInfo fence {};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (f_->vkCreateFence(device_, &fence, nullptr, &fence_) != VK_SUCCESS || !createTarget()) return false;
    VkPushConstantRange push {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout {};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (f_->vkCreatePipelineLayout(device_, &layout, nullptr, &layout_) != VK_SUCCESS) return false;
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
    pipeline.renderPass = pass_;
    const bool ok = vertex && fragment && f_->vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE,
        1, &pipeline, nullptr, &pipeline_) == VK_SUCCESS;
    if (vertex) f_->vkDestroyShaderModule(device_, vertex, nullptr);
    if (fragment) f_->vkDestroyShaderModule(device_, fragment, nullptr);
    return ok;
}
Renderer::~Renderer()
{
    if (!f_) return;
    // Interruption stops new submissions, not already executing GPU commands.
    // The parent process deadline also covers a driver stalled during cleanup.
    f_->vkDeviceWaitIdle(device_);
    if (pipeline_) f_->vkDestroyPipeline(device_, pipeline_, nullptr);
    if (layout_) f_->vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (framebuffer_) f_->vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    if (pass_) f_->vkDestroyRenderPass(device_, pass_, nullptr);
    if (view_) f_->vkDestroyImageView(device_, view_, nullptr);
    if (target_) f_->vkDestroyImage(device_, target_, nullptr);
    if (targetMemory_) f_->vkFreeMemory(device_, targetMemory_, nullptr);
    if (mapped_) f_->vkUnmapMemory(device_, readbackMemory_);
    if (readback_) f_->vkDestroyBuffer(device_, readback_, nullptr);
    if (readbackMemory_) f_->vkFreeMemory(device_, readbackMemory_, nullptr);
    if (fence_) f_->vkDestroyFence(device_, fence_, nullptr);
    if (pool_) f_->vkDestroyCommandPool(device_, pool_, nullptr);
    f_->vkDestroyDevice(device_, nullptr);
    instance_.resetDeviceFunctions(device_);
}
VkResult Renderer::render(float seconds, bool preview, QImage &image)
{
    VkResult result = f_->vkResetCommandBuffer(command_, 0);
    if (result != VK_SUCCESS) return result;
    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = f_->vkBeginCommandBuffer(command_, &begin);
    if (result != VK_SUCCESS) return result;
    VkClearValue clear {};
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo pass {};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = pass_;
    pass.framebuffer = framebuffer_;
    pass.renderArea.extent = {renderWidth, renderHeight};
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    f_->vkCmdBeginRenderPass(command_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    f_->vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport viewport {0, 0, static_cast<float>(renderWidth), static_cast<float>(renderHeight), 0, 1};
    VkRect2D scissor {{0, 0}, {renderWidth, renderHeight}};
    f_->vkCmdSetViewport(command_, 0, 1, &viewport);
    f_->vkCmdSetScissor(command_, 0, 1, &scissor);
    const float values[4] {static_cast<float>(renderWidth), static_cast<float>(renderHeight), seconds, 0};
    f_->vkCmdPushConstants(command_, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(values), values);
    f_->vkCmdDraw(command_, 3, 1, 0, 0);
    f_->vkCmdEndRenderPass(command_);
    if (preview) {
        VkBufferImageCopy copy {};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {renderWidth, renderHeight, 1};
        f_->vkCmdCopyImageToBuffer(command_, target_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &copy);
        VkBufferMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = readback_;
        barrier.size = VK_WHOLE_SIZE;
        f_->vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
    result = f_->vkEndCommandBuffer(command_);
    if (result != VK_SUCCESS) return result;
    result = f_->vkResetFences(device_, 1, &fence_);
    if (result != VK_SUCCESS) return result;
    VkSubmitInfo submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    result = f_->vkQueueSubmit(queue_, 1, &submit, fence_);
    if (result != VK_SUCCESS) return result;
    // Wait only for this one submission. A signaled fence returns immediately;
    // the 5 ms timeout polls cancellation and is not a frame pacing interval.
    QElapsedTimer waiting;
    waiting.start();
    do {
        result = f_->vkWaitForFences(device_, 1, &fence_, VK_TRUE, 5000000);
        if (QThread::currentThread()->isInterruptionRequested()) return VK_NOT_READY;
    } while (result == VK_TIMEOUT && waiting.elapsed() < 5000);
    if (result != VK_SUCCESS) return result;
    if (preview) {
        VkMappedMemoryRange range {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = readbackMemory_;
        range.size = VK_WHOLE_SIZE;
        result = f_->vkInvalidateMappedMemoryRanges(device_, 1, &range);
        if (result != VK_SUCCESS) return result;
        image = QImage(static_cast<const uchar *>(mapped_), static_cast<int>(renderWidth),
            static_cast<int>(renderHeight), static_cast<int>(renderWidth * 4), QImage::Format_RGBA8888).copy();
    }
    return VK_SUCCESS;
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
    GpuLinkSample peak;
    QString peakTime;
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
        if (!instance.create()) error = text("Vulkan 1.1 could not be initialized.");
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
                auto *preview = new QLabel(&dialog);
                preview->setFixedSize(480, 270);
                preview->setAlignment(Qt::AlignCenter);
                preview->setText(text("Starting renderer…"));
                layout->addWidget(preview);
                auto *rate = new QLabel(text("Offscreen rendering: starting…"), &dialog);
                rate->setTextFormat(Qt::PlainText);
                layout->addWidget(rate);
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
                bool ended = false;
                const auto failed = [&](const QString &why) {
                    if (ended) return;
                    ended = true;
                    error = why;
                    QTimer::singleShot(0, &dialog, &QDialog::reject);
                };
                const auto startSampling = [&] {
                    if (sampler || ended) return;
                    QTimer::singleShot(testDurationMs, &dialog, &QDialog::accept);
                    sampler = QThread::create([&] {
                        // Read in one bounded worker. Kernel reads can still stall;
                        // the parent process enforces the overall deadline.
                        while (!QThread::currentThread()->isInterruptionRequested()) {
                            struct stat now {};
                            if (stat(path.toUtf8().constData(), &now) != 0 || now.st_dev != identity.st_dev || now.st_ino != identity.st_ino) {
                                QMetaObject::invokeMethod(&dialog, [&] { failed(text("The selected GPU was removed or replaced.")); }, Qt::QueuedConnection);
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
                                QMetaObject::invokeMethod(&dialog, [&, why] { failed(why); }, Qt::QueuedConnection);
                                return;
                            }
                            GpuLinkSample sample;
                            if (!parseGpuLinkSample(speedText, widthText, sample)) {
                                QMetaObject::invokeMethod(&dialog, [&] { failed(text("The kernel PCIe link reading is unavailable or unreadable.")); }, Qt::QueuedConnection);
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
                                QMetaObject::invokeMethod(&dialog, [&] { failed(text("The selected GPU was removed or replaced.")); }, Qt::QueuedConnection);
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
                // At most one owned preview image is waiting for the GUI. There
                // is no per-frame event queue and no surface or swapchain.
                std::mutex renderMutex;
                QImage latestPreview;
                QString renderError;
                std::atomic<quint64> completedFrames {0};
                std::atomic<bool> renderFinished {false};
                auto *renderWorker = QThread::create([&] {
                    {
                        Renderer renderer(instance, devices[selected], bdf);
                        if (!renderer.initialize()) {
                            std::lock_guard<std::mutex> lock(renderMutex);
                            renderError = text("Could not initialize offscreen rendering on the selected PCI GPU.");
                        } else {
                            QElapsedTimer rendering;
                            rendering.start();
                            qint64 lastPreview = -previewIntervalMs;
                            while (!QThread::currentThread()->isInterruptionRequested()
                                   && rendering.elapsed() < testDurationMs) {
                                const bool updatePreview = rendering.elapsed() - lastPreview >= previewIntervalMs;
                                QImage image;
                                const VkResult result = renderer.render(rendering.elapsed() / 1000.0f, updatePreview, image);
                                if (result == VK_NOT_READY && QThread::currentThread()->isInterruptionRequested()) break;
                                if (result != VK_SUCCESS) {
                                    std::lock_guard<std::mutex> lock(renderMutex);
                                    renderError = result == VK_ERROR_DEVICE_LOST
                                        ? text("The Vulkan device was lost; test stopped.")
                                        : text("Offscreen rendering failed (Vulkan result %1).").arg(static_cast<int>(result));
                                    break;
                                }
                                // Only completed work counts as a rendering observation.
                                lastFrame.store(clock.elapsed());
                                completedFrames.fetch_add(1);
                                if (updatePreview) {
                                    lastPreview = rendering.elapsed();
                                    std::lock_guard<std::mutex> lock(renderMutex);
                                    latestPreview = std::move(image);
                                }
                            }
                        }
                    } // Vulkan cleanup stays on the rendering worker.
                    renderFinished.store(true);
                });
                QTimer previewTimer;
                previewTimer.setInterval(previewIntervalMs);
                quint64 previousFrames = 0;
                qint64 rateTime = clock.elapsed();
                const auto updatePreview = [&] {
                    QImage image;
                    QString failure;
                    {
                        std::lock_guard<std::mutex> lock(renderMutex);
                        image.swap(latestPreview);
                        failure = renderError;
                    }
                    if (!failure.isEmpty()) { failed(failure); return; }
                    if (!image.isNull()) preview->setPixmap(QPixmap::fromImage(image).scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    if (lastFrame.load() >= 0) startSampling();
                    const qint64 now = clock.elapsed();
                    if (now - rateTime >= 1000) {
                        const quint64 count = completedFrames.load();
                        rate->setText(text("Offscreen rendering: %1 frames/s (uncapped)")
                            .arg(QString::number((count - previousFrames) * 1000.0 / (now - rateTime), 'f', 0)));
                        previousFrames = count;
                        rateTime = now;
                    }
                    if (renderFinished.load() && !ended) dialog.accept();
                };
                QObject::connect(&previewTimer, &QTimer::timeout, &dialog, updatePreview);
                previewTimer.start();
                renderWorker->start();
                QTimer::singleShot(5000, &dialog, [&] {
                    if (!sampler && !ended) failed(text("The GPU renderer did not start within five seconds."));
                });
                dialog.exec();
                ended = true;
                previewTimer.stop();
                renderWorker->requestInterruption();
                if (sampler) sampler->requestInterruption();
                renderWorker->wait();
                delete renderWorker;
                {
                    std::lock_guard<std::mutex> lock(renderMutex);
                    if (!renderError.isEmpty()) error = renderError;
                }
                if (sampler) {
                    sampler->wait();
                    delete sampler;
                }
                if (error.isEmpty() && observed)
                    summary = text("Highest observed during graphics load: %1 (%2). Maximum not guaranteed.")
                        .arg(formatLink(peak.speed, peak.width), peakTime);
                else if (error.isEmpty()) error = text("Test stopped before any link readings were collected.");
            }
        }
    }
    if (error.isEmpty() && observed) {
        struct stat finalIdentity {};
        if (stat(path.toUtf8().constData(), &finalIdentity) != 0 || finalIdentity.st_dev != identity.st_dev || finalIdentity.st_ino != identity.st_ino)
            error = text("The selected GPU was removed or replaced.");
    }
    if (directory >= 0) close(directory);
    QJsonObject result {{QStringLiteral("ok"), error.isEmpty() && observed},
        {QStringLiteral("summary"), error.isEmpty() ? summary : error}};
    if (error.isEmpty() && observed) {
        result.insert(QStringLiteral("speed_gt_s"), peak.speed);
        result.insert(QStringLiteral("width"), peak.width);
        result.insert(QStringLiteral("observed_at"), peakTime);
    }
    const QByteArray report = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(report.constData(), 1, static_cast<size_t>(report.size()), stdout);
    std::fflush(stdout);
    return error.isEmpty() ? 0 : 1;
}
