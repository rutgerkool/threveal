/**
 *  @file       recording.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  JSON serialization of recordings.
 */

#include "threveal/storage/recording.hpp"

#include "threveal/analysis/event_store.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace threveal::storage
{

namespace
{

using nlohmann::json;

constexpr std::string_view kFormatName = "threveal-recording";

/**
 *  Reads an unsigned integer field, rejecting negative, fractional and out-of-range values.
 */
template <typename T>
auto unsignedField(const json& value, const char* key) -> T
{
    const auto& field = value.at(key);
    if (!field.is_number_unsigned() || field.get<std::uint64_t>() > std::numeric_limits<T>::max())
    {
        throw std::out_of_range(key);
    }
    return static_cast<T>(field.get<std::uint64_t>());
}

/**
 *  Reads a list of CPU IDs with the same checks as unsignedField().
 */
auto cpuList(const json& value, const char* key) -> std::vector<core::CpuId>
{
    std::vector<core::CpuId> cpus;
    for (const auto& cpu : value.at(key))
    {
        if (!cpu.is_number_unsigned() ||
            cpu.get<std::uint64_t>() > std::numeric_limits<core::CpuId>::max())
        {
            throw std::out_of_range(key);
        }
        cpus.push_back(static_cast<core::CpuId>(cpu.get<std::uint64_t>()));
    }
    return cpus;
}

auto toJson(const core::MigrationEvent& migration) -> json
{
    return json{
        {"timestamp_ns", migration.timestamp_ns},
        {"pid", migration.pid},
        {"tid", migration.tid},
        {"src_cpu", migration.src_cpu},
        {"dst_cpu", migration.dst_cpu},
        {"comm", migration.commAsStringView()},
    };
}

auto toJson(const core::PmuSample& sample) -> json
{
    return json{
        {"timestamp_ns", sample.timestamp_ns},
        {"tid", sample.tid},
        {"cpu_id", sample.cpu_id},
        {"instructions", sample.instructions},
        {"cycles", sample.cycles},
        {"llc_misses", sample.llc_misses},
        {"llc_references", sample.llc_references},
        {"branch_misses", sample.branch_misses},
    };
}

auto migrationFromJson(const json& value) -> core::MigrationEvent
{
    core::MigrationEvent migration{
        .timestamp_ns = unsignedField<std::uint64_t>(value, "timestamp_ns"),
        .pid = unsignedField<std::uint32_t>(value, "pid"),
        .tid = unsignedField<std::uint32_t>(value, "tid"),
        .src_cpu = unsignedField<core::CpuId>(value, "src_cpu"),
        .dst_cpu = unsignedField<core::CpuId>(value, "dst_cpu"),
        .comm = {},
    };

    // Keep the terminating null that commAsStringView() relies on
    auto comm = value.at("comm").get<std::string>();
    std::copy_n(comm.begin(), std::min(comm.size(), migration.comm.size() - 1),
                migration.comm.begin());
    return migration;
}

auto sampleFromJson(const json& value) -> core::PmuSample
{
    return core::PmuSample{
        .timestamp_ns = unsignedField<std::uint64_t>(value, "timestamp_ns"),
        .tid = unsignedField<std::uint32_t>(value, "tid"),
        .cpu_id = unsignedField<core::CpuId>(value, "cpu_id"),
        .instructions = unsignedField<std::uint64_t>(value, "instructions"),
        .cycles = unsignedField<std::uint64_t>(value, "cycles"),
        .llc_misses = unsignedField<std::uint64_t>(value, "llc_misses"),
        .llc_references = unsignedField<std::uint64_t>(value, "llc_references"),
        .branch_misses = unsignedField<std::uint64_t>(value, "branch_misses"),
    };
}

auto topologyFromJson(const json& value) -> core::TopologyMap
{
    return core::TopologyMap{cpuList(value, "p_cores"), cpuList(value, "e_cores")};
}

auto recordingFromJson(const json& value) -> std::expected<Recording, RecordingError>
{
    if (value.at("format").get<std::string>() != kFormatName ||
        unsignedField<std::uint32_t>(value, "version") != kRecordingVersion)
    {
        return std::unexpected(RecordingError::kUnsupportedVersion);
    }

    Recording recording{
        .topology = topologyFromJson(value.at("topology")),
        .duration_ns = unsignedField<std::uint64_t>(value, "duration_ns"),
        .events = {},
    };
    for (const auto& migration : value.at("migrations"))
    {
        recording.events.addMigration(migrationFromJson(migration));
    }
    for (const auto& sample : value.at("samples"))
    {
        recording.events.addPmuSample(sampleFromJson(sample));
    }
    return recording;
}

}  // namespace

auto serializeRecording(const Recording& recording) -> std::string
{
    auto migrations = json::array();
    for (const auto& migration : recording.events.allMigrations())
    {
        migrations.push_back(toJson(migration));
    }

    auto samples = json::array();
    for (const auto& sample : recording.events.allPmuSamples())
    {
        samples.push_back(toJson(sample));
    }

    json document{
        {"format", kFormatName},
        {"version", kRecordingVersion},
        {"duration_ns", recording.duration_ns},
        {"topology",
         {{"p_cores", recording.topology.getPCores()},
          {"e_cores", recording.topology.getECores()}}},
        {"migrations", std::move(migrations)},
        {"samples", std::move(samples)},
    };
    return document.dump();
}

auto parseRecording(std::string_view text) -> std::expected<Recording, RecordingError>
{
    auto document = json::parse(text, nullptr, false);
    if (document.is_discarded())
    {
        return std::unexpected(RecordingError::kMalformed);
    }

    try
    {
        return recordingFromJson(document);
    }
    catch (const json::exception&)
    {
        return std::unexpected(RecordingError::kMalformed);
    }
    catch (const std::out_of_range&)
    {
        return std::unexpected(RecordingError::kMalformed);
    }
}

}  // namespace threveal::storage
