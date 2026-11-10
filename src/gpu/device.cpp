#include "device.hpp"

#include "context_internal.hpp"
#include "nvm/machine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neura::gpu {
namespace {

constexpr std::uint32_t shader_words[]{
#include "graph_eval.spv.inc"
};

constexpr VkDeviceSize minimum_buffer = VkDeviceSize{1} << 20;
constexpr VkDeviceSize maximum_buffer = VkDeviceSize{256} << 20;
constexpr std::uint32_t maximum_requests = 65'535;
constexpr std::uint32_t request_words = 4;
constexpr std::uint32_t directory_words = 8;
constexpr std::uint32_t evaluated_status = 2;
constexpr std::uint32_t escaped_status = 3;

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;
    VkMemoryPropertyFlags properties = 0;
};

struct Request {
    std::size_t job;
    std::uint32_t slot;
    std::uint32_t wire_base;
    std::uint32_t output_base = 0;
    std::uint32_t status_index = 0;
};

VkBufferMemoryBarrier buffer_barrier(VkBuffer buffer, VkAccessFlags source, VkAccessFlags destination) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = source;
    barrier.dstAccessMask = destination;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

} // namespace

std::span<const std::uint32_t> graph_eval_words() { return shader_words; }

std::uint32_t Context::max_storage_buffer_range() const {
    return state_->properties.limits.maxStorageBufferRange;
}

std::uint32_t Context::max_requests_per_dispatch() const {
    return std::min(maximum_requests, state_->properties.limits.maxComputeWorkGroupCount[0]);
}

struct VulkanGraphDevice::State {
    State(Context::State& context, const nvr::Image& image) : context(context), image(image) {
        if (context.device_in_use) throw std::logic_error("Vulkan context already serves a graph device");
        context.device_in_use = true;
    }

    ~State() {
        if (!context.timed_out) {
            context.vk.DeviceWaitIdle(context.device);
            if (commands[0] != VK_NULL_HANDLE) {
                context.vk.FreeCommandBuffers(context.device, context.command_pool,
                                              static_cast<std::uint32_t>(commands.size()), commands.data());
            }
            if (descriptor_pool != VK_NULL_HANDLE) {
                context.vk.DestroyDescriptorPool(context.device, descriptor_pool, nullptr);
            }
            destroy_buffer(wires);
            destroy_buffer(io);
            destroy_buffer(table_buffer);
            destroy_buffer(staging);
        }
        context.device_in_use = false;
    }

    void initialize() {
        if (context.timed_out) {
            failure = "the Vulkan context has timed out";
            return;
        }
        const auto storage_range = context.properties.limits.maxStorageBufferRange;
        tables = nvm::pack_layered(image, storage_range);
        request_limit = std::min(maximum_requests, context.properties.limits.maxComputeWorkGroupCount[0]);
        const auto buffer_ceiling = std::min(maximum_buffer, VkDeviceSize{storage_range});
        VkDeviceSize io_initial = minimum_buffer;
        VkDeviceSize wires_initial = minimum_buffer;
        for (std::size_t index = 0; index < tables.slots.size(); ++index) {
            const auto slot = tables.slots[index];
            if (slot == nvr::absent) continue;
            io_initial = std::max(io_initial, tables.shapes[index].io_words * sizeof(std::uint32_t));
            wires_initial = std::max(wires_initial, VkDeviceSize{wire_count(slot)} * sizeof(std::int32_t));
        }
        if (io_initial > buffer_ceiling || wires_initial > buffer_ceiling) {
            failure = "cannot allocate device memory";
            return;
        }
        // Growth always uses powers of two. An initial capacity may itself
        // be larger than the greatest power of two that the device can bind.
        io_limit = std::max(io_initial, std::bit_floor(buffer_ceiling));
        wires_limit = std::max(wires_initial, std::bit_floor(buffer_ceiling));
        if (request_limit == 0) {
            failure = "invalid device dispatch limit";
            return;
        }

        const auto table_bytes = VkDeviceSize{tables.words.size()} * sizeof(std::uint32_t);
        if (!allocate(table_buffer, std::max(table_bytes, VkDeviceSize{sizeof(std::uint32_t)}),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
            !allocate_io(io, io_initial) || !allocate_wires(wires, wires_initial) ||
            !allocate_commands()) {
            return;
        }
        if (table_bytes != 0 && !upload_tables(table_bytes)) return;
        if (!allocate_descriptor()) return;
        update_descriptor();
        record_dispatch(commands[0], 1);
    }

    void evaluate(std::span<nvm::GraphJob> jobs) {
        nvm::validate_graph_jobs(image, jobs);
        for (auto& job : jobs) job.evaluated = false;
        if (failed()) {
            statistics.declined_failed += jobs.size();
            return;
        }
        dispatch.reserve(std::min(jobs.size(), std::size_t{request_limit}));
        std::size_t next = 0;
        while (next < jobs.size()) {
            dispatch.clear();
            VkDeviceSize io_words = 0;
            VkDeviceSize wire_words = 0;
            while (next < jobs.size() && dispatch.size() < request_limit) {
                auto& job = jobs[next];
                const auto& shape = tables.shapes[job.graph];
                if (!shape.eligible) {
                    ++statistics.declined_ineligible;
                    ++next;
                    continue;
                }
                const auto slot = tables.slots[job.graph];
                const auto wires_needed = wire_count(slot);
                if (io_words + shape.io_words > io_limit / sizeof(std::uint32_t) ||
                    wire_words + wires_needed > wires_limit / sizeof(std::int32_t)) {
                    break;
                }
                dispatch.push_back({next, slot, static_cast<std::uint32_t>(wire_words)});
                io_words += shape.io_words;
                wire_words += wires_needed;
                ++next;
            }
            if (dispatch.empty()) {
                if (next != jobs.size()) {
                    failure = "cannot allocate device memory";
                    statistics.declined_failed += jobs.size() - next;
                }
                return;
            }
            if (!grow_buffers(io_words * sizeof(std::uint32_t), wire_words * sizeof(std::int32_t)) ||
                !run_dispatch(jobs)) {
                statistics.declined_failed += dispatch.size() + (jobs.size() - next);
                return;
            }
        }
    }

    bool failed() const { return !failure.empty(); }

    std::uint32_t wire_count(std::uint32_t slot) const { return tables.words[directory_words * slot + 7]; }

    std::span<std::uint32_t> io_words() const {
        return {static_cast<std::uint32_t*>(io.mapped), static_cast<std::size_t>(io.capacity / sizeof(std::uint32_t))};
    }

    bool check(VkResult result, const char* operation) {
        if (result == VK_SUCCESS) return true;
        failure = std::string(operation) + " failed (" + std::to_string(result) + ')';
        return false;
    }

    void destroy_buffer(Buffer& buffer) {
        if (buffer.mapped) context.vk.UnmapMemory(context.device, buffer.memory);
        if (buffer.handle != VK_NULL_HANDLE) context.vk.DestroyBuffer(context.device, buffer.handle, nullptr);
        if (buffer.memory != VK_NULL_HANDLE) context.vk.FreeMemory(context.device, buffer.memory, nullptr);
        buffer = {};
    }

    std::uint32_t memory_type(std::uint32_t candidates, VkMemoryPropertyFlags required,
                              VkMemoryPropertyFlags preferred) const {
        std::uint32_t fallback = VK_MAX_MEMORY_TYPES;
        for (std::uint32_t i = 0; i < context.memory_properties.memoryTypeCount; ++i) {
            const auto flags = context.memory_properties.memoryTypes[i].propertyFlags;
            if ((candidates & (std::uint32_t{1} << i)) == 0 || (flags & required) != required) continue;
            if ((flags & preferred) == preferred) return i;
            if (fallback == VK_MAX_MEMORY_TYPES) fallback = i;
        }
        return fallback;
    }

    bool allocate(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred = 0) {
        const auto allocation_failed = [&] {
            destroy_buffer(buffer);
            failure = "cannot allocate device memory";
            return false;
        };
        VkBufferCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        create.size = bytes;
        create.usage = usage;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer handle = VK_NULL_HANDLE;
        if (context.vk.CreateBuffer(context.device, &create, nullptr, &handle) != VK_SUCCESS) {
            return allocation_failed();
        }
        buffer.handle = handle;
        VkMemoryRequirements requirements{};
        context.vk.GetBufferMemoryRequirements(context.device, buffer.handle, &requirements);
        const auto type = memory_type(requirements.memoryTypeBits, required, preferred);
        if (type == VK_MAX_MEMORY_TYPES) return allocation_failed();

        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (context.vk.AllocateMemory(context.device, &allocation, nullptr, &memory) != VK_SUCCESS) {
            return allocation_failed();
        }
        buffer.memory = memory;
        if (context.vk.BindBufferMemory(context.device, buffer.handle, buffer.memory, 0) != VK_SUCCESS) {
            return allocation_failed();
        }
        buffer.properties = context.memory_properties.memoryTypes[type].propertyFlags;
        if ((required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
            void* mapped = nullptr;
            if (context.vk.MapMemory(context.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
                return allocation_failed();
            }
            buffer.mapped = mapped;
        }
        buffer.capacity = bytes;
        return true;
    }

    bool allocate_io(Buffer& buffer, VkDeviceSize bytes) {
        return allocate(buffer, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    }

    bool allocate_wires(Buffer& buffer, VkDeviceSize bytes) {
        return allocate(buffer, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    bool allocate_commands() {
        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = context.command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = static_cast<std::uint32_t>(commands.size());
        std::array<VkCommandBuffer, 2> allocated{};
        if (!check(context.vk.AllocateCommandBuffers(context.device, &allocate_info, allocated.data()),
                   "vkAllocateCommandBuffers")) {
            return false;
        }
        commands = allocated;
        return true;
    }

    bool begin_command(VkCommandBuffer command, VkCommandBufferUsageFlags flags) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = flags;
        return check(context.vk.BeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    }

    bool wait_for_fence() {
        using namespace std::chrono_literals;
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        auto result = VK_NOT_READY;
        do {
            result = context.vk.GetFenceStatus(context.device, context.fence);
        } while (result == VK_NOT_READY && Clock::now() - start < 2ms);
        if (result == VK_NOT_READY) {
            const auto deadline = start + 30s;
            do {
                const auto remaining = deadline - Clock::now();
                if (remaining <= Clock::duration::zero()) {
                    context.timed_out = true;
                    failure = "dispatch timed out after 30 seconds";
                    return false;
                }
                const auto slice = std::min(remaining, std::chrono::duration_cast<Clock::duration>(100ms));
                const auto wait_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(slice).count();
                result = context.vk.WaitForFences(context.device, 1, &context.fence, VK_TRUE,
                                                  static_cast<std::uint64_t>(wait_ns));
            } while (result == VK_TIMEOUT);
            if (!check(result, "vkWaitForFences")) return false;
        } else if (!check(result, "vkGetFenceStatus")) {
            return false;
        }
        return check(context.vk.ResetFences(context.device, 1, &context.fence), "vkResetFences");
    }

    bool submit(VkCommandBuffer command, bool graph_dispatch) {
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command;
        if (!check(context.vk.QueueSubmit(context.queue, 1, &submit_info, context.fence), "vkQueueSubmit")) {
            return false;
        }
        if (graph_dispatch) ++statistics.dispatches;
        return wait_for_fence();
    }

    bool upload_tables(VkDeviceSize table_bytes) {
        if (!allocate(staging, table_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            return false;
        }
        std::memcpy(staging.mapped, tables.words.data(), static_cast<std::size_t>(table_bytes));
        if ((staging.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = staging.memory;
            range.size = VK_WHOLE_SIZE;
            if (!check(context.vk.FlushMappedMemoryRanges(context.device, 1, &range), "vkFlushMappedMemoryRanges")) {
                return false;
            }
        }
        const auto command = commands[1];
        if (!begin_command(command, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) return false;
        const VkBufferCopy copy{0, 0, table_bytes};
        context.vk.CmdCopyBuffer(command, staging.handle, table_buffer.handle, 1, &copy);
        const auto barrier = buffer_barrier(table_buffer.handle, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        context.vk.CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      0, 0, nullptr, 1, &barrier, 0, nullptr);
        if (!check(context.vk.EndCommandBuffer(command), "vkEndCommandBuffer") || !submit(command, false)) return false;
        destroy_buffer(staging);
        return true;
    }

    bool allocate_descriptor() {
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        VkDescriptorPool created = VK_NULL_HANDLE;
        if (!check(context.vk.CreateDescriptorPool(context.device, &pool, nullptr, &created),
                   "vkCreateDescriptorPool")) {
            return false;
        }
        descriptor_pool = created;
        VkDescriptorSetAllocateInfo set{};
        set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set.descriptorPool = descriptor_pool;
        set.descriptorSetCount = 1;
        set.pSetLayouts = &context.descriptor_layout;
        return check(context.vk.AllocateDescriptorSets(context.device, &set, &descriptor), "vkAllocateDescriptorSets");
    }

    void update_descriptor() {
        const std::array<VkDescriptorBufferInfo, 3> buffers{{
            {table_buffer.handle, 0, table_buffer.capacity},
            {io.handle, 0, io.capacity},
            {wires.handle, 0, wires.capacity},
        }};
        std::array<VkWriteDescriptorSet, buffers.size()> writes{};
        for (std::uint32_t i = 0; i < writes.size(); ++i) {
            auto& write = writes[i];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptor;
            write.dstBinding = i;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &buffers[i];
        }
        context.vk.UpdateDescriptorSets(context.device, static_cast<std::uint32_t>(writes.size()),
                                        writes.data(), 0, nullptr);
    }

    bool record_dispatch(VkCommandBuffer command, std::uint32_t count) {
        if (!begin_command(command, count == 1 ? 0 : VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) return false;
        context.vk.CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipeline);
        context.vk.CmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, context.pipeline_layout,
                                        0, 1, &descriptor, 0, nullptr);
        context.vk.CmdDispatch(command, count, 1, 1);
        const auto barrier = buffer_barrier(io.handle, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
        context.vk.CmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                      0, 0, nullptr, 1, &barrier, 0, nullptr);
        return check(context.vk.EndCommandBuffer(command), "vkEndCommandBuffer");
    }

    bool grow_buffers(VkDeviceSize io_needed, VkDeviceSize wires_needed) {
        const bool grow_io = io_needed > io.capacity;
        const bool grow_wires = wires_needed > wires.capacity;
        if (!grow_io && !grow_wires) return true;
        if (grow_io) {
            Buffer replacement;
            if (!allocate_io(replacement, std::bit_ceil(io_needed))) return false;
            destroy_buffer(io);
            io = replacement;
        }
        if (grow_wires) {
            Buffer replacement;
            if (!allocate_wires(replacement, std::bit_ceil(wires_needed))) return false;
            destroy_buffer(wires);
            wires = replacement;
        }
        update_descriptor();
        return record_dispatch(commands[0], 1);
    }

    bool run_dispatch(std::span<nvm::GraphJob> jobs) {
        auto words = io_words();
        const auto count = static_cast<std::uint32_t>(dispatch.size());
        std::uint32_t next = request_words * count;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& request = dispatch[i];
            const auto& job = jobs[request.job];
            const auto record = request_words * i;
            const auto input_base = next;
            words[record] = request.slot;
            words[record + 1] = request.wire_base;
            words[record + 2] = input_base;
            for (const auto lane : job.inputs) words[next++] = static_cast<std::uint32_t>(lane);
            request.output_base = next;
            words[record + 3] = request.output_base;
            request.status_index = next + static_cast<std::uint32_t>(job.outputs.size());
            words[request.status_index] = 0;
            next = request.status_index + 1;
        }
        const auto command = commands[count == 1 ? 0 : 1];
        if (count != 1 && !record_dispatch(command, count)) return false;
        if (!submit(command, true)) return false;

        // Reject the whole dispatch before exposing any results if a status
        // indicates that the shader did not complete as specified.
        for (const auto& request : dispatch) {
            const auto status = words[request.status_index];
            if (status != evaluated_status && status != escaped_status) {
                failure = "invalid dispatch status " + std::to_string(status);
                return false;
            }
        }
        for (const auto& request : dispatch) {
            auto& job = jobs[request.job];
            if (words[request.status_index] == escaped_status) {
                auto& shape = tables.shapes[job.graph];
                shape.eligible = false;
                shape.reason = nvm::IneligibleReason::escaped;
                ++statistics.escaped;
            } else {
                for (std::size_t lane = 0; lane < job.outputs.size(); ++lane) {
                    job.outputs[lane] = std::bit_cast<std::int32_t>(words[request.output_base + lane]);
                }
                job.evaluated = true;
                ++statistics.evaluated;
            }
        }
        return true;
    }

    Context::State& context;
    const nvr::Image& image;
    nvm::LayeredTables tables;
    nvm::GraphDeviceStatistics statistics;
    std::string failure;
    std::uint32_t request_limit = 0;
    VkDeviceSize io_limit = 0;
    VkDeviceSize wires_limit = 0;
    Buffer table_buffer;
    Buffer io;
    Buffer wires;
    Buffer staging;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, 2> commands{};
    std::vector<Request> dispatch;
};

VulkanGraphDevice::VulkanGraphDevice(Context& context, const nvr::Image& image)
    : state_(std::make_unique<State>(*context.state_, image)) {
    state_->initialize();
}

VulkanGraphDevice::VulkanGraphDevice(std::unique_ptr<Context> context, const nvr::Image& image)
    : owned_context_(std::move(context)) {
    if (!owned_context_) throw std::invalid_argument("Vulkan graph device needs a context");
    state_ = std::make_unique<State>(*owned_context_->state_, image);
    state_->initialize();
}

VulkanGraphDevice::~VulkanGraphDevice() = default;
const nvr::Image& VulkanGraphDevice::image() const { return state_->image; }
void VulkanGraphDevice::evaluate(std::span<nvm::GraphJob> jobs) { state_->evaluate(jobs); }
std::size_t VulkanGraphDevice::batch_hint() const { return std::numeric_limits<std::size_t>::max(); }
nvm::GraphDeviceStatistics VulkanGraphDevice::statistics() const { return state_->statistics; }
std::string_view VulkanGraphDevice::failure_reason() const { return state_->failure; }
std::span<const nvm::GraphShape> VulkanGraphDevice::graph_shapes() const { return state_->tables.shapes; }
bool VulkanGraphDevice::failed() const { return state_->failed(); }
const std::string& VulkanGraphDevice::reason() const { return state_->failure; }
const nvm::LayeredTables& VulkanGraphDevice::tables() const { return state_->tables; }

std::uint64_t VulkanGraphDevice::io_capacity() const { return state_->io.capacity; }
std::uint64_t VulkanGraphDevice::wires_capacity() const { return state_->wires.capacity; }

bool VulkanGraphDevice::self_test(Context& context) {
    auto fixtures = nvm::self_test_graphs();
    nvr::Image image;
    std::vector<nvm::GraphJob> jobs;
    std::vector<std::vector<std::int64_t>> outputs;
    for (std::uint32_t i = 0; i < fixtures.size(); ++i) {
        auto& fixture = fixtures[i];
        image.graphs.push_back(std::move(fixture.graph));
        for (const auto& inputs : fixture.inputs) {
            jobs.push_back({i, inputs, outputs.emplace_back(image.graphs.back().outputs.size())});
        }
    }
    VulkanGraphDevice device(context, image);
    device.evaluate(jobs);
    const auto& dispatch = device.state_->dispatch;
    if (device.failed() || device.statistics().dispatches != 1 || dispatch.size() != jobs.size()) return false;

    const auto words = device.state_->io_words();
    std::vector<std::int64_t> reference;
    for (const auto& request : dispatch) {
        const auto& job = jobs[request.job];
        const auto& graph = image.graphs[job.graph];
        reference.assign(job.inputs.begin(), job.inputs.end());
        if (nvm::evaluate_graph(graph, reference)) return false;
        const bool escapes = std::ranges::any_of(reference, [](auto value) {
            return value < -nvm::wire_bound || value > nvm::wire_bound;
        });
        if (words[request.status_index] != (escapes ? escaped_status : evaluated_status)) return false;
        for (std::size_t lane = 0; lane < graph.outputs.size(); ++lane) {
            if (std::bit_cast<std::int32_t>(words[request.output_base + lane]) != reference[graph.outputs[lane]]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace neura::gpu
