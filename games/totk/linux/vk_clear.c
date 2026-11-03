/* Linux bring-up: SDL2 window + Vulkan 1.3 swapchain, clears and presents each frame.
   First step of a Vulkan backend for the NVN layer (see how-it-works.md). Usage: vk_clear [--frames N] */
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s:%d %s = %d\n", __FILE__, __LINE__, #x, r_); exit(1); } } while (0)
#define FRAMES_IN_FLIGHT 2
#define MAX_IMAGES 8

static SDL_Window *win;
static VkInstance inst;
static VkSurfaceKHR surf;
static VkPhysicalDevice pdev;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;
static VkSwapchainKHR sc;
static VkFormat sc_format;
static VkExtent2D sc_extent;
static VkImage sc_images[MAX_IMAGES];
static VkSemaphore render_done[MAX_IMAGES];
static uint32_t sc_count;
static VkCommandPool pool;
static VkCommandBuffer cmd[FRAMES_IN_FLIGHT];
static VkSemaphore image_ready[FRAMES_IN_FLIGHT];
static VkFence in_flight[FRAMES_IN_FLIGHT];

static void create_instance(void) {
    unsigned n = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(win, &n, NULL)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); exit(1); }
    const char **ext = malloc(n * sizeof *ext);
    SDL_Vulkan_GetInstanceExtensions(win, &n, ext);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vk_clear", .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
                                .enabledExtensionCount = n, .ppEnabledExtensionNames = ext };
    CHECK(vkCreateInstance(&ci, NULL, &inst));
    free(ext);
    if (!SDL_Vulkan_CreateSurface(win, inst, &surf)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); exit(1); }
}

static void pick_device(void) {
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, NULL);
    if (!n) { fprintf(stderr, "no Vulkan device\n"); exit(1); }
    VkPhysicalDevice list[16]; if (n > 16) n = 16;
    vkEnumeratePhysicalDevices(inst, &n, list);
    for (int pass = 0; pass < 2 && !pdev; pass++)   /* discrete GPU first, then any */
        for (uint32_t i = 0; i < n && !pdev; i++) {
            VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(list[i], &p);
            if (pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;
            if (p.apiVersion < VK_API_VERSION_1_3) continue;
            uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(list[i], &qn, NULL);
            VkQueueFamilyProperties qp[16]; if (qn > 16) qn = 16;
            vkGetPhysicalDeviceQueueFamilyProperties(list[i], &qn, qp);
            for (uint32_t q = 0; q < qn; q++) {
                VkBool32 present = 0; vkGetPhysicalDeviceSurfaceSupportKHR(list[i], q, surf, &present);
                if ((qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { pdev = list[i]; qfam = q; break; }
            }
            if (pdev) printf("GPU: %s (Vulkan %u.%u.%u)\n", p.deviceName, VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion));
        }
    if (!pdev) { fprintf(stderr, "no Vulkan 1.3 device with graphics+present\n"); exit(1); }
}

static void create_device(void) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qfam, .queueCount = 1, .pQueuePriorities = &prio };
    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE };
    const char *ext[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi,
                              .enabledExtensionCount = 1, .ppEnabledExtensionNames = ext };
    CHECK(vkCreateDevice(pdev, &ci, NULL, &dev));
    vkGetDeviceQueue(dev, qfam, 0, &queue);
}

static void destroy_swapchain(void) {
    vkDeviceWaitIdle(dev);
    for (uint32_t i = 0; i < sc_count; i++) vkDestroySemaphore(dev, render_done[i], NULL);
    vkDestroySwapchainKHR(dev, sc, NULL);
    sc = VK_NULL_HANDLE; sc_count = 0;
}

static int create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps; CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pdev, surf, &caps));
    int w, h; SDL_Vulkan_GetDrawableSize(win, &w, &h);
    sc_extent = caps.currentExtent;
    if (sc_extent.width == 0xffffffffu) {
        sc_extent.width = (uint32_t)w < caps.minImageExtent.width ? caps.minImageExtent.width : (uint32_t)w > caps.maxImageExtent.width ? caps.maxImageExtent.width : (uint32_t)w;
        sc_extent.height = (uint32_t)h < caps.minImageExtent.height ? caps.minImageExtent.height : (uint32_t)h > caps.maxImageExtent.height ? caps.maxImageExtent.height : (uint32_t)h;
    }
    if (!sc_extent.width || !sc_extent.height) return 0;   /* minimised */

    uint32_t fn = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &fn, NULL);
    VkSurfaceFormatKHR fmts[32]; if (fn > 32) fn = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &fn, fmts);
    VkSurfaceFormatKHR pick = fmts[0];
    for (uint32_t i = 0; i < fn; i++) if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) pick = fmts[i];
    sc_format = pick.format;

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
    if (want > MAX_IMAGES) want = MAX_IMAGES;
    VkSwapchainCreateInfoKHR ci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surf, .minImageCount = want,
        .imageFormat = pick.format, .imageColorSpace = pick.colorSpace, .imageExtent = sc_extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE };
    CHECK(vkCreateSwapchainKHR(dev, &ci, NULL, &sc));
    sc_count = MAX_IMAGES; CHECK(vkGetSwapchainImagesKHR(dev, sc, &sc_count, sc_images));
    VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < sc_count; i++) CHECK(vkCreateSemaphore(dev, &si, NULL, &render_done[i]));
    return 1;
}

static void create_frame_objects(void) {
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = qfam };
    CHECK(vkCreateCommandPool(dev, &pi, NULL, &pool));
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = FRAMES_IN_FLIGHT };
    CHECK(vkAllocateCommandBuffers(dev, &ai, cmd));
    VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) { CHECK(vkCreateSemaphore(dev, &si, NULL, &image_ready[i])); CHECK(vkCreateFence(dev, &fi, NULL, &in_flight[i])); }
}

static void barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_acc, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_acc) {
    VkImageMemoryBarrier2 b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = src_stage, .srcAccessMask = src_acc,
        .dstStageMask = dst_stage, .dstAccessMask = dst_acc, .oldLayout = from, .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkDependencyInfo d = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b };
    vkCmdPipelineBarrier2(c, &d);
}

/* Returns 0 when the swapchain had to be rebuilt and the frame was skipped. */
static int draw_frame(int frame_index, uint64_t frame_no) {
    int f = frame_index % FRAMES_IN_FLIGHT;
    CHECK(vkWaitForFences(dev, 1, &in_flight[f], VK_TRUE, UINT64_MAX));
    uint32_t idx;
    VkResult r = vkAcquireNextImageKHR(dev, sc, UINT64_MAX, image_ready[f], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return 0;
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) CHECK(r);
    CHECK(vkResetFences(dev, 1, &in_flight[f]));

    VkCommandBuffer c = cmd[f];
    vkResetCommandBuffer(c, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    CHECK(vkBeginCommandBuffer(c, &bi));
    barrier(c, sc_images[idx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    float t = (float)(frame_no % 240) / 240.0f;
    VkClearColorValue col = { .float32 = { t, 0.2f, 1.0f - t, 1.0f } };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(c, sc_images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &range);
    barrier(c, sc_images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    CHECK(vkEndCommandBuffer(c));

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &image_ready[f], .pWaitDstStageMask = &wait_stage,
                        .commandBufferCount = 1, .pCommandBuffers = &c, .signalSemaphoreCount = 1, .pSignalSemaphores = &render_done[idx] };
    CHECK(vkQueueSubmit(queue, 1, &si, in_flight[f]));
    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &render_done[idx],
                            .swapchainCount = 1, .pSwapchains = &sc, .pImageIndices = &idx };
    r = vkQueuePresentKHR(queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) return 0;
    if (r != VK_SUCCESS) CHECK(r);
    return 1;
}

int main(int argc, char **argv) {
    long max_frames = -1;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--frames")) max_frames = atol(argv[i + 1]);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    win = SDL_CreateWindow("vk_clear", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }

    create_instance(); pick_device(); create_device(); create_frame_objects();
    int have_sc = create_swapchain();

    uint64_t frame = 0; int running = 1;
    while (running && (max_frames < 0 || (long)frame < max_frames)) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = 0;
        if (!have_sc) { SDL_Delay(50); have_sc = create_swapchain(); continue; }
        if (!draw_frame((int)frame, frame)) { destroy_swapchain(); have_sc = create_swapchain(); }
        frame++;
    }
    printf("presented %llu frames at %ux%u\n", (unsigned long long)frame, sc_extent.width, sc_extent.height);

    vkDeviceWaitIdle(dev);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) { vkDestroySemaphore(dev, image_ready[i], NULL); vkDestroyFence(dev, in_flight[i], NULL); }
    vkDestroyCommandPool(dev, pool, NULL);
    if (sc) destroy_swapchain();
    vkDestroyDevice(dev, NULL); vkDestroySurfaceKHR(inst, surf, NULL); vkDestroyInstance(inst, NULL);
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
