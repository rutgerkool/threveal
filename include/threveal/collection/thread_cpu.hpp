/**
 *  @file       thread_cpu.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Lookup of the CPU a thread last ran on.
 */

#ifndef THREVEAL_COLLECTION_THREAD_CPU_HPP_
#define THREVEAL_COLLECTION_THREAD_CPU_HPP_

#include "threveal/core/types.hpp"

#include <optional>
#include <string_view>
#include <sys/types.h>

namespace threveal::collection
{

/**
 *  Extracts the CPU a thread last ran on from the contents of /proc/<tid>/stat.
 *
 *  @param      stat_line  The stat line of the thread.
 *  @return     The CPU ID, or std::nullopt if the line is malformed.
 */
[[nodiscard]] auto parseLastCpu(std::string_view stat_line) -> std::optional<core::CpuId>;

/**
 *  Reads the CPU a thread last ran on.
 *
 *  @param      tid  The thread ID.
 *  @return     The CPU ID, or std::nullopt if the thread's stat cannot be read.
 */
[[nodiscard]] auto readLastCpu(pid_t tid) -> std::optional<core::CpuId>;

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_THREAD_CPU_HPP_
