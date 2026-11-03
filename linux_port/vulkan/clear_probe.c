#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s:%d: %s returned %d\n", __FILE__, __LINE__, #call, r_); exit(1); } } while (0)

static uint32_t memory_type(VkPhysicalDevice device, uint32_t bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(device, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & required) == required) return i;
    fprintf(stderr, "no Vulkan memory type for flags %#x\n", required);
    exit(1);
}

static VkDeviceMemory allocate(VkPhysicalDevice physical, VkDevice device, VkMemoryRequirements req,
                               VkMemoryPropertyFlags flags) {
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = memory_type(physical, req.memoryTypeBits, flags) };
    VkDeviceMemory memory;
    VK_CHECK(vkAllocateMemory(device, &ai, NULL, &memory));
    return memory;
}

int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "linux-port-clear-probe",
        .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&ici, NULL, &instance));

    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (!count) { fprintf(stderr, "no Vulkan physical device\n"); return 1; }
    VkPhysicalDevice *devices = calloc(count, sizeof *devices);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX;
    char gpu_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "unknown";
    for (uint32_t i = 0; i < count && !physical; i++) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qcount, NULL);
        VkQueueFamilyProperties *queues = calloc(qcount, sizeof *queues);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qcount, queues);
        for (uint32_t q = 0; q < qcount; q++) if (queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            physical = devices[i]; queue_family = q;
            VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical, &props);
            snprintf(gpu_name, sizeof gpu_name, "%s", props.deviceName);
            break;
        }
        free(queues);
    }
    free(devices);
    if (!physical) { fprintf(stderr, "no Vulkan graphics queue\n"); return 1; }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, queue_family, 0, &queue);

    VkImageCreateInfo image_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {64, 64, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VkImage image; VK_CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements image_req; vkGetImageMemoryRequirements(device, image, &image_req);
    VkDeviceMemory image_memory = allocate(physical, device, image_req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkBindImageMemory(device, image, image_memory, 0));

    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 64 * 64 * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer buffer; VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements buffer_req; vkGetBufferMemoryRequirements(device, buffer, &buffer_req);
    VkDeviceMemory buffer_memory = allocate(physical, device, buffer_req,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkBindBufferMemory(device, buffer, buffer_memory, 0));

    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_family, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT };
    VkCommandPool pool; VK_CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer command; VK_CHECK(vkAllocateCommandBuffers(device, &cai, &command));
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VK_CHECK(vkBeginCommandBuffer(command, &begin));

    VkImageMemoryBarrier to_clear = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &to_clear);
    VkClearColorValue color = { .float32 = {0.25f, 0.5f, 0.75f, 1.0f} };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);

    VkImageMemoryBarrier to_copy = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &to_copy);
    VkBufferImageCopy copy = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = {64, 64, 1} };
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    VK_CHECK(vkEndCommandBuffer(command));

    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &command };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    uint8_t *pixels;
    VK_CHECK(vkMapMemory(device, buffer_memory, 0, buffer_info.size, 0, (void **)&pixels));
    const uint8_t expected[4] = {64, 128, 191, 255};
    int failed = 0;
    for (size_t i = 0; i < 64 * 64; i++) if (memcmp(pixels + i * 4, expected, 4)) { failed = 1; break; }
    vkUnmapMemory(device, buffer_memory);
    printf("Vulkan device: %s\n", gpu_name);
    printf("offscreen RGBA8 clear/readback: %s\n", failed ? "FAILED" : "passed (16384 pixels)");

    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyBuffer(device, buffer, NULL); vkFreeMemory(device, buffer_memory, NULL);
    vkDestroyImage(device, image, NULL); vkFreeMemory(device, image_memory, NULL);
    vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL);
    return failed;
}
