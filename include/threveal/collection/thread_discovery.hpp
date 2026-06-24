/**
 *  @file       thread_discovery.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Discovery of the threads of a running process.
 */

#ifndef THREVEAL_COLLECTION_THREAD_DISCOVERY_HPP_
#define THREVEAL_COLLECTION_THREAD_DISCOVERY_HPP_

#include <optional>
#include <sys/types.h>
#include <vector>

namespace threveal::collection
{

/**
 *  Lists the threads of a process.
 *
 *  @param      pid  The process ID.
 *  @return     The thread IDs in ascending order, or std::nullopt if the process
 *              does not exist.
 */
[[nodiscard]] auto listThreads(pid_t pid) -> std::optional<std::vector<pid_t>>;

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_THREAD_DISCOVERY_HPP_
