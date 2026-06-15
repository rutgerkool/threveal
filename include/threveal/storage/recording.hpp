/**
 *  @file       recording.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Saved profiling sessions that can be analyzed later.
 */

#ifndef THREVEAL_STORAGE_RECORDING_HPP_
#define THREVEAL_STORAGE_RECORDING_HPP_

#include "threveal/analysis/event_store.hpp"
#include "threveal/core/topology.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace threveal::storage
{

/**
 *  Version of the recording format written by serializeRecording().
 */
inline constexpr std::uint32_t kRecordingVersion = 1;

/**
 *  Reasons a recording cannot be read.
 */
enum class RecordingError : std::uint8_t
{
    /**
     *  The text is not valid JSON or does not have the expected structure.
     */
    kMalformed = 0,

    /**
     *  The recording was written in a format or version this build cannot read.
     */
    kUnsupportedVersion = 1,
};

/**
 *  A profiling session: the machine's topology, its duration and the recorded events.
 */
struct Recording
{
    /**
     *  CPU topology of the machine the recording was made on.
     */
    core::TopologyMap topology;

    /**
     *  How long the session was profiled, in nanoseconds.
     */
    std::uint64_t duration_ns = 0;

    /**
     *  Migrations and PMU samples captured during the session.
     */
    analysis::EventStore events;
};

/**
 *  Serializes a recording to JSON text.
 *
 *  @param      recording  The recording to serialize.
 *  @return     The recording as JSON.
 */
[[nodiscard]] auto serializeRecording(const Recording& recording) -> std::string;

/**
 *  Parses a recording from JSON text.
 *
 *  @param      text  JSON text produced by serializeRecording().
 *  @return     The recording, or the reason it cannot be read.
 */
[[nodiscard]] auto parseRecording(std::string_view text)
    -> std::expected<Recording, RecordingError>;

}  // namespace threveal::storage

#endif  // THREVEAL_STORAGE_RECORDING_HPP_
